/**
 * Luma Tools - Statistics tracking implementation (SQLite backend)
 *
 * All stats are stored in a SQLite database (stats.db) next to the executable.
 * On first run, any existing stats.jsonl data is automatically migrated in.
 */

#include "stats.h"
#include "discord.h"
#include <sqlite3.h>
#include <functional>

// === Globals =================================================================

static mutex   g_stats_mutex;
static sqlite3* g_db = nullptr;

// === Internal helpers ========================================================

// Returns an absolute path to stats.db anchored to the process working directory
// (i.e. the project root, same level as downloads/ and processing/).
// MUST NOT be inside processing/ — that directory is cleaned on every restart.
static string stats_db_path() {
    // Resolve to absolute so the path is stable regardless of later chdir calls.
    return fs::absolute("stats.db").string();
}

static string stats_jsonl_path() {
    return fs::absolute("stats.jsonl").string();
}

static int64_t now_unix() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Simple FNV-1a hash of the IP string (for privacy - never store raw IPs)
static string hash_ip(const string& ip) {
    if (ip.empty()) return "";
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : ip) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return string(buf, 10);
}

static string hash_text(const string& text) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

static string random_token_hex(size_t byte_count = 32) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char HEX[] = "0123456789abcdef";
    std::uniform_int_distribution<int> nibble_dist(0, 15);
    string out;
    out.reserve(byte_count * 2);
    for (size_t i = 0; i < byte_count * 2; ++i) {
        out.push_back(HEX[nibble_dist(rng)]);
    }
    return out;
}

static string hash_password(const string& password, const string& salt) {
    // Use iterative FNV-64 with salt for password hashing (PBKDF2-like approach)
    string salted = salt + password;
    uint64_t h = 14695981039346656037ULL;
    // Apply hash 10000 times for password stretching
    for (int iter = 0; iter < 10000; ++iter) {
        for (unsigned char c : salted) {
            h ^= c;
            h *= 1099511628211ULL;
        }
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

static void bind_text_or_null(sqlite3_stmt* stmt, int index, const string& value) {
    if (value.empty()) sqlite3_bind_null(stmt, index);
    else sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

static bool load_account_user_row(sqlite3_stmt* stmt, AccountUser& user) {
    if (!stmt) return false;
    auto text_at = [&](int idx) -> string {
        auto ptr = sqlite3_column_text(stmt, idx);
        return ptr ? reinterpret_cast<const char*>(ptr) : "";
    };
    user.id = sqlite3_column_int(stmt, 0);
    user.email = text_at(1);
    user.display_name = text_at(2);
    user.account_status = text_at(3);
    user.created_ts = sqlite3_column_int64(stmt, 4);
    user.updated_ts = sqlite3_column_int64(stmt, 5);
    user.stripe_customer_id = text_at(6);
    user.stripe_price_id = text_at(7);
    user.stripe_subscription_id = text_at(8);
    user.plan = text_at(9);
    return !user.email.empty();
}

static bool load_account_user_row_with_password(sqlite3_stmt* stmt, AccountUser& user, string& out_password_hash, string& out_password_salt) {
    if (!load_account_user_row(stmt, user)) return false;
    auto text_at = [&](int idx) -> string {
        auto ptr = sqlite3_column_text(stmt, idx);
        return ptr ? reinterpret_cast<const char*>(ptr) : "";
    };
    out_password_hash = text_at(3);
    out_password_salt = text_at(4);
    return true;
}

static bool load_user_by_sql(const string& sql, const string& value, AccountUser& user) {
    if (!g_db) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            ok = load_account_user_row(stmt, user);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

// Format a UTC unix timestamp as "YYYY-MM-DD"
static string unix_to_date(int64_t ts) {
    time_t t = (time_t)ts;
    struct tm gmt {};
#ifdef _WIN32
    gmtime_s(&gmt, &t);
#else
    gmtime_r(&t, &gmt);
#endif
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
        gmt.tm_year + 1900, gmt.tm_mon + 1, gmt.tm_mday);
    return buf;
}

// === Migration (stats.jsonl → SQLite) ========================================

static void migrate_jsonl() {
    string jsonl_path = stats_jsonl_path();
    if (!fs::exists(jsonl_path)) return;

    // Check if the DB already has rows — if so, skip migration
    int existing = 0;
    sqlite3_exec(g_db, "SELECT COUNT(*) FROM stats",
        [](void* data, int, char** argv, char**) -> int {
            *static_cast<int*>(data) = argv[0] ? std::stoi(argv[0]) : 0;
            return 0;
        }, &existing, nullptr);
    if (existing > 0) return;

    try {
        ifstream f(jsonl_path);
        string line;
        int imported = 0;

        sqlite3_exec(g_db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

        while (std::getline(f, line)) {
            if (line.empty()) continue;
            try {
                auto j = json::parse(line);
                int64_t ts  = j.value("ts",   (int64_t)0);
                string kind = j.value("kind", "");
                string name = j.value("name", "");
                bool   ok   = j.value("ok",   true);
                string vh   = j.value("vh",   "");

                sqlite3_stmt* stmt = nullptr;
                const char* sql = "INSERT INTO stats (ts, kind, name, ok, vh) VALUES (?,?,?,?,?)";
                if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, ts);
                    sqlite3_bind_text(stmt,  2, kind.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt,  3, name.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt,   4, ok ? 1 : 0);
                    if (!vh.empty())
                        sqlite3_bind_text(stmt, 5, vh.c_str(), -1, SQLITE_TRANSIENT);
                    else
                        sqlite3_bind_null(stmt, 5);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    imported++;
                }
            } catch (...) {}
        }

        sqlite3_exec(g_db, "COMMIT", nullptr, nullptr, nullptr);

        // Rename the old file to .migrated so we don't re-import
        try {
            fs::rename(jsonl_path, jsonl_path + ".migrated");
        } catch (...) {}

        if (imported > 0)
            fprintf(stdout, "[stats] Migrated %d records from stats.jsonl to stats.db\n", imported);

    } catch (...) {}
}

// === DB init =================================================================

void stat_init_db() {
    lock_guard<mutex> lk(g_stats_mutex);

    string db_path = stats_db_path();
    if (sqlite3_open(db_path.c_str(), &g_db) != SQLITE_OK) {
        fprintf(stderr, "[stats] Failed to open stats.db: %s\n", sqlite3_errmsg(g_db));
        g_db = nullptr;
        return;
    }

    // Enable WAL mode for better concurrent read performance
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(g_db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);

    // Create table + indexes
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS stats (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            ts   INTEGER NOT NULL,
            kind TEXT    NOT NULL,
            name TEXT    NOT NULL,
            ok   INTEGER NOT NULL DEFAULT 1,
            vh   TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_stats_ts      ON stats(ts);
        CREATE INDEX IF NOT EXISTS idx_stats_kind    ON stats(kind);
        CREATE INDEX IF NOT EXISTS idx_stats_ts_kind ON stats(ts, kind);
        CREATE TABLE IF NOT EXISTS tool_config (
            tool_id        TEXT PRIMARY KEY,
            enabled        INTEGER NOT NULL DEFAULT 1,
            rate_limit_min INTEGER NOT NULL DEFAULT 0,
            max_file_mb    INTEGER NOT NULL DEFAULT 0,
            max_text_chars INTEGER NOT NULL DEFAULT 0,
            note           TEXT    NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS ai_calls (
            id     INTEGER PRIMARY KEY AUTOINCREMENT,
            ts     INTEGER NOT NULL,
            tool   TEXT    NOT NULL,
            model  TEXT    NOT NULL,
            tokens INTEGER NOT NULL DEFAULT 0,
            vh     TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_ai_ts    ON ai_calls(ts);
        CREATE INDEX IF NOT EXISTS idx_ai_model ON ai_calls(model);
        CREATE TABLE IF NOT EXISTS users (
            id                     INTEGER PRIMARY KEY AUTOINCREMENT,
            email                  TEXT    NOT NULL UNIQUE,
            display_name           TEXT    NOT NULL DEFAULT '',
            password_hash          TEXT    NOT NULL DEFAULT '',
            password_salt          TEXT    NOT NULL DEFAULT '',
            account_status         TEXT    NOT NULL DEFAULT 'active',
            created_ts             INTEGER NOT NULL,
            updated_ts             INTEGER NOT NULL,
            stripe_customer_id     TEXT    NOT NULL DEFAULT '',
            stripe_price_id        TEXT    NOT NULL DEFAULT '',
            stripe_subscription_id  TEXT    NOT NULL DEFAULT '',
            plan                   TEXT    NOT NULL DEFAULT 'free'
        );
        CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);
        CREATE TABLE IF NOT EXISTS sessions (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id       INTEGER NOT NULL,
            token_hash    TEXT    NOT NULL UNIQUE,
            created_ts    INTEGER NOT NULL,
            expires_ts    INTEGER NOT NULL,
            last_seen_ts  INTEGER NOT NULL,
            ip_hash       TEXT,
            user_agent    TEXT    NOT NULL DEFAULT '',
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);
        CREATE INDEX IF NOT EXISTS idx_sessions_token_hash ON sessions(token_hash);
        CREATE TABLE IF NOT EXISTS subscriptions (
            id                        INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id                   INTEGER NOT NULL,
            provider                  TEXT    NOT NULL DEFAULT 'stripe',
            provider_customer_id      TEXT    NOT NULL DEFAULT '',
            provider_subscription_id   TEXT    NOT NULL DEFAULT '',
            plan                      TEXT    NOT NULL DEFAULT 'free',
            status                    TEXT    NOT NULL DEFAULT 'inactive',
            started_ts                INTEGER NOT NULL DEFAULT 0,
            current_period_end_ts     INTEGER NOT NULL DEFAULT 0,
            canceled_ts               INTEGER NOT NULL DEFAULT 0,
            raw_json                  TEXT    NOT NULL DEFAULT '',
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_subscriptions_user_id ON subscriptions(user_id);
        CREATE INDEX IF NOT EXISTS idx_subscriptions_status ON subscriptions(status);
        CREATE INDEX IF NOT EXISTS idx_subscriptions_customer_id ON subscriptions(provider_customer_id);
        CREATE INDEX IF NOT EXISTS idx_subscriptions_subscription_id ON subscriptions(provider_subscription_id);
        CREATE TABLE IF NOT EXISTS account_counters (
            user_id    INTEGER PRIMARY KEY,
            tools_used INTEGER NOT NULL DEFAULT 0,
            downloads  INTEGER NOT NULL DEFAULT 0,
            updated_ts INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS oauth_identities (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            provider        TEXT    NOT NULL,
            provider_user_id TEXT   NOT NULL,
            user_id         INTEGER NOT NULL,
            created_ts      INTEGER NOT NULL,
            UNIQUE(provider, provider_user_id),
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_oauth_user_id ON oauth_identities(user_id);
        CREATE TABLE IF NOT EXISTS password_resets (
            token_hash  TEXT    PRIMARY KEY,
            user_id     INTEGER NOT NULL,
            created_ts  INTEGER NOT NULL,
            expires_ts  INTEGER NOT NULL,
            used_ts     INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_pwresets_user_id ON password_resets(user_id);
        CREATE TABLE IF NOT EXISTS api_keys (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id      INTEGER NOT NULL,
            key_hash     TEXT    NOT NULL UNIQUE,
            key_prefix   TEXT    NOT NULL,
            name         TEXT    NOT NULL DEFAULT '',
            created_ts   INTEGER NOT NULL,
            last_used_ts INTEGER NOT NULL DEFAULT 0,
            revoked_ts   INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_api_keys_user ON api_keys(user_id);
        CREATE INDEX IF NOT EXISTS idx_api_keys_hash ON api_keys(key_hash);
    )";
    char* errmsg = nullptr;
    if (sqlite3_exec(g_db, schema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "[stats] Schema error: %s\n", errmsg);
        sqlite3_free(errmsg);
    }

    // Migrate old stats.jsonl data (no-op if already done)
    migrate_jsonl();
}

// === Record ==================================================================

void stat_record(const string& kind, const string& name, bool ok, const string& ip) {
    if (!g_db) return;
    try {
        string vh = hash_ip(ip);
        lock_guard<mutex> lk(g_stats_mutex);

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO stats (ts, kind, name, ok, vh) VALUES (?,?,?,?,?)";
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, now_unix());
            sqlite3_bind_text(stmt,  2, kind.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,  3, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt,   4, ok ? 1 : 0);
            if (!vh.empty())
                sqlite3_bind_text(stmt, 5, vh.c_str(), -1, SQLITE_TRANSIENT);
            else
                sqlite3_bind_null(stmt, 5);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } catch (...) {}
}

void stat_record_event(const string& name) {
    stat_record("event", name, true, "");
}

void stat_record_ai_call(const string& tool, const string& model, int tokens_used, const string& ip) {
    if (!g_db) return;
    try {
        string vh = hash_ip(ip);
        lock_guard<mutex> lk(g_stats_mutex);
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO ai_calls (ts, tool, model, tokens, vh) VALUES (?,?,?,?,?)";
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, now_unix());
            sqlite3_bind_text(stmt,  2, tool.c_str(),  -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,  3, model.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt,   4, tokens_used);
            if (!vh.empty())
                sqlite3_bind_text(stmt, 5, vh.c_str(), -1, SQLITE_TRANSIENT);
            else
                sqlite3_bind_null(stmt, 5);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } catch (...) {}
}

AIStats stat_query_ai(int64_t from_unix, int64_t to_unix) {
    AIStats result;
    if (!g_db) return result;

    lock_guard<mutex> lk(g_stats_mutex);

    // Total calls + tokens
    {
        const char* sql = "SELECT COUNT(*), COALESCE(SUM(tokens),0) FROM ai_calls WHERE ts >= ? AND ts <= ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, from_unix);
            sqlite3_bind_int64(stmt, 2, to_unix);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                result.total_calls  = sqlite3_column_int(stmt, 0);
                result.total_tokens = sqlite3_column_int(stmt, 1);
            }
            sqlite3_finalize(stmt);
        }
    }

    // Per-model aggregates
    {
        const char* sql =
            "SELECT model, COUNT(*), COALESCE(SUM(tokens),0) FROM ai_calls "
            "WHERE ts >= ? AND ts <= ? GROUP BY model ORDER BY COUNT(*) DESC";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, from_unix);
            sqlite3_bind_int64(stmt, 2, to_unix);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                AIModelBucket b;
                auto m = sqlite3_column_text(stmt, 0);
                b.model  = m ? reinterpret_cast<const char*>(m) : "";
                b.calls  = sqlite3_column_int(stmt, 1);
                b.tokens = sqlite3_column_int(stmt, 2);
                result.by_model.push_back(b);
            }
            sqlite3_finalize(stmt);
        }
    }

    // Per-tool aggregates (calls, tokens, most recent model)
    {
        const char* sql =
            "SELECT tool, COUNT(*), COALESCE(SUM(tokens),0), "
            "(SELECT model FROM ai_calls a2 WHERE a2.tool=a1.tool AND a2.ts>=? AND a2.ts<=? ORDER BY a2.ts DESC LIMIT 1) "
            "FROM ai_calls a1 WHERE a1.ts >= ? AND a1.ts <= ? "
            "GROUP BY tool ORDER BY COUNT(*) DESC";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, from_unix);
            sqlite3_bind_int64(stmt, 2, to_unix);
            sqlite3_bind_int64(stmt, 3, from_unix);
            sqlite3_bind_int64(stmt, 4, to_unix);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                AIToolBucket b;
                auto t = sqlite3_column_text(stmt, 0);
                auto m = sqlite3_column_text(stmt, 3);
                b.tool       = t ? reinterpret_cast<const char*>(t) : "";
                b.calls      = sqlite3_column_int(stmt, 1);
                b.tokens     = sqlite3_column_int(stmt, 2);
                b.last_model = m ? reinterpret_cast<const char*>(m) : "";
                result.by_tool.push_back(b);
            }
            sqlite3_finalize(stmt);
        }
    }

    return result;
}

// === Query ===================================================================

StatSummary stat_query(int64_t from_unix, int64_t to_unix, const string& kind) {
    StatSummary s;
    if (!g_db) return s;

    map<string, int> counts;
    lock_guard<mutex> lk(g_stats_mutex);

    // Build query — exclude visitor/event unless explicitly requested
    string sql = "SELECT name, ok FROM stats WHERE ts >= ? AND ts <= ?";
    if (!kind.empty())
        sql += " AND kind = '" + kind + "'";
    else
        sql += " AND kind NOT IN ('visitor','event')";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, from_unix);
        sqlite3_bind_int64(stmt, 2, to_unix);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            string nm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int    ok = sqlite3_column_int(stmt, 1);
            s.total++;
            if (ok) s.successes++; else s.failures++;
            counts[nm]++;
        }
        sqlite3_finalize(stmt);
    }

    s.by_name.assign(counts.begin(), counts.end());
    std::sort(s.by_name.begin(), s.by_name.end(),
        [](const pair<string,int>& a, const pair<string,int>& b) { return a.second > b.second; });
    return s;
}

vector<DayBucket> stat_timeseries(int64_t from_unix, int64_t to_unix, const string& kind) {
    map<string, int> day_counts;

    if (g_db) {
        lock_guard<mutex> lk(g_stats_mutex);

        string sql = "SELECT ts FROM stats WHERE ts >= ? AND ts <= ? AND kind != 'event'";
        if (!kind.empty())
            sql += " AND kind = '" + kind + "'";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, from_unix);
            sqlite3_bind_int64(stmt, 2, to_unix);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int64_t ts = sqlite3_column_int64(stmt, 0);
                day_counts[unix_to_date(ts)]++;
            }
            sqlite3_finalize(stmt);
        }
    }

    vector<DayBucket> result;
    int64_t cur = from_unix;
    while (cur <= to_unix && cur <= now_unix() + 86400) {
        string date = unix_to_date(cur);
        auto it = day_counts.find(date);
        DayBucket b;
        b.date  = date;
        b.count = (it != day_counts.end()) ? it->second : 0;
        result.push_back(b);
        cur += 86400;
    }
    return result;
}

int stat_unique_visitors(int64_t from_unix, int64_t to_unix) {
    if (!g_db) return 0;
    lock_guard<mutex> lk(g_stats_mutex);

    const char* sql =
        "SELECT COUNT(DISTINCT vh) FROM stats "
        "WHERE ts >= ? AND ts <= ? AND kind IN ('tool','download','visitor') AND vh IS NOT NULL";
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, from_unix);
        sqlite3_bind_int64(stmt, 2, to_unix);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

vector<pair<string,int>> stat_events(int64_t from_unix, int64_t to_unix) {
    if (!g_db) return {};
    lock_guard<mutex> lk(g_stats_mutex);

    const char* sql =
        "SELECT name, COUNT(*) as cnt FROM stats "
        "WHERE ts >= ? AND ts <= ? AND kind = 'event' "
        "GROUP BY name ORDER BY cnt DESC";
    sqlite3_stmt* stmt = nullptr;
    vector<pair<string,int>> result;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, from_unix);
        sqlite3_bind_int64(stmt, 2, to_unix);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            string nm  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int    cnt = sqlite3_column_int(stmt, 1);
            result.emplace_back(nm, cnt);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

// === Time helpers ============================================================

int64_t stat_today_start() {
    // Truncate the current UTC unix timestamp to the day boundary (00:00:00 UTC).
    // Using pure integer arithmetic avoids any local-timezone contamination from mktime().
    return (now_unix() / 86400LL) * 86400LL;
}

int64_t stat_days_ago(int n) {
    return stat_today_start() - (int64_t)n * 86400LL;
}

// === Daily digest ============================================================

void stat_send_daily_digest() {
    int64_t day_start  = stat_today_start();
    int64_t day_end    = day_start + 86399;
    int64_t prev_start = day_start - 86400;

    auto today     = stat_query(day_start,  day_end,      "");
    auto tools     = stat_query(day_start,  day_end,      "tool");
    auto downloads = stat_query(day_start,  day_end,      "download");
    auto yesterday = stat_query(prev_start, day_start - 1, "");
    int  uniq      = stat_unique_visitors(day_start, day_end);
    auto events    = stat_events(day_start, day_end);
    auto ai        = stat_query_ai(day_start, day_end);

    string desc;
    int delta = today.total - yesterday.total;
    string trend = (delta > 0) ? ("+" + to_string(delta))
                 : (delta < 0) ? (to_string(delta))
                 : "=same";

    desc += "**Total requests:** " + to_string(today.total) + "  (" + trend + " vs yesterday)\n";
    desc += "**Tools:** " + to_string(tools.total) + "  |  **Downloads:** " + to_string(downloads.total) + "\n";
    desc += "**Unique visitors:** " + to_string(uniq) + "\n";
    if (today.failures > 0) desc += "**Errors:** " + to_string(today.failures) + "\n";

    if (!tools.by_name.empty()) {
        desc += "\n**Top Tools:**\n";
        for (int i = 0; i < (int)std::min((int)tools.by_name.size(), 5); i++)
            desc += "`" + tools.by_name[i].first + "`  " + to_string(tools.by_name[i].second) + "\n";
    }
    if (!downloads.by_name.empty()) {
        desc += "\n**Top Platforms:**\n";
        for (int i = 0; i < (int)std::min((int)downloads.by_name.size(), 5); i++)
            desc += "`" + downloads.by_name[i].first + "`  " + to_string(downloads.by_name[i].second) + "\n";
    }
    if (!events.empty()) {
        desc += "\n**Events:**\n";
        for (auto& [name, cnt] : events)
            desc += "`" + name + "`  " + to_string(cnt) + "\n";
    }

    if (ai.total_calls > 0) {
        desc += "\n**🤖 AI Usage:**\n";
        desc += "**Calls:** " + to_string(ai.total_calls) + "  |  ";
        desc += "**Tokens:** " + to_string(ai.total_tokens) + "\n";
        for (auto& b : ai.by_model) {
            // Shorten model names for readability
            string m = b.model;
            if (m == "llama-3.3-70b-versatile")        m = "Llama 3.3 70B";
            else if (m == "deepseek-r1-distill-llama-70b") m = "DeepSeek R1 70B";
            else if (m == "llama-3.1-8b-instant")       m = "Llama 3.1 8B";
            else if (m.rfind("ollama:", 0) == 0)         m = "Local (" + m.substr(7) + ")";
            desc += "`" + m + "` — " + to_string(b.calls) + " calls, " + to_string(b.tokens) + " tokens\n";
        }
    }

    auto week = stat_query(stat_days_ago(7), day_end, "");
    desc += "\n**Last 7 days:** " + to_string(week.total) + " total requests";

    discord_log("\xF0\x9F\x93\x8A Daily Digest", desc, 0x7C5CFF);
}

// === Scheduler ===============================================================

void stat_start_daily_scheduler() {
    thread([]() {
        while (true) {
            auto now = std::chrono::system_clock::now();
            auto t   = std::chrono::system_clock::to_time_t(now);
            struct tm gmt {};
#ifdef _WIN32
            gmtime_s(&gmt, &t);
#else
            gmtime_r(&t, &gmt);
#endif
            int secs_left = 86400 - (gmt.tm_hour * 3600 + gmt.tm_min * 60 + gmt.tm_sec);
            std::this_thread::sleep_for(std::chrono::seconds(secs_left));
            stat_send_daily_digest();
        }
    }).detach();
}

// === Tool config =============================================================

ToolConfig get_tool_config(const string& tool_id) {
    ToolConfig cfg;
    cfg.tool_id = tool_id;
    if (!g_db) return cfg;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT enabled, rate_limit_min, max_file_mb, max_text_chars, note "
                      "FROM tool_config WHERE tool_id=?";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, tool_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            cfg.enabled        = sqlite3_column_int(stmt, 0) != 0;
            cfg.rate_limit_min = sqlite3_column_int(stmt, 1);
            cfg.max_file_mb    = sqlite3_column_int(stmt, 2);
            cfg.max_text_chars = sqlite3_column_int(stmt, 3);
            auto note_ptr      = sqlite3_column_text(stmt, 4);
            cfg.note           = note_ptr ? reinterpret_cast<const char*>(note_ptr) : "";
        }
        sqlite3_finalize(stmt);
    }
    return cfg;
}

void set_tool_config(const ToolConfig& cfg) {
    if (!g_db) return;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO tool_config(tool_id, enabled, rate_limit_min, max_file_mb, max_text_chars, note) "
                      "VALUES(?,?,?,?,?,?) "
                      "ON CONFLICT(tool_id) DO UPDATE SET "
                      "enabled=excluded.enabled, rate_limit_min=excluded.rate_limit_min, "
                      "max_file_mb=excluded.max_file_mb, max_text_chars=excluded.max_text_chars, "
                      "note=excluded.note";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, cfg.tool_id.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt,  2, cfg.enabled ? 1 : 0);
        sqlite3_bind_int(stmt,  3, cfg.rate_limit_min);
        sqlite3_bind_int(stmt,  4, cfg.max_file_mb);
        sqlite3_bind_int(stmt,  5, cfg.max_text_chars);
        sqlite3_bind_text(stmt, 6, cfg.note.c_str(),     -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

vector<ToolConfig> get_all_tool_configs() {
    vector<ToolConfig> out;
    if (!g_db) return out;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT tool_id, enabled, rate_limit_min, max_file_mb, max_text_chars, note FROM tool_config ORDER BY tool_id";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ToolConfig cfg;
            auto id_ptr  = sqlite3_column_text(stmt, 0);
            cfg.tool_id        = id_ptr ? reinterpret_cast<const char*>(id_ptr) : "";
            cfg.enabled        = sqlite3_column_int(stmt, 1) != 0;
            cfg.rate_limit_min = sqlite3_column_int(stmt, 2);
            cfg.max_file_mb    = sqlite3_column_int(stmt, 3);
            cfg.max_text_chars = sqlite3_column_int(stmt, 4);
            auto note_ptr      = sqlite3_column_text(stmt, 5);
            cfg.note           = note_ptr ? reinterpret_cast<const char*>(note_ptr) : "";
            out.push_back(cfg);
        }
        sqlite3_finalize(stmt);
    }
    return out;
}

// === Account / billing storage ==============================================

bool account_get_user_by_id(int user_id, AccountUser& out_user) {
    if (!g_db || user_id <= 0) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, email, display_name, account_status, created_ts, updated_ts, "
        "stripe_customer_id, stripe_price_id, stripe_subscription_id, plan "
        "FROM users WHERE id = ?";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) ok = load_account_user_row(stmt, out_user);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_get_user_by_email(const string& email, AccountUser& out_user) {
    if (!g_db || email.empty()) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, email, display_name, account_status, created_ts, updated_ts, "
        "stripe_customer_id, stripe_price_id, stripe_subscription_id, plan "
        "FROM users WHERE email = ?";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) ok = load_account_user_row(stmt, out_user);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_upsert_user(const string& email, const string& display_name, const string& password, AccountUser& out_user) {
    if (!g_db || email.empty()) return false;
    bool ok = false;
    {
        // CRITICAL: std::mutex is non-recursive. The previous version called
        // account_get_user_by_email() at the end while still holding the lock,
        // which deadlocked the worker thread on every first-time OAuth signup.
        // That was the root cause of the "site freezes after Discord authorize"
        // bug. Now we release the lock before re-reading the row.
        lock_guard<mutex> lk(g_stats_mutex);
        const int64_t now = now_unix();
        sqlite3_stmt* stmt = nullptr;

        // Generate password salt and hash
        string password_salt = password.empty() ? "" : random_token_hex(16);
        string password_hash = password.empty() ? "" : hash_password(password, password_salt);

        const char* sql =
            "INSERT INTO users (email, display_name, password_hash, password_salt, account_status, created_ts, updated_ts, stripe_customer_id, stripe_price_id, stripe_subscription_id, plan) "
            "VALUES (?, ?, ?, ?, 'active', ?, ?, '', '', '', 'free')";
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, display_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, password_hash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, password_salt.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, now);
            sqlite3_bind_int64(stmt, 6, now);
            if (sqlite3_step(stmt) == SQLITE_DONE) ok = true;
        }
        if (stmt) sqlite3_finalize(stmt);
    } // <-- lock released here
    if (!ok) return false;
    return account_get_user_by_email(email, out_user);
}

bool account_verify_password(const string& email, const string& password, AccountUser& out_user) {
    if (!g_db || email.empty() || password.empty()) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, email, display_name, password_hash, password_salt, account_status, created_ts, updated_ts, "
        "stripe_customer_id, stripe_price_id, stripe_subscription_id, plan "
        "FROM users WHERE email = ?";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text_at = [&](int idx) -> string {
                auto ptr = sqlite3_column_text(stmt, idx);
                return ptr ? reinterpret_cast<const char*>(ptr) : "";
            };
            string stored_hash = text_at(3);
            string stored_salt = text_at(4);
            
            // Verify password
            if (!stored_hash.empty() && !stored_salt.empty()) {
                string computed_hash = hash_password(password, stored_salt);
                if (computed_hash == stored_hash) {
                    out_user.id = sqlite3_column_int(stmt, 0);
                    out_user.email = text_at(1);
                    out_user.display_name = text_at(2);
                    out_user.account_status = text_at(5);
                    out_user.created_ts = sqlite3_column_int64(stmt, 6);
                    out_user.updated_ts = sqlite3_column_int64(stmt, 7);
                    out_user.stripe_customer_id = text_at(8);
                    out_user.stripe_price_id = text_at(9);
                    out_user.stripe_subscription_id = text_at(10);
                    out_user.plan = text_at(11);
                    ok = true;
                }
            }
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_create_session(int user_id, const string& ip, const string& user_agent, string& out_token, int64_t& out_expires_ts) {
    if (!g_db || user_id <= 0) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    out_token = random_token_hex();
    out_expires_ts = now_unix() + 60LL * 60LL * 24LL * 30LL;
    const int64_t now = now_unix();
    const string token_hash = hash_text(out_token);
    const string ip_hash = hash_ip(ip);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO sessions (user_id, token_hash, created_ts, expires_ts, last_seen_ts, ip_hash, user_agent) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_text(stmt, 2, token_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now);
        sqlite3_bind_int64(stmt, 4, out_expires_ts);
        sqlite3_bind_int64(stmt, 5, now);
        bind_text_or_null(stmt, 6, ip_hash);
        sqlite3_bind_text(stmt, 7, user_agent.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) ok = true;
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_get_user_by_session(const string& token, AccountUser& out_user) {
    if (!g_db || token.empty()) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT u.id, u.email, u.display_name, u.account_status, u.created_ts, u.updated_ts, "
        "u.stripe_customer_id, u.stripe_price_id, u.stripe_subscription_id, u.plan "
        "FROM sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.token_hash = ? AND s.expires_ts >= ?";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash_text(token).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, now_unix());
        if (sqlite3_step(stmt) == SQLITE_ROW) ok = load_account_user_row(stmt, out_user);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_delete_session(const string& token) {
    if (!g_db || token.empty()) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM sessions WHERE token_hash = ?";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash_text(token).c_str(), -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_upsert_subscription(
    int user_id,
    const string& plan,
    const string& status,
    const string& stripe_customer_id,
    const string& stripe_subscription_id,
    const string& stripe_price_id,
    int64_t current_period_end_ts,
    const string& raw_json
) {
    if (!g_db || user_id <= 0) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    const int64_t now = now_unix();
    bool ok = false;

    sqlite3_exec(g_db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt = nullptr;
    const char* upsert_sql =
        "INSERT INTO subscriptions (user_id, provider, provider_customer_id, provider_subscription_id, plan, status, started_ts, current_period_end_ts, canceled_ts, raw_json) "
        "VALUES (?, 'stripe', ?, ?, ?, ?, ?, ?, 0, ?) "
        "ON CONFLICT(provider_subscription_id) DO UPDATE SET "
        "provider_customer_id=excluded.provider_customer_id, plan=excluded.plan, status=excluded.status, "
        "current_period_end_ts=excluded.current_period_end_ts, raw_json=excluded.raw_json";
    if (sqlite3_prepare_v2(g_db, upsert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_text(stmt, 2, stripe_customer_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, stripe_subscription_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, plan.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 6, now);
        sqlite3_bind_int64(stmt, 7, current_period_end_ts);
        sqlite3_bind_text(stmt, 8, raw_json.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) ok = true;
    }
    if (stmt) sqlite3_finalize(stmt);

    if (ok) {
        sqlite3_stmt* user_stmt = nullptr;
        const char* user_sql =
            "UPDATE users SET account_status=?, updated_ts=?, stripe_customer_id=?, stripe_price_id=?, stripe_subscription_id=?, plan=? WHERE id=?";
        if (sqlite3_prepare_v2(g_db, user_sql, -1, &user_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(user_stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(user_stmt, 2, now);
            sqlite3_bind_text(user_stmt, 3, stripe_customer_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(user_stmt, 4, stripe_price_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(user_stmt, 5, stripe_subscription_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(user_stmt, 6, plan.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(user_stmt, 7, user_id);
            sqlite3_step(user_stmt);
        }
        if (user_stmt) sqlite3_finalize(user_stmt);
    }

    sqlite3_exec(g_db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok;
}

bool account_find_user_by_stripe_customer_id(const string& stripe_customer_id, AccountUser& out_user) {
    if (!g_db || stripe_customer_id.empty()) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, email, display_name, account_status, created_ts, updated_ts, "
        "stripe_customer_id, stripe_price_id, stripe_subscription_id, plan "
        "FROM users WHERE stripe_customer_id = ?";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, stripe_customer_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) ok = load_account_user_row(stmt, out_user);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

// ─── Admin helpers ───────────────────────────────────────────────────────────

vector<AccountUser> account_list_users(int limit, int offset, const string& search) {
    vector<AccountUser> rows;
    if (!g_db) return rows;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    string sql =
        "SELECT id, email, display_name, account_status, created_ts, updated_ts, "
        "stripe_customer_id, stripe_price_id, stripe_subscription_id, plan "
        "FROM users";
    if (!search.empty()) sql += " WHERE email LIKE ? OR display_name LIKE ?";
    sql += " ORDER BY created_ts DESC LIMIT ? OFFSET ?";
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        int idx = 1;
        string like = "%" + search + "%";
        if (!search.empty()) {
            sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_int(stmt, idx++, limit > 0 ? limit : 200);
        sqlite3_bind_int(stmt, idx++, offset > 0 ? offset : 0);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AccountUser u;
            if (load_account_user_row(stmt, u)) rows.push_back(u);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return rows;
}

int account_count_users(const string& search) {
    if (!g_db) return 0;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    string sql = "SELECT COUNT(*) FROM users";
    if (!search.empty()) sql += " WHERE email LIKE ? OR display_name LIKE ?";
    int n = 0;
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (!search.empty()) {
            string like = "%" + search + "%";
            sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, like.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int(stmt, 0);
    }
    if (stmt) sqlite3_finalize(stmt);
    return n;
}

bool account_admin_set_plan(int user_id, const string& plan, const string& status) {
    if (!g_db || user_id <= 0) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    const char* sql = "UPDATE users SET plan=?, account_status=?, updated_ts=? WHERE id=?";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, plan.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now_unix());
        sqlite3_bind_int(stmt, 4, user_id);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_get_user_by_display_name(const string& display_name, AccountUser& out_user) {
    if (!g_db || display_name.empty()) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, email, display_name, account_status, created_ts, updated_ts, "
        "stripe_customer_id, stripe_price_id, stripe_subscription_id, plan "
        "FROM users WHERE LOWER(display_name) = LOWER(?) LIMIT 1";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, display_name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) ok = load_account_user_row(stmt, out_user);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

static void account_bump_counter(int user_id, const char* col) {
    if (!g_db || user_id <= 0) return;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    string sql = string("INSERT INTO account_counters(user_id, ") + col + ", updated_ts) "
                 "VALUES (?, 1, ?) "
                 "ON CONFLICT(user_id) DO UPDATE SET "
               + col + "=" + col + "+1, updated_ts=excluded.updated_ts";
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_int64(stmt, 2, now_unix());
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

void account_bump_tool_count(int user_id)     { account_bump_counter(user_id, "tools_used"); }
void account_bump_download_count(int user_id) { account_bump_counter(user_id, "downloads"); }

// ─── API keys (Bearer token auth) ───────────────────────────────────────────

bool account_api_key_create(int user_id, const string& name, string& out_plaintext, int& out_id) {
    out_id = 0;
    if (!g_db || user_id <= 0) return false;
    // "lt_" + 32 hex chars (16 random bytes) — looks like sk_xxx (Stripe-style).
    out_plaintext = "lt_" + random_token_hex(16);
    string key_hash   = hash_text(out_plaintext);
    string key_prefix = out_plaintext.substr(0, 11);   // "lt_xxxxxxxx" — visible later
    int64_t now = now_unix();

    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO api_keys (user_id, key_hash, key_prefix, name, created_ts) "
        "VALUES (?, ?, ?, ?, ?)";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_text(stmt, 2, key_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, key_prefix.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, now);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (ok) out_id = (int)sqlite3_last_insert_rowid(g_db);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

vector<ApiKey> account_api_key_list(int user_id) {
    vector<ApiKey> rows;
    if (!g_db || user_id <= 0) return rows;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, user_id, key_prefix, name, created_ts, last_used_ts, revoked_ts "
        "FROM api_keys WHERE user_id = ? AND revoked_ts = 0 ORDER BY id DESC";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ApiKey k;
            k.id           = sqlite3_column_int(stmt, 0);
            k.user_id      = sqlite3_column_int(stmt, 1);
            auto t = [&](int i){ auto p=sqlite3_column_text(stmt,i); return p?reinterpret_cast<const char*>(p):""; };
            k.key_prefix   = t(2);
            k.name         = t(3);
            k.created_ts   = sqlite3_column_int64(stmt, 4);
            k.last_used_ts = sqlite3_column_int64(stmt, 5);
            k.revoked_ts   = sqlite3_column_int64(stmt, 6);
            rows.push_back(k);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return rows;
}

bool account_api_key_revoke(int user_id, int key_id) {
    if (!g_db || user_id <= 0 || key_id <= 0) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    const char* sql = "UPDATE api_keys SET revoked_ts = ? WHERE id = ? AND user_id = ?";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, now_unix());
        sqlite3_bind_int(stmt, 2, key_id);
        sqlite3_bind_int(stmt, 3, user_id);
        ok = (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(g_db) > 0);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_find_user_by_api_key(const string& plaintext, AccountUser& out_user) {
    if (!g_db || plaintext.size() < 16 || plaintext.rfind("lt_", 0) != 0) return false;
    string key_hash = hash_text(plaintext);
    int user_id = 0;
    {
        lock_guard<mutex> lk(g_stats_mutex);
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT user_id FROM api_keys WHERE key_hash = ? AND revoked_ts = 0";
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key_hash.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) user_id = sqlite3_column_int(stmt, 0);
        }
        if (stmt) sqlite3_finalize(stmt);
        if (user_id > 0) {
            // bump last_used_ts in the same lock window so concurrent reads see it.
            sqlite3_stmt* up = nullptr;
            if (sqlite3_prepare_v2(g_db,
                "UPDATE api_keys SET last_used_ts = ? WHERE key_hash = ?", -1, &up, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(up, 1, now_unix());
                sqlite3_bind_text(up, 2, key_hash.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(up);
            }
            if (up) sqlite3_finalize(up);
        }
    }
    if (user_id <= 0) return false;
    return account_get_user_by_id(user_id, out_user);
}

// ─── Password reset ──────────────────────────────────────────────────────────

bool account_create_password_reset(int user_id, string& out_token, int ttl_seconds) {
    if (!g_db || user_id <= 0) return false;
    out_token = random_token_hex(32);  // 64 hex chars
    const string token_hash = hash_text(out_token);
    const int64_t now = now_unix();
    const int64_t expires = now + (ttl_seconds > 0 ? ttl_seconds : 1800);

    lock_guard<mutex> lk(g_stats_mutex);
    // Drop any pre-existing unused tokens for this user so only the newest works.
    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2(g_db,
        "DELETE FROM password_resets WHERE user_id=? AND used_ts=0", -1, &del, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(del, 1, user_id);
        sqlite3_step(del);
    }
    if (del) sqlite3_finalize(del);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO password_resets (token_hash, user_id, created_ts, expires_ts) VALUES (?, ?, ?, ?)";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, token_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, user_id);
        sqlite3_bind_int64(stmt, 3, now);
        sqlite3_bind_int64(stmt, 4, expires);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

int account_consume_password_reset(const string& token, bool mark_used) {
    if (!g_db || token.empty()) return 0;
    const string token_hash = hash_text(token);
    const int64_t now = now_unix();
    lock_guard<mutex> lk(g_stats_mutex);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT user_id, expires_ts, used_ts FROM password_resets WHERE token_hash = ?";
    int user_id = 0;
    int64_t expires = 0, used = 0;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, token_hash.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user_id = sqlite3_column_int(stmt, 0);
            expires = sqlite3_column_int64(stmt, 1);
            used    = sqlite3_column_int64(stmt, 2);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    if (user_id == 0 || used != 0 || expires < now) return 0;

    if (mark_used) {
        sqlite3_stmt* up = nullptr;
        if (sqlite3_prepare_v2(g_db,
            "UPDATE password_resets SET used_ts=? WHERE token_hash=?", -1, &up, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(up, 1, now);
            sqlite3_bind_text(up, 2, token_hash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(up);
        }
        if (up) sqlite3_finalize(up);
    }
    return user_id;
}

bool account_update_password(int user_id, const string& new_password) {
    if (!g_db || user_id <= 0 || new_password.empty()) return false;
    string salt = random_token_hex(16);
    string hash = hash_password(new_password, salt);
    const int64_t now = now_unix();
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    const char* sql = "UPDATE users SET password_hash=?, password_salt=?, updated_ts=? WHERE id=?";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now);
        sqlite3_bind_int(stmt, 4, user_id);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

void account_invalidate_all_sessions(int user_id) {
    if (!g_db || user_id <= 0) return;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "DELETE FROM sessions WHERE user_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

bool account_link_oauth_identity(const string& provider, const string& provider_user_id, int user_id) {
    if (!g_db || provider.empty() || provider_user_id.empty() || user_id <= 0) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO oauth_identities(provider, provider_user_id, user_id, created_ts) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(provider, provider_user_id) DO UPDATE SET user_id=excluded.user_id";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, provider_user_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, user_id);
        sqlite3_bind_int64(stmt, 4, now_unix());
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

bool account_find_user_by_oauth(const string& provider, const string& provider_user_id, AccountUser& out_user) {
    if (!g_db || provider.empty() || provider_user_id.empty()) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT u.id, u.email, u.display_name, u.account_status, u.created_ts, u.updated_ts, "
        "u.stripe_customer_id, u.stripe_price_id, u.stripe_subscription_id, u.plan "
        "FROM oauth_identities o JOIN users u ON u.id = o.user_id "
        "WHERE o.provider = ? AND o.provider_user_id = ? LIMIT 1";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, provider.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, provider_user_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) ok = load_account_user_row(stmt, out_user);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

AccountStats account_get_user_stats(int user_id) {
    AccountStats s;
    if (!g_db || user_id <= 0) return s;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT tools_used, downloads FROM account_counters WHERE user_id = ?";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            s.tools_used = sqlite3_column_int(stmt, 0);
            s.downloads  = sqlite3_column_int(stmt, 1);
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return s;
}

bool account_admin_delete_user(int user_id) {
    if (!g_db || user_id <= 0) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_exec(g_db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    auto run = [&](const char* sql) {
        sqlite3_stmt* s = nullptr;
        bool r = false;
        if (sqlite3_prepare_v2(g_db, sql, -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(s, 1, user_id);
            r = (sqlite3_step(s) == SQLITE_DONE);
        }
        if (s) sqlite3_finalize(s);
        return r;
    };
    bool ok = run("DELETE FROM sessions WHERE user_id=?")
           && run("DELETE FROM subscriptions WHERE user_id=?")
           && run("DELETE FROM users WHERE id=?");
    sqlite3_exec(g_db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
    return ok;
}

bool account_find_user_by_stripe_subscription_id(const string& stripe_subscription_id, AccountUser& out_user) {
    if (!g_db || stripe_subscription_id.empty()) return false;
    lock_guard<mutex> lk(g_stats_mutex);
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT u.id, u.email, u.display_name, u.account_status, u.created_ts, u.updated_ts, "
        "u.stripe_customer_id, u.stripe_price_id, u.stripe_subscription_id, u.plan "
        "FROM subscriptions s JOIN users u ON u.id = s.user_id WHERE s.provider_subscription_id = ?";
    bool ok = false;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, stripe_subscription_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) ok = load_account_user_row(stmt, out_user);
    }
    if (stmt) sqlite3_finalize(stmt);
    return ok;
}

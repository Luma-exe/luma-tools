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

static string stats_db_path() {
    string proc = get_processing_dir();
    fs::path p(proc);
    auto parent = p.parent_path();
    if (parent.empty() || !fs::exists(parent)) parent = p;
    return (parent / "stats.db").string();
}

static string stats_jsonl_path() {
    string proc = get_processing_dir();
    fs::path p(proc);
    auto parent = p.parent_path();
    if (parent.empty() || !fs::exists(parent)) parent = p;
    return (parent / "stats.jsonl").string();
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

static int64_t timezone_offset_seconds() {
    time_t epoch = 0;
    struct tm utc_tm {};
#ifdef _WIN32
    gmtime_s(&utc_tm, &epoch);
#else
    gmtime_r(&epoch, &utc_tm);
#endif
    return (int64_t)mktime(&utc_tm);
}

int64_t stat_today_start() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    struct tm gmt {};
#ifdef _WIN32
    gmtime_s(&gmt, &t);
#else
    gmtime_r(&t, &gmt);
#endif
    gmt.tm_hour = 0; gmt.tm_min = 0; gmt.tm_sec = 0;
    return (int64_t)mktime(&gmt) - timezone_offset_seconds();
}

int64_t stat_days_ago(int n) {
    return stat_today_start() - (int64_t)n * 86400;
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

/**
 * Luma Tools - Statistics tracking implementation
 */

#include "stats.h"
#include "discord.h"
#include <functional>

// === Internal helpers ========================================================

static mutex g_stats_mutex;

static string stats_file_path() {
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
    // Return as hex string (truncated to 10 chars)
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

// === Record ==================================================================

void stat_record(const string& kind, const string& name, bool ok, const string& ip) {
    try {
        json record = {
            {"ts",   now_unix()},
            {"kind", kind},
            {"name", name},
            {"ok",   ok}
        };
        string vh = hash_ip(ip);
        if (!vh.empty()) record["vh"] = vh;

        lock_guard<mutex> lk(g_stats_mutex);
        ofstream f(stats_file_path(), std::ios::app);
        f << record.dump() << "\n";
    } catch (...) {}
}

void stat_record_event(const string& name) {
    try {
        json record = {
            {"ts",   now_unix()},
            {"kind", "event"},
            {"name", name},
            {"ok",   true}
        };
        lock_guard<mutex> lk(g_stats_mutex);
        ofstream f(stats_file_path(), std::ios::app);
        f << record.dump() << "\n";
    } catch (...) {}
}

// === Query ===================================================================

// Shared line-reader: calls cb(json&) for every matching record
static void read_records(int64_t from_unix, int64_t to_unix,
                         const function<bool(const json&)>& filter,
                         const function<void(const json&)>& cb) {
    try {
        lock_guard<mutex> lk(g_stats_mutex);
        ifstream f(stats_file_path());
        string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            try {
                auto j = json::parse(line);
                int64_t ts = j.value("ts", (int64_t)0);
                if (ts < from_unix || ts > to_unix) continue;
                if (!filter(j)) continue;
                cb(j);
            } catch (...) {}
        }
    } catch (...) {}
}

StatSummary stat_query(int64_t from_unix, int64_t to_unix, const string& kind) {
    StatSummary s;
    map<string, int> counts;

    read_records(from_unix, to_unix,
        [&](const json& j) {
            const string& k = j.value("kind", "");
            if (!kind.empty() && k != kind) return false;
            if (k == "visitor" || k == "event") return false;
            return true;
        },
        [&](const json& j) {
            s.total++;
            if (j.value("ok", true)) s.successes++;
            else                     s.failures++;
            counts[j.value("name", "unknown")]++;
        });

    s.by_name.assign(counts.begin(), counts.end());
    std::sort(s.by_name.begin(), s.by_name.end(),
        [](const pair<string,int>& a, const pair<string,int>& b) { return a.second > b.second; });

    return s;
}

vector<DayBucket> stat_timeseries(int64_t from_unix, int64_t to_unix, const string& kind) {
    map<string, int> day_counts;

    read_records(from_unix, to_unix,
        [&](const json& j) {
            const string& k = j.value("kind", "");
            if (!kind.empty() && k != kind) return false;
            if (k == "event") return false;
            return true;
        },
        [&](const json& j) {
            day_counts[unix_to_date(j.value("ts", (int64_t)0))]++;
        });

    // Build sorted vector (fill gaps between from and to)
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
    set<string> seen;

    read_records(from_unix, to_unix,
        [](const json& j) -> bool {
            const string& k = j.value("kind", "");
            return k == "tool" || k == "download" || k == "visitor";
        },
        [&](const json& j) {
            string vh = j.value("vh", "");
            if (!vh.empty()) seen.insert(vh);
        });

    return (int)seen.size();
}

vector<pair<string,int>> stat_events(int64_t from_unix, int64_t to_unix) {
    map<string, int> counts;

    read_records(from_unix, to_unix,
        [](const json& j) -> bool { return j.value("kind", "") == "event"; },
        [&](const json& j) { counts[j.value("name", "unknown")]++; });

    vector<pair<string,int>> result(counts.begin(), counts.end());
    std::sort(result.begin(), result.end(),
        [](const pair<string,int>& a, const pair<string,int>& b) { return a.second > b.second; });
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

    auto today     = stat_query(day_start,  day_end,   "");
    auto tools     = stat_query(day_start,  day_end,   "tool");
    auto downloads = stat_query(day_start,  day_end,   "download");
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

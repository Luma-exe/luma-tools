/**
 * Luma Tools — Statistics tracking implementation
 */

#include "stats.h"
#include "discord.h"

// ─── Internal helpers ────────────────────────────────────────────────────────

static mutex g_stats_mutex;

static string stats_file_path() {
    // Store alongside the processing dir, one level up
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

// ─── Record ──────────────────────────────────────────────────────────────────

void stat_record(const string& kind, const string& name, bool ok) {
    try {
        json record = {
            {"ts",   now_unix()},
            {"kind", kind},
            {"name", name},
            {"ok",   ok}
        };

        lock_guard<mutex> lk(g_stats_mutex);
        ofstream f(stats_file_path(), std::ios::app);
        f << record.dump() << "\n";
    } catch (...) {}
}

// ─── Query ───────────────────────────────────────────────────────────────────

StatSummary stat_query(int64_t from_unix, int64_t to_unix, const string& kind) {
    StatSummary s;
    map<string, int> counts;

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
                if (!kind.empty() && j.value("kind", "") != kind) continue;

                s.total++;
                bool ok = j.value("ok", true);

                if (ok) s.successes++;
                else    s.failures++;

                string n = j.value("name", "unknown");
                counts[n]++;
            } catch (...) {}
        }
    } catch (...) {}

    // Sort by count descending
    s.by_name.assign(counts.begin(), counts.end());
    std::sort(s.by_name.begin(), s.by_name.end(),
        [](const pair<string,int>& a, const pair<string,int>& b){ return a.second > b.second; });

    return s;
}

// ─── Time helpers ────────────────────────────────────────────────────────────

// Returns the difference in seconds between local mktime and UTC.
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
    auto now  = std::chrono::system_clock::now();
    auto t    = std::chrono::system_clock::to_time_t(now);
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

// ─── Daily digest ─────────────────────────────────────────────────────────────

void stat_send_daily_digest() {
    int64_t day_start = stat_today_start();
    int64_t day_end   = day_start + 86399;
    int64_t prev_start = day_start - 86400;

    auto today    = stat_query(day_start,  day_end,   "");
    auto tools    = stat_query(day_start,  day_end,   "tool");
    auto downloads= stat_query(day_start,  day_end,   "download");
    auto yesterday= stat_query(prev_start, day_start - 1, "");

    // Build description
    string desc;

    // Totals with trend
    int delta = today.total - yesterday.total;
    string trend = (delta > 0) ? ("▲ " + to_string(delta))
                : (delta < 0) ? ("▼ " + to_string(-delta))
                : "▬ same";

    desc += "**Total requests:** " + to_string(today.total)
          + "  (" + trend + " vs yesterday)\n";
    desc += "**Tools:** " + to_string(tools.total)
          + "  |  **Downloads:** " + to_string(downloads.total) + "\n";

    if (today.failures > 0) {
        desc += "**Errors:** " + to_string(today.failures) + "\n";
    }

    // Top 5 tools
    if (!tools.by_name.empty()) {
        desc += "\n**Top Tools Today:**\n";
        int show = (int)std::min((int)tools.by_name.size(), 5);

        for (int i = 0; i < show; i++) {
            desc += "`" + tools.by_name[i].first + "` — "
                  + to_string(tools.by_name[i].second) + "\n";
        }
    }

    // Top 5 download platforms
    if (!downloads.by_name.empty()) {
        desc += "\n**Top Platforms Today:**\n";
        int show = (int)std::min((int)downloads.by_name.size(), 5);

        for (int i = 0; i < show; i++) {
            desc += "`" + downloads.by_name[i].first + "` — "
                  + to_string(downloads.by_name[i].second) + "\n";
        }
    }

    // 7-day rolling total
    auto week = stat_query(stat_days_ago(7), day_end, "");
    desc += "\n**Last 7 days:** " + to_string(week.total) + " total requests";

    discord_log("\xF0\x9F\x93\x8A Daily Digest", desc, 0x7C5CFF);
}

// ─── Scheduler ───────────────────────────────────────────────────────────────

void stat_start_daily_scheduler() {
    thread([]() {
        while (true) {
            // Sleep until next UTC midnight
            auto now = std::chrono::system_clock::now();
            auto t   = std::chrono::system_clock::to_time_t(now);
            struct tm gmt {};
#ifdef _WIN32
            gmtime_s(&gmt, &t);
#else
            gmtime_r(&t, &gmt);
#endif
            // Seconds remaining until next midnight UTC
            int secs_left = 86400 - (gmt.tm_hour * 3600 + gmt.tm_min * 60 + gmt.tm_sec);
            std::this_thread::sleep_for(std::chrono::seconds(secs_left));

            stat_send_daily_digest();
        }
    }).detach();
}

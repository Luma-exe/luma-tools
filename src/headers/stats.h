#pragma once
/**
 * Luma Tools - Statistics tracking
 *
 * Logs every tool use, download, visitor, and custom event to a JSON-lines
 * file on disk. Provides query functions for the dashboard and daily digest.
 *
 * Record shapes:
 *   tool/download: { "ts":<unix>, "kind":"tool"|"download", "name":"...", "ok":bool, "vh":"<ip_hash>" }
 *   visitor:       { "ts":<unix>, "kind":"visitor", "name":"page", "vh":"<ip_hash>" }
 *   event:         { "ts":<unix>, "kind":"event",   "name":"kofi_click"|"github_click"|... }
 */

#include "common.h"

// --- Record stat events ------------------------------------------------------

void stat_record(const string& kind, const string& name, bool ok = true, const string& ip = "");
void stat_record_event(const string& name);

// --- Query types -------------------------------------------------------------

struct StatSummary {
    int  total     = 0;
    int  successes = 0;
    int  failures  = 0;
    vector<pair<string, int>> by_name;
};

struct DayBucket {
    string date;
    int    count = 0;
};

StatSummary stat_query(int64_t from_unix, int64_t to_unix, const string& kind = "");
vector<DayBucket> stat_timeseries(int64_t from_unix, int64_t to_unix, const string& kind = "");
int stat_unique_visitors(int64_t from_unix, int64_t to_unix);
vector<pair<string,int>> stat_events(int64_t from_unix, int64_t to_unix);

// --- Time helpers ------------------------------------------------------------

int64_t stat_today_start();
int64_t stat_days_ago(int n);

// --- Daily digest ------------------------------------------------------------

void stat_send_daily_digest();
void stat_start_daily_scheduler();

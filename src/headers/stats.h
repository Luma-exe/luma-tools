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

// --- Database init (call once at startup before any other stat functions) ----

void stat_init_db();

// --- Record stat events ------------------------------------------------------

void stat_record(const string& kind, const string& name, bool ok = true, const string& ip = "");
void stat_record_event(const string& name);
void stat_record_ai_call(const string& tool, const string& model, int tokens_used, const string& ip = "");

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

struct AIModelBucket {
    string model;
    int    calls  = 0;
    int    tokens = 0;
};

struct AIToolBucket {
    string tool;
    string last_model;
    int    calls  = 0;
    int    tokens = 0;
};

struct AIStats {
    int  total_calls  = 0;
    int  total_tokens = 0;
    vector<AIModelBucket>  by_model;   // per-model aggregates
    vector<AIToolBucket>   by_tool;    // per-tool aggregates (calls, tokens, last model)
};

StatSummary stat_query(int64_t from_unix, int64_t to_unix, const string& kind = "");
vector<DayBucket> stat_timeseries(int64_t from_unix, int64_t to_unix, const string& kind = "");
int stat_unique_visitors(int64_t from_unix, int64_t to_unix);
vector<pair<string,int>> stat_events(int64_t from_unix, int64_t to_unix);
AIStats stat_query_ai(int64_t from_unix, int64_t to_unix);

// --- Time helpers ------------------------------------------------------------

int64_t stat_today_start();
int64_t stat_days_ago(int n);

// --- Daily digest ------------------------------------------------------------

void stat_send_daily_digest();
void stat_start_daily_scheduler();

// --- Tool config control (admin panel) --------------------------------------

struct ToolConfig {
    string tool_id;
    bool   enabled        = true;
    int    rate_limit_min = 0;   // 0 = use global limit
    int    max_file_mb    = 0;   // 0 = no extra limit
    int    max_text_chars = 0;   // 0 = no extra limit
    string note;
};

ToolConfig             get_tool_config(const string& tool_id);
void                   set_tool_config(const ToolConfig& cfg);
vector<ToolConfig>     get_all_tool_configs();

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

struct AccountUser {
    int    id = 0;
    string email;
    string display_name;
    string account_status;
    int64_t created_ts = 0;
    int64_t updated_ts = 0;
    string stripe_customer_id;
    string stripe_price_id;
    string stripe_subscription_id;
    string plan;
};

ToolConfig             get_tool_config(const string& tool_id);
void                   set_tool_config(const ToolConfig& cfg);
vector<ToolConfig>     get_all_tool_configs();

// --- Account / billing storage ----------------------------------------------

bool account_get_user_by_id(int user_id, AccountUser& out_user);
bool account_get_user_by_email(const string& email, AccountUser& out_user);
bool account_upsert_user(const string& email, const string& display_name, const string& password, AccountUser& out_user);
bool account_verify_password(const string& email, const string& password, AccountUser& out_user);
bool account_create_session(int user_id, const string& ip, const string& user_agent, string& out_token, int64_t& out_expires_ts);
bool account_get_user_by_session(const string& token, AccountUser& out_user);
bool account_delete_session(const string& token);
bool account_upsert_subscription(
    int user_id,
    const string& plan,
    const string& status,
    const string& stripe_customer_id,
    const string& stripe_subscription_id,
    const string& stripe_price_id,
    int64_t current_period_end_ts,
    const string& raw_json
);
bool account_find_user_by_stripe_customer_id(const string& stripe_customer_id, AccountUser& out_user);
bool account_find_user_by_stripe_subscription_id(const string& stripe_subscription_id, AccountUser& out_user);

// Admin helpers
vector<AccountUser> account_list_users(int limit = 200, int offset = 0, const string& search = "");
int  account_count_users(const string& search = "");
bool account_admin_set_plan(int user_id, const string& plan, const string& status);
bool account_admin_delete_user(int user_id);

// Lookup a user by display name (case-insensitive). For public profile pages.
bool account_get_user_by_display_name(const string& display_name, AccountUser& out_user);

// OAuth identity link (one row per (provider, provider_user_id)).
bool account_link_oauth_identity(const string& provider, const string& provider_user_id, int user_id);
bool account_find_user_by_oauth(const string& provider, const string& provider_user_id, AccountUser& out_user);

// Per-user counters bumped each time a tool runs / download completes.
void account_bump_tool_count(int user_id);
void account_bump_download_count(int user_id);
struct AccountStats { int tools_used = 0; int downloads = 0; };
AccountStats account_get_user_stats(int user_id);

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

// API keys (Pro feature) — Bearer auth for programmatic access.
// Token format: "lt_" + 32 random hex chars. Only the SHA-like hash is stored.
struct ApiKey {
    int    id = 0;
    int    user_id = 0;
    string key_prefix;   // first 8 chars of plaintext, for display ("lt_a1b2…")
    string name;
    string scopes;       // comma-separated scope list, or '*' for full access
    int64_t created_ts = 0;
    int64_t last_used_ts = 0;
    int64_t revoked_ts = 0;
};
bool account_api_key_create(int user_id, const string& name, const string& scopes,
                             string& out_plaintext, int& out_id);
vector<ApiKey> account_api_key_list(int user_id);
bool account_api_key_revoke(int user_id, int key_id);
// Verify a Bearer token; on hit returns the user, key scopes, and bumps
// last_used_ts. out_scopes lets the caller enforce scope checks per request.
bool account_find_user_by_api_key(const string& plaintext, AccountUser& out_user,
                                   string& out_scopes);

// AI top-up credits (one-off Stripe purchase). Free users hitting the daily
// quota consume credits before being blocked. Pro users ignore credits.
int  account_ai_credits(int user_id);
bool account_ai_credits_add(int user_id, int delta);  // negative to consume
bool account_ai_credits_consume(int user_id, int n);  // atomic: ok only if >= n

// Password reset (forgot-password) flow.
// Issues a single-use plaintext token; only its hash is stored. Token is valid
// for `ttl_seconds` (default 30 min). Returns false if user_id is invalid.
bool account_create_password_reset(int user_id, string& out_token, int ttl_seconds = 1800);
// Looks up an unexpired, unused reset token. On success, returns user_id;
// pass `mark_used=true` once you've actually applied the password change.
int  account_consume_password_reset(const string& token, bool mark_used);
// Hashes and stores a new password for a user. Also invalidates all sessions
// for that user (so an attacker with a stolen session is logged out).
bool account_update_password(int user_id, const string& new_password);
void account_invalidate_all_sessions(int user_id);

// Per-user counters bumped each time a tool runs / download completes.
void account_bump_tool_count(int user_id);
void account_bump_download_count(int user_id);
struct AccountStats { int tools_used = 0; int downloads = 0; };
AccountStats account_get_user_stats(int user_id);

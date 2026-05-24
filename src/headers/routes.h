#pragma once
/**
 * Luma Tools — Route registration
 */

#include "common.h"
#include "stats.h"

void register_download_routes(httplib::Server& svr, string dl_dir);
void register_tool_routes(httplib::Server& svr, string dl_dir);
void register_stats_routes(httplib::Server& svr);
void register_account_routes(httplib::Server& svr);

// ── Plan helpers (resolve the requester's billing plan from session cookie) ─
// Returns "pro" / "starter" / "free". Signed-out users are always "free".
string account_plan_for_request(const httplib::Request& req);
// Returns the signed-in user's id, or 0 if not signed in.
int    account_user_id_for_request(const httplib::Request& req);

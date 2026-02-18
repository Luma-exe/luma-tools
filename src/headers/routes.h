#pragma once
/**
 * Luma Tools — Route registration
 */

#include "common.h"

void register_download_routes(httplib::Server& svr, string dl_dir);
void register_tool_routes(httplib::Server& svr, string dl_dir);

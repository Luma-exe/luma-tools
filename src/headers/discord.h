#pragma once
/**
 * Luma Tools — Discord webhook logging
 */

#include "common.h"

// Send a rich embed to the configured Discord webhook
void discord_log(const string& title, const string& description, int color = 0x7C5CFF);

// Convenience helpers
void discord_log_download(const string& title, const string& platform, const string& format);
void discord_log_tool(const string& tool_name, const string& filename);
void discord_log_error(const string& context, const string& error);
void discord_log_server_start(int port, const string& version = "", const string& update_status = "");

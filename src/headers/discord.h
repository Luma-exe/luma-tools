#pragma once
/**
 * Luma Tools — Discord webhook logging
 */

#include "common.h"

// Send a rich embed to the configured Discord webhook
void discord_log(const string& title, const string& description, int color = 0x7C5CFF);

// Mask a filename: shows first 3 stem chars, replaces rest with *, keeps extension.
// e.g. "document.pdf" -> "doc******.pdf"
string mask_filename(const string& filename);

// Convenience helpers
void discord_log_download(const string& title, const string& platform, const string& format, const string& ip = "");
void discord_log_tool(const string& tool_name, const string& filename, const string& ip = "", const string& location = "server");
void discord_log_ai_tool(const string& tool_name, const string& filename, const string& model, int tokens_used, const string& ip = "", int tokens_remaining = -1);
void discord_log_error(const string& context, const string& error, const string& ip = "");
void discord_log_server_start(int port, const string& version = "");

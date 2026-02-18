/**
 * Luma Tools — Discord webhook logging implementation
 * Sends rich embeds to a Discord channel via webhook + curl.
 */

#include "discord.h"

static const string DISCORD_WEBHOOK =
    "https://discord.com/api/webhooks/1473490726678564916/"
    "Sd50rUgjDfiZIVYKDRxP63gzIEgdzD9KQw8Eu9NBVKV6XMnbcarYKXTOpelOm3Dbyv4W";

// ─── Internal: fire-and-forget POST via curl ────────────────────────────────

static void discord_send(const json& payload) {
    thread([payload]() {
        try {
            string tmp_dir = get_processing_dir();
            string tmp = tmp_dir + "/discord_" +
                to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".json";
            {
                ofstream f(tmp);
                f << payload.dump();
            }
            string cmd = "curl -s -X POST -H \"Content-Type: application/json\" -d @" +
                escape_arg(tmp) + " " + escape_arg(DISCORD_WEBHOOK);
            int code;
            exec_command(cmd, code);
            try { fs::remove(tmp); } catch (...) {}
        } catch (...) {}
    }).detach();
}

// ─── Get ISO-8601 timestamp ─────────────────────────────────────────────────

static string iso_now() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    struct tm gmt;
#ifdef _WIN32
    gmtime_s(&gmt, &t);
#else
    gmtime_r(&t, &gmt);
#endif
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gmt);
    return string(buf);
}

// ─── Public API ─────────────────────────────────────────────────────────────

void discord_log(const string& title, const string& description, int color) {
    json embed = {
        {"title",       title},
        {"description", description},
        {"color",       color},
        {"timestamp",   iso_now()},
        {"footer",      {{"text", "Luma Tools"}}}
    };
    json payload = {{"embeds", json::array({embed})}};
    discord_send(payload);
}

void discord_log_download(const string& title, const string& platform, const string& format) {
    string desc = "**Platform:** " + platform + "\n**Format:** " + format + "\n**Title:** " + title;
    discord_log("\xF0\x9F\x93\xA5 Download Started", desc, 0x3498DB);  // blue
}

void discord_log_tool(const string& tool_name, const string& filename) {
    string desc = "**Tool:** " + tool_name + "\n**File:** " + filename;
    discord_log("\xF0\x9F\x94\xA7 Tool Used", desc, 0x2ECC71);  // green
}

void discord_log_error(const string& context, const string& error) {
    string desc = "**Context:** " + context + "\n**Error:** " + error;
    discord_log("\xE2\x9D\x8C Error", desc, 0xE74C3C);  // red
}

void discord_log_server_start(int port) {
    string desc = "**Port:** " + to_string(port) + "\n**FFmpeg:** " +
        (g_ffmpeg_exe.empty() ? "not found" : "available") + "\n**yt-dlp:** " +
        (g_ytdlp_path.empty() ? "not found" : "available") + "\n**Ghostscript:** " +
        (g_ghostscript_path.empty() ? "not found" : "available");
    discord_log("\xF0\x9F\x9A\x80 Server Started", desc, 0x9B59B6);  // purple
}

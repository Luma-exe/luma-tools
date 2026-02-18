/**
 * Luma Tools — Discord webhook logging implementation
 * Sends rich embeds to a Discord channel via webhook + curl.
 */

#include "discord.h"

static const string DISCORD_WEBHOOK =
    "https://discord.com/api/webhooks/1473500051878838472/"
    "5twIcm0QuHOhO_Uy78KyzYwBnnYXxr7T_2DtqjX9r3xczrQOvVSc7l1-2XmKt0lMLhrC";

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

// ─── Filename masking ────────────────────────────────────────────────────────

// Shows first 3 chars of the stem, masks the rest with *, keeps the extension.
// e.g. "document.pdf" -> "doc******.pdf",  "ab.jpg" -> "ab*.jpg"
string mask_filename(const string& filename) {
    fs::path p(filename);
    string stem = p.stem().string();
    string ext  = p.extension().string();

    if (stem.empty()) return filename;

    size_t visible = (stem.size() < 3) ? stem.size() : 3;
    string masked  = stem.substr(0, visible) + string(stem.size() - visible, '*');
    return masked + ext;
}

// ─── Public API ─────────────────────────────────────────────────────────────

void discord_log(const string& title, const string& description, int color) {
    string footer_text = "Luma Tools";

    if (!g_hostname.empty()) footer_text += " | " + g_hostname;
    json embed = {
        {"title",       title},
        {"description", description},
        {"color",       color},
        {"timestamp",   iso_now()},
        {"footer",      {{"text", footer_text}}}
    };
    json payload = {{"embeds", json::array({embed})}};
    discord_send(payload);
}

void discord_log_download(const string& title, const string& platform, const string& format) {
    string desc = "**Platform:** " + platform + "\n**Format:** " + format + "\n**Title:** " + title;
    discord_log("\xF0\x9F\x93\xA5 Download Started", desc, 0x3498DB);  // blue
}

void discord_log_tool(const string& tool_name, const string& filename) {
    string desc = "**Tool:** " + tool_name + "\n**File:** " + mask_filename(filename);
    discord_log("\xF0\x9F\x94\xA7 Tool Used", desc, 0x2ECC71);  // green
}

void discord_log_error(const string& context, const string& error) {
    // Mask any filename-looking token in the error string (best-effort)
    // The raw error is preserved for context; callers passing filenames go through mask_filename.
    string desc = "**Context:** " + context + "\n**Error:** " + error;
    discord_log("\xE2\x9D\x8C Error", desc, 0xE74C3C);  // red
}

void discord_log_server_start(int port, const string& version) {
    string desc = "**Port:** " + to_string(port);

    if (!version.empty()) desc += "\n**Version:** " + version;
    desc += "\n**FFmpeg:** " +
        string(g_ffmpeg_exe.empty() ? "not found" : "available") + "\n**yt-dlp:** " +
        (g_ytdlp_path.empty() ? "not found" : "available") + "\n**Ghostscript:** " +
        (g_ghostscript_path.empty() ? "not found" : "available");
    discord_log("\xF0\x9F\x9A\x80 Server Started", desc, 0x9B59B6);  // purple
}

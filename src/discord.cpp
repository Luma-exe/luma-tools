/**
 * Luma Tools — Discord webhook logging implementation
 * Sends rich embeds to a Discord channel via webhook + curl.
 */

#include "discord.h"
#include "stats.h"

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║                        DISCORD CONFIGURATION                            ║
// ╠══════════════════════════════════════════════════════════════════════════╣
// ║  WEBHOOK_URL  — paste your Discord webhook URL here.                    ║
// ║                 Leave empty ("") to disable Discord logging entirely.   ║
// ║                                                                         ║
// ║  MASK_FILENAMES — true  = filenames are obfuscated in logs (default)    ║
// ║                   false = filenames appear as-is                        ║
// ╚══════════════════════════════════════════════════════════════════════════╝

// Pull Discord Webhook

static string discord_webhook_url() {
    const char* env = std::getenv("DISCORD_WEBHOOK_URL");
    return env ? string(env) : "";
}

static const string WEBHOOK_URL = discord_webhook_url();

static constexpr bool MASK_FILENAMES = true;

// ─────────────────────────────────────────────────────────────────────────────

// ─── Internal: fire-and-forget POST via curl ────────────────────────────────

static void discord_send(const json& payload) {
    if (WEBHOOK_URL.empty()) return;
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
                escape_arg(tmp) + " " + escape_arg(WEBHOOK_URL);
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

// Masks a filename preserving word boundaries.
// Each word > 3 chars: shows first 3 chars, replaces the rest with * (count
//   jittered by ±rand(1-7) so the original length can't be inferred).
// Each word ≤ 3 chars: replaced entirely with rand(1-7) stars (too short to
//   safely reveal any characters).
// Spaces between words are kept but their count is randomly 1-3.
// Extension is always preserved unchanged.
//
// e.g. "Luma GTA 5 Intro.mp4"  →  "Lum*  ***  *  ******.mp4"  (one possible output)
string mask_filename(const string& filename) {
    if (!MASK_FILENAMES) return filename;
    fs::path p(filename);
    string stem = p.stem().string();
    string ext  = p.extension().string();

    if (stem.empty()) return filename;

    // Per-call RNG — intentionally non-deterministic so repeated uploads of
    // the same file produce different masks.
    static std::mt19937 rng(std::random_device{}());
    auto rand_int = [&](int lo, int hi) -> int {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    // Split stem on spaces into words.
    vector<string> words;
    string buf;

    for (char c : stem) {
        if (c == ' ') {
            if (!buf.empty()) {
                words.push_back(buf);
                buf.clear();
            }
        } else {
            buf += c;
        }
    }

    if (!buf.empty()) words.push_back(buf);

    string result;

    for (size_t i = 0; i < words.size(); i++) {
        const string& word = words[i];
        string masked_word;

        if ((int)word.size() <= 3) {
            // Short word: replace entirety with 1-7 random stars.
            masked_word = string(rand_int(1, 7), '*');
        } else {
            // Longer word: reveal first 3 chars, mask remainder with jitter.
            int hidden  = (int)word.size() - 3;
            int jitter  = rand_int(-std::min(hidden - 1, 3), 7);
            int stars   = std::max(1, hidden + jitter);
            masked_word = word.substr(0, 3) + string(stars, '*');
        }

        result += masked_word;

        if (i + 1 < words.size()) {
            // Space count between words: randomly 1-3.
            result += string(rand_int(1, 3), ' ');
        }
    }

    return result + ext;
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

void discord_log_download(const string& title, const string& platform, const string& format, const string& ip) {
    stat_record("download", platform, true, ip);
    string desc = "**Platform:** " + platform + "\n**Format:** " + format + "\n**Title:** " + title;
    discord_log("\xF0\x9F\x93\xA5 Download Started", desc, 0x3498DB);  // blue
}

void discord_log_tool(const string& tool_name, const string& filename, const string& ip) {
    stat_record("tool", tool_name, true, ip);
    string display = MASK_FILENAMES ? mask_filename(filename) : filename;
    string desc = "**Tool:** " + tool_name + "\n**File:** " + display;
    discord_log("\xF0\x9F\x94\xA7 Tool Used", desc, 0x2ECC71);  // green
}

void discord_log_error(const string& context, const string& error, const string& ip) {
    stat_record("tool", context, false, ip);
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

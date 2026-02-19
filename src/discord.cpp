/**
 * Luma Tools — Discord webhook logging implementation
 * Sends rich embeds to a Discord channel via webhook + curl.
 */

#include "discord.h"
#include "stats.h"

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║                        DISCORD CONFIGURATION                            ║
// ╠══════════════════════════════════════════════════════════════════════════╣
// ║  WEBHOOK_URL  — Your Discord webhook now is an environemental variable  ║
// ║                                                                         ║
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
    string footer_text = "⚙️ Luma Tools";

    if (!g_hostname.empty()) footer_text += " • " + g_hostname;
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
    string desc = "🎬 **Platform** › `" + platform + "`\n"
                  "📦 **Format** › `" + format + "`\n"
                  "📝 **Title** › " + title;
    discord_log("📥 Media Download", desc, 0x5865F2);  // Discord blurple
}

void discord_log_tool(const string& tool_name, const string& filename, const string& ip, const string& location) {
    stat_record("tool", tool_name, true, ip);
    string display  = MASK_FILENAMES ? mask_filename(filename) : filename;
    string loc_icon = (location == "browser") ? "🔒 **In Browser**" : "🖥️ **On Server**";
    string desc = "🛠️ **Tool** › `" + tool_name + "`\n"
                  "📄 **File** › `" + display + "`\n"
                  "📍 **Location** › " + loc_icon;
    discord_log("⚡ Tool Executed", desc, 0x57F287);
}

void discord_log_ai_tool(const string& tool_name, const string& filename, const string& model, int tokens_used, const string& ip) {
    stat_record("tool", tool_name, true, ip);
    string display = MASK_FILENAMES ? mask_filename(filename) : filename;

    // Friendly model label
    string model_label = model;
    if (model == "llama-3.3-70b-versatile")           model_label = "Llama 3.3 70B (Primary)";
    else if (model == "deepseek-r1-distill-llama-70b") model_label = "DeepSeek R1 70B (Fallback 1)";
    else if (model == "llama-3.1-8b-instant")          model_label = "Llama 3.1 8B (Fallback 2)";
    else if (model.rfind("ollama:", 0) == 0)            model_label = "Local: " + model.substr(7) + " (Fallback 3)";

    string desc = "🤖 **Tool** › `" + tool_name + "`\n"
                  "📄 **File** › `" + display + "`\n"
                  "🧠 **Model** › " + model_label + "\n"
                  "🔢 **Tokens used** › `" + to_string(tokens_used) + "`";
    discord_log("🤖 AI Tool Executed", desc, 0xA855F7);  // Purple
}

void discord_log_error(const string& context, const string& error, const string& ip) {
    stat_record("tool", context, false, ip);
    string desc = "🔍 **Context** › `" + context + "`\n"
                  "💥 **Error** › " + error;
    discord_log("❌ Operation Failed", desc, 0xED4245);  // Discord red
}

void discord_log_server_start(int port, const string& version) {
    string desc = "🌐 **Port** › `" + to_string(port) + "`";

    if (!version.empty()) desc += "\n🏷️ **Version** › `" + version + "`";
    
    desc += "\n\n**📦 Dependencies**\n";
    desc += (g_ffmpeg_exe.empty() ? "❌" : "✅") + string(" FFmpeg\n");
    desc += (g_ytdlp_path.empty() ? "❌" : "✅") + string(" yt-dlp\n");
    desc += (g_ghostscript_path.empty() ? "❌" : "✅") + string(" Ghostscript");
    
    discord_log("🚀 Server Online", desc, 0x5865F2);  // Discord blurple
}

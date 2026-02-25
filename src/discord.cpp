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

void discord_log_ai_tool(const string& tool_name, const string& filename, const string& model, int tokens_used, const string& ip, int tokens_remaining) {
    stat_record("tool", tool_name, true, ip);
    string display = MASK_FILENAMES ? mask_filename(filename) : filename;

    // Friendly model label — matches 9-step chain (most powerful first)
    string model_label = model;
    if      (model == "llama-3.3-70b-versatile")           model_label = "Llama 3.3 70B (Step 1 · Groq)";
    else if (model == "llama-3.3-70b-specdec")             model_label = "Llama 3.3 70B Spec Dec (Step 2 · Groq)";
    else if (model == "deepseek-r1-distill-llama-70b")     model_label = "DeepSeek R1 · Llama 70B (Step 3 · Groq)";
    else if (model == "qwen-qwq-32b")                      model_label = "Qwen QwQ 32B (Step 4 · Groq)";
    else if (model == "deepseek-r1-distill-qwen-32b")      model_label = "DeepSeek R1 · Qwen 32B (Step 5 · Groq)";
    else if (model == "cerebras:llama-3.3-70b")            model_label = "Llama 3.3 70B (Step 6 · Cerebras)";
    else if (model == "gemini:gemini-2.0-flash")            model_label = "Gemini 2.0 Flash (Step 7 · Google)";
    else if (model == "llama-3.1-8b-instant")              model_label = "Llama 3.1 8B (Step 8 · Groq)";
    else if (model.rfind("ollama:", 0) == 0)               model_label = "Local: " + model.substr(7) + " (Step 9 · Ollama)";

    string desc = "🤖 **Tool** › `" + tool_name + "`\n"
                  "📄 **File** › `" + display + "`\n"
                  "🧠 **Model** › " + model_label + "\n"
                  "🔢 **Tokens used** › `" + to_string(tokens_used) + "`";
    if (tokens_remaining >= 0)
        desc += "\n📊 **Tokens remaining** › `" + to_string(tokens_remaining) + "`";
    discord_log("🤖 AI Tool Executed", desc, 0xA855F7);  // Purple
}

void discord_log_error(const string& context, const string& error, const string& ip) {
    stat_record("tool", context, false, ip);
    string desc = "🔍 **Context** › `" + context + "`\n"
                  "💥 **Error** › " + error;
    discord_log("❌ Operation Failed", desc, 0xED4245);  // Discord red
}

void discord_log_server_start(int port, const string& version) {
    // Capture all globals by value so the probe thread is self-contained.
    string cap_groq_key      = g_groq_key;
    string cap_cerebras_key  = g_cerebras_key;
    string cap_gemini_key    = g_gemini_key;
    string cap_ffmpeg    = g_ffmpeg_exe;
    string cap_ytdlp     = g_ytdlp_path;
    string cap_gs        = g_ghostscript_path;
    string cap_pandoc    = g_pandoc_path;
    string cap_deno      = g_deno_path;
    string cap_7z        = g_sevenzip_path;
    string cap_im        = g_imagemagick_path;
    bool   cap_rembg     = g_rembg_available;
    bool   cap_ollama    = g_ollama_available;
    string cap_commit    = g_git_commit;
    string cap_branch    = g_git_branch;
    string cap_hostname  = g_hostname;

    thread([=, port = port, version = version]() {
        // ── Probe each Groq model for tokens remaining (parallel) ─────────────
        // Listed in chain order: most powerful first
        static const vector<pair<string,string>> GROQ_PROBE_MODELS = {
            {"llama-3.3-70b-versatile",          "Llama 3.3 70B (Step 1)"},
            {"llama-3.3-70b-specdec",             "Llama 3.3 70B Spec Dec (Step 2)"},
            {"deepseek-r1-distill-llama-70b",     "DeepSeek R1 · Llama 70B (Step 3)"},
            {"qwen-qwq-32b",                      "Qwen QwQ 32B (Step 4)"},
            {"deepseek-r1-distill-qwen-32b",      "DeepSeek R1 · Qwen 32B (Step 5)"},
            {"llama-3.1-8b-instant",              "Llama 3.1 8B (Step 8)"},
        };

        map<string, int> tokens_map;  // model_id -> tokens_remaining (-1 = unknown)
        if (!cap_groq_key.empty()) {
            mutex tokens_mtx;
            vector<thread> probers;
            for (const auto& entry : GROQ_PROBE_MODELS) {
                probers.emplace_back([&, entry]() {
                    const string& model_id = entry.first;
                    try {
                        string tmp_dir  = get_processing_dir();
                        string safe_id  = model_id;
                        std::replace(safe_id.begin(), safe_id.end(), '-', '_');
                        string pf  = tmp_dir + "/gprobe_" + safe_id + "_pl.json";
                        string hf  = tmp_dir + "/gprobe_" + safe_id + "_hdr.txt";
                        string rf  = tmp_dir + "/gprobe_" + safe_id + "_resp.json";
                        string dhf = tmp_dir + "/gprobe_" + safe_id + "_dump.txt";

                        json probe_payload = {
                            {"model", model_id},
                            {"messages", json::array({{{"role","user"},{"content","hi"}}})},
                            {"max_tokens", 50}
                        };
                        { ofstream f(pf); f << probe_payload.dump(); }
                        { ofstream f(hf); f << "Authorization: Bearer " << cap_groq_key
                                            << "\r\nContent-Type: application/json"; }

                        string cmd =
                            "curl -s -X POST https://api.groq.com/openai/v1/chat/completions"
                            " -H @" + escape_arg(hf) +
                            " -D " + escape_arg(dhf) +
                            " -d @" + escape_arg(pf) +
                            " -o " + escape_arg(rf);
                        int rc; exec_command(cmd, rc);

                        // Parse x-ratelimit-remaining-tokens from header dump
                        int rem = -1;
                        if (fs::exists(dhf)) {
                            ifstream fh(dhf); string line;
                            static const string hkey = "x-ratelimit-remaining-tokens";
                            while (std::getline(fh, line)) {
                                if (line.size() > hkey.size() + 1 &&
                                    std::equal(hkey.begin(), hkey.end(), line.begin(),
                                        [](char a, char b){ return ::tolower(a)==::tolower(b); }) &&
                                    line[hkey.size()] == ':') {
                                    string v = line.substr(hkey.size() + 1);
                                    while (!v.empty() && (v.front()==' '||v.front()=='\t')) v.erase(v.begin());
                                    while (!v.empty() && (v.back()=='\r'||v.back()=='\n'))  v.pop_back();
                                    try { rem = std::stoi(v); } catch (...) {}
                                    break;
                                }
                            }
                        }
                        try { fs::remove(pf); fs::remove(hf); fs::remove(rf); fs::remove(dhf); } catch(...) {}
                        if (rem >= 0) { lock_guard<mutex> lk(tokens_mtx); tokens_map[model_id] = rem; }
                    } catch (...) {}
                });
            }
            for (auto& t : probers) t.join();
        }

        // ── Build embed description ───────────────────────────────────────────
        string desc = "🌐 **Port** › `" + to_string(port) + "`";
        if (!version.empty())      desc += "\n🏷️ **Version** › `" + version + "`";
        if (!cap_commit.empty())   desc += "\n📝 **Commit** › `" + cap_branch + "@" + cap_commit + "`";
        if (!cap_hostname.empty()) desc += "\n🖥️ **Host** › `" + cap_hostname + "`";

        // Core dependencies
        desc += "\n\n**📦 Core Dependencies**\n";
        desc += (cap_ffmpeg.empty()  ? "❌" : "✅") + string(" FFmpeg\n");
        desc += (cap_ytdlp.empty()   ? "❌" : "✅") + string(" yt-dlp\n");
        desc += (cap_gs.empty()      ? "❌" : "✅") + string(" Ghostscript\n");
        desc += (cap_pandoc.empty()  ? "❌" : "✅") + string(" Pandoc\n");
        desc += (cap_deno.empty()    ? "❌" : "✅") + string(" Deno");

        // Optional tools
        desc += "\n\n**🔧 Optional Tools**\n";
        desc += (cap_7z.empty()  ? "❌" : "✅") + string(" 7-Zip\n");
        desc += (cap_im.empty()  ? "❌" : "✅") + string(" ImageMagick\n");
        desc += (cap_rembg       ? "✅" : "❌") + string(" rembg\n");
        desc += (cap_ollama      ? "✅" : "❌") + string(" Ollama (local AI)");

        // AI model statuses — chain order: Groq Steps 1-5 → Cerebras 6 → Gemini 7 → Groq 8B 8 → Ollama 9
        if (!cap_groq_key.empty() || !cap_cerebras_key.empty() || !cap_gemini_key.empty()) {
            desc += "\n\n**🤖 AI Models (tokens remaining / status)**\n";

            // Groq models (Steps 1-5 and 8) — show probed token count
            // Steps 1-5 appear before Cerebras/Gemini; Step 8 (8B) at end of Groq section
            static const vector<pair<string,string>> GROQ_STEP_ORDER_MAIN = {
                {"llama-3.3-70b-versatile",      "Llama 3.3 70B (Step 1 · Groq)"},
                {"llama-3.3-70b-specdec",         "Llama 3.3 70B Spec Dec (Step 2 · Groq)"},
                {"deepseek-r1-distill-llama-70b", "DeepSeek R1 · Llama 70B (Step 3 · Groq)"},
                {"qwen-qwq-32b",                  "Qwen QwQ 32B (Step 4 · Groq)"},
                {"deepseek-r1-distill-qwen-32b",  "DeepSeek R1 · Qwen 32B (Step 5 · Groq)"},
            };
            for (const auto& entry : GROQ_STEP_ORDER_MAIN) {
                auto it = tokens_map.find(entry.first);
                if (it != tokens_map.end())
                    desc += "☁️ " + entry.second + " › `" + to_string(it->second) + " tok`\n";
                else
                    desc += "☁️ " + entry.second + " › *probe failed*\n";
            }

            // Cerebras Step 6
            desc += (!cap_cerebras_key.empty() ? "☁️" : "❌") +
                    string(" Llama 3.3 70B (Step 6 · Cerebras) › ") +
                    (!cap_cerebras_key.empty() ? "`key configured`" : "*no key*") + "\n";

            // Gemini Step 7
            desc += (!cap_gemini_key.empty() ? "☁️" : "❌") +
                    string(" Gemini 2.0 Flash (Step 7 · Google) › ") +
                    (!cap_gemini_key.empty() ? "`key configured`" : "*no key*") + "\n";

            // Groq 8B Step 8
            {
                auto it = tokens_map.find("llama-3.1-8b-instant");
                if (it != tokens_map.end())
                    desc += "☁️ Llama 3.1 8B (Step 8 · Groq) › `" + to_string(it->second) + " tok`\n";
                else
                    desc += "☁️ Llama 3.1 8B (Step 8 · Groq) › *probe failed*\n";
            }

            // Ollama Step 9
            desc += (cap_ollama ? "🏠 Llama 3.1 8B (Step 9 · Ollama) › `Unlimited`"
                                : "🏠 Ollama (Step 9) › *not running*");
        } else {
            desc += "\n\n❌ **No AI API keys set** — cloud AI tools unavailable";
            if (cap_ollama) desc += "\n🏠 Ollama (local) › `Unlimited`";
        }

        discord_log("🚀 Server Online", desc, 0x5865F2);
    }).detach();
}

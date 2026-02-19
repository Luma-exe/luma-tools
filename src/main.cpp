/**
 * Luma Tools — Main entry point
 * Server initialization, executable discovery, and startup.
 */

#include "common.h"
#include "discord.h"
#include "routes.h"
#include "stats.h"

int main() {
    httplib::Server svr;

    string public_dir = "public";

    if (!fs::exists(public_dir)) {
        if (fs::exists("../public"))      public_dir = "../public";
        else if (fs::exists("../../public")) public_dir = "../../public";
    }

    string dl_dir = get_downloads_dir();
    cout << "[Luma Tools] Downloads directory: " << fs::absolute(dl_dir) << endl;

    // ── Read hostname ─────────────────────────────────────────────────────────
    {
        const char* env_name = std::getenv("COMPUTERNAME");

        if (!env_name) env_name = std::getenv("HOSTNAME");
        if (env_name) g_hostname = env_name;
        else g_hostname = "Unknown";
        cout << "[Luma Tools] Hostname: " << g_hostname << endl;
    }

    // ── Read git info ────────────────────────────────────────────────────────
    {
        // Walk upward to find .git directory
        auto exe_dir = fs::absolute(".");
        auto search = exe_dir;

        while (!search.empty() && search.has_parent_path()) {
            if (fs::exists(search / ".git")) break;
            auto parent = search.parent_path();

            if (parent == search) { search = ""; break; }
            search = parent;
        }

        if (!search.empty() && fs::exists(search / ".git")) {
            string git_dir = search.string();
            int rc;

            // Auto-add safe.directory so git works under NSSM/SYSTEM user
            exec_command("git config --global --add safe.directory " + escape_arg(git_dir), rc);

            string commit = exec_command("git -C " + escape_arg(git_dir) + " rev-parse --short HEAD", rc);
            commit.erase(std::remove(commit.begin(), commit.end(), '\n'), commit.end());
            commit.erase(std::remove(commit.begin(), commit.end(), '\r'), commit.end());

            if (rc == 0 && !commit.empty() && commit.find("fatal") == string::npos) g_git_commit = commit;

            string branch = exec_command("git -C " + escape_arg(git_dir) + " rev-parse --abbrev-ref HEAD", rc);
            branch.erase(std::remove(branch.begin(), branch.end(), '\n'), branch.end());
            branch.erase(std::remove(branch.begin(), branch.end(), '\r'), branch.end());

            if (rc == 0 && !branch.empty() && branch.find("fatal") == string::npos) g_git_branch = branch;

            cout << "[Luma Tools] Git: " << g_git_branch << "@" << g_git_commit << endl;

            // Check for updates in a background thread (git fetch can hang under NSSM/SYSTEM)
            _putenv_s("GIT_TERMINAL_PROMPT", "0"); // prevent git from waiting for credentials
            string captured_git_dir = git_dir;
            std::thread([captured_git_dir]() {
                int rc;
                exec_command("git -C " + escape_arg(captured_git_dir) + " fetch --quiet 2>&1", rc);

                if (rc == 0) {
                    string behind_str = exec_command(
                        "git -C " + escape_arg(captured_git_dir) + " rev-list --count HEAD..@{u}", rc);
                    behind_str.erase(std::remove(behind_str.begin(), behind_str.end(), '\n'), behind_str.end());
                    behind_str.erase(std::remove(behind_str.begin(), behind_str.end(), '\r'), behind_str.end());

                    if (rc == 0 && !behind_str.empty() && behind_str.find("fatal") == string::npos) {
                        int behind = std::atoi(behind_str.c_str());

                        if (behind > 0) {
                            cout << "[Luma Tools] \033[33mUpdate available: " << behind
                                 << " commit" << (behind > 1 ? "s" : "") << " behind\033[0m" << endl;
                        } else {
                            cout << "[Luma Tools] \033[32mUp to date\033[0m" << endl;
                        }
                    }
                } else {
                    cout << "[Luma Tools] Update check skipped (fetch failed)" << endl;
                }
            }).detach();
        }
    }

    // ── Refresh PATH from system registry ────────────────────────────────────
    refresh_system_path();

    // ── Find yt-dlp ─────────────────────────────────────────────────────────
    g_ytdlp_path = find_ytdlp();

    if (g_ytdlp_path.empty()) {
        cerr << "[Luma Tools] WARNING: yt-dlp not found! Downloads will fail." << endl;
        cerr << "[Luma Tools] Install it: pip install yt-dlp" << endl;
        g_ytdlp_path = "yt-dlp"; // fallback
    } else {
        int code;
        string ver = exec_command(escape_arg(g_ytdlp_path) + " --version", code);
        ver.erase(std::remove(ver.begin(), ver.end(), '\n'), ver.end());
        ver.erase(std::remove(ver.begin(), ver.end(), '\r'), ver.end());
        cout << "[Luma Tools] yt-dlp found: " << g_ytdlp_path << " (v" << ver << ")" << endl;
    }

    // ── Find ffmpeg ─────────────────────────────────────────────────────────
    string ffmpeg_full = find_executable("ffmpeg");

    if (!ffmpeg_full.empty()) {
        g_ffmpeg_exe  = ffmpeg_full;                                  // full path to the exe
        g_ffmpeg_path = fs::path(ffmpeg_full).parent_path().string(); // directory only
        cout << "[Luma Tools] ffmpeg found: " << g_ffmpeg_exe << endl;
        cout << "[Luma Tools] ffmpeg dir:   " << g_ffmpeg_path << endl;
    } else {
        cerr << "[Luma Tools] WARNING: ffmpeg not found. Media processing will fail." << endl;
    }

    // ── Find deno ───────────────────────────────────────────────────────────
    g_deno_path = find_executable("deno");

    if (!g_deno_path.empty()) {
        cout << "[Luma Tools] deno found: " << g_deno_path << endl;
    }

    // ── Find Ghostscript (for PDF tools) ─────────────────────────────────────
    g_ghostscript_path = find_ghostscript();

    if (!g_ghostscript_path.empty()) {
        cout << "[Luma Tools] Ghostscript found: " << g_ghostscript_path << endl;
    } else {
        cerr << "[Luma Tools] WARNING: Ghostscript not found. PDF tools will be limited." << endl;
    }

    // ── Find Pandoc (for Markdown → PDF) ─────────────────────────────────────
    g_pandoc_path = find_pandoc();

    if (!g_pandoc_path.empty()) {
        cout << "[Luma Tools] Pandoc found: " << g_pandoc_path << endl;
    } else {
        cerr << "[Luma Tools] WARNING: Pandoc not found. Markdown to PDF will be unavailable." << endl;
    }

    // ── Groq key (for AI Study Notes) ────────────────────────────────────────
    {
        const char* key = std::getenv("GROQ_API_KEY");
        if (key && key[0]) {
            g_groq_key = key;
            cout << "[Luma Tools] Groq API key loaded from environment." << endl;
        } else {
            cerr << "[Luma Tools] WARNING: GROQ_API_KEY not set. AI Study Notes will be unavailable." << endl;
        }
    }

    // ── Add ffmpeg & deno directories to process PATH ────────────────────────
    {
        string current_path;
        const char* env_path = std::getenv("PATH");

        if (env_path) current_path = env_path;

        bool modified = false;

        if (!g_ffmpeg_path.empty() && current_path.find(g_ffmpeg_path) == string::npos) {
            current_path = g_ffmpeg_path + ";" + current_path;
            modified = true;
            cout << "[Luma Tools] Added ffmpeg dir to PATH" << endl;
        }

        if (!g_deno_path.empty()) {
            string deno_dir = fs::path(g_deno_path).parent_path().string();

            if (current_path.find(deno_dir) == string::npos) {
                current_path = deno_dir + ";" + current_path;
                modified = true;
                cout << "[Luma Tools] Added deno dir to PATH" << endl;
            }
        }

        if (modified) {
            _putenv_s("PATH", current_path.c_str());
        }
    }

    // ── Serve static files ──────────────────────────────────────────────────
    svr.set_mount_point("/", public_dir);
    svr.set_mount_point("/downloads", dl_dir);

    // Allow large file uploads (500 MB) and generous timeouts for video processing
    svr.set_payload_max_length(500 * 1024 * 1024);
    svr.set_read_timeout(300, 0);
    svr.set_write_timeout(300, 0);

    // CORS headers + rate limiting on /api/tools/*
    static std::mutex g_rate_mutex;
    static std::unordered_map<std::string, std::pair<int, std::time_t>> g_rate_map;
    // Per-tool per-IP rate limit map: tool_id -> ip -> {count, window_start}
    static std::unordered_map<std::string, std::unordered_map<std::string, std::pair<int,std::time_t>>> g_tool_rate_map;
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

        if (req.path.find("/api/tools/") == 0 && req.method == "POST") {
            // Extract tool id from path: /api/tools/<tool-id>
            string tool_id = req.path.substr(11); // after "/api/tools/"
            auto slash = tool_id.find('/');
            if (slash != string::npos) tool_id = tool_id.substr(0, slash);

            // Check tool enabled/config
            ToolConfig cfg = get_tool_config(tool_id);
            if (!cfg.enabled) {
                res.status = 503;
                res.set_content(json({{"error", "This tool is currently disabled by the administrator."}}).dump(), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }

            auto now = std::time(nullptr);
            auto ip  = req.remote_addr;
            std::lock_guard<std::mutex> lk(g_rate_mutex);

            // Global rate limit: 30 requests per 60 seconds per IP
            auto& entry = g_rate_map[ip];
            if (now - entry.second >= 60) { entry.first = 0; entry.second = now; }
            entry.first++;
            if (entry.first > 30) {
                res.status = 429;
                res.set_header("Retry-After", "60");
                res.set_content(R"({"error":"Too many requests. Please wait a moment."})", "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }

            // Per-tool rate limit (if configured)
            if (cfg.rate_limit_min > 0) {
                auto& tool_map = g_tool_rate_map[tool_id];
                auto& tool_entry = tool_map[ip];
                if (now - tool_entry.second >= 60) { tool_entry.first = 0; tool_entry.second = now; }
                tool_entry.first++;
                if (tool_entry.first > cfg.rate_limit_min) {
                    res.status = 429;
                    res.set_header("Retry-After", "60");
                    res.set_content(json({{"error", "Rate limit exceeded for this tool. Please wait a moment."}}).dump(), "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
            }
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // ── Startup file cleanup — remove stale processing files (> 30 min old) ──
    {
        string proc_dir = get_processing_dir();
        int cleaned = 0;
        try {
            auto cutoff = fs::file_time_type::clock::now() - std::chrono::minutes(30);
            for (auto& entry : fs::directory_iterator(proc_dir)) {
                if (!entry.is_regular_file()) continue;
                auto mtime = fs::last_write_time(entry.path());
                if (mtime < cutoff) {
                    fs::remove(entry.path());
                    ++cleaned;
                }
            }
        } catch (...) {}
        if (cleaned > 0)
            cout << "[Luma Tools] Startup cleanup: removed " << cleaned << " stale temp file(s)" << endl;
    }

    // ── Initialise stats database (opens/creates stats.db, migrates jsonl) ──
    stat_init_db();

    // ── Register all routes ─────────────────────────────────────────────────
    register_download_routes(svr, dl_dir);
    register_tool_routes(svr, dl_dir);
    register_stats_routes(svr);

    // ── Start the server ────────────────────────────────────────────────────
    int port = 8080;
    const char* port_env = std::getenv("PORT");

    if (port_env) port = std::atoi(port_env);

    // Build version string for banner
    string ver_line = "    Universal Media Toolkit v2.1";

    if (g_git_commit != "unknown") {
        ver_line += "  (" + g_git_branch + "@" + g_git_commit + ")";
    }

    cout << R"(
  ╦  ╦ ╦╔╦╗╔═╗  ╔╦╗╔═╗╔═╗╦  ╔═╗
  ║  ║ ║║║║╠═╣   ║ ║ ║║ ║║  ╚═╗
  ╩═╝╚═╝╩ ╩╩ ╩   ╩ ╚═╝╚═╝╩═╝╚═╝
)";
    cout << ver_line << endl;
    cout << endl;

    cout << "[Luma Tools] Server starting on http://localhost:" << port << endl;
    cout << "[Luma Tools] Static files: " << fs::absolute(public_dir) << endl;
    cout << "[Luma Tools] Press Ctrl+C to stop" << endl;

    // Log server start to Discord
    string discord_ver;

    if (g_git_commit != "unknown") {
        discord_ver = g_git_branch + "@" + g_git_commit;
    }

    discord_log_server_start(port, discord_ver);

    // Start daily stats digest scheduler
    stat_start_daily_scheduler();

    // Warn if stats dashboard has no password configured
    if (!std::getenv("STATS_PASSWORD")) {
        cerr << "[Luma Tools] WARNING: STATS_PASSWORD not set. Stats dashboard is disabled." << endl;
        cerr << "[Luma Tools]          Set it to enable: set STATS_PASSWORD=yourpassword" << endl;
    } else {
        cout << "[Luma Tools] Stats dashboard enabled at /stats" << endl;
    }

    if (!std::getenv("DISCORD_WEBHOOK_URL")) {
        cerr << "[Luma Tools] WARNING: DISCORD_WEBHOOK_URL not set. Discord logging is disabled." << endl;
        cerr << "[Luma Tools]          Set it to enable: set DISCORD_WEBHOOK_URL=your_webhook_url" << endl;
    } else {
        cout << "[Luma Tools] Discord logging enabled." << endl;
    }

    if (!svr.listen("0.0.0.0", port)) {
        cerr << "[Luma Tools] Failed to start server on port " << port << endl;
        return 1;
    }

    return 0;
}

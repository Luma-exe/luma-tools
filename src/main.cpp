/**
 * Luma Tools — Main entry point
 * Server initialization, executable discovery, and startup.
 */

#include "common.h"
#include "discord.h"
#include "routes.h"

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
            string commit = exec_command("git -C " + escape_arg(git_dir) + " rev-parse --short HEAD", rc);
            commit.erase(std::remove(commit.begin(), commit.end(), '\n'), commit.end());
            commit.erase(std::remove(commit.begin(), commit.end(), '\r'), commit.end());
            if (rc == 0 && !commit.empty()) g_git_commit = commit;

            string branch = exec_command("git -C " + escape_arg(git_dir) + " rev-parse --abbrev-ref HEAD", rc);
            branch.erase(std::remove(branch.begin(), branch.end(), '\n'), branch.end());
            branch.erase(std::remove(branch.begin(), branch.end(), '\r'), branch.end());
            if (rc == 0 && !branch.empty()) g_git_branch = branch;

            cout << "[Luma Tools] Git: " << g_git_branch << "@" << g_git_commit << endl;
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

    // CORS headers
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // ── Register all routes ─────────────────────────────────────────────────
    register_download_routes(svr, dl_dir);
    register_tool_routes(svr, dl_dir);

    // ── Start the server ────────────────────────────────────────────────────
    int port = 8080;
    const char* port_env = std::getenv("PORT");
    if (port_env) port = std::atoi(port_env);

    cout << R"(
  ╦  ╦ ╦╔╦╗╔═╗  ╔╦╗╔═╗╔═╗╦  ╔═╗
  ║  ║ ║║║║╠═╣   ║ ║ ║║ ║║  ╚═╗
  ╩═╝╚═╝╩ ╩╩ ╩   ╩ ╚═╝╚═╝╩═╝╚═╝
    Universal Media Toolkit v2.1
)" << endl;

    cout << "[Luma Tools] Server starting on http://localhost:" << port << endl;
    cout << "[Luma Tools] Static files: " << fs::absolute(public_dir) << endl;
    cout << "[Luma Tools] Press Ctrl+C to stop" << endl;

    // Log server start to Discord
    discord_log_server_start(port);

    if (!svr.listen("0.0.0.0", port)) {
        cerr << "[Luma Tools] Failed to start server on port " << port << endl;
        return 1;
    }

    return 0;
}

/**
 * Luma Tools — Main entry point
 * Server initialization, executable discovery, and startup.
 */

#include "common.h"
#include "discord.h"
#include "routes.h"
#include "stats.h"

// ── Free-plan caps (shared between pre-routing enforcement and /quota endpoint)
static const int64_t FREE_MAX_UPLOAD_BYTES = 100LL * 1024 * 1024;
static const int     FREE_AI_DAILY_QUOTA   = 20;
static std::mutex g_ai_quota_mutex;
static std::unordered_map<std::string, std::pair<int, std::time_t>> g_ai_quota_map;

// AI endpoints that count against the free daily quota.
static bool is_ai_endpoint(const std::string& path) {
    if (path == "/api/mind-map" || path == "/api/youtube-summary") return true;
    if (path.rfind("/api/tools/", 0) != 0) return false;
    string tail = path.substr(11);
    auto slash = tail.find('/');
    if (slash != string::npos) tail = tail.substr(0, slash);
    return tail == "paraphrase" || tail == "study-notes" || tail == "flashcards" ||
           tail == "quiz" || tail == "citation-generate";
}

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
#ifdef _WIN32
            _putenv_s("GIT_TERMINAL_PROMPT", "0"); // prevent git from waiting for credentials
#else
            setenv("GIT_TERMINAL_PROMPT", "0", 1);
#endif
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
    g_deno_path = find_deno();

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

    // ── AI API keys ───────────────────────────────────────────────────────────
    {
        const char* key = std::getenv("GROQ_API_KEY");
        if (key && key[0]) {
            g_groq_key = key;
            cout << "[Luma Tools] Groq API key loaded from environment." << endl;
        } else {
            cerr << "[Luma Tools] WARNING: GROQ_API_KEY not set. AI Study Notes will be unavailable." << endl;
        }
    }
    {
        const char* key = std::getenv("CEREBRAS_API_KEY");
        if (key && key[0]) {
            g_cerebras_key = key;
            cout << "[Luma Tools] Cerebras API key loaded from environment." << endl;
        }
    }
    {
        const char* key = std::getenv("GEMINI_API_KEY");
        if (key && key[0]) {
            g_gemini_key = key;
            cout << "[Luma Tools] Gemini API key loaded from environment." << endl;
        }
    }

    // ── Find optional tools: 7-Zip, ImageMagick, rembg, Ollama ──────────────
    {
        // 7-Zip
        g_sevenzip_path = find_executable("7z", {"C:\\Program Files\\7-Zip"});
        if (g_sevenzip_path.empty())
            g_sevenzip_path = find_executable("7za");
        if (!g_sevenzip_path.empty())
            cout << "[Luma Tools] 7-Zip found: " << g_sevenzip_path << endl;
        else
            cout << "[Luma Tools] 7-Zip not found (optional)" << endl;

        // ImageMagick (try 'magick' first, then 'convert')
        g_imagemagick_path = find_executable("magick");
        if (g_imagemagick_path.empty())
            g_imagemagick_path = find_executable("convert");
        if (!g_imagemagick_path.empty())
            cout << "[Luma Tools] ImageMagick found: " << g_imagemagick_path << endl;
        else
            cout << "[Luma Tools] ImageMagick not found (optional)" << endl;

        // rembg — find rembg.exe by existence check (don't rely on exit code of --help)
        {
            // Helper: checks if rembg.exe exists at a path; if so marks available + patches PATH
            auto try_rembg = [&](const string& candidate) -> bool {
                if (!fs::exists(candidate)) return false;
                string scripts_dir = fs::path(candidate).parent_path().string();
                const char* cur = std::getenv("PATH");
#ifdef _WIN32
                string new_path = scripts_dir + ";" + (cur ? cur : "");
                _putenv_s("PATH", new_path.c_str());
#else
                string new_path = scripts_dir + ":" + (cur ? cur : "");
                setenv("PATH", new_path.c_str(), 1);
#endif
                g_rembg_available = true;
                cout << "[Luma Tools] rembg found: " << candidate << endl;
                return true;
            };

            // 1. Try bare command first (already on PATH)
            int rc;
            exec_command("rembg --version 2>&1", rc);
            if (rc == 0) {
                g_rembg_available = true;
            } else {
                // 2. Scan all Python installs for rembg.exe
                vector<string> scripts_roots;

                // C:\Program Files\Python*\Scripts  (system-wide pip install)
                for (const string& pf_base : {"C:\\Program Files", "C:\\Program Files (x86)"}) {
                    try {
                        for (const auto& e : fs::directory_iterator(pf_base)) {
                            if (!e.is_directory()) continue;
                            if (e.path().filename().string().rfind("Python", 0) == 0)
                                scripts_roots.push_back(e.path().string() + "\\Scripts");
                        }
                    } catch (...) {}
                }

                // C:\Users\*\AppData\Local\Programs\Python\Python*\Scripts  (user pip install)
                // C:\Users\*\AppData\Roaming\Python\Python*\Scripts
                try {
                    for (const auto& user : fs::directory_iterator("C:\\Users")) {
                        if (!user.is_directory()) continue;
                        string base = user.path().string();
                        for (const string& sub : {
                            "\\AppData\\Local\\Programs\\Python",
                            "\\AppData\\Roaming\\Python"}) {
                            string pybase = base + sub;
                            if (!fs::exists(pybase)) continue;
                            try {
                                for (const auto& ver : fs::directory_iterator(pybase)) {
                                    if (ver.is_directory())
                                        scripts_roots.push_back(ver.path().string() + "\\Scripts");
                                }
                            } catch (...) {}
                        }
                        // Also check flat Roaming\Python\Scripts
                        scripts_roots.push_back(base + "\\AppData\\Roaming\\Python\\Scripts");
                    }
                } catch (...) {}

                for (const auto& scripts : scripts_roots) {
                    if (try_rembg(scripts + "\\rembg.exe")) break;
                }
            }
            cout << "[Luma Tools] rembg: " << (g_rembg_available ? "available" : "not found (optional)") << endl;
        }

        // Ollama — probe by querying the local REST endpoint
        {
            int rc;
            string resp = exec_command(
                "curl -s --max-time 3 http://localhost:11434/api/tags", rc);
            // Ollama returns JSON with a "models" key on success
            g_ollama_available = (rc == 0 && resp.find("\"models\"") != string::npos);
            cout << "[Luma Tools] Ollama: " << (g_ollama_available ? "available" : "not found (optional)") << endl;
        }
    }

    // ── Add ffmpeg & deno directories to process PATH ────────────────────────
    {
        string current_path;
        const char* env_path = std::getenv("PATH");

        if (env_path) current_path = env_path;

        bool modified = false;
#ifdef _WIN32
        const string path_sep = ";";
#else
        const string path_sep = ":";
#endif

        if (!g_ffmpeg_path.empty() && current_path.find(g_ffmpeg_path) == string::npos) {
            current_path = g_ffmpeg_path + path_sep + current_path;
            modified = true;
            cout << "[Luma Tools] Added ffmpeg dir to PATH" << endl;
        }

        if (!g_deno_path.empty()) {
            string deno_dir = fs::path(g_deno_path).parent_path().string();

            if (current_path.find(deno_dir) == string::npos) {
                current_path = deno_dir + path_sep + current_path;
                modified = true;
                cout << "[Luma Tools] Added deno dir to PATH" << endl;
            }
        }

        if (modified) {
#ifdef _WIN32
            _putenv_s("PATH", current_path.c_str());
#else
            setenv("PATH", current_path.c_str(), 1);
#endif
        }
    }

    // ── Serve static files ──────────────────────────────────────────────────
    // sw.js and index.html must never be cached so updates are always picked up
    svr.Get("/sw.js", [&](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(public_dir + "/sw.js", std::ios::binary);
        if (f) {
            std::ostringstream ss; ss << f.rdbuf();
            res.set_content(ss.str(), "text/javascript");
            res.set_header("Cache-Control", "no-store, no-cache, must-revalidate");
            res.set_header("Pragma", "no-cache");
        } else { res.status = 404; }
    });
    svr.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(public_dir + "/index.html", std::ios::binary);
        if (f) {
            std::ostringstream ss; ss << f.rdbuf();
            res.set_content(ss.str(), "text/html; charset=utf-8");
            res.set_header("Cache-Control", "no-store, no-cache, must-revalidate");
            res.set_header("Pragma", "no-cache");
        } else { res.status = 404; }
    });

    svr.set_mount_point("/", public_dir);
    svr.set_mount_point("/downloads", dl_dir);

    // Prevent Cloudflare/browser from caching local JS/CSS/HTML indefinitely.
    // Static files served via mount_point have no Cache-Control by default.
    svr.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        const auto& p = req.path;
        if ((p.size() > 3 && (p.rfind(".js") == p.size()-3 || p.rfind(".css") == p.size()-4)) ||
            p == "/" || p.rfind(".html") == p.size()-5) {
            if (res.get_header_value("Cache-Control").empty()) {
                res.set_header("Cache-Control", "no-cache, must-revalidate");
            }
        }
    });

    // Allow large file uploads (2 GB hard cap — Pro plan). Free users are
    // further limited to 100 MB by the plan-enforcement block below.
    svr.set_payload_max_length(2LL * 1024 * 1024 * 1024);
    svr.set_read_timeout(300, 0);
    svr.set_write_timeout(300, 0);
    // Per-connection idle timeout — without this, slow/dead clients pin worker
    // threads in the thread pool until exhaustion.
    svr.set_idle_interval(0, 500'000'000);  // 0.5s

    // Bump the thread pool well above httplib's default (≈ hardware
    // concurrency). With AI calls that can legitimately take 60s and a busy
    // tunnel, the default exhausts and the listener stops accepting.
    svr.new_task_queue = [] {
        return new httplib::ThreadPool(64);
    };

    // Heartbeat thread: prints to stderr every 30s so docker logs prove the
    // process is alive AND the runtime isn't blocked. If this stops, we know
    // something is wrong with the whole process (not just the listener).
    std::thread([]() {
        int64_t start = (int64_t)std::time(nullptr);
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            cerr << "[Luma Tools] heartbeat uptime=" << ((int64_t)std::time(nullptr) - start) << "s" << endl;
        }
    }).detach();

    // CORS headers + rate limiting on /api/tools/*
    static std::mutex g_rate_mutex;
    static std::unordered_map<std::string, std::pair<int, std::time_t>> g_rate_map;
    // Per-tool per-IP rate limit map: tool_id -> ip -> {count, window_start}
    static std::unordered_map<std::string, std::unordered_map<std::string, std::pair<int,std::time_t>>> g_tool_rate_map;

    // ── Plan-based enforcement (Free vs Pro) ────────────────────────────────
    // Caps and state are defined at file scope (FREE_MAX_UPLOAD_BYTES, etc.)
    // so the /api/account/quota endpoint can read the same counters.

    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        // Required for SharedArrayBuffer / ffmpeg.wasm (crossOriginIsolated = true)
        res.set_header("Cross-Origin-Opener-Policy", "same-origin");
        res.set_header("Cross-Origin-Embedder-Policy", "credentialless");

        // ── Extract real client IP ──────────────────────────────────────────────
        // Behind a reverse proxy (Caddy/nginx), req.remote_addr is always 127.0.0.1.
        // X-Forwarded-For contains the real client IP set by the proxy.
        string real_ip = req.remote_addr;
        if (req.has_header("X-Forwarded-For")) {
            string xff = req.get_header_value("X-Forwarded-For");
            // Take only the first (leftmost) address — the original client.
            auto comma = xff.find(',');
            if (comma != string::npos) xff = xff.substr(0, comma);
            // Trim whitespace
            auto s = xff.find_first_not_of(" \t");
            if (s != string::npos) xff = xff.substr(s);
            auto e = xff.find_last_not_of(" \t\r\n");
            if (e != string::npos) xff = xff.substr(0, e + 1);
            if (!xff.empty()) real_ip = xff;
        }
        if (real_ip == "::1") real_ip = "127.0.0.1";

        // ── Record a visitor entry for every page/tool GET request ─────────────
        // Only record for non-localhost IPs so dev traffic isn't counted.
        // stat_unique_visitors() uses COUNT(DISTINCT vh) so duplicate records
        // from the same IP in the same session count as exactly 1 unique visitor.
        if (req.method == "GET" &&
            real_ip != "127.0.0.1" && real_ip != "::1" && !real_ip.empty() &&
            req.path.find("/api/") != 0 &&
            req.path != "/favicon.svg" &&
            req.path.rfind("/downloads/", 0) != 0) {
            stat_record("visitor", "page", true, real_ip);
        }

        // ── Per-user counters (public profile badge) ────────────────────────
        //    Optimistic: bumped at request time, regardless of success. Tiny
        //    inflation from failures is acceptable for a vanity counter.
        if (req.method == "POST" &&
            (req.path.find("/api/tools/") == 0 ||
             req.path == "/api/download" ||
             is_ai_endpoint(req.path))) {
            int uid = account_user_id_for_request(req);
            if (uid > 0) {
                if (req.path == "/api/download") account_bump_download_count(uid);
                else                             account_bump_tool_count(uid);
            }
        }

        // ── Plan enforcement: applies to all POST /api/tools/* + AI endpoints ──
        bool is_tool_post  = (req.path.find("/api/tools/") == 0 && req.method == "POST");
        bool is_ai_post    = (req.method == "POST" && is_ai_endpoint(req.path));
        if (is_tool_post || is_ai_post) {
            string plan = account_plan_for_request(req);
            bool is_pro = (plan == "pro" || plan == "starter");

            // (a0) Batch processing is Pro-only. Frontend tags batch jobs
            //      with the X-Lt-Batch header.
            if (!is_pro && req.has_header("X-Lt-Batch") &&
                req.get_header_value("X-Lt-Batch") != "0") {
                res.status = 402;
                res.set_content(json({
                    {"error", "Batch processing is a Pro feature. Process files one at a time on the Free plan, or upgrade for unlimited batches."},
                    {"plan_required", "pro"}
                }).dump(), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }

            // (a) Upload-size cap for free users.
            if (!is_pro && req.has_header("Content-Length")) {
                int64_t clen = 0;
                try { clen = std::stoll(req.get_header_value("Content-Length")); } catch (...) {}
                if (clen > FREE_MAX_UPLOAD_BYTES) {
                    res.status = 413;
                    res.set_content(json({
                        {"error", "Files over 100 MB are a Pro feature. Upgrade at /account/login to lift the limit to 2 GB."},
                        {"plan_required", "pro"},
                        {"max_bytes", FREE_MAX_UPLOAD_BYTES}
                    }).dump(), "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
            }

            // (b) Daily AI-call quota for free users.
            if (!is_pro && is_ai_post) {
                int uid = account_user_id_for_request(req);
                string key = (uid > 0) ? ("u:" + to_string(uid)) : ("ip:" + real_ip);
                std::time_t now = std::time(nullptr);
                std::lock_guard<std::mutex> lk(g_ai_quota_mutex);
                auto& slot = g_ai_quota_map[key];
                if (now - slot.second >= 24 * 3600) { slot.first = 0; slot.second = now; }
                if (slot.first >= FREE_AI_DAILY_QUOTA) {
                    int64_t retry = 24 * 3600 - (now - slot.second);
                    if (retry < 60) retry = 60;
                    res.status = 429;
                    res.set_header("Retry-After", to_string(retry));
                    res.set_content(json({
                        {"error", "Daily AI limit reached (20/day on Free). Upgrade to Pro for unlimited AI requests."},
                        {"plan_required", "pro"},
                        {"quota", FREE_AI_DAILY_QUOTA},
                        {"retry_after_seconds", retry}
                    }).dump(), "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
                slot.first++;
            }
        }

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
            auto ip  = real_ip;  // use resolved real IP for rate limiting
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
        res.set_header("Cross-Origin-Opener-Policy", "same-origin");
        res.set_header("Cross-Origin-Embedder-Policy", "credentialless");
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
                // Never delete database or migration files — these must survive restarts.
                auto ext = entry.path().extension().string();
                if (ext == ".db" || ext == ".jsonl" || ext == ".migrated") continue;
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
    register_account_routes(svr);

    // ── /health — uptime monitoring (UptimeRobot, BetterStack, etc.) ────────
    //    Public, no auth. Returns version/uptime; HEAD returns 200 with no body.
    static const int64_t k_started_ts = (int64_t)std::time(nullptr);
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        int64_t now = (int64_t)std::time(nullptr);
        json payload = {
            {"ok", true},
            {"service", "luma-tools"},
            {"version", g_git_commit},
            {"branch",  g_git_branch},
            {"uptime_seconds", now - k_started_ts},
            {"now_unix", now}
        };
        res.set_header("Cache-Control", "no-store");
        res.set_content(payload.dump(), "application/json");
    });
    svr.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
        // Minimal 200/OK for k8s-style liveness probes.
        res.set_content("ok", "text/plain");
    });

    // ── /api/account/quota — what the current requester can do and how much
    //     of their daily AI quota they have left. Used by the frontend to show
    //     "X of 20 AI requests left today" and to enable Pro-only UI affordances.
    svr.Get("/api/account/quota", [](const httplib::Request& req, httplib::Response& res) {
        string plan = account_plan_for_request(req);
        bool is_pro = (plan == "pro" || plan == "starter");
        int uid = account_user_id_for_request(req);

        // Compute the same key the pre-routing handler uses.
        string real_ip = req.remote_addr;
        if (req.has_header("X-Forwarded-For")) {
            string xff = req.get_header_value("X-Forwarded-For");
            auto comma = xff.find(',');
            if (comma != string::npos) xff = xff.substr(0, comma);
            auto s = xff.find_first_not_of(" \t");
            if (s != string::npos) xff = xff.substr(s);
            if (!xff.empty()) real_ip = xff;
        }
        if (real_ip == "::1") real_ip = "127.0.0.1";

        int ai_used = 0;
        int64_t reset_in = 0;
        {
            string key = (uid > 0) ? ("u:" + to_string(uid)) : ("ip:" + real_ip);
            std::lock_guard<std::mutex> lk(g_ai_quota_mutex);
            auto it = g_ai_quota_map.find(key);
            if (it != g_ai_quota_map.end()) {
                std::time_t now = std::time(nullptr);
                if (now - it->second.second < 24 * 3600) {
                    ai_used  = it->second.first;
                    reset_in = 24 * 3600 - (now - it->second.second);
                }
            }
        }

        json payload = {
            {"plan", plan},
            {"signed_in", uid > 0},
            {"ai", {
                {"used", ai_used},
                {"quota", is_pro ? -1 : FREE_AI_DAILY_QUOTA},
                {"remaining", is_pro ? -1 : std::max(0, FREE_AI_DAILY_QUOTA - ai_used)},
                {"unlimited", is_pro},
                {"resets_in_seconds", reset_in}
            }},
            {"upload", {
                {"max_bytes", is_pro ? (int64_t)(2LL * 1024 * 1024 * 1024) : FREE_MAX_UPLOAD_BYTES},
                {"max_mb",    is_pro ? 2048 : 100}
            }},
            {"batch_allowed", is_pro}
        };
        res.set_header("Cache-Control", "no-store");
        res.set_content(payload.dump(), "application/json");
    });

    // ── Start the server ────────────────────────────────────────────────────
    int port = 8080;
    const char* port_env = std::getenv("PORT");

    if (port_env) port = std::atoi(port_env);

    // Build version string for banner
    string ver_line = "    Universal Media Toolkit v2.2.3";

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

    // Billing/account scaffolding for the upcoming paid tiers.
    if (!std::getenv("SESSION_SECRET")) {
        cerr << "[Luma Tools] WARNING: SESSION_SECRET not set. Account sessions will not be enabled yet." << endl;
    }
    if (!std::getenv("STRIPE_SECRET_KEY")) {
        cerr << "[Luma Tools] WARNING: STRIPE_SECRET_KEY not set. Paid checkout will be disabled until billing is configured." << endl;
    }
    if (!std::getenv("STRIPE_WEBHOOK_SECRET")) {
        cerr << "[Luma Tools] WARNING: STRIPE_WEBHOOK_SECRET not set. Stripe webhook syncing will be disabled." << endl;
    }

    if (!std::getenv("DISCORD_WEBHOOK_URL")) {
        cerr << "[Luma Tools] WARNING: DISCORD_WEBHOOK_URL not set. Discord logging is disabled." << endl;
        cerr << "[Luma Tools]          Set it to enable: set DISCORD_WEBHOOK_URL=your_webhook_url" << endl;
    } else {
        cout << "[Luma Tools] Discord logging enabled." << endl;
    }

    // ── Per-request exception safety net ────────────────────────────────────
    //    Without this, an uncaught exception in any handler propagates up to
    //    httplib's worker thread and can corrupt server state (worst case:
    //    the accept loop stops scheduling new work). With it, we always send
    //    a JSON 500 and keep the listener healthy.
    svr.set_exception_handler([](const httplib::Request& req, httplib::Response& res,
                                  std::exception_ptr ep) {
        string msg = "Internal error";
        try { if (ep) std::rethrow_exception(ep); }
        catch (const std::exception& e) { msg = e.what(); }
        catch (...)                     { msg = "unknown exception"; }
        cerr << "[Luma Tools] Handler threw on " << req.method << " " << req.path
             << " : " << msg << endl;
        try {
            discord_log("⚠️ luma-tools handler exception",
                        "**" + req.method + "** `" + req.path + "`\n```\n" + msg + "\n```",
                        0xF59E0B);
        } catch (...) {}
        res.status = 500;
        res.set_content(json({{"error", "Server hit an unexpected error. Please try again."},
                              {"path", req.path}}).dump(), "application/json");
    });

    // ── Listener + crash recovery ────────────────────────────────────────────
    //    Wrap svr.listen() so any exception that escapes a handler is logged
    //    instead of silently killing the accept loop (which is what caused the
    //    "process alive but unresponsive" outage on 2026-05-25). On exception
    //    or false return we exit non-zero so Docker's restart-policy recreates
    //    the container; the HEALTHCHECK below also catches the stuck state.
    try {
        bool ok = svr.listen("0.0.0.0", port);
        if (!ok) {
            cerr << "[Luma Tools] svr.listen returned false (port " << port
                 << " busy? socket error?). Exiting so Docker restarts us." << endl;
            return 1;
        }
    } catch (const std::exception& e) {
        cerr << "[Luma Tools] FATAL: listener threw uncaught exception: "
             << e.what() << ". Exiting." << endl;
        try {
            discord_log("🚨 luma-tools listener crashed",
                        string("Uncaught exception: ") + e.what() +
                        "\nDocker will restart the container.",
                        0xEF4444);
        } catch (...) {}
        return 2;
    } catch (...) {
        cerr << "[Luma Tools] FATAL: listener threw unknown exception. Exiting." << endl;
        return 2;
    }

    return 0;
}

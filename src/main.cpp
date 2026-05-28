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

// ── Priority-lane in-flight counters (Pro skips the free queue) ─────────────
static const int FREE_MAX_INFLIGHT = 4;
static const int PRO_MAX_INFLIGHT  = 32;
static std::atomic<int> g_free_inflight{0};
static std::atomic<int> g_pro_inflight{0};

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
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, X-LT-Key");
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
        //    Bumped at request time. Most tools are browser-side WASM and
        //    record stats via /api/browser-tool, NOT /api/tools/* — so the
        //    profile counter MUST include /api/browser-tool, otherwise heavy
        //    browser-tool users see 0 forever (which is exactly the bug that
        //    bit a Pro user on 2026-05-26).
        if (req.method == "POST" &&
            (req.path.find("/api/tools/") == 0 ||
             req.path == "/api/download" ||
             req.path == "/api/browser-tool" ||
             is_ai_endpoint(req.path))) {
            int uid = account_user_id_for_request(req);
            if (uid > 0) {
                if (req.path == "/api/download") account_bump_download_count(uid);
                else                             account_bump_tool_count(uid);
            }
        }

        // ── API key validation (X-LT-Key header) ───────────────────────────────
        // If a valid key is present, track the call count and allow the request
        // to proceed with free-tier limits. Invalid keys are rejected immediately.
        static std::mutex s_apikey_mutex;
        static const int API_FREE_DAILY_LIMIT = 200;
        if (req.has_header("X-LT-Key") &&
            req.path.rfind("/api/keys/", 0) != 0) { // don't validate on key-mgmt routes
            string provided_key = req.get_header_value("X-LT-Key");
            string key_file = get_processing_dir() + "/api_keys.json";
            bool key_valid = false;
            bool key_quota_ok = true;
            {
                lock_guard<std::mutex> lk(s_apikey_mutex);
                try {
                    json store;
                    if (fs::exists(key_file)) {
                        ifstream f(key_file);
                        store = json::parse(f);
                    } else {
                        store = {{"keys", json::array()}};
                    }
                    for (auto& k : store["keys"]) {
                        if (k.value("key", "") == provided_key && k.value("active", false)) {
                            // Check daily call limit (reset based on UTC day)
                            std::time_t now = std::time(nullptr);
                            int64_t last_reset = k.value("day_reset", (int64_t)0);
                            int today_calls    = k.value("today_calls", 0);
                            // Reset counter if a new UTC day has started
                            if (now - last_reset >= 86400) {
                                k["today_calls"] = 0;
                                k["day_reset"]   = (int64_t)now;
                                today_calls      = 0;
                            }
                            if (today_calls >= API_FREE_DAILY_LIMIT) {
                                key_quota_ok = false;
                            } else {
                                k["calls"]       = k.value("calls", 0) + 1;
                                k["today_calls"] = today_calls + 1;
                                key_valid = true;
                            }
                            // Persist updated counts
                            ofstream fw(key_file);
                            fw << store.dump(2);
                            break;
                        }
                    }
                } catch (...) {}
            }
            if (!key_quota_ok) {
                res.status = 429;
                res.set_content(json({
                    {"error", "API key daily limit reached (200 requests/day on the free tier). Resets every 24 hours."},
                    {"limit", API_FREE_DAILY_LIMIT}
                }).dump(), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
            if (!key_valid && !provided_key.empty()) {
                res.status = 401;
                res.set_content(R"({"error":"Invalid or revoked API key. Generate a new key at tools.lumaplayground.com/#api-access"})",
                                "application/json");
                return httplib::Server::HandlerResponse::Handled;
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
            //     Order: quota counter first, then fall back to AI top-up
            //     credits (consumes one). Rate-limit headers on EVERY response
            //     so script writers can back off cleanly.
            if (!is_pro && is_ai_post) {
                int uid = account_user_id_for_request(req);
                string key = (uid > 0) ? ("u:" + to_string(uid)) : ("ip:" + real_ip);
                std::time_t now = std::time(nullptr);
                int used = 0;
                int64_t reset_at = now;
                {
                    std::lock_guard<std::mutex> lk(g_ai_quota_mutex);
                    auto& slot = g_ai_quota_map[key];
                    if (now - slot.second >= 24 * 3600) { slot.first = 0; slot.second = now; }
                    used     = slot.first;
                    reset_at = slot.second + 24 * 3600;
                    bool over_daily = used >= FREE_AI_DAILY_QUOTA;
                    if (over_daily) {
                        // Try a top-up credit before refusing. Signed-in users only.
                        bool used_credit = (uid > 0) && account_ai_credits_consume(uid, 1);
                        if (!used_credit) {
                            int64_t retry = reset_at - now;
                            if (retry < 60) retry = 60;
                            int credits = (uid > 0) ? account_ai_credits(uid) : 0;
                            res.status = 429;
                            res.set_header("Retry-After", to_string(retry));
                            res.set_header("X-RateLimit-Limit",     to_string(FREE_AI_DAILY_QUOTA));
                            res.set_header("X-RateLimit-Remaining", "0");
                            res.set_header("X-RateLimit-Reset",     to_string(reset_at));
                            res.set_content(json({
                                {"error", "Daily AI limit reached (20/day on Free). Buy a top-up at /account or upgrade to Pro for unlimited AI."},
                                {"plan_required", "pro"},
                                {"quota", FREE_AI_DAILY_QUOTA},
                                {"credits_remaining", credits},
                                {"retry_after_seconds", retry}
                            }).dump(), "application/json");
                            return httplib::Server::HandlerResponse::Handled;
                        }
                        // Credit consumed → don't bump daily counter.
                    } else {
                        slot.first++;
                        used = slot.first;
                    }
                }
                int remaining = (used >= FREE_AI_DAILY_QUOTA) ? 0 : (FREE_AI_DAILY_QUOTA - used);
                res.set_header("X-RateLimit-Limit",     to_string(FREE_AI_DAILY_QUOTA));
                res.set_header("X-RateLimit-Remaining", to_string(remaining));
                res.set_header("X-RateLimit-Reset",     to_string(reset_at));
            } else if (is_pro && is_ai_post) {
                // Pro users get sentinel "unlimited" headers so clients don't
                // panic when remaining never drops.
                res.set_header("X-RateLimit-Limit",     "unlimited");
                res.set_header("X-RateLimit-Remaining", "unlimited");
            }
        }

        // ── Priority lane: rate-limit free traffic separately from pro ─────
        //    Free users share a small concurrency budget; Pro users get their
        //    own much larger one. A flood of free requests therefore can't
        //    starve paying customers, fulfilling the "Priority queue" pricing
        //    claim. Quick-running endpoints (analytics, /healthz, static)
        //    are not throttled — only POSTs to /api/tools/* and /api/download.
        //    Counters live at file scope (above main) so the post-routing
        //    handler can release them.
        if ((req.path.find("/api/tools/") == 0 || req.path == "/api/download") &&
            req.method == "POST") {
            string plan = account_plan_for_request(req);
            bool is_pro = (plan == "pro" || plan == "starter");
            auto& counter = is_pro ? g_pro_inflight : g_free_inflight;
            int    cap    = is_pro ? PRO_MAX_INFLIGHT : FREE_MAX_INFLIGHT;
            if (counter.load() >= cap) {
                res.status = 503;
                res.set_header("Retry-After", "5");
                res.set_content(json({
                    {"error", is_pro
                        ? "Pro queue is full right now (very rare — try in a few seconds)."
                        : "Server is busy. Pro users skip this queue — upgrade for priority processing."},
                    {"plan_required", is_pro ? "" : "pro"}
                }).dump(), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
            // Reserve a slot. The post-routing handler below decrements it.
            counter.fetch_add(1);
            // Stash the plan in a response header we own so the post-routing
            // handler knows which counter to release. (httplib has no native
            // per-request user-data dict.) Header is stripped before send.
            res.set_header("X-Lt-Lane", is_pro ? "pro" : "free");
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

    // Post-routing: release the priority-lane slot we reserved in pre-routing.
    // The pre-routing block set X-Lt-Lane = "pro"|"free" to remember which
    // counter to decrement; we strip that header before sending so clients
    // never see it.
    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        string lane = res.get_header_value("X-Lt-Lane");
        if (lane == "pro")  { g_pro_inflight.fetch_sub(1); res.headers.erase("X-Lt-Lane"); }
        else if (lane == "free") { g_free_inflight.fetch_sub(1); res.headers.erase("X-Lt-Lane"); }
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

    // ── /sitemap.xml ─────────────────────────────────────────────────────
    svr.Get("/sitemap.xml", [](const httplib::Request&, httplib::Response& res) {
        const vector<string> TOOL_IDS = {
            "ai-study-notes","ai-coverage","ai-flashcards","ai-quiz","ai-paraphrase",
            "citation-gen","mind-map","youtube-summary",
            "downloader","bulk-install",
            "image-compress","image-resize","image-convert","image-crop","image-watermark",
            "image-bg-remove","image-upscale","ocr","metadata-strip","favicon-generate",
            "redact","screenshot-annotate","color-palette",
            "video-compress","video-trim","video-convert","video-extract-audio",
            "video-to-gif","gif-to-video","gif-frame-remove","video-remove-audio",
            "video-speed","video-frame","video-stabilize","subtitle-extract",
            "audio-convert","audio-normalize","audio-trim","audio-separate",
            "pdf-compress","pdf-merge","pdf-split","pdf-to-images","pdf-to-word",
            "word-to-pdf","images-to-pdf","markdown-to-pdf",
            "resume-builder","invoice-gen",
            "qr-generate","hash-generate","archive-extract","base64","json-format",
            "color-convert","markdown-preview","diff-checker","word-counter","csv-json",
            "unix-date","regex-tester","code-beautify","uuid-gen","url-encode",
            "password-gen","jwt-decode","api-access",
        };
        const vector<string> DOWNLOADER_SLUGS = {
            "youtube-downloader","tiktok-downloader","instagram-downloader",
            "spotify-downloader","twitter-downloader","reddit-downloader",
            "facebook-downloader","twitch-downloader","vimeo-downloader",
        };
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n"
            << "  <url><loc>https://tools.lumaplayground.com/</loc><priority>1.0</priority></url>\n";
        for (const auto& slug : DOWNLOADER_SLUGS) {
            xml << "  <url><loc>https://tools.lumaplayground.com/" << slug
                << "</loc><priority>0.9</priority></url>\n";
        }
        for (const auto& id : TOOL_IDS) {
            xml << "  <url><loc>https://tools.lumaplayground.com/tools/" << id
                << "</loc><priority>0.8</priority></url>\n";
        }
        xml << "</urlset>";
        res.set_header("Content-Type", "application/xml");
        res.set_content(xml.str(), "application/xml");
    });

    // ── Platform-specific downloader SEO pages ───────────────────────────
    // High-traffic search terms: "youtube downloader", "tiktok downloader", etc.
    // Each page has a rich, keyword-targeted title + description then
    // JS-redirects directly into the Media Downloader tool in the SPA.
    {
        struct PlatformPage {
            string slug;       // URL path (no leading slash)
            string platform;   // Display name
            string desc;       // SEO description
        };
        const vector<PlatformPage> PLATFORMS = {
            {"youtube-downloader", "YouTube Downloader",
             "Free YouTube video downloader — download YouTube videos as MP4 or MP3 in any quality. "
             "No signup, no account, no ads. Paste the URL and download instantly."},
            {"tiktok-downloader", "TikTok Downloader",
             "Download TikTok videos without watermark — free, fast, and no account required. "
             "Save TikTok videos as MP4 or extract the audio as MP3 in seconds."},
            {"instagram-downloader", "Instagram Downloader",
             "Download Instagram videos, Reels, and stories for free. "
             "No login, no account needed — paste the URL and save any Instagram video instantly."},
            {"spotify-downloader", "Spotify Downloader",
             "Download Spotify songs and playlists as MP3 for free. "
             "No account or Spotify Premium required — just paste the track or playlist URL."},
            {"twitter-downloader", "Twitter / X Video Downloader",
             "Download videos from Twitter and X for free. "
             "Save any tweet video or GIF as MP4 — no signup, no extensions, just paste the URL."},
            {"reddit-downloader", "Reddit Video Downloader",
             "Download Reddit videos with audio for free — no account, no watermark. "
             "Paste any Reddit post URL and save the video or GIF instantly."},
            {"facebook-downloader", "Facebook Video Downloader",
             "Download Facebook videos for free — public posts, Reels, and Watch videos. "
             "No login required. Paste the URL and save as MP4 in HD."},
            {"twitch-downloader", "Twitch Clip Downloader",
             "Download Twitch clips and VOD highlights as MP4 for free. "
             "No Twitch account needed — paste the clip URL and download instantly."},
            {"vimeo-downloader", "Vimeo Downloader",
             "Download Vimeo videos for free in any quality. "
             "No account needed — paste the Vimeo URL and save as MP4 or MP3."},
        };

        for (const auto& p : PLATFORMS) {
            string captured_slug     = p.slug;
            string captured_platform = p.platform;
            string captured_desc     = p.desc;
            string canonical = "https://tools.lumaplayground.com/" + captured_slug;

            svr.Get("/" + captured_slug, [=](const httplib::Request&, httplib::Response& res) {
                std::ostringstream html;
                html << "<!DOCTYPE html><html lang=\"en\"><head>"
                     << "<meta charset=\"UTF-8\">"
                     << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                     << "<title>" << captured_platform << " — Free, No Signup | Luma Tools</title>"
                     << "<meta name=\"description\" content=\"" << captured_desc << "\">"
                     << "<link rel=\"canonical\" href=\"" << canonical << "\">"
                     << "<meta property=\"og:type\" content=\"website\">"
                     << "<meta property=\"og:url\" content=\"" << canonical << "\">"
                     << "<meta property=\"og:title\" content=\"" << captured_platform << " — Free, No Signup | Luma Tools\">"
                     << "<meta property=\"og:description\" content=\"" << captured_desc << "\">"
                     << "<meta property=\"og:image\" content=\"https://tools.lumaplayground.com/og/" << captured_slug << ".svg\">"
                     << "<meta name=\"twitter:card\" content=\"summary_large_image\">"
                     << "<meta name=\"twitter:title\" content=\"" << captured_platform << " — Free, No Signup | Luma Tools\">"
                     << "<meta name=\"twitter:description\" content=\"" << captured_desc << "\">"
                     << "<meta name=\"twitter:image\" content=\"https://tools.lumaplayground.com/og/" << captured_slug << ".svg\">"
                     << "<meta name=\"theme-color\" content=\"#7c5cff\">"
                     << "<script>window.location.replace('https://tools.lumaplayground.com/#downloader');</script>"
                     << "<noscript><meta http-equiv=\"refresh\" content=\"0;url=https://tools.lumaplayground.com/#downloader\"></noscript>"
                     << "</head><body style=\"background:#0a0a0f;color:#f0f0f5;font-family:system-ui;display:flex;align-items:center;justify-content:center;height:100vh;margin:0\">"
                     << "<div style=\"text-align:center\">"
                     << "<p style=\"font-size:1.1rem;font-weight:600\">" << captured_platform << "</p>"
                     << "<p style=\"color:#8888a0;font-size:.9rem;margin-top:6px\">Loading Luma Tools…</p>"
                     << "</div>"
                     << "</body></html>";
                res.set_header("Cache-Control", "public, max-age=3600");
                res.set_content(html.str(), "text/html");
            });
        }
    }

    // ── /tools/:id — SEO landing page for each tool ──────────────────────
    // Returns a fully-indexed HTML page (proper title, OG tags) then
    // JS-redirects into the SPA so users land in the app automatically.
    svr.Get(R"(/tools/([a-z0-9-]+))", [](const httplib::Request& req, httplib::Response& res) {
        const string tool_id = req.matches[1];
        // Simple display name: replace hyphens with spaces, capitalise words
        string display = tool_id;
        bool cap = true;
        for (char& c : display) {
            if (c == '-') { c = ' '; cap = true; }
            else if (cap) { c = toupper(c); cap = false; }
        }
        const string desc = "Free online " + display + " tool — no upload, no account needed. "
                            "Part of Luma Tools, 65+ free browser tools for students, creators and developers.";
        const string canonical = "https://tools.lumaplayground.com/tools/" + tool_id;
        const string spaUrl    = "https://tools.lumaplayground.com/#" + tool_id;
        std::ostringstream html;
        html << "<!DOCTYPE html><html lang=\"en\"><head>"
             << "<meta charset=\"UTF-8\">"
             << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             << "<title>" << display << " — Luma Tools</title>"
             << "<meta name=\"description\" content=\"" << desc << "\">"
             << "<link rel=\"canonical\" href=\"" << canonical << "\">"
             << "<meta property=\"og:type\" content=\"website\">"
             << "<meta property=\"og:url\" content=\"" << canonical << "\">"
             << "<meta property=\"og:title\" content=\"" << display << " — Luma Tools\">"
             << "<meta property=\"og:description\" content=\"" << desc << "\">"
             << "<meta property=\"og:image\" content=\"https://tools.lumaplayground.com/icon-512.png\">"
             << "<meta name=\"twitter:card\" content=\"summary\">"
             << "<meta name=\"twitter:title\" content=\"" << display << " — Luma Tools\">"
             << "<meta name=\"twitter:description\" content=\"" << desc << "\">"
             << "<meta name=\"theme-color\" content=\"#7c5cff\">"
             << "<script>window.location.replace('" << spaUrl << "');</script>"
             << "<noscript><meta http-equiv=\"refresh\" content=\"0;url=" << spaUrl << "\"></noscript>"
             << "</head><body style=\"background:#0a0a0f;color:#f0f0f5;font-family:system-ui;display:flex;align-items:center;justify-content:center;height:100vh;margin:0\">"
             << "<div style=\"text-align:center\">"
             << "<p style=\"font-size:1.1rem;font-weight:600\">" << display << "</p>"
             << "<p style=\"color:#8888a0;font-size:.9rem;margin-top:6px\">Loading Luma Tools…</p>"
             << "</div>"
             << "</body></html>";
        res.set_header("Cache-Control", "public, max-age=3600");
        res.set_content(html.str(), "text/html");
    });

    // /status + /api/status routes are no longer here — that whole feature
    // lives in the separate Luma Status repo + container at
    // status.lumaplayground.com (see github.com/Luma-exe/luma-status).
    // Both old paths now 302 to the standalone site for any old bookmarks.
    svr.Get("/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Location", "https://status.lumaplayground.com/");
        res.status = 302;
    });
    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Location", "https://status.lumaplayground.com/api/status");
        res.status = 302;
    });


    // ── /og/<tool-id>.svg — per-tool social-card image, generated on demand.
    //    Twitter / Discord / Slack will fetch this when someone shares a tool
    //    URL. Pure SVG (no font deps inside the container) — browsers + most
    //    crawlers render it; for the few that demand a raster the same SVG is
    //    valid through Cloudflare's image-resizing service.
    svr.Get(R"(/og/([a-zA-Z0-9_-]+)\.svg)", [](const httplib::Request& req, httplib::Response& res) {
        string raw = req.matches[1].str();
        // Human-readable label: hyphen → space, capitalise words.
        string label;
        bool cap = true;
        for (char c : raw) {
            if (c == '-' || c == '_') { label += ' '; cap = true; }
            else if (cap)             { label += (char)std::toupper((unsigned char)c); cap = false; }
            else                      { label += c; }
        }
        // Custom raw-string delimiter (SVG) so the embedded )" in `url(#g)" `
        // doesn't terminate the literal.
        std::ostringstream svg;
        svg << R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="630" viewBox="0 0 1200 630">)SVG"
            << R"SVG(<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">)SVG"
            << R"SVG(<stop offset="0" stop-color="#7c5cff"/><stop offset="1" stop-color="#ff6bca"/></linearGradient></defs>)SVG"
            << R"SVG(<rect width="1200" height="630" fill="#0a0a0f"/>)SVG"
            << R"SVG(<circle cx="940" cy="220" r="260" fill="url(#g)" opacity="0.35"/>)SVG"
            << R"SVG(<circle cx="240" cy="480" r="220" fill="#00d4ff" opacity="0.25"/>)SVG"
            << R"SVG(<text x="80" y="170" font-family="-apple-system,Segoe UI,Roboto,sans-serif" font-size="34" font-weight="700" fill="#7c5cff" letter-spacing="3">LUMA TOOLS</text>)SVG"
            << R"SVG(<text x="80" y="310" font-family="-apple-system,Segoe UI,Roboto,sans-serif" font-size="92" font-weight="800" fill="#fff">)SVG"
            << label
            << R"SVG(</text>)SVG"
            << R"SVG(<text x="80" y="380" font-family="-apple-system,Segoe UI,Roboto,sans-serif" font-size="34" font-weight="500" fill="#a7a7b5">Free, fast, in your browser.</text>)SVG"
            << R"SVG(<text x="80" y="560" font-family="-apple-system,Segoe UI,Roboto,sans-serif" font-size="26" font-weight="600" fill="#a7a7b5">tools.lumaplayground.com</text>)SVG"
            << R"SVG(</svg>)SVG";
        res.set_header("Cache-Control", "public, max-age=86400, immutable");
        res.set_header("Content-Type", "image/svg+xml; charset=utf-8");
        res.set_content(svg.str(), "image/svg+xml");
    });

    // ── /api/feedback — bottom-right "Send feedback" widget posts here.
    //    Forwards to Discord (uses the existing webhook) with the user's
    //    email/IP and the message. No persistence beyond Discord.
    svr.Post("/api/feedback", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
        }
        string msg = body.value("message", "");
        if (msg.empty() || msg.size() > 4000) {
            res.status = 400;
            res.set_content(R"({"error":"Message must be 1-4000 chars."})", "application/json");
            return;
        }
        string page = body.value("page", "");
        // current_account is static in routes_account.cpp — use the public
        // helper to resolve the signed-in user without including that file.
        int uid = account_user_id_for_request(req);
        string from = "anonymous";
        if (uid > 0) {
            AccountUser user;
            if (account_get_user_by_id(uid, user)) {
                from = user.email + " (id=" + to_string(user.id) + ", plan=" + user.plan + ")";
            }
        }
        try {
            // Feedback channel webhook — overrides the general DISCORD_WEBHOOK_URL.
            // Set via FEEDBACK_WEBHOOK_URL env var; ping <@851332798231871508> in
            // the message content so the maintainer is notified immediately.
            const char* fb_env = std::getenv("FEEDBACK_WEBHOOK_URL");
            string fb_url = fb_env ? string(fb_env) : "";
            string ping = "<@851332798231871508> new feedback received";
            if (!fb_url.empty()) {
                discord_log_to(fb_url, ping,
                    "💬 Feedback",
                    "**From:** " + from +
                    "\n**Page:** `" + page + "`" +
                    "\n**Message:**\n```\n" + msg + "\n```",
                    0x60A5FA);
            } else {
                // Fallback: still post to the general webhook if no dedicated
                // feedback URL is configured — but include the ping in content.
                discord_log("💬 Feedback " + ping,
                    "**From:** " + from +
                    "\n**Page:** `" + page + "`" +
                    "\n**Message:**\n```\n" + msg + "\n```",
                    0x60A5FA);
            }
        } catch (...) {}
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── /api/feedback-rate — thumbs up/down after tool completes ─────────
    svr.Post("/api/feedback-rate", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"bad json"})", "application/json"); return;
        }
        string tool   = body.value("tool", "unknown");
        string rating = body.value("rating", ""); // "up" or "down"
        if (tool.empty() || (rating != "up" && rating != "down")) {
            res.status = 400; res.set_content(R"({"error":"bad params"})", "application/json"); return;
        }
        string emoji = (rating == "up") ? "👍" : "👎";
        int color    = (rating == "up") ? 0x34D399 : 0xF87171;
        int uid = account_user_id_for_request(req);
        string from = "anonymous";
        if (uid > 0) {
            AccountUser user;
            if (account_get_user_by_id(uid, user)) from = user.email + " (id=" + to_string(user.id) + ")";
        }
        try {
            const char* fb_env = std::getenv("FEEDBACK_WEBHOOK_URL");
            string fb_url = fb_env ? string(fb_env) : "";
            string desc = emoji + " **" + tool + "**\n**From:** " + from;
            if (!fb_url.empty())
                discord_log_to(fb_url, "", emoji + " Tool rated", desc, color);
            else
                discord_log(emoji + " Tool rated", desc, color);
        } catch (...) {}
        res.set_content(R"({"ok":true})", "application/json");
    });

    // /embed/<tool-id> — clean URL for third-party iframe embeds.
    // <iframe src="https://tools.lumaplayground.com/embed/image-compress">
    // Redirects to /?embed=1#image-compress; the JS picks up ?embed=1 and
    // hides sidebar/header/footer via the .lt-embed body class.
    svr.Get(R"(/embed/([a-zA-Z0-9_-]+))", [](const httplib::Request& req, httplib::Response& res) {
        string tool = req.matches[1].str();
        res.set_header("Location", "/?embed=1#" + tool);
        // Allow framing — default httplib does not set X-Frame-Options, good.
        res.status = 302;
    });

    // ══════════════════════════════════════════════════════════════════════════
    // API KEY SYSTEM
    // Developer API keys stored in processing/api_keys.json.
    // Schema: { "keys": [ { "key": "lt_...", "label": "My app", "ip": "1.2.3.4",
    //                        "created": 1700000000, "calls": 42, "active": true } ] }
    // Auth: X-LT-Key header on any /api/* request grants plan="api_free" access.
    // ══════════════════════════════════════════════════════════════════════════

    static std::mutex g_apikeys_mutex;

    // Returns path to the key store file
    auto apikeys_path = []() -> string {
        return get_processing_dir() + "/api_keys.json";
    };

    // Load key store (call with mutex held)
    auto apikeys_load = [&apikeys_path]() -> json {
        string path = apikeys_path();
        if (!fs::exists(path)) return {{"keys", json::array()}};
        try {
            ifstream f(path);
            return json::parse(f);
        } catch (...) {
            return {{"keys", json::array()}};
        }
    };

    // Save key store (call with mutex held)
    auto apikeys_save = [&apikeys_path](const json& store) {
        ofstream f(apikeys_path());
        f << store.dump(2);
    };

    // Generate a random API key: lt_<32 hex chars>
    auto gen_key = []() -> string {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        std::ostringstream ss;
        ss << "lt_";
        for (int i = 0; i < 4; i++) {
            uint64_t v = dist(gen);
            char buf[17];
            snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
            ss << buf;
        }
        return ss.str();
    };

    // ── POST /api/keys/create — generate a new API key ───────────────────────
    svr.Post("/api/keys/create", [&g_apikeys_mutex, &apikeys_load, &apikeys_save, &gen_key](
        const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) { body = {}; }
        string label = body.value("label", "My API key");
        if (label.empty()) label = "My API key";
        if (label.size() > 64) label = label.substr(0, 64);

        string new_key = gen_key();
        string ip = req.remote_addr;
        int64_t now = (int64_t)std::time(nullptr);

        lock_guard<std::mutex> lock(g_apikeys_mutex);
        json store = apikeys_load();
        auto& keys = store["keys"];

        // Limit: 5 active keys per IP
        int active_count = 0;
        for (const auto& k : keys)
            if (k.value("ip", "") == ip && k.value("active", false)) active_count++;
        if (active_count >= 5) {
            res.status = 429;
            res.set_content(R"({"error":"Max 5 active API keys per IP. Revoke an existing key first."})",
                            "application/json");
            return;
        }

        json entry = {
            {"key", new_key}, {"label", label}, {"ip", ip},
            {"created", now}, {"calls", 0}, {"active", true}
        };
        keys.push_back(entry);
        apikeys_save(store);

        res.set_header("Cache-Control", "no-store");
        res.set_content(json({{"key", new_key}, {"label", label}, {"created", now}}).dump(),
                        "application/json");
    });

    // ── GET /api/keys/list — list caller's active keys (by IP) ───────────────
    svr.Get("/api/keys/list", [&g_apikeys_mutex, &apikeys_load](
        const httplib::Request& req, httplib::Response& res) {
        string ip = req.remote_addr;
        lock_guard<std::mutex> lock(g_apikeys_mutex);
        json store = apikeys_load();
        json out = json::array();
        for (const auto& k : store["keys"]) {
            if (k.value("ip", "") == ip && k.value("active", false)) {
                // Mask key: show first 8 + last 4 chars
                string full = k.value("key", "");
                string masked = full.size() > 12
                    ? full.substr(0, 8) + "…" + full.substr(full.size() - 4)
                    : full;
                out.push_back({
                    {"key_masked", masked},
                    {"key", full},
                    {"label", k.value("label", "")},
                    {"created", k.value("created", 0)},
                    {"calls", k.value("calls", 0)}
                });
            }
        }
        res.set_header("Cache-Control", "no-store");
        res.set_content(json({{"keys", out}}).dump(), "application/json");
    });

    // ── DELETE /api/keys/revoke — revoke a key (only the owner IP can) ───────
    svr.Delete("/api/keys/revoke", [&g_apikeys_mutex, &apikeys_load, &apikeys_save](
        const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {}
        string target = body.value("key", "");
        string ip = req.remote_addr;
        if (target.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"Missing key"})", "application/json");
            return;
        }
        lock_guard<std::mutex> lock(g_apikeys_mutex);
        json store = apikeys_load();
        bool found = false;
        for (auto& k : store["keys"]) {
            if (k.value("key", "") == target && k.value("ip", "") == ip) {
                k["active"] = false;
                found = true;
                break;
            }
        }
        if (!found) {
            res.status = 404;
            res.set_content(R"({"error":"Key not found or not yours"})", "application/json");
            return;
        }
        apikeys_save(store);
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── GET /api/keys/docs — machine-readable endpoint catalogue ─────────────
    svr.Get("/api/keys/docs", [](const httplib::Request&, httplib::Response& res) {
        json docs = {
            {"version", "1"},
            {"base_url", "https://tools.lumaplayground.com"},
            {"auth", "Pass your key as the X-LT-Key header on every request"},
            {"rate_limit", "200 requests / day on the free API tier"},
            {"endpoints", json::array({
                {{"method","POST"},{"path","/api/tools/image-compress"},  {"desc","Compress an image (JPEG/PNG/WebP). Multipart: file=<image>. Returns compressed file."}},
                {{"method","POST"},{"path","/api/tools/image-resize"},    {"desc","Resize an image. Multipart: file=<image>, width=<px>, height=<px>."}},
                {{"method","POST"},{"path","/api/tools/image-convert"},   {"desc","Convert image format. Multipart: file=<image>, format=png|jpg|webp|avif."}},
                {{"method","POST"},{"path","/api/tools/image-watermark"}, {"desc","Add text watermark. Multipart: file=<image>, text=<str>, position=<str>."}},
                {{"method","POST"},{"path","/api/tools/image-bg-remove"}, {"desc","Remove image background (AI). Multipart: file=<image>. Returns PNG."}},
                {{"method","POST"},{"path","/api/tools/video-compress"},  {"desc","Compress a video. Multipart: file=<video>. Returns compressed MP4."}},
                {{"method","POST"},{"path","/api/tools/video-convert"},   {"desc","Convert video format. Multipart: file=<video>, format=mp4|webm|mov."}},
                {{"method","POST"},{"path","/api/tools/audio-convert"},   {"desc","Convert audio format. Multipart: file=<audio>, format=mp3|flac|wav|ogg|aac."}},
                {{"method","POST"},{"path","/api/tools/pdf-compress"},    {"desc","Compress a PDF. Multipart: file=<pdf>. Returns compressed PDF."}},
                {{"method","POST"},{"path","/api/tools/pdf-merge"},       {"desc","Merge PDFs. Multipart: file=<pdf1>, file=<pdf2>, ... Returns merged PDF."}},
                {{"method","POST"},{"path","/api/tools/paraphrase"},      {"desc","AI paraphrase text. JSON body: {text, tone?}. Returns {result}."}},
                {{"method","POST"},{"path","/api/tools/study-notes"},     {"desc","AI study notes from text. JSON body: {text}. Returns {result} (async job)."}},
                {{"method","POST"},{"path","/api/tools/flashcards"},      {"desc","AI flashcard generation. JSON body: {text}. Returns {cards:[{front,back}]}."}},
                {{"method","POST"},{"path","/api/tools/csv-json"},        {"desc","Convert CSV to JSON. Multipart: file=<csv>. Returns JSON array."}},
                {{"method","POST"},{"path","/api/detect"},                {"desc","Detect media platform from URL. JSON body: {url}. Returns {platform, type}."}},
            })}
        };
        res.set_header("Cache-Control", "public, max-age=3600");
        res.set_content(docs.dump(2), "application/json");
    });

    // ── /api-docs — serve the API docs SPA panel ─────────────────────────────
    svr.Get("/api-docs", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Location", "https://tools.lumaplayground.com/#api-access");
        res.status = 302;
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

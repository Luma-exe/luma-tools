/**
 * Luma Tools — Common utilities implementation
 */

#include "common.h"
#include <deque>

// ─── Global variable definitions ────────────────────────────────────────────

string g_ffmpeg_path;
string g_ffmpeg_exe;
string g_deno_path;
string g_ytdlp_path;
string g_ghostscript_path;
string g_pandoc_path;
string g_groq_key;
string g_cerebras_key;
string g_gemini_key;
string g_git_commit = "unknown";
string g_git_branch = "unknown";
string g_hostname;
string g_sevenzip_path;
string g_imagemagick_path;
bool   g_rembg_available    = false;
bool   g_ollama_available   = false;

// ─── Internal state (file-local) ────────────────────────────────────────────

static mutex downloads_mutex;
static map<string, json> download_status_map;
static int download_counter = 0;

static mutex ip_mutex;
static set<string> active_ips;

static mutex jobs_mutex;
static map<string, json> job_status_map;
static map<string, string> job_results_map;
static map<string, string> job_raw_text_map;
static int job_counter = 0;

// ─── JSON helpers ───────────────────────────────────────────────────────────

string json_str(const json& j, const string& key, const string& def) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<string>();
    return def;
}

// ─── String utilities ───────────────────────────────────────────────────────

string sanitize_utf8(const string& s) {
    string out;
    out.reserve(s.size());

    for (unsigned char c : s) {
        if (c < 0x80) out += static_cast<char>(c);
        else out += '_';
    }

    return out;
}

string clean_filename(const string& raw) {
    string out;

    for (unsigned char c : raw) {
        if (c >= 0x80) continue;
        if (std::isalnum(c) || c == '-' || c == '(' || c == ')') out += static_cast<char>(c);
        else if (c == ' ' || c == '_' || c == '.') out += ' ';
    }

    string result;
    bool prev_space = true;

    for (char c : out) {
        if (c == ' ') { if (!prev_space) result += ' '; prev_space = true; }
        else { result += c; prev_space = false; }
    }

    while (!result.empty() && result.back() == ' ') result.pop_back();
    if (result.empty()) result = "download";
    return result;
}

string escape_arg(const string& arg) {
#ifdef _WIN32
    string escaped = "\"";

    for (char c : arg) {
        if (c == '"') escaped += "\\\"";
        else escaped += c;
    }

    escaped += "\"";
    return escaped;
#else
    string escaped = "'";

    for (char c : arg) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }

    escaped += "'";
    return escaped;
#endif
}

string ffmpeg_cmd() {
    if (g_ffmpeg_exe.empty()) return "ffmpeg";
    return escape_arg(g_ffmpeg_exe);
}

// ─── Shell execution ────────────────────────────────────────────────────────

string exec_command(const string& cmd, int& exit_code) {
    string result;
    array<char, 4096> buffer;

#ifdef _WIN32
    string full_cmd = "\"" + cmd + " 2>&1\"";
    unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(full_cmd.c_str(), "r"), _pclose);
#else
    string full_cmd = cmd + " 2>&1";
    unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"), pclose);
#endif

    if (!pipe) { exit_code = -1; return "Failed to execute command"; }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    // Capture the real process exit code
#ifdef _WIN32
    exit_code = _pclose(pipe.release());
#else
    int raw = pclose(pipe.release());
    exit_code = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
#endif
    return result;
}

string exec_command(const string& cmd) {
    int code;
    return exec_command(cmd, code);
}

// ─── Path refresh (Windows: read current PATH from registry) ────────────────

void refresh_system_path() {
#ifdef _WIN32
    string cmd = "powershell -NoProfile -Command \""
        "[System.Environment]::GetEnvironmentVariable('Path','Machine') + ';' + "
        "[System.Environment]::GetEnvironmentVariable('Path','User')\"";
    string new_path;
    array<char, 32768> buffer;
    string full_cmd = cmd + " 2>&1";
    unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(full_cmd.c_str(), "r"), _pclose);

    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            new_path += buffer.data();
        }
    }

    while (!new_path.empty() && (new_path.back() == '\n' || new_path.back() == '\r' || new_path.back() == ' ')) {
        new_path.pop_back();
    }

    if (!new_path.empty()) {
        _putenv_s("PATH", new_path.c_str());
        cout << "[Luma Tools] System PATH refreshed" << endl;
    }

#endif
}

string find_executable(const string& name, const vector<string>& extra_paths) {
    string result;
    array<char, 4096> buf;
#ifdef _WIN32
    string wcmd = "where.exe " + name + " 2>&1";
    unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(wcmd.c_str(), "r"), _pclose);
#else
    string wcmd = "which " + name + " 2>&1";
    unique_ptr<FILE, decltype(&pclose)> pipe(popen(wcmd.c_str(), "r"), pclose);
#endif

    if (pipe) {
        while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr) { result += buf.data(); }
    }

    auto nl = result.find('\n');

    if (nl != string::npos) result = result.substr(0, nl);
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());

    if (!result.empty() && fs::exists(result)) return result;
    for (const auto& p : extra_paths) { if (fs::exists(p)) return p; }
    return "";
}

string find_ytdlp() {
    {
        int code;
        string ver = exec_command("yt-dlp --version", code);

        if (!ver.empty() && ver.find("not recognized") == string::npos
            && ver.find("not found") == string::npos) {
            return "yt-dlp";
        }
    }

    vector<string> candidates;
#ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    const char* localappdata = std::getenv("LOCALAPPDATA");
    const char* appdata = std::getenv("APPDATA");

    if (localappdata) {
        string pybase = string(localappdata) + "\\Programs\\Python";

        if (fs::exists(pybase)) {
            for (const auto& entry : fs::directory_iterator(pybase)) {
                if (entry.is_directory()) {
                    candidates.push_back(entry.path().string() + "\\Scripts\\yt-dlp.exe");
                }
            }
        }
    }

    if (userprofile) {
        for (const char* ver : {"310", "311", "312", "313"}) {
            candidates.push_back(string(userprofile) + "\\AppData\\Local\\Programs\\Python\\Python" + ver + "\\Scripts\\yt-dlp.exe");
        }

        candidates.push_back(string(userprofile) + "\\.local\\bin\\yt-dlp.exe");
        candidates.push_back(string(userprofile) + "\\scoop\\shims\\yt-dlp.exe");
    }

    if (appdata) candidates.push_back(string(appdata) + "\\Python\\Scripts\\yt-dlp.exe");
    candidates.push_back("C:\\ProgramData\\chocolatey\\bin\\yt-dlp.exe");
#else
    candidates.push_back("/usr/local/bin/yt-dlp");
    candidates.push_back("/usr/bin/yt-dlp");
    const char* home = std::getenv("HOME");

    if (home) candidates.push_back(string(home) + "/.local/bin/yt-dlp");
#endif

    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            cout << "[Luma Tools] Found yt-dlp at: " << path << endl;
            return path;
        }
    }

#ifdef _WIN32
    {
        int code;
        string ver = exec_command("py -m yt_dlp --version", code);

        if (!ver.empty() && ver.find("not recognized") == string::npos) return "py -m yt_dlp";
    }

#endif
    return "";
}

string find_ghostscript() {
#ifdef _WIN32
    auto gs = find_executable("gswin64c");

    if (!gs.empty()) return gs;
    gs = find_executable("gswin32c");

    if (!gs.empty()) return gs;
    gs = find_executable("gs");

    if (!gs.empty()) return gs;
    const char* pf = std::getenv("ProgramFiles");
    string progfiles = pf ? pf : "C:\\Program Files";
    try {
        string gs_dir = progfiles + "\\gs";

        if (fs::exists(gs_dir)) {
            for (const auto& entry : fs::directory_iterator(gs_dir)) {
                if (entry.is_directory()) {
                    string c = entry.path().string() + "\\bin\\gswin64c.exe";

                    if (fs::exists(c)) return c;
                    c = entry.path().string() + "\\bin\\gswin32c.exe";

                    if (fs::exists(c)) return c;
                }
            }
        }
    } catch (...) {}
#else
    auto gs = find_executable("gs");

    if (!gs.empty()) return gs;
#endif
    return "";
}

string find_pandoc() {
    auto p = find_executable("pandoc");
    if (!p.empty()) return p;
#ifdef _WIN32
    const char* pf = std::getenv("ProgramFiles");
    string progfiles = pf ? pf : "C:\\Program Files";
    string candidate = progfiles + "\\Pandoc\\pandoc.exe";
    if (fs::exists(candidate)) return candidate;
    const char* pf86 = std::getenv("ProgramFiles(x86)");
    if (pf86) {
        candidate = string(pf86) + "\\Pandoc\\pandoc.exe";
        if (fs::exists(candidate)) return candidate;
    }
    // Current process LOCALAPPDATA
    const char* appdata = std::getenv("LOCALAPPDATA");
    if (appdata) {
        candidate = string(appdata) + "\\Pandoc\\pandoc.exe";
        if (fs::exists(candidate)) return candidate;
    }
    // Scan all user profiles under C:\Users\*
    try {
        for (const auto& entry : fs::directory_iterator("C:\\Users")) {
            if (!entry.is_directory()) continue;
            candidate = entry.path().string() + "\\AppData\\Local\\Pandoc\\pandoc.exe";
            if (fs::exists(candidate)) return candidate;
        }
    } catch (...) {}
#endif
    return "";
}

string find_deno() {
    auto d = find_executable("deno");
    if (!d.empty()) return d;
#ifdef _WIN32
    // Current process USERPROFILE
    const char* up = std::getenv("USERPROFILE");
    if (up) {
        string candidate = string(up) + "\\.deno\\bin\\deno.exe";
        if (fs::exists(candidate)) return candidate;
    }
    // Scan all user profiles under C:\Users\*
    try {
        for (const auto& entry : fs::directory_iterator("C:\\Users")) {
            if (!entry.is_directory()) continue;
            string candidate = entry.path().string() + "\\.deno\\bin\\deno.exe";
            if (fs::exists(candidate)) return candidate;
        }
    } catch (...) {}
#endif
    return "";
}

string build_ytdlp_cmd() {
    if (g_ytdlp_path.find("py ") == 0) return g_ytdlp_path;
    return escape_arg(g_ytdlp_path);
}

// ─── Download manager ───────────────────────────────────────────────────────

bool has_active_download(const string& ip) {
    lock_guard<mutex> lock(ip_mutex);
    return active_ips.count(ip) > 0;
}

void register_active_download(const string& ip, const string&) {
    lock_guard<mutex> lock(ip_mutex);
    active_ips.insert(ip);
    cout << "[Luma Tools] Registered active download for IP: " << ip << endl;
}

void unregister_active_download(const string& ip) {
    lock_guard<mutex> lock(ip_mutex);
    active_ips.erase(ip);
    cout << "[Luma Tools] Released download slot for IP: " << ip << endl;
}

string generate_download_id() {
    lock_guard<mutex> lock(downloads_mutex);
    return "dl_" + to_string(++download_counter) + "_" +
           to_string(std::chrono::system_clock::now().time_since_epoch().count());
}

void update_download_status(const string& id, const json& status) {
    lock_guard<mutex> lock(downloads_mutex);

    // Track insertion order for eviction (prevent unbounded memory growth).
    static std::deque<string> dl_order;
    if (!download_status_map.count(id)) {
        dl_order.push_back(id);
    }

    download_status_map[id] = status;

    constexpr size_t MAX_DOWNLOADS = 500;
    while (dl_order.size() > MAX_DOWNLOADS) {
        string oldest = dl_order.front();
        download_status_map.erase(oldest);
        dl_order.pop_front();
    }
}

json get_download_status(const string& id) {
    lock_guard<mutex> lock(downloads_mutex);

    if (download_status_map.count(id)) return download_status_map[id];
    return {{"error", "not_found"}};
}

string get_downloads_dir() {
    string dir = "downloads";

    if (!fs::exists(dir)) fs::create_directories(dir);
    return dir;
}

// ─── Processing job manager ─────────────────────────────────────────────────

string generate_job_id() {
    lock_guard<mutex> lock(jobs_mutex);
    return "job_" + to_string(++job_counter) + "_" +
           to_string(std::chrono::system_clock::now().time_since_epoch().count());
}

void update_job(const string& id, const json& status, const string& result_path) {
    lock_guard<mutex> lock(jobs_mutex);

    // Track insertion order so we can evict the oldest entries when the maps grow too large.
    static std::deque<string> job_order;
    if (!job_status_map.count(id)) {
        job_order.push_back(id);
    }

    job_status_map[id] = status;
    if (!result_path.empty()) job_results_map[id] = result_path;

    // Evict oldest entries if maps exceed the cap (prevents unbounded memory growth).
    constexpr size_t MAX_JOBS = 500;
    while (job_order.size() > MAX_JOBS) {
        string oldest = job_order.front();   // copy by value before pop
        job_status_map.erase(oldest);
        job_results_map.erase(oldest);
        job_raw_text_map.erase(oldest);
        job_order.pop_front();
    }
}

json get_job(const string& id) {
    lock_guard<mutex> lock(jobs_mutex);

    if (job_status_map.count(id)) return job_status_map[id];
    return {{"error", "not_found"}};
}

string get_job_result_path(const string& id) {
    lock_guard<mutex> lock(jobs_mutex);

    if (job_results_map.count(id)) return job_results_map[id];
    return "";
}

void update_job_raw_text(const string& id, const string& raw_text) {
    lock_guard<mutex> lock(jobs_mutex);
    job_raw_text_map[id] = raw_text;
}

string get_job_raw_text(const string& id) {
    lock_guard<mutex> lock(jobs_mutex);
    if (job_raw_text_map.count(id)) return job_raw_text_map[id];
    return "";
}

// ─── File processing helpers ────────────────────────────────────────────────

string get_processing_dir() {
    string dir = "processing";

    if (!fs::exists(dir)) fs::create_directories(dir);
    return dir;
}

string read_file_binary(const string& path) {
    ifstream f(path, std::ios::binary);
    return string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

string mime_from_ext(const string& ext) {
    string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);

    if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
    if (e == ".png") return "image/png";
    if (e == ".webp") return "image/webp";
    if (e == ".gif") return "image/gif";
    if (e == ".bmp") return "image/bmp";
    if (e == ".tiff" || e == ".tif") return "image/tiff";
    if (e == ".mp4") return "video/mp4";
    if (e == ".webm") return "video/webm";
    if (e == ".mkv") return "video/x-matroska";
    if (e == ".avi") return "video/x-msvideo";
    if (e == ".mov") return "video/quicktime";
    if (e == ".mp3") return "audio/mpeg";
    if (e == ".wav") return "audio/wav";
    if (e == ".flac") return "audio/flac";
    if (e == ".aac") return "audio/aac";
    if (e == ".ogg") return "audio/ogg";
    if (e == ".m4a") return "audio/mp4";
    if (e == ".wma") return "audio/x-ms-wma";
    if (e == ".pdf") return "application/pdf";
    if (e == ".zip") return "application/zip";
    return "application/octet-stream";
}

void send_file_response(httplib::Response& res, const string& path, const string& filename) {
    auto data = read_file_binary(path);

    if (data.empty()) {
        res.status = 500;
        res.set_content(json({{"error", "Failed to read output file"}}).dump(), "application/json");
        return;
    }

    string ext = fs::path(filename).extension().string();
    res.set_content(data, mime_from_ext(ext));
    res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
}

string save_upload(const httplib::MultipartFormData& file, const string& prefix) {
    string proc_dir = get_processing_dir();
    string ext = fs::path(file.filename).extension().string();
    string safe_name = prefix + "_input" + ext;
    string path = proc_dir + "/" + safe_name;
    ofstream out(path, std::ios::binary);
    out.write(file.content.data(), file.content.size());
    out.close();
    return path;
}

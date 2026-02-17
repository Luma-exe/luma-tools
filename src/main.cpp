/**
 * Luma Tools - Universal Media Downloader
 * C++ Backend Server using cpp-httplib + yt-dlp
 */

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <array>
#include <memory>
#include <regex>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <set>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ─── Safe JSON accessors (handle null values) ──────────────────────────────────

template<typename T>
T json_num(const json& j, const std::string& key, T def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<T>();
    return def;
}

std::string json_str(const json& j, const std::string& key, const std::string& def = "") {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}
// Strip any non-ASCII bytes that could break JSON UTF-8 serialization
std::string sanitize_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else {
            // Replace non-ASCII with underscore to avoid encoding issues
            out += '_';
        }
    }
    return out;
}

// Build a clean filename from a title: ASCII only, no junk, trimmed
std::string clean_filename(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (c >= 0x80) continue;          // drop non-ASCII / emojis
        if (std::isalnum(c) || c == '-' || c == '(' || c == ')') {
            out += static_cast<char>(c);
        } else if (c == ' ' || c == '_' || c == '.') {
            out += ' ';
        }
    }
    // Collapse consecutive spaces, trim
    std::string result;
    bool prev_space = true; // true = trim leading
    for (char c : out) {
        if (c == ' ') {
            if (!prev_space) result += ' ';
            prev_space = true;
        } else {
            result += c;
            prev_space = false;
        }
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    if (result.empty()) result = "download";
    return result;
}
// ─── Platform Detection ────────────────────────────────────────────────────────

struct PlatformInfo {
    std::string id;
    std::string name;
    std::string icon;
    std::string color;
    bool supports_video;
    bool supports_audio;
};

static const std::vector<std::pair<std::regex, PlatformInfo>> PLATFORMS = {
    { std::regex(R"((youtube\.com|youtu\.be))", std::regex::icase),
      { "youtube", "YouTube", "fab fa-youtube", "#FF0000", true, true } },
    { std::regex(R"(tiktok\.com)", std::regex::icase),
      { "tiktok", "TikTok", "fab fa-tiktok", "#00F2EA", true, true } },
    { std::regex(R"(instagram\.com)", std::regex::icase),
      { "instagram", "Instagram", "fab fa-instagram", "#E1306C", true, true } },
    { std::regex(R"(spotify\.com)", std::regex::icase),
      { "spotify", "Spotify", "fab fa-spotify", "#1DB954", false, true } },
    { std::regex(R"(soundcloud\.com)", std::regex::icase),
      { "soundcloud", "SoundCloud", "fab fa-soundcloud", "#FF5500", false, true } },
    { std::regex(R"(twitter\.com|x\.com)", std::regex::icase),
      { "twitter", "X / Twitter", "fab fa-x-twitter", "#1DA1F2", true, true } },
    { std::regex(R"(facebook\.com|fb\.watch)", std::regex::icase),
      { "facebook", "Facebook", "fab fa-facebook", "#1877F2", true, true } },
    { std::regex(R"(twitch\.tv)", std::regex::icase),
      { "twitch", "Twitch", "fab fa-twitch", "#9146FF", true, true } },
    { std::regex(R"(vimeo\.com)", std::regex::icase),
      { "vimeo", "Vimeo", "fab fa-vimeo-v", "#1AB7EA", true, true } },
    { std::regex(R"(dailymotion\.com)", std::regex::icase),
      { "dailymotion", "Dailymotion", "fas fa-play-circle", "#0066DC", true, true } },
    { std::regex(R"(reddit\.com)", std::regex::icase),
      { "reddit", "Reddit", "fab fa-reddit-alien", "#FF4500", true, true } },
    { std::regex(R"(pinterest\.com)", std::regex::icase),
      { "pinterest", "Pinterest", "fab fa-pinterest", "#E60023", true, true } },
};

PlatformInfo detect_platform(const std::string& url) {
    for (const auto& [pattern, info] : PLATFORMS) {
        if (std::regex_search(url, pattern)) {
            return info;
        }
    }
    return { "unknown", "Unknown", "fas fa-globe", "#888888", true, true };
}

// ─── Path refresh (Windows: read current PATH from registry) ────────────────

std::string g_ffmpeg_path;  // Global: resolved path to ffmpeg
std::string g_deno_path;    // Global: resolved path to deno

void refresh_system_path() {
#ifdef _WIN32
    // Read the current Machine and User PATH from registry and merge into our process
    std::string cmd = "powershell -NoProfile -Command \""
        "[System.Environment]::GetEnvironmentVariable('Path','Machine') + ';' + "
        "[System.Environment]::GetEnvironmentVariable('Path','User')\"";
    int code;
    std::string new_path;
    std::array<char, 32768> buffer;
    std::string full_cmd = cmd + " 2>&1";
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(full_cmd.c_str(), "r"), _pclose);
    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            new_path += buffer.data();
        }
    }
    // Trim whitespace
    while (!new_path.empty() && (new_path.back() == '\n' || new_path.back() == '\r' || new_path.back() == ' ')) {
        new_path.pop_back();
    }
    if (!new_path.empty()) {
        _putenv_s("PATH", new_path.c_str());
        std::cout << "[Luma Tools] System PATH refreshed" << std::endl;
    }
#endif
}

std::string find_executable(const std::string& name, const std::vector<std::string>& extra_paths = {}) {
    // Check PATH first
    int code;
    std::string result;
    std::array<char, 4096> buf;
#ifdef _WIN32
    std::string wcmd = "where.exe " + name + " 2>&1";
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(wcmd.c_str(), "r"), _pclose);
#else
    std::string wcmd = "which " + name + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(wcmd.c_str(), "r"), pclose);
#endif
    if (pipe) {
        while (fgets(buf.data(), buf.size(), pipe.get()) != nullptr) {
            result += buf.data();
        }
    }
    // Get first line
    auto nl = result.find('\n');
    if (nl != std::string::npos) result = result.substr(0, nl);
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    
    if (!result.empty() && fs::exists(result)) return result;
    
    // Check extra paths
    for (const auto& p : extra_paths) {
        if (fs::exists(p)) return p;
    }
    return "";
}

// ─── Shell Execution ───────────────────────────────────────────────────────────

std::string exec_command(const std::string& cmd, int& exit_code) {
    std::string result;
    std::array<char, 4096> buffer;

#ifdef _WIN32
    // On Windows, _popen calls cmd.exe /c <command>.
    // If the command contains multiple quoted segments, cmd.exe strips the
    // first and last quote characters (not matching pairs), breaking paths.
    // Wrapping the entire command in an extra layer of quotes prevents this.
    std::string full_cmd = "\"" + cmd + " 2>&1\"";
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(full_cmd.c_str(), "r"), _pclose);
#else
    std::string full_cmd = cmd + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"), pclose);
#endif

    if (!pipe) {
        exit_code = -1;
        return "Failed to execute command";
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

#ifdef _WIN32
    exit_code = 0; // _pclose already called via unique_ptr
#else
    exit_code = 0;
#endif

    return result;
}

std::string exec_command(const std::string& cmd) {
    int code;
    return exec_command(cmd, code);
}

// ─── Download Manager ──────────────────────────────────────────────────────────

static std::mutex downloads_mutex;
static std::map<std::string, json> download_status;
static int download_counter = 0;

// Per-IP active download tracking (1 concurrent download per device)
static std::mutex ip_mutex;
static std::set<std::string> active_ips;

bool has_active_download(const std::string& ip) {
    std::lock_guard<std::mutex> lock(ip_mutex);
    return active_ips.count(ip) > 0;
}

void register_active_download(const std::string& ip, const std::string& /*dl_id*/) {
    std::lock_guard<std::mutex> lock(ip_mutex);
    active_ips.insert(ip);
    std::cout << "[Luma Tools] Registered active download for IP: " << ip << std::endl;
}

void unregister_active_download(const std::string& ip) {
    std::lock_guard<std::mutex> lock(ip_mutex);
    active_ips.erase(ip);
    std::cout << "[Luma Tools] Released download slot for IP: " << ip << std::endl;
}

std::string generate_download_id() {
    std::lock_guard<std::mutex> lock(downloads_mutex);
    return "dl_" + std::to_string(++download_counter) + "_" +
           std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
}

void update_download_status(const std::string& id, const json& status) {
    std::lock_guard<std::mutex> lock(downloads_mutex);
    download_status[id] = status;
}

json get_download_status(const std::string& id) {
    std::lock_guard<std::mutex> lock(downloads_mutex);
    if (download_status.count(id)) {
        return download_status[id];
    }
    return { {"error", "not_found"} };
}

// ─── Ensure downloads directory ────────────────────────────────────────────────

std::string get_downloads_dir() {
    std::string dir = "downloads";
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    return dir;
}

// ─── Find yt-dlp executable ────────────────────────────────────────────────────

std::string g_ytdlp_path; // Global: resolved full path to yt-dlp

std::string find_ytdlp() {
    // 1. Check if it's already on PATH
    {
        int code;
        std::string ver = exec_command("yt-dlp --version", code);
        if (!ver.empty() && ver.find("not recognized") == std::string::npos
            && ver.find("not found") == std::string::npos) {
            return "yt-dlp";
        }
    }

    // 2. Search common Windows locations
    std::vector<std::string> candidates;

#ifdef _WIN32
    // Get USERPROFILE
    const char* userprofile = std::getenv("USERPROFILE");
    const char* localappdata = std::getenv("LOCALAPPDATA");
    const char* appdata = std::getenv("APPDATA");

    // Python Scripts folders (all Python versions)
    if (localappdata) {
        std::string pybase = std::string(localappdata) + "\\Programs\\Python";
        if (fs::exists(pybase)) {
            for (const auto& entry : fs::directory_iterator(pybase)) {
                if (entry.is_directory()) {
                    candidates.push_back(entry.path().string() + "\\Scripts\\yt-dlp.exe");
                }
            }
        }
    }

    if (userprofile) {
        candidates.push_back(std::string(userprofile) + "\\AppData\\Local\\Programs\\Python\\Python310\\Scripts\\yt-dlp.exe");
        candidates.push_back(std::string(userprofile) + "\\AppData\\Local\\Programs\\Python\\Python311\\Scripts\\yt-dlp.exe");
        candidates.push_back(std::string(userprofile) + "\\AppData\\Local\\Programs\\Python\\Python312\\Scripts\\yt-dlp.exe");
        candidates.push_back(std::string(userprofile) + "\\AppData\\Local\\Programs\\Python\\Python313\\Scripts\\yt-dlp.exe");
        candidates.push_back(std::string(userprofile) + "\\.local\\bin\\yt-dlp.exe");
        candidates.push_back(std::string(userprofile) + "\\scoop\\shims\\yt-dlp.exe");
    }
    if (appdata) {
        candidates.push_back(std::string(appdata) + "\\Python\\Scripts\\yt-dlp.exe");
    }
    candidates.push_back("C:\\ProgramData\\chocolatey\\bin\\yt-dlp.exe");
#else
    candidates.push_back("/usr/local/bin/yt-dlp");
    candidates.push_back("/usr/bin/yt-dlp");
    const char* home = std::getenv("HOME");
    if (home) {
        candidates.push_back(std::string(home) + "/.local/bin/yt-dlp");
    }
#endif

    // 3. Try each candidate
    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            std::cout << "[Luma Tools] Found yt-dlp at: " << path << std::endl;
            return path;
        }
    }

    // 4. Try "py -m yt_dlp" as last resort (Windows)
#ifdef _WIN32
    {
        int code;
        std::string ver = exec_command("py -m yt_dlp --version", code);
        if (!ver.empty() && ver.find("not recognized") == std::string::npos) {
            return "py -m yt_dlp";
        }
    }
#endif

    return ""; // Not found
}

// ─── Escape shell argument ─────────────────────────────────────────────────────

std::string escape_arg(const std::string& arg) {
#ifdef _WIN32
    // Windows: wrap in double quotes, escape internal double quotes
    std::string escaped = "\"";
    for (char c : arg) {
        if (c == '"') escaped += "\\\"";
        else escaped += c;
    }
    escaped += "\"";
    return escaped;
#else
    // Unix: wrap in single quotes
    std::string escaped = "'";
    for (char c : arg) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }
    escaped += "'";
    return escaped;
#endif
}

// ─── Build yt-dlp base command ────────────────────────────────────────────────

std::string build_ytdlp_cmd() {
    // ffmpeg and deno directories are already on PATH (added at startup),
    // so yt-dlp will discover them automatically — no extra flags needed.
    if (g_ytdlp_path.find("py ") == 0) {
        return g_ytdlp_path;
    }
    return escape_arg(g_ytdlp_path);
}

// ─── Main Server ───────────────────────────────────────────────────────────────

int main() {
    httplib::Server svr;

    std::string public_dir = "public";
    // Try various paths for the public folder
    if (!fs::exists(public_dir)) {
        if (fs::exists("../public")) public_dir = "../public";
        else if (fs::exists("../../public")) public_dir = "../../public";
    }

    std::string dl_dir = get_downloads_dir();
    std::cout << "[Luma Tools] Downloads directory: " << fs::absolute(dl_dir) << std::endl;

    // ── Refresh PATH from system registry ────────────────────────────────────
    refresh_system_path();

    // ── Find yt-dlp ─────────────────────────────────────────────────────────
    g_ytdlp_path = find_ytdlp();
    if (g_ytdlp_path.empty()) {
        std::cerr << "[Luma Tools] WARNING: yt-dlp not found! Downloads will fail." << std::endl;
        std::cerr << "[Luma Tools] Install it: pip install yt-dlp" << std::endl;
        g_ytdlp_path = "yt-dlp"; // fallback
    } else {
        int code;
        std::string ver = exec_command(escape_arg(g_ytdlp_path) + " --version", code);
        ver.erase(std::remove(ver.begin(), ver.end(), '\n'), ver.end());
        ver.erase(std::remove(ver.begin(), ver.end(), '\r'), ver.end());
        std::cout << "[Luma Tools] yt-dlp found: " << g_ytdlp_path << " (v" << ver << ")" << std::endl;
    }

    // ── Find ffmpeg & deno ───────────────────────────────────────────────────
    g_ffmpeg_path = find_executable("ffmpeg");
    if (!g_ffmpeg_path.empty()) {
        // We want the directory, not the exe itself
        g_ffmpeg_path = fs::path(g_ffmpeg_path).parent_path().string();
        std::cout << "[Luma Tools] ffmpeg dir: " << g_ffmpeg_path << std::endl;
    } else {
        std::cerr << "[Luma Tools] WARNING: ffmpeg not found. Audio conversion may fail." << std::endl;
    }

    g_deno_path = find_executable("deno");
    if (!g_deno_path.empty()) {
        std::cout << "[Luma Tools] deno found: " << g_deno_path << std::endl;
    } else {
        std::cerr << "[Luma Tools] WARNING: deno not found. YouTube extraction may fail." << std::endl;
    }

    // ── Add ffmpeg & deno directories to process PATH so yt-dlp finds them ──
    {
        std::string current_path;
        const char* env_path = std::getenv("PATH");
        if (env_path) current_path = env_path;

        bool modified = false;
        if (!g_ffmpeg_path.empty()) {
            // g_ffmpeg_path is already the directory
            if (current_path.find(g_ffmpeg_path) == std::string::npos) {
                current_path = g_ffmpeg_path + ";" + current_path;
                modified = true;
                std::cout << "[Luma Tools] Added ffmpeg dir to PATH" << std::endl;
            }
        }
        if (!g_deno_path.empty()) {
            std::string deno_dir = fs::path(g_deno_path).parent_path().string();
            if (current_path.find(deno_dir) == std::string::npos) {
                current_path = deno_dir + ";" + current_path;
                modified = true;
                std::cout << "[Luma Tools] Added deno dir to PATH" << std::endl;
            }
        }
        if (modified) {
            _putenv_s("PATH", current_path.c_str());
        }
    }

    // ── Serve static files ──────────────────────────────────────────────────
    svr.set_mount_point("/", public_dir);
    svr.set_mount_point("/downloads", dl_dir);

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

    // ── POST /api/detect — detect platform from URL ─────────────────────────
    svr.Post("/api/detect", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string url = body.value("url", "");

            if (url.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "URL is required"}}).dump(), "application/json");
                return;
            }

            auto platform = detect_platform(url);
            json response = {
                {"platform", {
                    {"id", platform.id},
                    {"name", platform.name},
                    {"icon", platform.icon},
                    {"color", platform.color},
                    {"supports_video", platform.supports_video},
                    {"supports_audio", platform.supports_audio}
                }}
            };

            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ── POST /api/analyze — get media info from URL via yt-dlp ──────────────
    svr.Post("/api/analyze", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string url = body.value("url", "");

            if (url.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "URL is required"}}).dump(), "application/json");
                return;
            }

            auto platform = detect_platform(url);
            json platform_json = {
                {"id", platform.id},
                {"name", platform.name},
                {"icon", platform.icon},
                {"color", platform.color},
                {"supports_video", platform.supports_video},
                {"supports_audio", platform.supports_audio}
            };

            // ── Check if it's a playlist first ──────────────────────────────
            // Use --flat-playlist --dump-single-json to quickly get playlist metadata
            bool is_playlist = false;
            {
                std::string probe_cmd = build_ytdlp_cmd() + " --flat-playlist --dump-single-json --no-warnings " + escape_arg(url);
                int probe_code;
                std::string probe_output = exec_command(probe_cmd, probe_code);

                // Find the JSON object
                auto json_start = probe_output.find('{');
                if (json_start != std::string::npos) {
                    try {
                        json probe = json::parse(probe_output.substr(json_start));
                        // It's a playlist if _type is "playlist" and there are entries
                        std::string ptype = json_str(probe, "_type");
                        if ((ptype == "playlist" || ptype == "multi_video") && probe.contains("entries") && probe["entries"].is_array() && probe["entries"].size() > 1) {
                            is_playlist = true;

                            // Build playlist response
                            json items = json::array();
                            int index = 0;
                            for (const auto& entry : probe["entries"]) {
                                index++;
                                std::string item_url = json_str(entry, "url");
                                if (item_url.empty()) item_url = json_str(entry, "webpage_url");
                                // If url is just a video ID, construct full URL
                                if (!item_url.empty() && item_url.find("http") != 0) {
                                    std::string extractor = json_str(entry, "ie_key", json_str(entry, "extractor"));
                                    std::string vid_id = item_url;
                                    if (extractor == "Youtube" || extractor == "youtube") {
                                        item_url = "https://www.youtube.com/watch?v=" + vid_id;
                                    } else {
                                        // Try webpage_url as fallback
                                        std::string wp = json_str(entry, "webpage_url");
                                        if (!wp.empty()) item_url = wp;
                                    }
                                }

                                // Get title and sanitize — keep underscores from emoji replacement
                                // but if the result is empty/only-underscores, use fallback
                                std::string raw_title = json_str(entry, "title", "");
                                std::string title = sanitize_utf8(raw_title);
                                // Trim leading/trailing underscores and spaces
                                while (!title.empty() && (title.front() == '_' || title.front() == ' ')) title.erase(title.begin());
                                while (!title.empty() && (title.back() == '_' || title.back() == ' ')) title.pop_back();

                                // If title is still empty, extract a readable name from the URL slug
                                if (title.empty() && !item_url.empty()) {
                                    // e.g. https://soundcloud.com/user/my-cool-track → "My Cool Track"
                                    std::string slug = item_url;
                                    // Remove query params
                                    auto qpos = slug.find('?');
                                    if (qpos != std::string::npos) slug = slug.substr(0, qpos);
                                    // Get last path segment
                                    auto spos = slug.rfind('/');
                                    if (spos != std::string::npos) slug = slug.substr(spos + 1);
                                    // Check if slug is all digits (API URL like api-v2.soundcloud.com/tracks/ID)
                                    bool all_digits = !slug.empty() && std::all_of(slug.begin(), slug.end(), ::isdigit);
                                    if (!all_digits && !slug.empty()) {
                                        // Replace hyphens with spaces and capitalize first letter of each word
                                        std::string readable;
                                        bool cap_next = true;
                                        for (char c : slug) {
                                            if (c == '-' || c == '_') {
                                                readable += ' ';
                                                cap_next = true;
                                            } else if (cap_next && std::isalpha(c)) {
                                                readable += (char)std::toupper(c);
                                                cap_next = false;
                                            } else {
                                                readable += c;
                                                cap_next = false;
                                            }
                                        }
                                        while (!readable.empty() && readable.back() == ' ') readable.pop_back();
                                        if (!readable.empty()) title = readable;
                                    }
                                }
                                if (title.empty()) title = "Track " + std::to_string(index);

                                items.push_back({
                                    {"index", index - 1},
                                    {"title", title},
                                    {"url", item_url},
                                    {"duration", json_num(entry, "duration", 0)},
                                    {"thumbnail", json_str(entry, "thumbnail", "")},
                                    {"uploader", sanitize_utf8(json_str(entry, "uploader", json_str(entry, "channel", "")))},
                                });
                            }

                            json response = {
                                {"type", "playlist"},
                                {"title", sanitize_utf8(json_str(probe, "title", "Playlist"))},
                                {"uploader", sanitize_utf8(json_str(probe, "uploader", json_str(probe, "channel", "Unknown")))},
                                {"thumbnail", json_str(probe, "thumbnails", "") == "" ? json_str(probe, "thumbnail", "") : json_str(probe, "thumbnail", "")},
                                {"item_count", (int)items.size()},
                                {"items", items},
                                {"platform", platform_json}
                            };

                            std::cout << "[Luma Tools] Playlist detected: " << items.size() << " items" << std::endl;
                            res.set_content(response.dump(), "application/json");
                            return;
                        }
                    } catch (const json::exception& e) {
                        // Not valid JSON or not a playlist — fall through to single item
                        std::cout << "[Luma Tools] Playlist probe parse failed: " << e.what() << std::endl;
                    }
                }
            }

            // ── Single item analysis ────────────────────────────────────────
            std::string cmd = build_ytdlp_cmd() + " --dump-json --no-warnings --no-playlist " + escape_arg(url);
            int exit_code;
            std::string output = exec_command(cmd, exit_code);

            if (output.empty() || output[0] != '{') {
                // Try to find the JSON in the output
                auto pos = output.find('{');
                if (pos != std::string::npos) {
                    output = output.substr(pos);
                } else {
                    // Extract meaningful error from yt-dlp output
                    std::string error_msg = "Failed to analyze URL";
                    auto err_pos = output.find("ERROR:");
                    if (err_pos != std::string::npos) {
                        error_msg = output.substr(err_pos);
                        // Trim to first line
                        auto nl = error_msg.find('\n');
                        if (nl != std::string::npos) error_msg = error_msg.substr(0, nl);
                        // Clean up
                        error_msg.erase(std::remove(error_msg.begin(), error_msg.end(), '\r'), error_msg.end());
                    } else if (output.find("not recognized") != std::string::npos || output.find("not found") != std::string::npos) {
                        error_msg = "yt-dlp is not installed or not on PATH";
                    }
                    res.status = 500;
                    res.set_content(json({
                        {"error", error_msg},
                        {"details", output}
                    }).dump(), "application/json");
                    return;
                }
            }

            // Parse only the first JSON object (in case of playlists)
            auto end_pos = output.find("}\n{");
            if (end_pos != std::string::npos) {
                output = output.substr(0, end_pos + 1);
            }

            json info = json::parse(output);

            // Extract relevant format info
            json formats = json::array();
            std::set<std::string> seen_qualities;

            if (info.contains("formats") && info["formats"].is_array()) {
                for (const auto& fmt : info["formats"]) {
                    std::string format_id = json_str(fmt, "format_id");
                    std::string ext = json_str(fmt, "ext");
                    int height = json_num(fmt, "height", 0);
                    double filesize = json_num(fmt, "filesize", 0.0);
                    double filesize_approx = json_num(fmt, "filesize_approx", 0.0);
                    std::string vcodec = json_str(fmt, "vcodec", "none");
                    std::string acodec = json_str(fmt, "acodec", "none");
                    double tbr = json_num(fmt, "tbr", 0.0);

                    bool has_video = vcodec != "none" && !vcodec.empty();
                    bool has_audio = acodec != "none" && !acodec.empty();

                    if (has_video && height > 0) {
                        std::string quality = std::to_string(height) + "p";
                        if (seen_qualities.count(quality)) continue;
                        seen_qualities.insert(quality);

                        formats.push_back({
                            {"format_id", format_id},
                            {"ext", ext},
                            {"height", height},
                            {"quality", quality},
                            {"has_video", true},
                            {"has_audio", has_audio},
                            {"filesize", filesize > 0 ? filesize : filesize_approx},
                            {"tbr", tbr}
                        });
                    }
                }

                // Sort by height descending
                std::sort(formats.begin(), formats.end(), [](const json& a, const json& b) {
                    return a.value("height", 0) > b.value("height", 0);
                });
            }

            json response = {
                {"type", "single"},
                {"title", json_str(info, "title", "Unknown")},
                {"thumbnail", json_str(info, "thumbnail")},
                {"duration", json_num(info, "duration", 0)},
                {"uploader", json_str(info, "uploader", json_str(info, "channel", "Unknown"))},
                {"description", json_str(info, "description").substr(0, std::min((size_t)200, json_str(info, "description").size()))},
                {"platform", platform_json},
                {"formats", formats}
            };

            res.set_content(response.dump(), "application/json");
        } catch (const json::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", std::string("JSON parse error: ") + e.what()}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ── POST /api/download — start downloading media ────────────────────────
    svr.Post("/api/download", [&dl_dir](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string url = body.value("url", "");
            std::string format = body.value("format", "mp3");   // mp3, mp4
            std::string quality = body.value("quality", "best"); // best, 1080p, 720p, etc.

            if (url.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "URL is required"}}).dump(), "application/json");
                return;
            }

            // ── Per-device concurrency limit (1 download at a time) ─────────
            std::string client_ip = req.remote_addr;
            // Prefer X-Forwarded-For if behind a reverse proxy
            if (req.has_header("X-Forwarded-For")) {
                client_ip = req.get_header_value("X-Forwarded-For");
                auto comma = client_ip.find(',');
                if (comma != std::string::npos) client_ip = client_ip.substr(0, comma);
                // Trim whitespace
                client_ip.erase(0, client_ip.find_first_not_of(" "));
                client_ip.erase(client_ip.find_last_not_of(" ") + 1);
            }
            // Normalize loopback (::1 and 127.0.0.1 are the same device)
            if (client_ip == "::1") client_ip = "127.0.0.1";

            if (has_active_download(client_ip)) {
                res.status = 429;
                res.set_content(json({
                    {"error", "You already have an active download. Please wait for it to finish."},
                    {"code", "RATE_LIMITED"}
                }).dump(), "application/json");
                return;
            }

            std::string title = body.value("title", "download");
            std::string download_id = generate_download_id();
            // Use download_id only in the template (no title) to avoid emoji/Unicode path issues
            std::string out_template = dl_dir + "/" + download_id + ".%(ext)s";

            register_active_download(client_ip, download_id);

            update_download_status(download_id, {
                {"status", "starting"},
                {"progress", 0},
                {"eta", nullptr},
                {"speed", ""},
                {"filesize", ""}
            });

            // Build yt-dlp command
            std::string cmd = build_ytdlp_cmd() + " --no-warnings --newline --progress --no-playlist ";

            if (format == "mp3") {
                cmd += "-x --audio-format mp3 --audio-quality 0 ";
            } else if (format == "mp4") {
                // Re-encode audio to AAC so MP4 plays everywhere (YouTube uses Opus which many players reject)
                std::string mp4_audio = "--merge-output-format mp4 --postprocessor-args \"ffmpeg:-c:v copy -c:a aac -b:a 192k\" ";
                if (quality == "best") {
                    cmd += "-f \"bv*+ba/b\" " + mp4_audio;
                } else {
                    // Extract height from quality string (e.g., "1080p" -> "1080")
                    std::string height = quality;
                    height.erase(std::remove(height.begin(), height.end(), 'p'), height.end());
                    cmd += "-f \"bv*[height<=" + height + "]+ba/b[height<=" + height + "]\" " + mp4_audio;
                }
            }

            cmd += "-o " + escape_arg(out_template) + " " + escape_arg(url);

            std::cout << "[Luma Tools] Download cmd: " << cmd << std::endl;

            // Run download in a thread with real-time progress parsing
            std::thread([cmd, download_id, dl_dir, client_ip, title]() {
                update_download_status(download_id, {
                    {"status", "downloading"},
                    {"progress", 0},
                    {"eta", nullptr},
                    {"speed", ""},
                    {"filesize", ""}
                });

                // Parse yt-dlp output line-by-line for real progress
                std::string full_output;
                std::array<char, 4096> buffer;
                auto start_time = std::chrono::steady_clock::now();

#ifdef _WIN32
                std::string full_cmd = "\"" + cmd + " 2>&1\"";
                std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(full_cmd.c_str(), "r"), _pclose);
#else
                std::string full_cmd = cmd + " 2>&1";
                std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"), pclose);
#endif

                if (pipe) {
                    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                        std::string line(buffer.data());
                        full_output += line;

                        // Parse yt-dlp progress lines like:
                        // [download]  45.2% of ~123.45MiB at 5.67MiB/s ETA 00:15
                        // [download]  45.2% of  123.45MiB at  5.67MiB/s ETA 00:15
                        if (line.find("[download]") != std::string::npos && line.find('%') != std::string::npos) {
                            double pct = 0;
                            std::string speed_str, eta_str, size_str;

                            // Extract percentage
                            auto pct_pos = line.find('%');
                            if (pct_pos != std::string::npos) {
                                auto start = line.rfind(' ', pct_pos);
                                if (start == std::string::npos) start = line.rfind(']', pct_pos);
                                if (start != std::string::npos) {
                                    try { pct = std::stod(line.substr(start + 1, pct_pos - start - 1)); } catch (...) {}
                                }
                            }

                            // Extract filesize ("of ~123.45MiB" or "of  123.45MiB")
                            auto of_pos = line.find("of");
                            if (of_pos != std::string::npos) {
                                auto at_pos = line.find(" at ", of_pos);
                                if (at_pos != std::string::npos) {
                                    size_str = line.substr(of_pos + 2, at_pos - of_pos - 2);
                                    // Trim
                                    size_str.erase(0, size_str.find_first_not_of(" ~"));
                                    size_str.erase(size_str.find_last_not_of(" \r\n") + 1);
                                }
                            }

                            // Extract speed ("at 5.67MiB/s")
                            auto at_pos = line.find(" at ");
                            if (at_pos != std::string::npos) {
                                auto eta_pos = line.find(" ETA ", at_pos);
                                if (eta_pos != std::string::npos) {
                                    speed_str = line.substr(at_pos + 4, eta_pos - at_pos - 4);
                                } else {
                                    speed_str = line.substr(at_pos + 4);
                                }
                                speed_str.erase(0, speed_str.find_first_not_of(" "));
                                speed_str.erase(speed_str.find_last_not_of(" \r\n") + 1);
                            }

                            // Extract ETA ("ETA 00:15")
                            auto eta_pos = line.find("ETA ");
                            int eta_seconds = -1;
                            if (eta_pos != std::string::npos) {
                                eta_str = line.substr(eta_pos + 4);
                                eta_str.erase(eta_str.find_last_not_of(" \r\n") + 1);
                                // Parse MM:SS or HH:MM:SS
                                int parts[3] = {0, 0, 0};
                                int n = 0;
                                std::istringstream iss(eta_str);
                                std::string tok;
                                while (std::getline(iss, tok, ':') && n < 3) {
                                    try { parts[n++] = std::stoi(tok); } catch (...) {}
                                }
                                if (n == 2) eta_seconds = parts[0] * 60 + parts[1];
                                else if (n == 3) eta_seconds = parts[0] * 3600 + parts[1] * 60 + parts[2];
                            }

                            // Update status with real progress
                            json st = {
                                {"status", "downloading"},
                                {"progress", pct},
                                {"speed", sanitize_utf8(speed_str)},
                                {"filesize", sanitize_utf8(size_str)}
                            };
                            if (eta_seconds >= 0) st["eta"] = eta_seconds;
                            else st["eta"] = nullptr;

                            update_download_status(download_id, st);
                        }
                        // Detect post-processing phase
                        else if (line.find("[ExtractAudio]") != std::string::npos ||
                                 line.find("[Merger]") != std::string::npos ||
                                 line.find("[ffmpeg]") != std::string::npos) {
                            update_download_status(download_id, {
                                {"status", "processing"},
                                {"progress", 95},
                                {"eta", nullptr},
                                {"speed", ""},
                                {"filesize", ""}
                            });
                        }
                    }
                }

                // Find the downloaded file
                std::string found_file;
                try {
                    for (const auto& entry : fs::directory_iterator(dl_dir)) {
                        // Use u8string() to safely handle Unicode/emoji filenames on Windows
                        std::string filename;
                        try {
#ifdef _WIN32
                            filename = entry.path().filename().u8string();
#else
                            filename = entry.path().filename().string();
#endif
                        } catch (...) {
                            // Last resort: skip files we can't read the name of
                            continue;
                        }
                        if (filename.rfind(download_id, 0) == 0) {
                            // Extract extension
                            auto dot_pos = filename.rfind('.');
                            std::string ext = (dot_pos != std::string::npos) ? filename.substr(dot_pos) : "";

                            // Build clean filename: {title}_LumaTools.{ext}
                            std::string clean_name = clean_filename(title) + "_LumaTools" + ext;

                            // Handle collision: if file already exists, append a counter
                            fs::path target = fs::path(dl_dir) / clean_name;
                            if (fs::exists(target)) {
                                try { fs::remove(target); } catch (...) {}
                            }

                            // Rename from download_id to clean name
                            try {
                                fs::rename(entry.path(), target);
                                found_file = clean_name;
                                std::cout << "[Luma Tools] Renamed to: " << clean_name << std::endl;
                            } catch (const std::exception& rename_err) {
                                std::cerr << "[Luma Tools] Rename failed: " << rename_err.what() << std::endl;
                                // Fallback: sanitize original name
                                found_file = sanitize_utf8(filename);
                                if (found_file != filename) {
                                    try { fs::rename(entry.path(), fs::path(dl_dir) / found_file); } catch (...) {}
                                }
                            }
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[Luma Tools] Error scanning downloads: " << e.what() << std::endl;
                }

                // Release the per-IP slot
                unregister_active_download(client_ip);

                if (!found_file.empty()) {
                    update_download_status(download_id, {
                        {"status", "completed"},
                        {"progress", 100},
                        {"eta", 0},
                        {"speed", ""},
                        {"filename", found_file},
                        {"download_url", "/downloads/" + found_file}
                    });
                } else {
                    std::cerr << "[Luma Tools] Download failed. Output:\n" << sanitize_utf8(full_output) << std::endl;
                    update_download_status(download_id, {
                        {"status", "error"},
                        {"progress", 0},
                        {"error", "Download failed"},
                        {"details", sanitize_utf8(full_output)}
                    });
                }
            }).detach();

            json response = {
                {"download_id", download_id},
                {"status", "started"}
            };

            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ── POST /api/resolve-title — fetch real title for a single URL ──────────
    svr.Post("/api/resolve-title", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string url = body.value("url", "");
            if (url.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "Missing url"}}).dump(), "application/json");
                return;
            }

            // Use yt-dlp --print title to get the real title quickly
            std::string cmd = build_ytdlp_cmd() + " --no-download --no-warnings --print title " + escape_arg(url);
            int code;
            std::string output = exec_command(cmd, code);

            // Clean up
            output.erase(std::remove(output.begin(), output.end(), '\r'), output.end());
            output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());
            std::string title = sanitize_utf8(output);
            // Trim underscores/spaces 
            while (!title.empty() && (title.front() == '_' || title.front() == ' ')) title.erase(title.begin());
            while (!title.empty() && (title.back() == '_' || title.back() == ' ')) title.pop_back();

            if (title.empty() || code != 0) {
                res.set_content(json({{"title", ""}}).dump(), "application/json");
            } else {
                res.set_content(json({{"title", title}}).dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", std::string("Resolve failed: ") + e.what()}}).dump(), "application/json");
        }
    });

    // ── GET /api/status/:id — check download progress ───────────────────────
    svr.Get(R"(/api/status/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            json status = get_download_status(id);
            res.set_content(status.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", std::string("Status error: ") + e.what()}}).dump(), "application/json");
        }
    });

    // ── GET /api/health — server health check ───────────────────────────────
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        // Check if yt-dlp is available
        int code;
        std::string version = exec_command(build_ytdlp_cmd() + " --version", code);
        version.erase(std::remove(version.begin(), version.end(), '\n'), version.end());
        version.erase(std::remove(version.begin(), version.end(), '\r'), version.end());

        json response = {
            {"status", "ok"},
            {"server", "Luma Tools v1.0"},
            {"yt_dlp_version", version.empty() ? "not installed" : version},
            {"yt_dlp_available", !version.empty()}
        };

        res.set_content(response.dump(), "application/json");
    });

    // ── Start the server ────────────────────────────────────────────────────
    int port = 8080;
    const char* port_env = std::getenv("PORT");
    if (port_env) {
        port = std::atoi(port_env);
    }

    std::cout << R"(
  ╦  ╦ ╦╔╦╗╔═╗  ╔╦╗╔═╗╔═╗╦  ╔═╗
  ║  ║ ║║║║╠═╣   ║ ║ ║║ ║║  ╚═╗
  ╩═╝╚═╝╩ ╩╩ ╩   ╩ ╚═╝╚═╝╩═╝╚═╝
    Universal Media Downloader
)" << std::endl;

    std::cout << "[Luma Tools] Server starting on http://localhost:" << port << std::endl;
    std::cout << "[Luma Tools] Static files: " << fs::absolute(public_dir) << std::endl;
    std::cout << "[Luma Tools] Press Ctrl+C to stop" << std::endl;

    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "[Luma Tools] Failed to start server on port " << port << std::endl;
        return 1;
    }

    return 0;
}

#pragma once
/**
 * Luma Tools — Common header
 * Shared includes, using declarations, globals, and utility declarations
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
#include <cstdio>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <random>
#include <vector>
#include <set>
#include <functional>
#include <unordered_map>
#include <ctime>

// ─── Type aliases & namespace shortcuts ─────────────────────────────────────

using json = nlohmann::json;
namespace fs = std::filesystem;

using std::string;
using std::vector;
using std::map;
using std::set;
using std::mutex;
using std::thread;
using std::cout;
using std::cerr;
using std::endl;
using std::array;
using std::unique_ptr;
using std::lock_guard;
using std::function;
using std::ofstream;
using std::ifstream;
using std::istringstream;
using std::pair;
using std::regex;
using std::to_string;

// ─── Global variables ───────────────────────────────────────────────────────

extern string g_ffmpeg_path;       // Directory containing ffmpeg
extern string g_ffmpeg_exe;        // Full path to ffmpeg executable
extern string g_deno_path;         // Full path to deno executable
extern string g_ytdlp_path;       // Full path or command for yt-dlp
extern string g_ghostscript_path;  // Full path to ghostscript executable
extern string g_pandoc_path;       // Full path to pandoc executable
extern string g_groq_key;          // Groq API key (from GROQ_API_KEY env var)
extern string g_git_commit;        // Short git commit hash (set at startup)
extern string g_git_branch;        // Git branch name (set at startup)
extern string g_hostname;           // Machine hostname (set at startup)
extern string g_sevenzip_path;     // Full path to 7z/7za (empty = not found)
extern string g_imagemagick_path;  // Full path to magick or convert (empty = not found)
extern bool   g_rembg_available;   // true if `rembg` is callable
extern bool   g_ollama_available;  // true if Ollama responded at localhost:11434

// ─── Safe JSON accessors (handles null values) ──────────────────────────────

template<typename T>
T json_num(const json& j, const string& key, T def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<T>();
    return def;
}

string json_str(const json& j, const string& key, const string& def = "");

// ─── String / file utilities ────────────────────────────────────────────────

string sanitize_utf8(const string& s);
string clean_filename(const string& raw);
string escape_arg(const string& arg);
string ffmpeg_cmd();     // Returns escaped ffmpeg path, or bare "ffmpeg" fallback

// ─── Shell execution ────────────────────────────────────────────────────────

string exec_command(const string& cmd, int& exit_code);
string exec_command(const string& cmd);

// ─── Path and executable finding ────────────────────────────────────────────

void   refresh_system_path();
string find_executable(const string& name, const vector<string>& extra_paths = {});
string find_ytdlp();
string find_ghostscript();
string find_pandoc();
string find_deno();
string build_ytdlp_cmd();

// ─── Download manager ───────────────────────────────────────────────────────

string generate_download_id();
void   update_download_status(const string& id, const json& status);
json   get_download_status(const string& id);
bool   has_active_download(const string& ip);
void   register_active_download(const string& ip, const string& dl_id);
void   unregister_active_download(const string& ip);
string get_downloads_dir();

// ─── Processing job manager ─────────────────────────────────────────────────

string generate_job_id();
void   update_job(const string& id, const json& status, const string& result_path = "");
json   get_job(const string& id);
string get_job_result_path(const string& id);
void   update_job_raw_text(const string& id, const string& raw_text);
string get_job_raw_text(const string& id);

// ─── File processing helpers ────────────────────────────────────────────────

string get_processing_dir();
string read_file_binary(const string& path);
string mime_from_ext(const string& ext);
void   send_file_response(httplib::Response& res, const string& path, const string& filename);
string save_upload(const httplib::MultipartFormData& file, const string& prefix);

// ─── Platform detection ─────────────────────────────────────────────────────

struct PlatformInfo {
    string id;
    string name;
    string icon;
    string color;
    bool supports_video;
    bool supports_audio;
};

PlatformInfo detect_platform(const string& url);

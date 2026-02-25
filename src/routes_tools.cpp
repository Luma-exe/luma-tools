#include <vector>
#include <sstream>

// Simple join for vector<string>
static std::string join(const std::vector<std::string>& v, const std::string& delim) {
    std::ostringstream oss;

    for (size_t i = 0; i < v.size(); ++i) {
        if (i) oss << delim;
        oss << v[i];
    }

    return oss.str();
}

/**
 * Luma Tools — File processing tool route handlers
 * All /api/tools/* endpoints (image, video, audio, PDF)
 *
 * Key fix: uses ffmpeg_cmd() which returns the full escaped path to ffmpeg,
 * instead of bare "ffmpeg" which would fail when ffmpeg is not on the shell PATH.
 */

#include "common.h"
#include "discord.h"
#include "routes.h"

// ── Groq model chain with automatic fallback ─────────────────────────────────
static const vector<string> GROQ_MODEL_CHAIN = {
    "llama-3.3-70b-versatile",
    "deepseek-r1-distill-llama-70b",
    "llama-3.1-8b-instant"
};

// ── Last-used AI model cache (updated on every successful AI call) ────────────
static mutex  g_model_cache_mutex;
static string g_last_used_model;
static map<string, int> g_groq_tokens_cache;  // model_id → last known tokens_remaining

struct GroqResult {
    json   response;
    string model_used;
    int    tokens_used      = 0;
    int    tokens_remaining = -1;   // from rate-limit header; -1 = unknown
    bool   ok               = false;
};

static GroqResult call_groq(json payload, const string& proc, const string& prefix) {
    string pf  = proc + "/" + prefix + "_pl.json";
    string hf  = proc + "/" + prefix + "_hdr.txt";
    string rf  = proc + "/" + prefix + "_resp.json";
    string dhf = proc + "/" + prefix + "_dump.txt";   // response header dump
    { ofstream f(hf); f << "Authorization: Bearer " << g_groq_key << "\r\nContent-Type: application/json"; }
    string curl_cmd = "curl -s -X POST https://api.groq.com/openai/v1/chat/completions"
                      " -H @" + escape_arg(hf) +
                      " -D " + escape_arg(dhf) +
                      " -d @" + escape_arg(pf) +
                      " -o " + escape_arg(rf);

    // Helper: read a header value from the curl -D dump file
    auto read_header = [&](const string& key) -> string {
        if (!fs::exists(dhf)) return "";
        try {
            std::ifstream fh(dhf); string line;
            while (std::getline(fh, line)) {
                if (line.size() > key.size() + 1 &&
                    std::equal(key.begin(), key.end(), line.begin(),
                        [](char a, char b){ return ::tolower(a) == ::tolower(b); }) &&
                    line[key.size()] == ':') {
                    string v = line.substr(key.size() + 1);
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                    while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
                    return v;
                }
            }
        } catch (...) {}
        return "";
    };

    GroqResult result;
    for (const auto& model : GROQ_MODEL_CHAIN) {
        payload["model"] = model;
        // Use error_handler_t::replace so invalid UTF-8 bytes (e.g. 0xA0 from
        // Windows-1252 encoded PDFs) never cause type_error.316 to throw here.
        { ofstream f(pf); f << payload.dump(-1, ' ', false, json::error_handler_t::replace); }
        try { if (fs::exists(rf))  fs::remove(rf);  } catch (...) {}
        try { if (fs::exists(dhf)) fs::remove(dhf); } catch (...) {}
        int rc; exec_command(curl_cmd, rc);
        if (!fs::exists(rf) || fs::file_size(rf) == 0) continue;
        try {
            std::ifstream f(rf); std::ostringstream ss; ss << f.rdbuf();
            auto rj = json::parse(ss.str());
            bool rate_limited = rj.contains("error") && rj["error"].is_object() &&
                (rj["error"].value("message","").find("Rate limit") != string::npos ||
                 rj["error"].value("message","").find("rate limit") != string::npos);
            if (rate_limited) continue;
            result.response   = rj;
            result.model_used = model;
            result.ok         = rj.contains("choices") && !rj["choices"].empty();
            if (result.ok) { lock_guard<mutex> lk(g_model_cache_mutex); g_last_used_model = model; }
            // Extract token usage from response body
            if (rj.contains("usage") && rj["usage"].is_object())
                result.tokens_used = rj["usage"].value("total_tokens", 0);
            // Extract remaining tokens from rate-limit response header
            string rem = read_header("x-ratelimit-remaining-tokens");
            if (!rem.empty()) {
                try { result.tokens_remaining = std::stoi(rem); } catch (...) {}
            }
            if (result.tokens_remaining >= 0) {
                lock_guard<mutex> lk(g_model_cache_mutex);
                g_groq_tokens_cache[model] = result.tokens_remaining;
            }
            break;
        } catch (...) {}
    }
    // ── Ollama local fallback ─────────────────────────────────────────────────
    if (!result.ok) {
        string ollama_rf = proc + "/" + prefix + "_ollama_resp.json";
        // Build Ollama-compat payload (messages array stays the same)
        json ol_payload = payload;
        ol_payload["model"] = "llama3.1:8b";
        string ol_pf = proc + "/" + prefix + "_ollama_pl.json";
        { ofstream f(ol_pf); f << ol_payload.dump(-1, ' ', false, json::error_handler_t::replace); }
        string ol_cmd = "curl -s -X POST http://localhost:11434/v1/chat/completions"
                        " -H \"Content-Type: application/json\""
                        " -d @" + escape_arg(ol_pf) +
                        " -o " + escape_arg(ollama_rf);
        int ol_rc; exec_command(ol_cmd, ol_rc);
        if (fs::exists(ollama_rf) && fs::file_size(ollama_rf) > 0) {
            try {
                std::ifstream f(ollama_rf); std::ostringstream ss; ss << f.rdbuf();
                auto rj = json::parse(ss.str());
                if (rj.contains("choices") && !rj["choices"].empty()) {
                    result.response = rj;
                    result.model_used = "ollama:llama3.1:8b";
                    result.ok = true;
                    { lock_guard<mutex> lk(g_model_cache_mutex); g_last_used_model = "ollama:llama3.1:8b"; }
                }
            } catch (...) {}
        }
        try { fs::remove(ol_pf); fs::remove(ollama_rf); } catch (...) {}
    }
    try { fs::remove(pf); fs::remove(hf); fs::remove(rf); fs::remove(dhf); } catch (...) {}
    return result;
}

// -----------------------------------------------------------------------------
// Helper: strip invalid UTF-8 bytes and BOMs so json::dump() never throws 316
// (0xA0 and other Latin-1/Windows-1252 bytes that appear in PDF-extracted text)
static string sanitize_utf8(const string& s) {
    string out;
    out.reserve(s.size());
    size_t i = 0;
    // Strip UTF-8 BOM (EF BB BF) or UTF-16 BOM (FF FE / FE FF)
    if (s.size() >= 3 && (unsigned char)s[0]==0xEF && (unsigned char)s[1]==0xBB && (unsigned char)s[2]==0xBF) i = 3;
    else if (s.size() >= 2 && ((unsigned char)s[0]==0xFF && (unsigned char)s[1]==0xFE)) i = 2;
    else if (s.size() >= 2 && ((unsigned char)s[0]==0xFE && (unsigned char)s[1]==0xFF)) i = 2;
    for (; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        int seq = 0;
        if      (c <= 0x7F)                         seq = 1;
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2)  seq = 2;
        else if ((c & 0xF0) == 0xE0)                seq = 3;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4)  seq = 4;
        if (seq == 0) { i++; out += '?'; continue; } // invalid lead byte → replace
        if (i + seq > s.size()) { i++; out += '?'; continue; }
        bool ok = true;
        for (int k = 1; k < seq; k++)
            if (((unsigned char)s[i+k] & 0xC0) != 0x80) { ok = false; break; }
        if (ok) { out.append(s, i, seq); i += seq; }
        else    { out += '?'; i++; }
    }
    return out;
}

// extract_text_from_upload  — shared helper used by Flashcards, Quiz, etc.
// Saves the multipart file to proc/jid_input<ext>, extracts text via the
// appropriate method, then cleans up temp files.  Returns empty string on fail.
// -----------------------------------------------------------------------------
static string extract_text_from_upload(
    const httplib::MultipartFormData& file,
    const string& proc, const string& jid)
{
    fs::path fp(file.filename);
    string file_ext = fp.extension().string();
    std::transform(file_ext.begin(), file_ext.end(), file_ext.begin(), ::tolower);

    string input_path = proc + "/" + jid + "_input" + file_ext;
    { ofstream f(input_path, std::ios::binary); f.write(file.content.data(), file.content.size()); }

    string text;
    string txt_path = proc + "/" + jid + "_text.txt";

    if (file_ext == ".txt" || file_ext == ".md" || file_ext == ".rtf") {
        ifstream f(input_path, std::ios::binary);
        std::ostringstream ss; ss << f.rdbuf();
        text = ss.str();
    } else if (file_ext == ".pdf") {
        // Try Ghostscript first (with -dTextFormat=3 for consistent UTF-8 output)
        if (!g_ghostscript_path.empty()) {
            string cmd = escape_arg(g_ghostscript_path)
                       + " -q -dNOPAUSE -dBATCH -sDEVICE=txtwrite"
                         " -dTextFormat=3"
                       " -sOutputFile=" + escape_arg(txt_path)
                       + " " + escape_arg(input_path);
            int code; exec_command(cmd, code);
            if (fs::exists(txt_path) && fs::file_size(txt_path) > 0) {
                ifstream f(txt_path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf();
                text = ss.str();
            }
        }
        // Fallback: pdftotext
        if (text.empty()) {
            string cmd = "pdftotext " + escape_arg(input_path) + " " + escape_arg(txt_path);
            int code; exec_command(cmd, code);
            if (fs::exists(txt_path) && fs::file_size(txt_path) > 0) {
                ifstream f(txt_path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf();
                text = ss.str();
            }
        }
    } else if (file_ext == ".docx") {
        string pandoc_exe = g_pandoc_path.empty() ? "pandoc" : g_pandoc_path;
        string cmd = escape_arg(pandoc_exe) + " -f docx -t plain " + escape_arg(input_path) + " -o " + escape_arg(txt_path);
        int code; exec_command(cmd, code);
        if (fs::exists(txt_path)) {
            ifstream f(txt_path); std::ostringstream ss; ss << f.rdbuf();
            text = ss.str();
        }
    }

    try { fs::remove(input_path); fs::remove(txt_path); } catch (...) {}
    return sanitize_utf8(text);
}

void register_tool_routes(httplib::Server& svr, string dl_dir) {

    // ── POST /api/tools/image-compress ──────────────────────────────────────
    svr.Post("/api/tools/image-compress", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");

        {
            string ec = fs::path(file.filename).extension().string();
            std::transform(ec.begin(), ec.end(), ec.begin(), ::tolower);
            if (ec == ".svg") {
                res.status = 400;
                res.set_content(json({{"error", "SVG is a vector format and cannot be compressed. Use Image Convert to rasterise it to PNG or JPEG first."}}).dump(), "application/json");
                return;
            }
        }

        int quality = 75;

        if (req.has_file("quality")) {
            try { quality = std::stoi(req.get_file_value("quality").content); } catch (...) {}
        }
        if (quality < 1)   quality = 1;
        if (quality > 100) quality = 100;

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_out" + ext;

        string qarg;
        string e = ext;
        std::transform(e.begin(), e.end(), e.begin(), ::tolower);

        if (e == ".jpg" || e == ".jpeg") {
            int qv = 2 + (100 - quality) * 29 / 100;
            qarg = "-q:v " + to_string(qv);
        } else if (e == ".webp") {
            qarg = "-quality " + to_string(quality);
        } else if (e == ".png") {
            qarg = "-compression_level 9";
        } else {
            int qv = 2 + (100 - quality) * 29 / 100;
            qarg = "-q:v " + to_string(qv);
        }

        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " " + qarg + " " + escape_arg(output_path);
        cout << "[Luma Tools] Image compress: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            discord_log_tool("Image Compress", file.filename, req.remote_addr);
            string out_name = fs::path(file.filename).stem().string() + "_compressed" + ext;
            send_file_response(res, output_path, out_name);
        } else {
            string err_msg = "Image compression failed for: " + mask_filename(file.filename);
            discord_log_error("Image Compress", err_msg);
            res.status = 500;
            res.set_content(json({{"error", "Image compression failed"}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/image-resize ────────────────────────────────────────
    svr.Post("/api/tools/image-resize", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");

        {
            string ec = fs::path(file.filename).extension().string();
            std::transform(ec.begin(), ec.end(), ec.begin(), ::tolower);
            if (ec == ".svg") {
                res.status = 400;
                res.set_content(json({{"error", "SVG is a vector format and cannot be resized this way. Use Image Convert to rasterise it to PNG first."}}).dump(), "application/json");
                return;
            }
        }

        string width_raw  = req.has_file("width")  ? req.get_file_value("width").content  : "";
        string height_raw = req.has_file("height") ? req.get_file_value("height").content : "";

        if (width_raw.empty() && height_raw.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "Width or height required"}}).dump(), "application/json");
            return;
        }

        // Parse as integers to prevent FFmpeg filter injection (e.g. "scale=injected:value")
        int w_val = -1, h_val = -1;
        if (!width_raw.empty()) {
            try { w_val = std::stoi(width_raw); } catch (...) { w_val = -2; }
        }
        if (!height_raw.empty()) {
            try { h_val = std::stoi(height_raw); } catch (...) { h_val = -2; }
        }
        if (w_val == -2 || h_val == -2 || w_val == 0 || h_val == 0) {
            res.status = 400;
            res.set_content(json({{"error", "Width and height must be positive integers"}}).dump(), "application/json");
            return;
        }
        if (w_val > 16000 || h_val > 16000) {
            res.status = 400;
            res.set_content(json({{"error", "Dimensions too large (max 16000px)"}}).dump(), "application/json");
            return;
        }
        // -1 means ffmpeg auto-scales that dimension (maintain aspect ratio)
        string sw = (w_val < 0) ? "-1" : to_string(w_val);
        string sh = (h_val < 0) ? "-1" : to_string(h_val);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_out" + ext;
        string filter = "scale=" + sw + ":" + sh;

        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " -vf " + escape_arg(filter) + " " + escape_arg(output_path);
        cout << "[Luma Tools] Image resize: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            discord_log_tool("Image Resize", file.filename, req.remote_addr);
            string out_name = fs::path(file.filename).stem().string() + "_resized" + ext;
            send_file_response(res, output_path, out_name);
        } else {
            string err_msg = "Image resize failed for: " + mask_filename(file.filename);
            discord_log_error("Image Resize", err_msg);
            res.status = 500;
            res.set_content(json({{"error", "Image resize failed"}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/image-convert ───────────────────────────────────────
    svr.Post("/api/tools/image-convert", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string format = req.has_file("format") ? req.get_file_value("format").content : "png";

        // Allowlist to prevent unexpected formats/paths
        static const set<string> ALLOWED_IMG_FMT = {
            "png","jpg","jpeg","webp","bmp","tiff","tif","gif","avif","ico"
        };
        if (ALLOWED_IMG_FMT.find(format) == ALLOWED_IMG_FMT.end()) {
            res.status = 400;
            res.set_content(json({{"error", "Unsupported output format: " + format}}).dump(), "application/json");
            return;
        }

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string out_ext = "." + (format == "jpg" ? "jpg" : format);
        string output_path = get_processing_dir() + "/" + jid + "_out" + out_ext;

        // ── SVG rasterisation ──────────────────────────────────────────────
        // ffmpeg cannot decode SVG without librsvg. Rasterise to PNG first
        // using whichever tool is available, then let ffmpeg do the rest.
        string in_ext = fs::path(file.filename).extension().string();
        std::transform(in_ext.begin(), in_ext.end(), in_ext.begin(), ::tolower);

        string ffmpeg_input = input_path; // may be overridden to rasterised PNG

        if (in_ext == ".svg") {
            string png_path = get_processing_dir() + "/" + jid + "_raster.png";
            bool rasterised = false;
            int rc = 1;

            // Priority 1: rsvg-convert  (librsvg — highest fidelity for SVG)
            string rsvg = find_executable("rsvg-convert", {});
            if (!rsvg.empty()) {
                string cmd = escape_arg(rsvg) + " -f png -o " + escape_arg(png_path) + " " + escape_arg(input_path);
                cout << "[Luma Tools] SVG rasterise (rsvg-convert): " << cmd << endl;
                exec_command(cmd, rc);
                if (fs::exists(png_path) && fs::file_size(png_path) > 0) rasterised = true;
            }

            // Priority 2: inkscape (headless)
            if (!rasterised) {
                string ink = find_executable("inkscape", {
                    "/usr/bin/inkscape", "/usr/local/bin/inkscape",
                    "C:\\Program Files\\Inkscape\\bin\\inkscape.exe",
                    "C:\\Program Files (x86)\\Inkscape\\bin\\inkscape.exe"
                });
                if (!ink.empty()) {
                    string cmd = escape_arg(ink) + " --export-type=png --export-filename=" + escape_arg(png_path) + " " + escape_arg(input_path);
                    cout << "[Luma Tools] SVG rasterise (inkscape): " << cmd << endl;
                    exec_command(cmd, rc);
                    if (fs::exists(png_path) && fs::file_size(png_path) > 0) rasterised = true;
                }
            }

            // Priority 3: ImageMagick magick (v7, Windows)
            if (!rasterised) {
                string magick7 = find_executable("magick", {});
                if (!magick7.empty()) {
                    string cmd = escape_arg(magick7) + " convert " + escape_arg(input_path) + " " + escape_arg(png_path);
                    cout << "[Luma Tools] SVG rasterise (magick convert): " << cmd << endl;
                    exec_command(cmd, rc);
                    if (fs::exists(png_path) && fs::file_size(png_path) > 0) rasterised = true;
                }
            }

            // Priority 4: ImageMagick convert (v6 / Linux)
            if (!rasterised) {
                string magick = find_executable("convert", {"/usr/bin/convert", "/usr/local/bin/convert"});
                if (!magick.empty()) {
                    string cmd = escape_arg(magick) + " " + escape_arg(input_path) + " " + escape_arg(png_path);
                    cout << "[Luma Tools] SVG rasterise (ImageMagick convert): " << cmd << endl;
                    exec_command(cmd, rc);
                    if (fs::exists(png_path) && fs::file_size(png_path) > 0) rasterised = true;
                }
            }

            if (!rasterised) {
                string err_msg = "SVG rasterisation failed for " + mask_filename(file.filename) + " — rsvg-convert, inkscape or ImageMagick must be installed on the server";
                discord_log_error("Image Convert", err_msg);
                try { fs::remove(input_path); } catch (...) {}
                res.status = 500;
                res.set_content(json({{"error", "SVG rasterisation failed — rsvg-convert, inkscape or ImageMagick must be installed on the server."}}).dump(), "application/json");
                return;
            }

            ffmpeg_input = png_path; // hand the rasterised PNG to ffmpeg below

            // If the requested output is PNG and we already have the raster, serve it directly
            if (format == "png") {
                discord_log_tool("Image Convert", file.filename + " -> png (SVG rasterised)", req.remote_addr);
                string out_name = fs::path(file.filename).stem().string() + ".png";
                send_file_response(res, png_path, out_name);
                try { fs::remove(input_path); fs::remove(png_path); } catch (...) {}
                return;
            }
        }
        // ── End SVG rasterisation ──────────────────────────────────────────

        // Build codec flags per format
        string codec_flags;
        if (format == "avif") {
            // AV1 encoder; -cpu-used 6 trades quality for much faster encoding
            codec_flags = "-c:v libaom-av1 -crf 30 -b:v 0 -cpu-used 6 -pix_fmt yuv420p";
        }

        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(ffmpeg_input);
        if (!codec_flags.empty()) cmd += " " + codec_flags;
        cmd += " " + escape_arg(output_path);
        cout << "[Luma Tools] Image convert: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string label = file.filename + " -> " + format;
            if (in_ext == ".svg") label += " (SVG rasterised)";
            discord_log_tool("Image Convert", label, req.remote_addr);
            string out_name = fs::path(file.filename).stem().string() + out_ext;
            send_file_response(res, output_path, out_name);
        } else {
            string err_msg = "Image conversion failed for: " + mask_filename(file.filename) + " -> " + format;
            discord_log_error("Image Convert", err_msg);
            res.status = 500;
            res.set_content(json({{"error", "Image conversion failed"}}).dump(), "application/json");
        }

        // Clean up — also remove the rasterised PNG if SVG path was taken
        string raster_path = get_processing_dir() + "/" + jid + "_raster.png";
        try {
            fs::remove(input_path);
            fs::remove(output_path);
            if (fs::exists(raster_path)) fs::remove(raster_path);
        } catch (...) {}
    });

    // ── POST /api/tools/audio-convert ───────────────────────────────────────
    svr.Post("/api/tools/audio-convert", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string format = req.has_file("format") ? req.get_file_value("format").content : "mp3";

        // Allowlist: prevent path traversal and unknown format injection
        static const set<string> ALLOWED_AUDIO_FMT = {"mp3","aac","m4a","wav","flac","ogg","wma"};
        if (ALLOWED_AUDIO_FMT.find(format) == ALLOWED_AUDIO_FMT.end()) {
            res.status = 400;
            res.set_content(json({{"error", "Unsupported audio format: " + format}}).dump(), "application/json");
            return;
        }

        discord_log_tool("Audio Convert", file.filename + " -> " + format, req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string out_ext = "." + format;
        string output_path = get_processing_dir() + "/" + jid + "_out" + out_ext;

        string codec;

        if (format == "mp3") codec = "-c:a libmp3lame -q:a 2";
        else if (format == "aac" || format == "m4a") codec = "-c:a aac -b:a 192k";
        else if (format == "wav") codec = "-c:a pcm_s16le";
        else if (format == "flac") codec = "-c:a flac";
        else if (format == "ogg") codec = "-c:a libvorbis -q:a 6";
        else if (format == "wma") codec = "-c:a wmav2 -b:a 192k";

        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " " + codec + " " + escape_arg(output_path);
        cout << "[Luma Tools] Audio convert: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string out_name = fs::path(file.filename).stem().string() + out_ext;
            send_file_response(res, output_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("Audio Convert", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "Audio conversion failed"}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/video-compress (async) ──────────────────────────────
    svr.Post("/api/tools/video-compress", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string preset = req.has_file("preset") ? req.get_file_value("preset").content : "medium";

        discord_log_tool("Video Compress", file.filename + " (" + preset + ")", req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_out.mp4";
        string orig_name = fs::path(file.filename).stem().string();

        update_job(jid, {{"status", "processing"}, {"progress", 0}, {"stage", "Compressing video..."}});

        thread([jid, input_path, output_path, preset, orig_name]() {
            // Map preset names from frontend (light/medium/heavy) to CRF values
            int crf = 26;

            if (preset == "light")       crf = 28;
            else if (preset == "medium") crf = 26;
            else if (preset == "heavy")  crf = 32;
            // Legacy names from old frontend
            else if (preset == "low")    crf = 28;
            else if (preset == "high")   crf = 20;

            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -c:v libx264 -crf " + to_string(crf) +
                " -preset medium -c:a aac -b:a 128k " + escape_arg(output_path);
            cout << "[Luma Tools] Video compress: " << cmd << endl;
            int code;
            exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
                string result_name = orig_name + "_compressed.mp4";
                update_job(jid, {{"status", "completed"}, {"progress", 100}, {"filename", result_name}}, output_path);
            } else {
                discord_log_error("Video Compress", "Failed for: " + mask_filename(orig_name));
                update_job(jid, {{"status", "error"}, {"error", "Video compression failed"}});
            }

            try { fs::remove(input_path); } catch (...) {}
        }).detach();

        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/video-trim (async) — with frame-level precision ─────
    svr.Post("/api/tools/video-trim", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string start = req.has_file("start") ? req.get_file_value("start").content : "00:00:00";
        string end   = req.has_file("end")   ? req.get_file_value("end").content   : "";
        string mode  = req.has_file("mode")  ? req.get_file_value("mode").content  : "fast";

        if (end.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "End time is required"}}).dump(), "application/json");
            return;
        }

        // Validate timestamps: only digits, colons, and periods allowed (prevent injection)
        auto is_valid_timestamp = [](const string& ts) {
            return !ts.empty() && ts.size() <= 20 &&
                   std::all_of(ts.begin(), ts.end(), [](char c) {
                       return std::isdigit((unsigned char)c) || c == ':' || c == '.';
                   });
        };
        if (!is_valid_timestamp(start) || !is_valid_timestamp(end)) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid timestamp format"}}).dump(), "application/json");
            return;
        }

        discord_log_tool("Video Trim (" + mode + ")", file.filename + " [" + start + " -> " + end + "]", req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        // Precise mode forces mp4 output (re-encode with x264+aac)
        string out_ext = (mode == "precise") ? ".mp4" : ext;
        string output_path = get_processing_dir() + "/" + jid + "_out" + out_ext;
        string orig_name = fs::path(file.filename).stem().string();

        update_job(jid, {{"status", "processing"}, {"progress", 0}, {"stage", "Trimming video..."}});

        thread([jid, input_path, output_path, start, end, out_ext, orig_name, mode]() {
            string cmd;

            if (mode == "precise") {
                // Frame-accurate: re-encode (slower but exact frame cuts)
                cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                    " -ss " + start + " -to " + end +
                    " -c:v libx264 -crf 18 -preset fast -c:a aac -b:a 192k " +
                    escape_arg(output_path);
            } else {
                // Fast: stream copy (instant but keyframe-aligned)
                cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                    " -ss " + start + " -to " + end + " -c copy " + escape_arg(output_path);
            }

            cout << "[Luma Tools] Video trim (" << mode << "): " << cmd << endl;
            int code;
            exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
                string result_name = orig_name + "_trimmed" + out_ext;
                update_job(jid, {{"status", "completed"}, {"progress", 100}, {"filename", result_name}}, output_path);
            } else {
                discord_log_error("Video Trim", "Failed for: " + mask_filename(orig_name));
                update_job(jid, {{"status", "error"}, {"error", "Video trimming failed"}});
            }

            try { fs::remove(input_path); } catch (...) {}
        }).detach();

        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/video-convert (async) ───────────────────────────────
    svr.Post("/api/tools/video-convert", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string format = req.has_file("format") ? req.get_file_value("format").content : "mp4";

        // Allowlist: prevent path traversal and unknown format injection
        static const set<string> ALLOWED_VIDEO_FMT = {"mp4","webm","mkv","avi","mov","gif"};
        if (ALLOWED_VIDEO_FMT.find(format) == ALLOWED_VIDEO_FMT.end()) {
            res.status = 400;
            res.set_content(json({{"error", "Unsupported video format: " + format}}).dump(), "application/json");
            return;
        }

        discord_log_tool("Video Convert", file.filename + " -> " + format, req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string out_ext = "." + format;
        string output_path = get_processing_dir() + "/" + jid + "_out" + out_ext;
        string orig_name = fs::path(file.filename).stem().string();

        update_job(jid, {{"status", "processing"}, {"progress", 0}, {"stage", "Converting video..."}});

        thread([jid, input_path, output_path, format, out_ext, orig_name]() {
            string codec;

            if (format == "mp4")       codec = "-c:v libx264 -c:a aac";
            else if (format == "webm") codec = "-c:v libvpx-vp9 -c:a libopus";
            else if (format == "mkv")  codec = "-c:v libx264 -c:a aac";
            else if (format == "avi")  codec = "-c:v libx264 -c:a mp3";
            else if (format == "mov")  codec = "-c:v libx264 -c:a aac";
            else if (format == "gif")  codec = "-vf \"fps=15,scale=480:-1:flags=lanczos\" -loop 0";

            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " " + codec + " " + escape_arg(output_path);
            cout << "[Luma Tools] Video convert: " << cmd << endl;
            int code;
            exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
                string result_name = orig_name + out_ext;
                update_job(jid, {{"status", "completed"}, {"progress", 100}, {"filename", result_name}}, output_path);
            } else {
                discord_log_error("Video Convert", "Failed for: " + mask_filename(orig_name));
                update_job(jid, {{"status", "error"}, {"error", "Video conversion failed"}});
            }

            try { fs::remove(input_path); } catch (...) {}
        }).detach();

        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/video-extract-audio (async) ─────────────────────────
    svr.Post("/api/tools/video-extract-audio", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string format = req.has_file("format") ? req.get_file_value("format").content : "mp3";

        // Allowlist: prevent path traversal and unknown format injection
        static const set<string> ALLOWED_EXTRACT_FMT = {"mp3","aac","m4a","wav","flac","ogg"};
        if (ALLOWED_EXTRACT_FMT.find(format) == ALLOWED_EXTRACT_FMT.end()) {
            res.status = 400;
            res.set_content(json({{"error", "Unsupported audio format: " + format}}).dump(), "application/json");
            return;
        }

        discord_log_tool("Extract Audio", file.filename + " -> " + format, req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string out_ext = "." + format;
        string output_path = get_processing_dir() + "/" + jid + "_out" + out_ext;
        string orig_name = fs::path(file.filename).stem().string();

        update_job(jid, {{"status", "processing"}, {"progress", 0}, {"stage", "Extracting audio..."}});

        thread([jid, input_path, output_path, format, out_ext, orig_name]() {
            string codec;

            if (format == "mp3")                      codec = "-c:a libmp3lame -q:a 2";
            else if (format == "aac" || format == "m4a") codec = "-c:a aac -b:a 192k";
            else if (format == "wav")                 codec = "-c:a pcm_s16le";
            else if (format == "flac")                codec = "-c:a flac";
            else if (format == "ogg")                 codec = "-c:a libvorbis -q:a 6";

            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -vn " + codec + " " + escape_arg(output_path);
            cout << "[Luma Tools] Extract audio: " << cmd << endl;
            int code;
            exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
                string result_name = orig_name + out_ext;
                update_job(jid, {{"status", "completed"}, {"progress", 100}, {"filename", result_name}}, output_path);
            } else {
                discord_log_error("Extract Audio", "Failed for: " + mask_filename(orig_name));
                update_job(jid, {{"status", "error"}, {"error", "Audio extraction failed"}});
            }

            try { fs::remove(input_path); } catch (...) {}
        }).detach();

        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── GET /api/ai-status — return last-used AI model (for frontend badge) ───
    svr.Get("/api/ai-status", [](const httplib::Request&, httplib::Response& res) {
        string model;
        { lock_guard<mutex> lk(g_model_cache_mutex); model = g_last_used_model; }
        // Fall back to the primary model so the badge is never stuck on "Checking"
        if (model.empty() && !GROQ_MODEL_CHAIN.empty()) model = GROQ_MODEL_CHAIN[0];
        json j = model.empty() ? json{{"model", nullptr}} : json{{"model", model}};
        res.set_header("Cache-Control", "no-store");
        res.set_content(j.dump(), "application/json");
    });

    // ── GET /api/tools/status/:id — check processing job ────────────────────
    svr.Get(R"(/api/tools/status/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        string id = req.matches[1];
        json status = get_job(id);
        res.set_content(status.dump(), "application/json");
    });

    // ── GET /api/tools/result/:id — download processed file ─────────────────
    svr.Get(R"(/api/tools/result/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        string id = req.matches[1];
        string path = get_job_result_path(id);

        if (path.empty() || !fs::exists(path)) {
            res.status = 404;
            res.set_content(json({{"error", "Result not found"}}).dump(), "application/json");
            return;
        }

        json status = get_job(id);
        string filename = json_str(status, "filename", "processed_file");
        send_file_response(res, path, filename);
    });

    // ── GET /api/tools/raw-text/:id — get raw extracted text for comparison ─
    svr.Get(R"(/api/tools/raw-text/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        string id = req.matches[1];
        string raw = get_job_raw_text(id);

        if (raw.empty()) {
            res.status = 404;
            res.set_content(json({{"error", "Raw text not found"}}).dump(), "application/json");
            return;
        }

        res.set_content(raw, "text/plain; charset=utf-8");
    });

    // ── POST /api/tools/ai-coverage-analysis — AI-powered coverage comparison ─
    svr.Post("/api/tools/ai-coverage-analysis", [](const httplib::Request& req, httplib::Response& res) {
        if (g_groq_key.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "AI features are not configured on this server."}}).dump(), "application/json");
            return;
        }

        string job_id = req.has_file("job_id") ? req.get_file_value("job_id").content : "";
        string notes = req.has_file("notes") ? req.get_file_value("notes").content : "";

        if (job_id.empty() || notes.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "Missing job_id or notes"}}).dump(), "application/json");
            return;
        }

        string source_text = get_job_raw_text(job_id);
        if (source_text.empty()) {
            res.status = 404;
            res.set_content(json({{"error", "Source text not found for this job"}}).dump(), "application/json");
            return;
        }

        // Truncate to fit API limits
        if (source_text.size() > 14000) source_text = source_text.substr(0, 14000);
        if (notes.size() > 14000) notes = notes.substr(0, 14000);

        string proc = get_processing_dir();
        string rid = generate_job_id(); // request ID for temp files

        // Build prompt for AI coverage analysis
        string system_prompt = R"(You are an expert study advisor analyzing the coverage between source material and generated study notes.

Your task is to:
1. Identify the key concepts, facts, and topics in the SOURCE material
2. Check which of these key concepts are covered in the NOTES
3. Identify any important topics that are missing from the notes

Respond ONLY with valid JSON in this exact format (no markdown, no code blocks, just pure JSON):
{
  "overall_score": <number 0-100>,
  "verdict": "<Excellent|Good|Adequate|Needs Improvement>",
  "summary": "<1-2 sentence overall assessment>",
  "key_concepts": [
    {"topic": "<concept name>", "covered": <true/false>, "importance": "<high|medium|low>", "notes_excerpt": "<brief quote from notes if covered, empty if not>"}
  ],
  "strengths": ["<what the notes do well>"],
  "gaps": ["<important topics missing or underexplained>"],
  "study_tips": ["<actionable recommendations>"]
}

Focus on educational value. A concept is "covered" if its key information is present in the notes, even if worded differently.
Return 10-20 key concepts. Be thorough but fair in your assessment.)";

        string user_prompt = "SOURCE MATERIAL:\n" + source_text + "\n\n---\n\nGENERATED NOTES:\n" + notes;

        json payload = {
            {"model", "llama-3.3-70b-versatile"},
            {"messages", json::array({
                {{"role","system"}, {"content", system_prompt}},
                {{"role","user"},   {"content", user_prompt}}
            })},
            {"max_tokens", 2000},
            {"temperature", 0.3}
        };

        auto gr = call_groq(payload, proc, rid + "_coverage");

        json result;
        bool ok = false;
        if (gr.ok) {
            try {
                string content = gr.response["choices"][0]["message"]["content"].get<string>();
                // Strip markdown code blocks if present
                if (content.find("```json") != string::npos) {
                    size_t start = content.find("```json") + 7;
                    size_t end = content.find("```", start);
                    if (end != string::npos) content = content.substr(start, end - start);
                } else if (content.find("```") != string::npos) {
                    size_t start = content.find("```") + 3;
                    size_t end = content.find("```", start);
                    if (end != string::npos) content = content.substr(start, end - start);
                }
                // Trim whitespace
                while (!content.empty() && (content.front() == ' ' || content.front() == '\n')) content.erase(0, 1);
                while (!content.empty() && (content.back() == ' ' || content.back() == '\n')) content.pop_back();
                result = json::parse(content);
                result["model_used"] = gr.model_used;
                ok = true;
            } catch (const std::exception& e) {
                result = {{"error", string("Failed to parse AI response: ") + e.what()}};
            }
        } else if (!gr.response.is_null() && gr.response.contains("error")) {
            result = {{"error", gr.response["error"].value("message", "AI API error")}};
        } else {
            result = {{"error", "AI API call failed"}};
        }

        if (!ok && !result.contains("error")) {
            result = {{"error", "Unknown AI analysis error"}};
        }

        res.set_content(result.dump(), "application/json");
    });

    // ── POST /api/tools/pdf-compress ────────────────────────────────────────
    svr.Post("/api/tools/pdf-compress", [](const httplib::Request& req, httplib::Response& res) {
        if (g_ghostscript_path.empty()) {
            res.status = 500;
            res.set_content(json({{"error", "Ghostscript not installed. PDF tools require Ghostscript."}}).dump(), "application/json");
            return;
        }

        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string level = req.has_file("level") ? req.get_file_value("level").content : "ebook";

        // Allowlist: prevent GhostScript option injection via -dPDFSETTINGS=/...
        static const set<string> ALLOWED_PDF_LEVELS = {"screen","ebook","printer","prepress","default"};
        if (ALLOWED_PDF_LEVELS.find(level) == ALLOWED_PDF_LEVELS.end()) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid PDF quality level. Use: screen, ebook, printer, prepress, or default."}}).dump(), "application/json");
            return;
        }

        discord_log_tool("PDF Compress", file.filename + " (" + level + ")", req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_out.pdf";

        string cmd = escape_arg(g_ghostscript_path) +
            " -sDEVICE=pdfwrite -dCompatibilityLevel=1.4 -dPDFSETTINGS=/" + level +
            " -dNOPAUSE -dQUIET -dBATCH -sOutputFile=" + escape_arg(output_path) +
            " " + escape_arg(input_path);
        cout << "[Luma Tools] PDF compress: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string out_name = fs::path(file.filename).stem().string() + "_compressed.pdf";
            send_file_response(res, output_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("PDF Compress", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "PDF compression failed"}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/pdf-merge ───────────────────────────────────────────
    svr.Post("/api/tools/pdf-merge", [](const httplib::Request& req, httplib::Response& res) {
        if (g_ghostscript_path.empty()) {
            res.status = 500;
            res.set_content(json({{"error", "Ghostscript not installed. PDF tools require Ghostscript."}}).dump(), "application/json");
            return;
        }

        int count_val = 0;

        if (req.has_file("count")) {
            try { count_val = std::stoi(req.get_file_value("count").content); } catch (...) {}
        }

        if (count_val < 2) {
            res.status = 400;
            res.set_content(json({{"error", "At least 2 PDF files required"}}).dump(), "application/json");
            return;
        }
        if (count_val > 50) {
            res.status = 400;
            res.set_content(json({{"error", "Too many files. Maximum is 50 PDFs per merge."}}).dump(), "application/json");
            return;
        }

        discord_log_tool("PDF Merge", to_string(count_val) + " files", req.remote_addr);

        string jid = generate_job_id();
        string proc_dir = get_processing_dir();
        vector<string> input_paths;

        for (int i = 0; i < count_val; i++) {
            string key = "file" + to_string(i);

            if (!req.has_file(key)) continue;
            auto f = req.get_file_value(key);
            string path = proc_dir + "/" + jid + "_in" + to_string(i) + ".pdf";
            ofstream out(path, std::ios::binary);
            out.write(f.content.data(), f.content.size());
            out.close();
            input_paths.push_back(path);
        }

        if (input_paths.size() < 2) {
            res.status = 400;
            res.set_content(json({{"error", "At least 2 valid PDF files required"}}).dump(), "application/json");

            for (auto& p : input_paths) try { fs::remove(p); } catch (...) {}
            return;
        }

        string output_path = proc_dir + "/" + jid + "_merged.pdf";
        string cmd = escape_arg(g_ghostscript_path) +
            " -dNOPAUSE -dBATCH -sDEVICE=pdfwrite -sOutputFile=" + escape_arg(output_path);

        for (const auto& p : input_paths) cmd += " " + escape_arg(p);

        cout << "[Luma Tools] PDF merge: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            send_file_response(res, output_path, "merged.pdf");
        } else {
            res.status = 500;
            discord_log_error("PDF Merge", "Merge failed");
            res.set_content(json({{"error", "PDF merge failed"}}).dump(), "application/json");
        }

        for (auto& p : input_paths) try { fs::remove(p); } catch (...) {}
        try { fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/pdf-to-images ───────────────────────────────────────
    svr.Post("/api/tools/pdf-to-images", [dl_dir](const httplib::Request& req, httplib::Response& res) {
        if (g_ghostscript_path.empty()) {
            res.status = 500;
            res.set_content(json({{"error", "Ghostscript not installed. PDF tools require Ghostscript."}}).dump(), "application/json");
            return;
        }

        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string format = req.has_file("format") ? req.get_file_value("format").content : "png";
        string dpi    = req.has_file("dpi")    ? req.get_file_value("dpi").content    : "200";

        // Allowlist for format; parse dpi as integer (prevent GhostScript injection)
        static const set<string> ALLOWED_PDF2IMG_FMT = {"png","jpg","jpeg","tiff","tif"};
        if (ALLOWED_PDF2IMG_FMT.find(format) == ALLOWED_PDF2IMG_FMT.end()) {
            res.status = 400;
            res.set_content(json({{"error", "Unsupported image format. Use: png, jpg, tiff."}}).dump(), "application/json");
            return;
        }
        {
            int dpi_val = 200;
            try { dpi_val = std::stoi(dpi); } catch (...) {}
            if (dpi_val < 72)  dpi_val = 72;
            if (dpi_val > 600) dpi_val = 600;
            dpi = to_string(dpi_val);
        }

        discord_log_tool("PDF to Images", file.filename + " (" + format + ", " + dpi + " DPI)", req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string proc_dir = get_processing_dir();
        string out_pattern = proc_dir + "/" + jid + "_page_%03d." + format;

        string device = "png16m";

        if (format == "jpg" || format == "jpeg") device = "jpeg";
        else if (format == "tiff" || format == "tif") device = "tiff24nc";

        string cmd = escape_arg(g_ghostscript_path) +
            " -dNOPAUSE -dBATCH -sDEVICE=" + device +
            " -r" + dpi +
            " -sOutputFile=" + escape_arg(out_pattern) +
            " " + escape_arg(input_path);
        cout << "[Luma Tools] PDF to images: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        vector<string> pages;

        for (int i = 1; i <= 999; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%03d", i);
            string page_path = proc_dir + "/" + jid + "_page_" + buf + "." + format;

            if (fs::exists(page_path)) pages.push_back(page_path);
            else break;
        }

        if (pages.empty()) {
            res.status = 500;
            discord_log_error("PDF to Images", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "PDF to images conversion failed"}}).dump(), "application/json");
            try { fs::remove(input_path); } catch (...) {}
            return;
        }

        if (pages.size() == 1) {
            string out_name = fs::path(file.filename).stem().string() + "_page1." + format;
            send_file_response(res, pages[0], out_name);
        } else {
            json files_json = json::array();
            string base_name = fs::path(file.filename).stem().string();

            for (size_t i = 0; i < pages.size(); i++) {
                string page_name = base_name + "_page" + to_string(i + 1) + "." + format;
                string dest = dl_dir + "/" + page_name;
                try { fs::copy_file(pages[i], dest, fs::copy_options::overwrite_existing); } catch (...) {}
                files_json.push_back({{"name", page_name}, {"url", "/downloads/" + page_name}});
            }

            json resp = {{"pages", files_json}, {"count", (int)pages.size()}};
            res.set_content(resp.dump(), "application/json");
        }

        try { fs::remove(input_path); } catch (...) {}

        for (auto& p : pages) try { fs::remove(p); } catch (...) {}
    });

    // ── POST /api/tools/video-to-gif (async) ────────────────────────────────
    svr.Post("/api/tools/video-to-gif", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        int fps = 15; if (req.has_file("fps")) try { fps = std::stoi(req.get_file_value("fps").content); } catch (...) {}
        int width = 480; if (req.has_file("width")) try { width = std::stoi(req.get_file_value("width").content); } catch (...) {}
        if (fps < 1)      fps   = 1;    if (fps > 60)     fps   = 60;
        if (width < 64)   width = 64;   if (width > 3840) width = 3840;

        discord_log_tool("Video to GIF", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string proc_dir = get_processing_dir();
        string palette_path = proc_dir + "/" + jid + "_palette.png";
        string output_path = proc_dir + "/" + jid + "_out.gif";
        string orig_name = fs::path(file.filename).stem().string();
        update_job(jid, {{"status","processing"},{"progress",0},{"stage","Converting to GIF..."}});

        thread([jid, input_path, palette_path, output_path, orig_name, fps, width]() {
            int code;
            string vf = "fps=" + to_string(fps) + ",scale=" + to_string(width) + ":-1:flags=lanczos";
            string cmd1 = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -vf \"" + vf + ",palettegen=stats_mode=diff\" " + escape_arg(palette_path);
            exec_command(cmd1, code);
            string cmd2 = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -i " + escape_arg(palette_path) +
                " -lavfi \"" + vf + "[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=5\" -loop 0 " + escape_arg(output_path);
            exec_command(cmd2, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0)
                update_job(jid, {{"status","completed"},{"progress",100},{"filename", orig_name + ".gif"}}, output_path);
            else { discord_log_error("Video to GIF", "Failed for: " + mask_filename(orig_name)); update_job(jid, {{"status","error"},{"error","GIF conversion failed"}}); }
            try { fs::remove(input_path); fs::remove(palette_path); } catch (...) {}
        }).detach();
        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/gif-to-video (async) ────────────────────────────────
    svr.Post("/api/tools/gif-to-video", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        discord_log_tool("GIF to Video", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_out.mp4";
        string orig_name = fs::path(file.filename).stem().string();
        update_job(jid, {{"status","processing"},{"progress",0},{"stage","Converting to MP4..."}});
        thread([jid, input_path, output_path, orig_name]() {
            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -movflags faststart -pix_fmt yuv420p -vf \"scale=trunc(iw/2)*2:trunc(ih/2)*2\""
                " -c:v libx264 -crf 20 " + escape_arg(output_path);
            int code; exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0)
                update_job(jid, {{"status","completed"},{"progress",100},{"filename", orig_name + ".mp4"}}, output_path);
            else { discord_log_error("GIF to Video", "Failed for: " + mask_filename(orig_name)); update_job(jid, {{"status","error"},{"error","GIF to video conversion failed"}}); }
            try { fs::remove(input_path); } catch (...) {}
        }).detach();
        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/video-remove-audio (async) ──────────────────────────
    svr.Post("/api/tools/video-remove-audio", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        discord_log_tool("Remove Audio", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_out" + ext;
        string orig_name = fs::path(file.filename).stem().string();
        update_job(jid, {{"status","processing"},{"progress",0},{"stage","Removing audio..."}});
        thread([jid, input_path, output_path, orig_name, ext]() {
            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " -an -c:v copy " + escape_arg(output_path);
            int code; exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0)
                update_job(jid, {{"status","completed"},{"progress",100},{"filename", orig_name + "_muted" + ext}}, output_path);
            else { discord_log_error("Remove Audio", "Failed for: " + mask_filename(orig_name)); update_job(jid, {{"status","error"},{"error","Removing audio failed"}}); }
            try { fs::remove(input_path); } catch (...) {}
        }).detach();
        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/video-speed (async) ─────────────────────────────────
    svr.Post("/api/tools/video-speed", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        double speed = 2.0;

        if (req.has_file("speed")) try { speed = std::stod(req.get_file_value("speed").content); } catch (...) {}
        if (speed < 0.25) speed = 0.25; if (speed > 4.0) speed = 4.0;
        discord_log_tool("Video Speed", file.filename + " (" + to_string(speed) + "x)", req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_out.mp4";
        string orig_name = fs::path(file.filename).stem().string();
        update_job(jid, {{"status","processing"},{"progress",0},{"stage","Changing speed..."}});
        thread([jid, input_path, output_path, orig_name, speed]() {
            double pts = 1.0 / speed;
            // atempo supports 0.5–2.0; chain for beyond
            string atempo; double rem = speed;

            if (rem > 2.0) { while (rem > 2.0) { atempo += "atempo=2.0,"; rem /= 2.0; } atempo += "atempo=" + to_string(rem); }
            else if (rem < 0.5) { while (rem < 0.5) { atempo += "atempo=0.5,"; rem *= 2.0; } atempo += "atempo=" + to_string(rem); }
            else atempo = "atempo=" + to_string(rem);
            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -filter_complex \"[0:v]setpts=" + to_string(pts) + "*PTS[v];[0:a]" + atempo + "[a]\"" +
                " -map \"[v]\" -map \"[a]\" -c:v libx264 -crf 20 -preset fast -c:a aac " + escape_arg(output_path);
            int code; exec_command(cmd, code);

            if (!fs::exists(output_path) || fs::file_size(output_path) == 0) {
                cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                    " -vf \"setpts=" + to_string(pts) + "*PTS\" -an -c:v libx264 -crf 20 " + escape_arg(output_path);
                exec_command(cmd, code);
            }

            if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
                char s[16]; snprintf(s, sizeof(s), "%.1fx", speed);
                update_job(jid, {{"status","completed"},{"progress",100},{"filename", orig_name + "_" + string(s) + ".mp4"}}, output_path);
            } else { discord_log_error("Video Speed", "Failed for: " + mask_filename(orig_name)); update_job(jid, {{"status","error"},{"error","Speed change failed"}}); }
            try { fs::remove(input_path); } catch (...) {}
        }).detach();
        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/video-frame (sync) ──────────────────────────────────
    svr.Post("/api/tools/video-frame", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        string timestamp = req.has_file("timestamp") ? req.get_file_value("timestamp").content : "00:00:00";
        // Validate timestamp: only digits, colons, and periods allowed (prevent injection)
        if (timestamp.empty() || timestamp.size() > 20 ||
            !std::all_of(timestamp.begin(), timestamp.end(), [](char c) {
                return std::isdigit((unsigned char)c) || c == ':' || c == '.';
            })) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid timestamp format"}}).dump(), "application/json");
            return;
        }
        discord_log_tool("Frame Extract", file.filename + " @ " + timestamp, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_frame.png";
        string cmd = ffmpeg_cmd() + " -y -ss " + timestamp + " -i " + escape_arg(input_path) + " -frames:v 1 -q:v 2 " + escape_arg(output_path);
        int code; exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            send_file_response(res, output_path, fs::path(file.filename).stem().string() + "_frame.png");
        } else { res.status = 500; res.set_content(json({{"error","Frame extraction failed"}}).dump(), "application/json"); }
        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/video-stabilize (async) ─────────────────────────────
    svr.Post("/api/tools/video-stabilize", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        discord_log_tool("Video Stabilize", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_out.mp4";
        string orig_name = fs::path(file.filename).stem().string();
        update_job(jid, {{"status","processing"},{"progress",0},{"stage","Stabilizing video..."}});
        thread([jid, input_path, output_path, orig_name]() {
            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -vf deshake -c:v libx264 -crf 20 -preset fast -c:a aac " + escape_arg(output_path);
            int code; exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0)
                update_job(jid, {{"status","completed"},{"progress",100},{"filename", orig_name + "_stabilized.mp4"}}, output_path);
            else { discord_log_error("Video Stabilize", "Failed for: " + mask_filename(orig_name)); update_job(jid, {{"status","error"},{"error","Stabilization failed"}}); }
            try { fs::remove(input_path); } catch (...) {}
        }).detach();
        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/audio-normalize (async) ─────────────────────────────
    svr.Post("/api/tools/audio-normalize", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        discord_log_tool("Audio Normalize", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_out" + ext;
        string orig_name = fs::path(file.filename).stem().string();
        update_job(jid, {{"status","processing"},{"progress",0},{"stage","Normalizing audio..."}});
        thread([jid, input_path, output_path, orig_name, ext]() {
            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -af loudnorm=I=-16:TP=-1.5:LRA=11 " + escape_arg(output_path);
            int code; exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0)
                update_job(jid, {{"status","completed"},{"progress",100},{"filename", orig_name + "_normalized" + ext}}, output_path);
            else { discord_log_error("Audio Normalize", "Failed for: " + mask_filename(orig_name)); update_job(jid, {{"status","error"},{"error","Normalization failed"}}); }
            try { fs::remove(input_path); } catch (...) {}
        }).detach();
        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/subtitle-extract ────────────────────────────────────
    svr.Post("/api/tools/subtitle-extract", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        string format = req.has_file("format") ? req.get_file_value("format").content : "srt";
        // Allowlist: prevent path traversal
        static const set<string> ALLOWED_SUB_FMT = {"srt","vtt","ass","ssa"};
        if (ALLOWED_SUB_FMT.find(format) == ALLOWED_SUB_FMT.end()) {
            res.status = 400;
            res.set_content(json({{"error", "Unsupported subtitle format: " + format}}).dump(), "application/json");
            return;
        }
        discord_log_tool("Subtitle Extract", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_subs." + format;
        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " -map 0:s:0 " + escape_arg(output_path);
        int code; exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            send_file_response(res, output_path, fs::path(file.filename).stem().string() + "." + format);
        } else { res.status = 500; res.set_content(json({{"error","No subtitle track found in this video"}}).dump(), "application/json"); }
        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/metadata-strip ──────────────────────────────────────
    svr.Post("/api/tools/metadata-strip", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");

        {
            string ec = fs::path(file.filename).extension().string();
            std::transform(ec.begin(), ec.end(), ec.begin(), ::tolower);
            if (ec == ".svg") {
                res.status = 400;
                res.set_content(json({{"error", "SVG metadata stripping is not supported. SVG is an XML format — open it in a text editor to remove metadata manually."}}).dump(), "application/json");
                return;
            }
        }

        discord_log_tool("Metadata Strip", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_clean" + ext;
        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " -map_metadata -1 -c copy " + escape_arg(output_path);
        int code; exec_command(cmd, code);

        if (!fs::exists(output_path) || fs::file_size(output_path) == 0) {
            cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " -map_metadata -1 " + escape_arg(output_path);
            exec_command(cmd, code);
        }

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            send_file_response(res, output_path, fs::path(file.filename).stem().string() + "_clean" + ext);
        } else { res.status = 500; res.set_content(json({{"error","Metadata removal failed"}}).dump(), "application/json"); }
        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/favicon-generate ────────────────────────────────────
    svr.Post("/api/tools/favicon-generate", [dl_dir](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        discord_log_tool("Favicon Generator", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string proc_dir = get_processing_dir();
        string base_name = fs::path(file.filename).stem().string();
        vector<int> sizes = {16, 32, 48, 180, 192, 512};
        json files_json = json::array();

        for (int sz : sizes) {
            string out_name = base_name + "_" + to_string(sz) + "x" + to_string(sz) + ".png";
            string out_path = proc_dir + "/" + jid + "_" + to_string(sz) + ".png";
            string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                " -vf \"scale=" + to_string(sz) + ":" + to_string(sz) + ":flags=lanczos\" " + escape_arg(out_path);
            int code; exec_command(cmd, code);

            if (fs::exists(out_path) && fs::file_size(out_path) > 0) {
                string dest = dl_dir + "/" + out_name;
                try { fs::copy_file(out_path, dest, fs::copy_options::overwrite_existing); } catch (...) {}
                files_json.push_back({{"name", out_name}, {"url", "/downloads/" + out_name}, {"size", to_string(sz) + "x" + to_string(sz)}});
            }

            try { fs::remove(out_path); } catch (...) {}
        }

        string ico_name = base_name + "_favicon.ico";
        string ico_path = proc_dir + "/" + jid + ".ico";
        string ico_cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " -vf scale=32:32:flags=lanczos " + escape_arg(ico_path);
        int ico_code; exec_command(ico_cmd, ico_code);

        if (fs::exists(ico_path) && fs::file_size(ico_path) > 0) {
            string dest = dl_dir + "/" + ico_name;
            try { fs::copy_file(ico_path, dest, fs::copy_options::overwrite_existing); } catch (...) {}
            files_json.push_back({{"name", ico_name}, {"url", "/downloads/" + ico_name}, {"size", "ICO"}});
        }

        try { fs::remove(ico_path); fs::remove(input_path); } catch (...) {}

        if (files_json.empty()) { res.status = 500; res.set_content(json({{"error","Favicon generation failed"}}).dump(), "application/json"); }
        else res.set_content(json({{"pages", files_json}, {"count", (int)files_json.size()}}).dump(), "application/json");
    });

    // ── POST /api/tools/image-crop ──────────────────────────────────────────
    svr.Post("/api/tools/image-crop", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");

        {
            string ec = fs::path(file.filename).extension().string();
            std::transform(ec.begin(), ec.end(), ec.begin(), ::tolower);
            if (ec == ".svg") {
                res.status = 400;
                res.set_content(json({{"error", "SVG is a vector format and cannot be cropped this way. Use Image Convert to rasterise it to PNG first."}}).dump(), "application/json");
                return;
            }
        }

        // Parse crop params as integers to prevent filter injection
        int x_v = 0, y_v = 0, w_v = 0, h_v = 0;
        if (req.has_file("x")) try { x_v = std::stoi(req.get_file_value("x").content); } catch (...) {}
        if (req.has_file("y")) try { y_v = std::stoi(req.get_file_value("y").content); } catch (...) {}
        if (req.has_file("w")) try { w_v = std::stoi(req.get_file_value("w").content); } catch (...) {}
        if (req.has_file("h")) try { h_v = std::stoi(req.get_file_value("h").content); } catch (...) {}

        if (w_v <= 0 || h_v <= 0) {
            res.status = 400;
            res.set_content(json({{"error", "Crop dimensions required (width and height must be positive integers)"}}).dump(), "application/json");
            return;
        }
        if (x_v < 0) x_v = 0;
        if (y_v < 0) y_v = 0;

        discord_log_tool("Image Crop", file.filename, req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_cropped" + ext;

        // ffmpeg crop filter: crop=w:h:x:y  — use integer strings (safe from injection)
        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path)
            + " -vf crop=" + to_string(w_v) + ":" + to_string(h_v) + ":" + to_string(x_v) + ":" + to_string(y_v)
            + " " + escape_arg(output_path);
        cout << "[Luma Tools] Image crop: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string out_name = fs::path(file.filename).stem().string() + "_cropped" + ext;
            send_file_response(res, output_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("Image Crop", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "Image crop failed"}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/image-bg-remove ─────────────────────────────────────
    svr.Post("/api/tools/image-bg-remove", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");

        {
            string ec = fs::path(file.filename).extension().string();
            std::transform(ec.begin(), ec.end(), ec.begin(), ::tolower);
            if (ec == ".svg") {
                res.status = 400;
                res.set_content(json({{"error", "SVG files are not supported for background removal. Use Image Convert to rasterise it to PNG first."}}).dump(), "application/json");
                return;
            }
        }

        string method = "auto";

        if (req.has_file("method")) method = req.get_file_value("method").content;
        // Clamp unknown methods to "auto" to avoid ambiguous silent failure
        if (method != "auto" && method != "white" && method != "black") method = "auto";

        discord_log_tool("Background Remover", file.filename + " (" + method + ")", req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_nobg.png";

        int code;
        string cmd;

        if (method == "auto") {
            // Try rembg (Python AI-based background removal)
            cmd = "rembg i " + escape_arg(input_path) + " " + escape_arg(output_path);
            cout << "[Luma Tools] BG remove (rembg): " << cmd << endl;
            exec_command(cmd, code);

            // Fallback: use ffmpeg colorkey on white if rembg not available
            if (!fs::exists(output_path) || fs::file_size(output_path) == 0) {
                cout << "[Luma Tools] rembg not available, falling back to colorkey white" << endl;
                cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path)
                    + " -vf \"colorkey=white:0.3:0.15,format=rgba\" "
                    + escape_arg(output_path);
                exec_command(cmd, code);
            }
        } else if (method == "white") {
            cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path)
                + " -vf \"colorkey=white:0.3:0.15,format=rgba\" "
                + escape_arg(output_path);
            cout << "[Luma Tools] BG remove (white): " << cmd << endl;
            exec_command(cmd, code);
        } else if (method == "black") {
            cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path)
                + " -vf \"colorkey=black:0.3:0.15,format=rgba\" "
                + escape_arg(output_path);
            cout << "[Luma Tools] BG remove (black): " << cmd << endl;
            exec_command(cmd, code);
        }

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string out_name = fs::path(file.filename).stem().string() + "_nobg.png";
            send_file_response(res, output_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("Background Remover", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "Background removal failed. If using Auto mode, ensure rembg is installed."}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/redact-video ─────────────────────────────────────
    svr.Post("/api/tools/redact-video", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string regions_json = req.has_file("regions") ? req.get_file_value("regions").content : "";

        if (regions_json.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "No redaction regions provided"}}).dump(), "application/json");
            return;
        }

        json regions;
        try { regions = json::parse(regions_json); } catch (...) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid regions JSON"}}).dump(), "application/json");
            return;
        }

        discord_log_tool("Redact Video", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_redacted" + ext;
        // Build FFmpeg filter string
        vector<string> filters;

        for (const auto& reg : regions) {
            int x = reg.value("x", 0), y = reg.value("y", 0), w = reg.value("w", 0), h = reg.value("h", 0);
            string type = reg.value("type", "box");

            if (w < 8 || h < 8) continue;
            if (type == "box") {
                filters.push_back("drawbox=x=" + to_string(x) + ":y=" + to_string(y) + ":w=" + to_string(w) + ":h=" + to_string(h) + ":color=black@1:t=fill");
            } else if (type == "blur") {
                filters.push_back("boxblur=enable='between(t,0,1)',luma_radius=20:luma_power=1:chroma_radius=10:chroma_power=1, crop=x=" + to_string(x) + ":y=" + to_string(y) + ":w=" + to_string(w) + ":h=" + to_string(h));
            }
        }

        string vf = join(filters, ",");
        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path);

        if (!vf.empty()) cmd += " -vf \"" + vf + "\"";
        cmd += " " + escape_arg(output_path);
        cout << "[Luma Tools] Redact video: " << cmd << endl;
        int code; exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string out_name = fs::path(file.filename).stem().string() + "_redacted" + ext;
            send_file_response(res, output_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("Redact Video", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "Video redaction failed"}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/images-to-pdf ───────────────────────────────────────
    svr.Post("/api/tools/images-to-pdf", [](const httplib::Request& req, httplib::Response& res) {
        int count_val = 0;

        if (req.has_file("count")) try { count_val = std::stoi(req.get_file_value("count").content); } catch (...) {}
        if (count_val < 1) { res.status = 400; res.set_content(json({{"error","At least 1 image required"}}).dump(),"application/json"); return; }
        if (count_val > 50) { res.status = 400; res.set_content(json({{"error","Too many images. Maximum is 50 per conversion."}}).dump(),"application/json"); return; }
        discord_log_tool("Images to PDF", to_string(count_val) + " images", req.remote_addr);
        string jid = generate_job_id();
        string proc_dir = get_processing_dir();
        // Convert all images to JPEG and probe dimensions
        struct ImgInfo { string path; int w; int h; };
        vector<ImgInfo> imgs;
        string ffprobe_path = g_ffmpeg_exe;
        auto fp = ffprobe_path.rfind("ffmpeg");

        if (fp != string::npos) ffprobe_path.replace(fp, 6, "ffprobe");
        for (int i = 0; i < count_val; i++) {
            string key = "file" + to_string(i);

            if (!req.has_file(key)) continue;
            auto f = req.get_file_value(key);
            string raw_path = proc_dir + "/" + jid + "_in" + to_string(i) + fs::path(f.filename).extension().string();
            { ofstream out(raw_path, std::ios::binary); out.write(f.content.data(), f.content.size()); }
            string jpg_path = proc_dir + "/" + jid + "_img" + to_string(i) + ".jpg";
            int code; exec_command(ffmpeg_cmd() + " -y -i " + escape_arg(raw_path) + " -q:v 2 " + escape_arg(jpg_path), code);

            if (fs::exists(jpg_path) && fs::file_size(jpg_path) > 0) {
                string dims = exec_command(escape_arg(ffprobe_path) + " -v quiet -show_entries stream=width,height -of csv=p=0 " + escape_arg(jpg_path), code);
                int w = 612, h = 792;
                auto comma = dims.find(',');

                if (comma != string::npos) { try { w = std::stoi(dims.substr(0, comma)); h = std::stoi(dims.substr(comma + 1)); } catch (...) {} }
                imgs.push_back({jpg_path, w, h});
            }

            try { fs::remove(raw_path); } catch (...) {}
        }

        if (imgs.empty()) { res.status = 500; res.set_content(json({{"error","No valid images"}}).dump(), "application/json"); return; }
        // Build minimal PDF with embedded JPEG images
        string output_path = proc_dir + "/" + jid + "_output.pdf";
        {
            ofstream pdf(output_path, std::ios::binary);
            int np = (int)imgs.size();
            vector<long> off(2 + np * 3 + 1, 0);
            pdf << "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";
            off[1] = (long)pdf.tellp(); pdf << "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
            off[2] = (long)pdf.tellp(); pdf << "2 0 obj\n<< /Type /Pages /Kids [";

            for (int i = 0; i < np; i++) { if (i) pdf << " "; pdf << (3+i*3) << " 0 R"; }
            pdf << "] /Count " << np << " >>\nendobj\n";

            for (int i = 0; i < np; i++) {
                ifstream img(imgs[i].path, std::ios::binary);
                string jdata((std::istreambuf_iterator<char>(img)), std::istreambuf_iterator<char>());
                img.close();
                int po = 3+i*3, co = 4+i*3, io = 5+i*3;
                string ct = "q " + to_string(imgs[i].w) + " 0 0 " + to_string(imgs[i].h) + " 0 0 cm /Img Do Q\n";
                off[po] = (long)pdf.tellp();
                pdf << po << " 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << imgs[i].w << " " << imgs[i].h
                    << "] /Contents " << co << " 0 R /Resources << /XObject << /Img " << io << " 0 R >> >> >>\nendobj\n";
                off[co] = (long)pdf.tellp();
                pdf << co << " 0 obj\n<< /Length " << ct.size() << " >>\nstream\n" << ct << "endstream\nendobj\n";
                off[io] = (long)pdf.tellp();
                pdf << io << " 0 obj\n<< /Type /XObject /Subtype /Image /Width " << imgs[i].w
                    << " /Height " << imgs[i].h << " /BitsPerComponent 8 /ColorSpace /DeviceRGB"
                    << " /Filter /DCTDecode /Length " << jdata.size() << " >>\nstream\n";
                pdf.write(jdata.data(), jdata.size());
                pdf << "\nendstream\nendobj\n";
            }

            int to = 2 + np * 3; long xo = (long)pdf.tellp();
            pdf << "xref\n0 " << (to+1) << "\n0000000000 65535 f \n";

            for (int i = 1; i <= to; i++) { char b[21]; snprintf(b, sizeof(b), "%010ld 00000 n \n", off[i]); pdf << b; }
            pdf << "trailer\n<< /Size " << (to+1) << " /Root 1 0 R >>\nstartxref\n" << xo << "\n%%EOF\n";
        }

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) send_file_response(res, output_path, "images.pdf");
        else { res.status = 500; res.set_content(json({{"error","PDF generation failed"}}).dump(), "application/json"); }

        for (auto& im : imgs) try { fs::remove(im.path); } catch (...) {}
        try { fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/hash-generate ───────────────────────────────────────
    svr.Post("/api/tools/hash-generate", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) { res.status = 400; res.set_content(json({{"error","No file uploaded"}}).dump(),"application/json"); return; }
        auto file = req.get_file_value("file");
        discord_log_tool("Hash Generator", file.filename, req.remote_addr);
        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        json hashes;

        for (const auto& algo : {"MD5", "SHA1", "SHA256"}) {
            int code; string output = exec_command("certutil -hashfile " + escape_arg(input_path) + " " + algo, code);
            istringstream iss(output); string line;
            getline(iss, line); getline(iss, line);
            line.erase(std::remove(line.begin(), line.end(), ' '), line.end());
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
            line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

            if (code == 0 && !line.empty() && line.find("CertUtil") == string::npos) hashes[algo] = line;
        }

        try { fs::remove(input_path); } catch (...) {}
        res.set_content(json({{"filename", file.filename}, {"size", (long long)file.content.size()}, {"hashes", hashes}}).dump(), "application/json");
    });

    // ── GET /api/tools/progress/:id  (SSE — stream job status updates) ──────
    svr.Get(R"(/api/tools/progress/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        string jid = req.matches[1];
        res.set_header("Content-Type",  "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");

        // Poll the job up to 300 seconds (300 × 1s ticks)
        res.set_content_provider("text/event-stream",
            [jid](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                for (int tick = 0; tick < 300; ++tick) {
                    json job = get_job(jid);
                    if (job.is_null()) {
                        string msg = "data: {\"status\":\"not_found\"}\n\n";
                        sink.write(msg.c_str(), msg.size());
                        return false; // close stream
                    }
                    string payload = "data: " + job.dump() + "\n\n";
                    if (!sink.write(payload.c_str(), payload.size())) return false;

                    string status = job.value("status", "");
                    if (status == "completed" || status == "error") return false;

                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
                string done = "data: {\"status\":\"timeout\"}\n\n";
                sink.write(done.c_str(), done.size());
                return false;
            });
    });

    // ── POST /api/tools/audio-trim (async) ──────────────────────────────────
    svr.Post("/api/tools/audio-trim", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }
        auto file  = req.get_file_value("file");
        string start = req.has_file("start") ? req.get_file_value("start").content : "00:00:00";
        string end   = req.has_file("end")   ? req.get_file_value("end").content   : "";
        string mode  = req.has_file("mode")  ? req.get_file_value("mode").content  : "fast";

        if (end.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "End time is required"}}).dump(), "application/json");
            return;
        }

        // Validate timestamps: only digits, colons, and periods allowed (prevent injection)
        {
            auto is_valid_ts = [](const string& ts) {
                return !ts.empty() && ts.size() <= 20 &&
                       std::all_of(ts.begin(), ts.end(), [](char c) {
                           return std::isdigit((unsigned char)c) || c == ':' || c == '.';
                       });
            };
            if (!is_valid_ts(start) || !is_valid_ts(end)) {
                res.status = 400;
                res.set_content(json({{"error", "Invalid timestamp format"}}).dump(), "application/json");
                return;
            }
        }
        discord_log_tool("Audio Trim (" + mode + ")", file.filename + " [" + start + " -> " + end + "]", req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string out_ext = (mode == "precise") ? ".mp3" : ext;
        string output_path = get_processing_dir() + "/" + jid + "_out" + out_ext;
        string orig_name = fs::path(file.filename).stem().string();
        update_job(jid, {{"status", "processing"}, {"progress", 0}, {"stage", "Trimming audio..."}});

        thread([jid, input_path, output_path, start, end, out_ext, orig_name, mode]() {
            string cmd;
            if (mode == "precise") {
                cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                    " -ss " + start + " -to " + end +
                    " -c:a libmp3lame -q:a 2 " + escape_arg(output_path);
            } else {
                cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
                    " -ss " + start + " -to " + end + " -c copy " + escape_arg(output_path);
            }
            cout << "[Luma Tools] Audio trim (" << mode << "): " << cmd << endl;
            int code; exec_command(cmd, code);

            if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
                update_job(jid, {{"status", "completed"}, {"progress", 100},
                    {"filename", orig_name + "_trimmed" + out_ext}}, output_path);
            } else {
                discord_log_error("Audio Trim", "Failed for: " + mask_filename(orig_name));
                update_job(jid, {{"status", "error"}, {"error", "Audio trimming failed"}});
            }
            try { fs::remove(input_path); } catch (...) {}
        }).detach();

        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/pdf-split ────────────────────────────────────────────
    svr.Post("/api/tools/pdf-split", [](const httplib::Request& req, httplib::Response& res) {
        if (g_ghostscript_path.empty()) {
            res.status = 500;
            res.set_content(json({{"error", "Ghostscript not installed. PDF tools require Ghostscript."}}).dump(), "application/json");
            return;
        }
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }
        auto file = req.get_file_value("file");
        string from_s = req.has_file("from") ? req.get_file_value("from").content : "1";
        string to_s   = req.has_file("to")   ? req.get_file_value("to").content   : "";

        int from_page = 1, to_page = 0;
        try { from_page = std::stoi(from_s); } catch (...) {}
        if (!to_s.empty()) try { to_page = std::stoi(to_s); } catch (...) {}
        if (from_page < 1) from_page = 1;
        if (to_page < from_page && to_page > 0) to_page = from_page;

        discord_log_tool("PDF Split", file.filename + " [p" + from_s + "-" + (to_s.empty() ? "end" : to_s) + "]", req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string output_path = get_processing_dir() + "/" + jid + "_split.pdf";
        string orig_name = fs::path(file.filename).stem().string();

        string page_args = " -dFirstPage=" + to_string(from_page);
        if (to_page > 0) page_args += " -dLastPage=" + to_string(to_page);

        string cmd = escape_arg(g_ghostscript_path) +
            " -dNOPAUSE -dBATCH -sDEVICE=pdfwrite" + page_args +
            " -sOutputFile=" + escape_arg(output_path) +
            " " + escape_arg(input_path);
        cout << "[Luma Tools] PDF split: " << cmd << endl;
        int code; exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string suffix = (to_page > 0 && to_page != from_page)
                ? "_p" + to_string(from_page) + "-" + to_string(to_page)
                : "_p" + to_string(from_page);
            send_file_response(res, output_path, orig_name + suffix + ".pdf");
        } else {
            res.status = 500;
            discord_log_error("PDF Split", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "PDF split failed — check page range"}}).dump(), "application/json");
        }
        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/image-watermark ─────────────────────────────────────
    svr.Post("/api/tools/image-watermark", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }
        auto file = req.get_file_value("file");
        string wm_text    = req.has_file("text")     ? req.get_file_value("text").content     : "";
        string wm_font_sz = req.has_file("fontsize")  ? req.get_file_value("fontsize").content  : "36";
        string wm_color   = req.has_file("color")    ? req.get_file_value("color").content    : "white";
        string wm_opacity = req.has_file("opacity")  ? req.get_file_value("opacity").content  : "0.6";
        string wm_pos     = req.has_file("position") ? req.get_file_value("position").content : "bottom-right";

        // Parse/clamp fontsize to prevent FFmpeg filter injection
        {
            int fs_val = 36;
            try { fs_val = std::stoi(wm_font_sz); } catch (...) {}
            if (fs_val < 8)   fs_val = 8;
            if (fs_val > 200) fs_val = 200;
            wm_font_sz = to_string(fs_val);
        }
        // Validate color: allow only alphanumeric chars and '#' (CSS names + hex codes)
        {
            bool color_ok = !wm_color.empty() && std::all_of(wm_color.begin(), wm_color.end(),
                [](char c){ return std::isalnum((unsigned char)c) || c == '#'; });
            if (!color_ok) {
                res.status = 400;
                res.set_content(json({{"error", "Invalid color value. Use a color name (white, black...) or hex (#RRGGBB)."}}).dump(), "application/json");
                return;
            }
        }

        if (wm_text.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "Watermark text is required"}}).dump(), "application/json");
            return;
        }
        if (wm_text.size() > 200) {
            res.status = 400;
            res.set_content(json({{"error", "Watermark text too long (max 200 characters)"}}).dump(), "application/json");
            return;
        }

        // Sanitize text against FFmpeg filter injection
        string safe_text;
        for (char c : wm_text) {
            if (c == '\'' || c == '\\' || c == ':' || c == '[' || c == ']') safe_text += '\\';
            safe_text += c;
        }

        // Build x/y expression from position
        string x_expr, y_expr;
        string pad = "20";
        if (wm_pos == "top-left")      { x_expr = pad;                      y_expr = pad; }
        else if (wm_pos == "top-center")    { x_expr = "(w-text_w)/2";           y_expr = pad; }
        else if (wm_pos == "top-right")     { x_expr = "w-text_w-" + pad;        y_expr = pad; }
        else if (wm_pos == "center")        { x_expr = "(w-text_w)/2";           y_expr = "(h-text_h)/2"; }
        else if (wm_pos == "bottom-left")   { x_expr = pad;                      y_expr = "h-text_h-" + pad; }
        else if (wm_pos == "bottom-center") { x_expr = "(w-text_w)/2";           y_expr = "h-text_h-" + pad; }
        else /* bottom-right */             { x_expr = "w-text_w-" + pad;        y_expr = "h-text_h-" + pad; }

        // Parse opacity to alpha (0-255)
        double op = 0.6;
        try { op = std::stod(wm_opacity); } catch (...) {}
        if (op < 0) op = 0; if (op > 1) op = 1;
        int alpha = (int)(op * 255);
        char alpha_hex[5]; snprintf(alpha_hex, sizeof(alpha_hex), "%02X", alpha);
        // Convert color name to RRGGBBAA for drawtext (use clamped op value, not raw user input)
        string color_str = wm_color + "@" + to_string(op);

        discord_log_tool("Image Watermark", file.filename, req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        if (ext == ".jpg" || ext == ".jpeg") ext = ".jpg";
        else if (ext != ".png" && ext != ".webp" && ext != ".tiff") ext = ".png";
        string output_path = get_processing_dir() + "/" + jid + "_wm" + ext;
        string orig_name = fs::path(file.filename).stem().string();

        string drawtext = "drawtext=text='" + safe_text + "'"
            ":fontsize=" + wm_font_sz +
            ":fontcolor=" + color_str +
            ":x=" + x_expr +
            ":y=" + y_expr;

        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) +
            " -vf \"" + drawtext + "\" " + escape_arg(output_path);
        cout << "[Luma Tools] Image watermark: " << cmd << endl;
        int code; exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            send_file_response(res, output_path, orig_name + "_watermarked" + ext);
        } else {
            res.status = 500;
            discord_log_error("Image Watermark", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "Watermark failed. Check that FFmpeg has freetype support."}}).dump(), "application/json");
        }
        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
    });

    // ── POST /api/tools/markdown-to-pdf ─────────────────────────────────────
    // Accepts a .md/.txt file, pre-processes Obsidian syntax, runs pandoc.
    svr.Post("/api/tools/markdown-to-pdf", [](const httplib::Request& req, httplib::Response& res) {
        if (g_pandoc_path.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "Pandoc is not installed on this server."}}).dump(), "application/json");
            return;
        }
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }
        auto file = req.get_file_value("file");
        discord_log_tool("Markdown to PDF", file.filename, req.remote_addr);

        string jid = generate_job_id();
        string proc = get_processing_dir();
        string md_path  = proc + "/" + jid + "_in.md";
        string pdf_path = proc + "/" + jid + "_out.pdf";

        // ── Pre-process Obsidian-flavoured Markdown ──────────────────────────
        string src = file.content;

        // 1. Strip YAML frontmatter (--- ... ---)
        if (src.substr(0, 3) == "---") {
            auto end = src.find("\n---", 3);
            if (end != string::npos) src = src.substr(end + 4);
        }
        // 2. Wikilinks [[Page]] → **Page**, [[Page|Alias]] → **Alias**
        {
            string out; size_t i = 0;
            while (i < src.size()) {
                if (i + 1 < src.size() && src[i] == '[' && src[i+1] == '[') {
                    auto close = src.find("]]", i + 2);
                    if (close != string::npos) {
                        string inner = src.substr(i + 2, close - i - 2);
                        auto pipe = inner.find('|');
                        string label = (pipe != string::npos) ? inner.substr(pipe + 1) : inner;
                        out += "**" + label + "**";
                        i = close + 2;
                        continue;
                    }
                }
                out += src[i++];
            }
            src = out;
        }
        // 3. Obsidian callouts  > [!note] Title  →  **[NOTE] Title**\n>\n
        {
            std::istringstream ss(src); string line, out;
            while (std::getline(ss, line)) {
                if (line.size() > 4 && line[0] == '>' && line[1] == ' ' && line[2] == '[' && line[3] == '!') {
                    auto close = line.find(']', 3);
                    if (close != string::npos) {
                        string type = line.substr(4, close - 4);
                        string rest = line.substr(close + 1);
                        // trim rest
                        size_t s = rest.find_first_not_of(" \t");
                        if (s != string::npos) rest = rest.substr(s);
                        out += "> **[" + type + "]** " + rest + "\n";
                        continue;
                    }
                }
                out += line + "\n";
            }
            src = out;
        }
        // 4. Inline tags #tag → *(#tag)*
        {
            string out; size_t i = 0;
            while (i < src.size()) {
                if (src[i] == '#' && (i == 0 || src[i-1] == ' ' || src[i-1] == '\n')) {
                    // heading or tag?
                    size_t j = i + 1;
                    while (j < src.size() && src[j] != ' ' && src[j] != '\n') j++;
                    string word = src.substr(i + 1, j - i - 1);
                    if (!word.empty() && std::isalpha((unsigned char)word[0])) {
                        out += "*(" + src.substr(i, j - i) + ")*";
                        i = j; continue;
                    }
                }
                out += src[i++];
            }
            src = out;
        }

        { ofstream f(md_path); f << src; }

        string cmd = escape_arg(g_pandoc_path) +
            " " + escape_arg(md_path) +
            " -o " + escape_arg(pdf_path) +
            " --pdf-engine=xelatex"
            " -V geometry:margin=2.5cm"
            " -V fontsize=11pt"
            " 2>&1";
        // Fallback to wkhtmltopdf if xelatex not available
        int code = 0;
        exec_command(cmd, code);
        if (code != 0 || !fs::exists(pdf_path) || fs::file_size(pdf_path) == 0) {
            // Try without explicit engine (pandoc picks available one)
            string cmd2 = escape_arg(g_pandoc_path) +
                " " + escape_arg(md_path) +
                " -o " + escape_arg(pdf_path) +
                " -V geometry:margin=2.5cm"
                " 2>&1";
            exec_command(cmd2, code);
        }

        if (fs::exists(pdf_path) && fs::file_size(pdf_path) > 0) {
            string out_name = fs::path(file.filename).stem().string() + ".pdf";
            send_file_response(res, pdf_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("Markdown to PDF", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "PDF generation failed. Ensure a LaTeX engine is installed (e.g. MiKTeX / TeX Live)."}}).dump(), "application/json");
        }
        try { fs::remove(md_path); fs::remove(pdf_path); } catch (...) {}
    });

    // ── POST /api/tools/csv-json ─────────────────────────────────────────────
    // direction = "csv-to-json" | "json-to-csv"
    svr.Post("/api/tools/csv-json", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }
        auto file = req.get_file_value("file");
        string direction = req.has_file("direction") ? req.get_file_value("direction").content : "csv-to-json";
        discord_log_tool("CSV/JSON Convert", file.filename + " (" + direction + ")", req.remote_addr);

        string jid = generate_job_id();
        string proc = get_processing_dir();

        if (direction == "json-to-csv") {
            // ── JSON → CSV ───────────────────────────────────────────────────
            json data;
            try { data = json::parse(file.content); } catch (...) {
                res.status = 400;
                res.set_content(json({{"error", "Invalid JSON"}}).dump(), "application/json");
                return;
            }
            if (!data.is_array() || data.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "JSON must be a non-empty array of objects"}}).dump(), "application/json");
                return;
            }
            // Collect headers from first object
            vector<string> headers;
            for (auto& [k, v] : data[0].items()) headers.push_back(k);

            string out_path = proc + "/" + jid + "_out.csv";
            ofstream f(out_path);
            // Header row
            for (size_t i = 0; i < headers.size(); i++) {
                if (i) f << ",";
                f << "\"" << headers[i] << "\"";
            }
            f << "\n";
            // Data rows
            for (auto& row : data) {
                for (size_t i = 0; i < headers.size(); i++) {
                    if (i) f << ",";
                    string cell;
                    if (row.contains(headers[i])) {
                        auto& v = row[headers[i]];
                        if (v.is_string())      cell = v.get<string>();
                        else if (!v.is_null())  cell = v.dump();
                    }
                    // Escape quotes and wrap in quotes if contains comma/quote/newline
                    bool needs_quote = cell.find_first_of(",\"\n\r") != string::npos;
                    string escaped;
                    for (char c : cell) { if (c == '"') escaped += '"'; escaped += c; }
                    if (needs_quote) f << "\"" << escaped << "\"";
                    else             f << escaped;
                }
                f << "\n";
            }
            f.close();
            string out_name = fs::path(file.filename).stem().string() + ".csv";
            send_file_response(res, out_path, out_name);
            try { fs::remove(out_path); } catch (...) {}

        } else {
            // ── CSV → JSON ───────────────────────────────────────────────────
            std::istringstream ss(file.content);
            string line;
            vector<string> headers;
            json arr = json::array();

            // Simple CSV parser (handles quoted fields)
            auto parse_csv_line = [](const string& line) -> vector<string> {
                vector<string> fields;
                string field;
                bool in_quotes = false;
                for (size_t i = 0; i < line.size(); i++) {
                    char c = line[i];
                    if (in_quotes) {
                        if (c == '"') {
                            if (i + 1 < line.size() && line[i+1] == '"') { field += '"'; i++; }
                            else in_quotes = false;
                        } else field += c;
                    } else {
                        if (c == '"') in_quotes = true;
                        else if (c == ',') { fields.push_back(field); field.clear(); }
                        else if (c == '\r') {}
                        else field += c;
                    }
                }
                fields.push_back(field);
                return fields;
            };

            bool first = true;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                auto fields = parse_csv_line(line);
                if (first) { headers = fields; first = false; continue; }
                json obj;
                for (size_t i = 0; i < headers.size(); i++)
                    obj[headers[i]] = (i < fields.size()) ? fields[i] : "";
                arr.push_back(obj);
            }

            string out_path = proc + "/" + jid + "_out.json";
            { ofstream f(out_path); f << arr.dump(2); }
            string out_name = fs::path(file.filename).stem().string() + ".json";
            send_file_response(res, out_path, out_name);
            try { fs::remove(out_path); } catch (...) {}
        }
    });

    // ── POST /api/tools/ai-study-notes (async) ───────────────────────────────
    // Accepts text directly, OR a file (PDF, DOCX, TXT). Extracts text, sends to Groq.
    svr.Post("/api/tools/ai-study-notes", [](const httplib::Request& req, httplib::Response& res) {
        if (g_groq_key.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "AI features are not configured on this server."}}).dump(), "application/json");
            return;
        }
        
        bool has_text = req.has_file("text") && !req.get_file_value("text").content.empty();
        // Multi-file support: filecount + file0..fileN, or single "file" for backward compat
        int filecount = 0;
        if (req.has_file("filecount")) {
            try { filecount = std::stoi(req.get_file_value("filecount").content); } catch (...) {}
        }
        bool has_file = (filecount > 0) ||
                        (req.has_file("file") && !req.get_file_value("file").content.empty());
        
        if (!has_text && !has_file) {
            res.status = 400;
            res.set_content(json({{"error", "No content provided. Upload a file or paste text."}}).dump(), "application/json");
            return;
        }
        
        string format = req.has_file("format") ? req.get_file_value("format").content : "markdown";
        string math_fmt = req.has_file("math") ? req.get_file_value("math").content : "dollar";
        string jid = generate_job_id();
        string proc = get_processing_dir();
        
        // Variables for the thread
        string input_text;
        string filename;
        string input_path;
        string file_ext;
        
        string ip = req.remote_addr;
        string input_desc;
        if (has_text) {
            // Pasted text mode
            input_text = req.get_file_value("text").content;
            filename = "pasted_text";
            input_desc = "Pasted Text (" + format + ")";
        } else if (filecount > 1) {
            // Multi-file mode: extract text from all files before the async thread
            vector<string> parts;
            for (int fi = 0; fi < filecount && fi < 10; fi++) {
                string key = "file" + to_string(fi);
                if (!req.has_file(key)) continue;
                auto f = req.get_file_value(key);
                if (f.content.empty()) continue;
                string extracted = extract_text_from_upload(f, proc, jid + "_sn" + to_string(fi));
                if (!extracted.empty()) {
                    if (!parts.empty()) parts.push_back("\n\n--- " + f.filename + " ---\n\n");
                    parts.push_back(extracted);
                }
            }
            for (const auto& p : parts) input_text += p;
            filename = to_string(filecount) + " files";
            input_desc = to_string(filecount) + " files (" + format + ")";
            has_text = true; // treat combined text as pasted-text mode in thread
        } else {
            // Single file mode (original behavior, or filecount == 1)
            string key = (filecount == 1) ? "file0" : "file";
            if (!req.has_file(key)) key = "file"; // fallback
            auto file = req.get_file_value(key);
            filename = file.filename;
            input_desc = filename + " (" + format + ")";
            
            // Detect file extension
            fs::path fp(filename);
            file_ext = fp.extension().string();
            std::transform(file_ext.begin(), file_ext.end(), file_ext.begin(), ::tolower);
            
            input_path = proc + "/" + jid + "_input" + file_ext;
            { ofstream f(input_path, std::ios::binary); f.write(file.content.data(), file.content.size()); }
        }

        update_job(jid, {{"status", "processing"}, {"progress", 10}, {"stage", has_text ? "Processing pasted text..." : "Extracting text from file..."}});

        thread([jid, input_text, input_path, file_ext, format, math_fmt, proc, has_text, filename, ip, input_desc]() {
          string txt_path = proc + "/" + jid + "_text.txt";
          try {
            string text;
            bool extracted = false;

            // Helper: strip invalid UTF-8 bytes and UTF BOMs so json::dump() never throws
            auto sanitize_utf8 = [](const string& s) -> string {
                string out;
                out.reserve(s.size());
                size_t i = 0;
                // Strip UTF-8 BOM (EF BB BF) or UTF-16 BOM (FF FE / FE FF)
                if (s.size() >= 3 && (unsigned char)s[0]==0xEF && (unsigned char)s[1]==0xBB && (unsigned char)s[2]==0xBF) i = 3;
                else if (s.size() >= 2 && ((unsigned char)s[0]==0xFF && (unsigned char)s[1]==0xFE)) i = 2;
                else if (s.size() >= 2 && ((unsigned char)s[0]==0xFE && (unsigned char)s[1]==0xFF)) i = 2;
                for (; i < s.size(); ) {
                    unsigned char c = (unsigned char)s[i];
                    int seq = 0;
                    if      (c <= 0x7F)                         seq = 1;
                    else if ((c & 0xE0) == 0xC0 && c >= 0xC2)  seq = 2;
                    else if ((c & 0xF0) == 0xE0)                seq = 3;
                    else if ((c & 0xF8) == 0xF0 && c <= 0xF4)  seq = 4;
                    if (seq == 0) { i++; out += '?'; continue; } // invalid lead byte
                    if (i + seq > s.size()) { i++; out += '?'; continue; }
                    bool ok = true;
                    for (int k = 1; k < seq; k++)
                        if (((unsigned char)s[i+k] & 0xC0) != 0x80) { ok = false; break; }
                    if (ok) { out.append(s, i, seq); i += seq; }
                    else    { out += '?'; i++; }
                }
                return out;
            };
            
            if (has_text) {
                // Direct text input — just sanitize
                text = sanitize_utf8(input_text);
                extracted = !text.empty();
            } else if (file_ext == ".txt" || file_ext == ".rtf") {
                // Plain text file — read directly
                ifstream f(input_path, std::ios::binary);
                std::ostringstream ss; ss << f.rdbuf();
                text = sanitize_utf8(ss.str());
                extracted = !text.empty();
            } else if (file_ext == ".docx") {
                // Word document — try pandoc first, then fallback to unzip/grep
                string cmd = "pandoc -f docx -t plain " + escape_arg(input_path) + " -o " + escape_arg(txt_path);
                int code; exec_command(cmd, code);
                if (fs::exists(txt_path) && fs::file_size(txt_path) > 0) {
                    ifstream f(txt_path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf();
                    text = sanitize_utf8(ss.str());
                    extracted = !text.empty();
                }
                // Fallback: extract text from document.xml using basic parsing
                if (!extracted) {
                    #ifdef _WIN32
                    string unzip_cmd = "powershell -Command \"Expand-Archive -Path '" + input_path + "' -DestinationPath '" + proc + "/" + jid + "_docx' -Force\"";
                    #else
                    string unzip_cmd = "unzip -o " + escape_arg(input_path) + " -d " + escape_arg(proc + "/" + jid + "_docx");
                    #endif
                    exec_command(unzip_cmd, code);
                    string doc_xml = proc + "/" + jid + "_docx/word/document.xml";
                    if (fs::exists(doc_xml)) {
                        ifstream f(doc_xml); std::ostringstream ss; ss << f.rdbuf();
                        string xml = ss.str();
                        // Strip XML tags, keep text content
                        string plain;
                        bool in_tag = false;
                        for (char c : xml) {
                            if (c == '<') in_tag = true;
                            else if (c == '>') { in_tag = false; plain += ' '; }
                            else if (!in_tag) plain += c;
                        }
                        text = sanitize_utf8(plain);
                        extracted = !text.empty();
                    }
                    try { fs::remove_all(proc + "/" + jid + "_docx"); } catch (...) {}
                }
            } else if (file_ext == ".doc") {
                // Old Word format — try antiword or catdoc
                string cmd = "antiword " + escape_arg(input_path) + " > " + escape_arg(txt_path);
                int code; exec_command(cmd, code);
                if (!fs::exists(txt_path) || fs::file_size(txt_path) == 0) {
                    cmd = "catdoc " + escape_arg(input_path) + " > " + escape_arg(txt_path);
                    exec_command(cmd, code);
                }
                if (fs::exists(txt_path) && fs::file_size(txt_path) > 0) {
                    ifstream f(txt_path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf();
                    text = sanitize_utf8(ss.str());
                    extracted = !text.empty();
                }
            } else if (file_ext == ".pdf") {
                // PDF — use Ghostscript or pdftotext
                if (!g_ghostscript_path.empty()) {
                    string cmd = escape_arg(g_ghostscript_path) +
                        " -q -dNOPAUSE -dBATCH -sDEVICE=txtwrite"
                        " -dTextFormat=3"
                        " -sOutputFile=" + escape_arg(txt_path) +
                        " " + escape_arg(input_path);
                    int code; exec_command(cmd, code);
                    if (fs::exists(txt_path) && fs::file_size(txt_path) > 0) {
                        ifstream f(txt_path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf();
                        text = sanitize_utf8(ss.str());
                        extracted = !text.empty();
                    }
                }
                // Fallback: pdftotext
                if (!extracted) {
                    string cmd = "pdftotext " + escape_arg(input_path) + " " + escape_arg(txt_path);
                    int code; exec_command(cmd, code);
                    if (fs::exists(txt_path) && fs::file_size(txt_path) > 0) {
                        ifstream f(txt_path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf();
                        text = sanitize_utf8(ss.str());
                        extracted = !text.empty();
                    }
                }
            } else {
                // Unknown format — try reading as plain text
                ifstream f(input_path, std::ios::binary);
                std::ostringstream ss; ss << f.rdbuf();
                text = sanitize_utf8(ss.str());
                extracted = !text.empty();
            }

            if (!extracted || text.size() < 50) {
                string err_msg = has_text ? "Text too short (minimum 50 characters)." : 
                    "Could not extract text from file. Try a different format or paste text directly.";
                update_job(jid, {{"status","error"},{"error", err_msg}});
                if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
                try { fs::remove(txt_path); } catch (...) {}
                return;
            }

            // Truncate to ~24 000 chars to stay within token limits
            if (text.size() > 24000) text = text.substr(0, 24000) + "\n\n[... truncated ...]";

            // Store raw text in memory so the client can fetch it for comparison
            update_job_raw_text(jid, text);

            // ── Subject detection ────────────────────────────────────────────
            // Analyse source text to tailor the prompt for the subject area
            auto text_lower = [](const string& s) {
                string r = s; for (char& c : r) c = (char)::tolower((unsigned char)c); return r;
            };
            string tl = text_lower(text);
            bool is_math = (tl.find("theorem") != string::npos || tl.find("integral") != string::npos ||
                            tl.find("derivative") != string::npos || tl.find("matrix") != string::npos ||
                            tl.find("vector") != string::npos || tl.find("calculus") != string::npos ||
                            tl.find("algebra") != string::npos || tl.find("probability") != string::npos ||
                            tl.find("equation") != string::npos || tl.find("proof") != string::npos);
            bool is_code = (tl.find("function") != string::npos || tl.find("algorithm") != string::npos ||
                            tl.find("array") != string::npos || tl.find("class ") != string::npos ||
                            tl.find("loop") != string::npos || tl.find("recursion") != string::npos ||
                            tl.find("complexity") != string::npos);
            bool is_science = (tl.find("velocity") != string::npos || tl.find("acceleration") != string::npos ||
                               tl.find("molecule") != string::npos || tl.find("reaction") != string::npos ||
                               tl.find("wavelength") != string::npos || tl.find("entropy") != string::npos);

            string subject_rules;
            if (is_math) {
                subject_rules = "SUBJECT-SPECIFIC (Mathematics): "
                    "Every theorem must include its full statement and any conditions/constraints. "
                    "Every formula must have ALL variables defined. "
                    "Every worked example must be solved step-by-step showing all algebra. "
                    "Include a 'Key Formulas' section summarising all formulas used. "
                    "Add > blockquote exam hints for common mistakes (e.g. sign errors, domain restrictions). ";
            } else if (is_code) {
                subject_rules = "SUBJECT-SPECIFIC (Computer Science): "
                    "Every algorithm must include pseudocode or code. "
                    "State time and space complexity using Big-O notation. "
                    "Trace through any data structure operations step-by-step with examples. "
                    "Note edge cases and why they matter. ";
            } else if (is_science) {
                subject_rules = "SUBJECT-SPECIFIC (Science): "
                    "Every equation must have all variables and units defined. "
                    "Include diagrams described in text form where relevant. "
                    "Show unit conversions in worked examples. "
                    "Connect theory to practical/lab applications explicitly. ";
            } else {
                subject_rules = "SUBJECT-SPECIFIC: "
                    "Define every key term precisely on first use. "
                    "Include real-world examples that illustrate abstract concepts. "
                    "Preserve any argument structures, frameworks, or models from the source. ";
            }

            // ── Pre-pass: extract exhaustive coverage checklist ──────────────
            update_job(jid, {{"status","processing"},{"progress",30},{"stage","Building content checklist..."}});

            string coverage_checklist;
            {
                json cl_payload = {
                    {"model", "llama-3.1-8b-instant"},
                    {"messages", json::array({
                        {{"role","system"}, {"content",
                            "You are an academic content analyst. Extract a complete bullet-point checklist of "
                            "EVERY topic, concept, definition, formula, theorem, algorithm, worked example, property, "
                            "and application present in the provided lecture material. "
                            "Be completely exhaustive — nothing may be omitted. Include even minor sub-points. "
                            "Output ONLY a flat bullet list, one item per line starting with '-'. No headings, no commentary."}},
                        {{"role","user"}, {"content", "Extract a complete coverage checklist from this lecture:\n\n" + text}}
                    })},
                    {"max_tokens", 1500},
                    {"temperature", 0.1}
                };
                auto cl_r = call_groq(cl_payload, proc, jid + "_checklist");
                if (cl_r.ok && cl_r.response.contains("choices")) {
                    coverage_checklist = cl_r.response["choices"][0]["message"]["content"].get<string>();
                }
            }

            update_job(jid, {{"status","processing"},{"progress",50},{"stage","Generating study notes with AI..."}});

            // ── Step 2: call Groq ────────────────────────────────────────────
            string math_instruction;
            if (format == "markdown") {
                if (math_fmt == "latex") {
                    math_instruction = " For mathematical notation, use LaTeX delimiters: \\(...\\) for inline math and \\[...\\] on its own line for display math. Never use $...$ or $$...$$ — always use \\(...\\) and \\[...\\] for all mathematical expressions. "
                        "Greek letters: \\alpha, \\beta, \\gamma, \\theta, \\omega, \\pi; uppercase: \\Gamma, \\Delta, \\Omega. "
                        "Subscripts/superscripts: x_i, x^2, x_i^2. Fractions: \\frac{a}{b}. Square roots: \\sqrt{x}, \\sqrt[3]{x}. "
                        "Apply to ALL maths including set notation, equations, and formulas.";
                } else if (math_fmt == "dollar") {
                    math_instruction = " For mathematical notation, use $...$ for inline math and $$...$$ on its own line for display/block math. "
                        "EVERY mathematical expression — including set notation, simple equations, variables in isolation, and all formulas — must be wrapped in $ or $$. Never write maths in plain text or code blocks. "
                        "Greek letters: \\alpha, \\beta, \\gamma, \\theta, \\omega, \\pi; uppercase: \\Gamma, \\Delta, \\Omega. "
                        "Subscripts and superscripts: x_i, x^2, x_i^2. "
                        "Fractions: \\frac{a+1}{b+1}. Square roots: \\sqrt{x^3}, \\sqrt[3]{\\frac{x}{y}}. "
                        "For example, write $f(a) = a^2$ not f(a) = a^2.";
                } else {
                    math_instruction = " Do not use any special math notation delimiters — write all mathematical expressions in plain readable text.";
                }
            }
            string style_instruction;
            if (format == "markdown") {
                style_instruction = "Format the output as clean Markdown optimised for Obsidian. "
                    "Use ## headings (never numbered). Use bullet points for all details — never long paragraphs. "
                    "Bold (**) every key term on first use. Use > blockquotes for instructor emphasis points and exam hints. "
                    "Use `code blocks` only for actual programming code, never for maths." + math_instruction;
            } else {
                style_instruction = "Write clear, readable plain text notes with UPPERCASE section labels and dash bullet points.";
            }

            string system_prompt = "You are an expert academic tutor producing thorough, university-level study notes. "
                "Your notes must be long, detailed, and self-contained — a student who never attended the lecture must be able to fully understand the topic, "
                "work through every example independently, and use these notes as their sole exam resource. "
                "Rules you must follow: "
                "(1) Cover EVERY heading, sub-topic, concept, definition, formula, and worked example present in the source — do not skip or condense anything. "
                "(2) For each formula or equation, state what every variable represents and give the units where applicable. "
                "(3) Re-work every example step-by-step, showing all intermediate algebra. "
                "(4) For any topic where understanding a later section requires an earlier one, include a brief recap so the notes are self-sufficient. "
                "(5) Include all practical applications, real-world examples, and connections to other course topics. "
                "(6) Flag exam hints and common mistakes explicitly. "
                "(7) Your response must be substantially longer than a summary — aim for depth over brevity. "
                + subject_rules
                + style_instruction;

            string user_prompt;
            if (!coverage_checklist.empty()) {
                user_prompt = "SOURCE MATERIAL:\n" + text +
                    "\n\n---\n\nMANDATORY COVERAGE CHECKLIST — every single item below MUST be fully addressed in the notes. "
                    "Check each item off mentally as you write. Missing even one item is unacceptable:\n" +
                    coverage_checklist +
                    "\n\n---\n\nCreate thorough, in-depth study notes that address EVERY item in the checklist. "
                    "For each: give a full explanation, define all variables, re-work examples step-by-step. "
                    "The notes must be long, detailed, and suitable as a complete exam revision resource.";
            } else {
                user_prompt = "Create thorough, in-depth study notes from the following lecture content. "
                    "Go through EVERY section systematically. Include every formula with full variable explanations, every worked example re-solved step-by-step, "
                    "every definition, every application, and every exam hint. "
                    "Do not summarise or condense — the notes should be comprehensive enough to fully replace attending the lecture. "
                    "This response should be long and detailed:\n\n" + text;
            }

            json payload = {
                {"model", "llama-3.3-70b-versatile"},
                {"messages", json::array({
                    {{"role","system"}, {"content", system_prompt}},
                    {{"role","user"},   {"content", user_prompt}}
                })},
                {"max_tokens", 8192},
                {"temperature", 0.3}
            };

            auto gr = call_groq(payload, proc, jid + "_sn");

            string notes;
            bool ai_ok = gr.ok;
            if (ai_ok) {
                notes = gr.response["choices"][0]["message"]["content"].get<string>();
            } else if (!gr.response.is_null() && gr.response.contains("error")) {
                string msg = gr.response["error"].value("message", "AI API error");
                update_job(jid, {{"status","error"},{"error", msg}});
                if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
                try { fs::remove(txt_path); } catch (...) {}
                return;
            }

            if (!ai_ok) {
                update_job(jid, {{"status","error"},{"error","AI API call failed. Check server logs and Groq key."}});
                if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
                try { fs::remove(txt_path); } catch (...) {}
                return;
            }

            // ── Auto-refine pass: fill any gaps the main pass missed ──────────
            if (!coverage_checklist.empty() && notes.size() > 500) {
                update_job(jid, {{"status","processing"},{"progress",78},{"stage","Refining and filling gaps..."}});

                string refine_system =
                    "You are an expert study notes editor. You receive a coverage checklist and a draft of study notes. "
                    "Your ONLY job is to identify checklist items that are missing or only briefly mentioned in the notes, "
                    "then output a complete revised version of the notes that fully addresses every gap.\n\n"
                    "RULES:\n"
                    "1. Preserve ALL existing correct content — never remove or shorten any existing section.\n"
                    "2. Add full explanations, worked steps, and variable definitions for every missing item.\n"
                    "3. Keep the same Markdown structure (## headings, bullet points, **bold** terms, > blockquotes).\n"
                    "4. Output ONLY the improved notes — no commentary, no preamble, no meta-text.\n"
                    "5. If the notes already fully cover all checklist items, return them unchanged.";

                string refine_user =
                    "COVERAGE CHECKLIST (every item must be in the notes):\n" + coverage_checklist +
                    "\n\n---\n\nCURRENT NOTES:\n" + notes +
                    "\n\n---\n\n"
                    "Identify any checklist items missing or underdeveloped in the notes, then output the complete improved notes.";

                json refine_payload = {
                    {"model", "llama-3.3-70b-versatile"},
                    {"messages", json::array({
                        {{"role","system"}, {"content", refine_system}},
                        {{"role","user"},   {"content", refine_user}}
                    })},
                    {"max_tokens", 8192},
                    {"temperature", 0.2}
                };

                auto rr = call_groq(refine_payload, proc, jid + "_refine");
                if (rr.ok && rr.response.contains("choices")) {
                    string refined = rr.response["choices"][0]["message"]["content"].get<string>();
                    // Accept refined output only if it's substantial (at least 60% of original length)
                    if (refined.size() >= notes.size() * 6 / 10) {
                        notes = refined;
                    }
                }
            }

            // ── Step 3: write output file ─────────────────────────────────────
            string ext = (format == "markdown") ? ".md" : ".txt";
            string out_path = proc + "/" + jid + "_notes" + ext;
            { ofstream f(out_path); f << notes; }

            update_job(jid, {{"status","completed"},{"progress",100},{"filename","study_notes" + ext},{"model_used", gr.model_used}}, out_path);
            stat_record_ai_call("AI Study Notes", gr.model_used, gr.tokens_used, ip);
            discord_log_ai_tool("AI Study Notes", input_desc, gr.model_used, gr.tokens_used, ip, gr.tokens_remaining);
            if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
            try { fs::remove(txt_path); } catch (...) {}
          } catch (const std::exception& e) {
              update_job(jid, {{"status","error"},{"error", string("Internal error: ") + e.what()}});
              if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
              try { fs::remove(txt_path); } catch (...) {}
          } catch (...) {
              update_job(jid, {{"status","error"},{"error","Unknown internal error in study notes worker"}});
              if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
              try { fs::remove(txt_path); } catch (...) {}
          }
        }).detach();

        res.set_content(json({{"job_id", jid}}).dump(), "application/json");
    });

    // ── POST /api/tools/ai-improve-notes ────────────────────────────────────
    // Takes current notes + feedback and generates improved notes
    svr.Post("/api/tools/ai-improve-notes", [](const httplib::Request& req, httplib::Response& res) {
        if (g_groq_key.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "AI features are not configured on this server."}}).dump(), "application/json");
            return;
        }

        string job_id = req.get_file_value("job_id").content;
        string current_notes = req.get_file_value("current_notes").content;
        string feedback_json = req.get_file_value("feedback").content;

        if (current_notes.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "No notes provided"}}).dump(), "application/json");
            return;
        }
        // Cap notes size to stay within token limits and prevent excessive API cost
        if (current_notes.size() > 16000) current_notes = current_notes.substr(0, 16000);

        // Parse feedback
        json feedback;
        try {
            feedback = json::parse(feedback_json);
        } catch (...) {
            feedback = json::object();
        }
        
        auto gaps = feedback.value("gaps", json::array());
        auto tips = feedback.value("studyTips", json::array());
        auto missed = feedback.value("missed", json::array());  // array of {topic, importance} objects
        auto keyConcepts = feedback.value("keyConcepts", json::array());
        int overallScore = feedback.value("overallScore", 0);

        // Separate missed topics by importance level
        string high_missed, medium_missed, low_missed;
        for (const auto& m : missed) {
            string topic = m.is_object() ? m.value("topic", "") : m.get<string>();
            string imp   = m.is_object() ? m.value("importance", "medium") : "medium";
            if (imp == "high")        high_missed   += "- " + topic + "\n";
            else if (imp == "medium") medium_missed += "- " + topic + "\n";
            else                      low_missed    += "- " + topic + "\n";
        }

        // High-priority concepts that ARE covered but may need expansion
        string high_covered_weak;
        for (const auto& c : keyConcepts) {
            if (!c.is_object()) continue;
            if (c.value("importance", "") == "high" && c.value("covered", false)) {
                string excerpt = c.value("notes_excerpt", "");
                // If the excerpt is very short the coverage is likely thin
                if (excerpt.size() < 80) {
                    high_covered_weak += "- " + c.value("topic", "") + "\n";
                }
            }
        }

        // Build gap/tips lists
        string gaps_list, tips_list;
        for (const auto& g : gaps) gaps_list += "- " + g.get<string>() + "\n";
        for (const auto& t : tips) tips_list += "- " + t.get<string>() + "\n";

        string system_prompt = R"(You are an expert study notes editor. You are given study notes alongside a detailed AI coverage analysis. Your goal is to substantially improve the notes so that the coverage score increases — especially by fully addressing every missing or thin topic.

CRITICAL RULES:
1. HIGH PRIORITY missing topics MUST be added with complete explanations, definitions, key properties, worked examples, and any relevant formulas or equations — do not just mention them
2. MEDIUM PRIORITY missing topics must be added with a clear explanation and at least one example
3. LOW PRIORITY missing topics should be added briefly if they fit the flow
4. Expand weakly-covered high-priority topics with more depth, examples, and worked solutions
5. Use $...$ for inline math and $$...$$ on its own line for block equations — never put maths in plain text
6. Preserve ALL existing correct content — never remove or shorten material that is already there
7. Keep the same Markdown structure (## headings, bullet points, **bold** key terms, > blockquotes for hints)
8. Output ONLY the improved notes — absolutely no meta-commentary, preamble, or explanations outside the notes)";

        string user_prompt = "CURRENT NOTES (Coverage Score: " + std::to_string(overallScore) + "/100):\n\n---\n" + current_notes + "\n---\n\n";

        if (!high_missed.empty()) {
            user_prompt += "\u26a0\ufe0f HIGH PRIORITY MISSING TOPICS — add these fully to significantly raise the score:\n" + high_missed + "\n";
        }
        if (!medium_missed.empty()) {
            user_prompt += "MEDIUM PRIORITY MISSING TOPICS — add with clear explanations:\n" + medium_missed + "\n";
        }
        if (!low_missed.empty()) {
            user_prompt += "LOW PRIORITY MISSING TOPICS — add briefly if relevant:\n" + low_missed + "\n";
        }
        if (!high_covered_weak.empty()) {
            user_prompt += "HIGH PRIORITY TOPICS NEEDING MORE DEPTH (already in notes but coverage is thin):\n" + high_covered_weak + "\n";
        }
        if (!gaps_list.empty()) {
            user_prompt += "AREAS TO IMPROVE:\n" + gaps_list + "\n";
        }
        if (!tips_list.empty()) {
            user_prompt += "IMPROVEMENT SUGGESTIONS:\n" + tips_list + "\n";
        }

        user_prompt += "\nRewrite and expand the notes to address all the feedback above. Prioritise the HIGH PRIORITY items — fully explaining each one will have the biggest impact on the score. Output only the improved notes in Markdown format.";

        // Call Groq API
        json payload = {
            {"model", "llama-3.3-70b-versatile"},
            {"messages", {
                {{"role", "system"}, {"content", system_prompt}},
                {{"role", "user"}, {"content", user_prompt}}
            }},
            {"temperature", 0.4},
            {"max_tokens", 8192}
        };

        string proc = get_processing_dir();

        auto gr = call_groq(payload, proc, "improve_" + job_id);

        if (!gr.ok) {
            string msg = (!gr.response.is_null() && gr.response.contains("error"))
                ? gr.response["error"].value("message", "AI API error") : "Failed to improve notes";
            res.status = 500;
            res.set_content(json({{"error", msg}}).dump(), "application/json");
            return;
        }

        string improved_notes = gr.response["choices"][0]["message"]["content"].get<string>();
        stat_record_ai_call("AI Improve Notes", gr.model_used, gr.tokens_used, req.remote_addr);
        discord_log_ai_tool("AI Improve Notes", "Notes improvement", gr.model_used, gr.tokens_used, req.remote_addr, gr.tokens_remaining);
        res.set_content(json({{"improved_notes", improved_notes}, {"model_used", gr.model_used}}).dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════════════
    // AI FLASHCARD GENERATOR
    // ══════════════════════════════════════════════════════════════════════════════
    svr.Post("/api/tools/ai-flashcards", [](const httplib::Request& req, httplib::Response& res) {
        if (g_groq_key.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "AI features are not configured on this server."}}).dump(), "application/json");
            return;
        }

        bool has_text = req.has_file("text") && !req.get_file_value("text").content.empty();
        // Multi-file support
        int filecount = 0;
        if (req.has_file("filecount")) {
            try { filecount = std::stoi(req.get_file_value("filecount").content); } catch (...) {}
        }
        bool has_file = (filecount > 0) ||
                        (req.has_file("file") && !req.get_file_value("file").content.empty());

        if (!has_text && !has_file) {
            res.status = 400;
            res.set_content(json({{"error", "No content provided"}}).dump(), "application/json");
            return;
        }

        string count_raw = req.has_file("count") ? req.get_file_value("count").content : "20";
        bool max_mode = (count_raw == "max" || count_raw == "0");
        int count = 20;
        if (!max_mode) {
            try { count = std::stoi(count_raw); } catch (...) {}
            count = std::min(std::max(count, 5), 100);
        }

        string text;
        string proc = get_processing_dir();
        string jid = generate_job_id();

        string input_desc;
        if (has_text) {
            text = sanitize_utf8(req.get_file_value("text").content);
            input_desc = "Pasted Text";
        } else if (filecount > 1) {
            vector<string> parts;
            for (int fi = 0; fi < filecount && fi < 10; fi++) {
                string key = "file" + to_string(fi);
                if (!req.has_file(key)) continue;
                auto f = req.get_file_value(key);
                if (f.content.empty()) continue;
                string extracted = extract_text_from_upload(f, proc, jid + "_fc" + to_string(fi));
                if (!extracted.empty()) {
                    if (!parts.empty()) parts.push_back("\n\n--- " + f.filename + " ---\n\n");
                    parts.push_back(extracted);
                }
            }
            for (const auto& p : parts) text += p;
            text = sanitize_utf8(text); // re-sanitize combined text
            input_desc = to_string(filecount) + " files";
        } else {
            string key = (filecount == 1) ? "file0" : "file";
            if (!req.has_file(key)) key = "file";
            auto file = req.get_file_value(key);
            input_desc = file.filename;
            text = extract_text_from_upload(file, proc, jid + "_fc");
        }

        if (text.size() < 50) {
            res.status = 400;
            res.set_content(json({{"error", "Content too short (minimum 50 characters)"}}).dump(), "application/json");
            return;
        }
        if (text.size() > 16000) text = text.substr(0, 16000);

        string system_prompt, user_prompt;
        int max_tokens;

        if (max_mode) {
            system_prompt = "You are an expert educator creating flashcards. "
                "Generate the MAXIMUM number of flashcards possible from the provided content — cover every concept, term, definition, fact, and relationship present. "
                "Do not skip anything that could be tested. "
                "Output ONLY a valid JSON array with objects containing 'question', 'answer', and 'tag' fields. "
                "The 'tag' field must be a short topic name (2-4 words) that categorises the card — use consistent topic names across related cards. "
                "Questions should be clear and specific. Answers should be concise but complete.";
            user_prompt = "Generate as many flashcards as possible from this content — cover every testable concept, term, and fact:\n\n" + text +
                "\n\nOutput ONLY JSON array: [{\"question\": \"...\", \"answer\": \"...\", \"tag\": \"Topic Name\"}]";
            max_tokens = 8192;
        } else {
            system_prompt = "You are an expert educator creating flashcards. Generate exactly " + to_string(count) + " flashcards from the provided content. "
                "Each flashcard should test a key concept, term, or fact. "
                "Output ONLY a valid JSON array with objects containing 'question', 'answer', and 'tag' fields. "
                "The 'tag' field must be a short topic name (2-4 words) that categorises the card — use consistent topic names across related cards. "
                "Questions should be clear and specific. Answers should be concise but complete.";
            user_prompt = "Create " + to_string(count) + " flashcards from this content:\n\n" + text +
                "\n\nOutput as JSON array: [{\"question\": \"...\", \"answer\": \"...\", \"tag\": \"Topic Name\"}]";
            max_tokens = 4096;
        }

        json payload = {
            {"model", "llama-3.3-70b-versatile"},
            {"messages", {
                {{"role", "system"}, {"content", system_prompt}},
                {{"role", "user"}, {"content", user_prompt}}
            }},
            {"temperature", 0.5},
            {"max_tokens", max_tokens}
        };

        auto gr = call_groq(payload, proc, jid + "_fc");

        json flashcards = json::array();
        bool success = false;

        if (gr.ok) {
            try {
                string content = gr.response["choices"][0]["message"]["content"].get<string>();
                size_t start = content.find('[');
                size_t end = content.rfind(']');
                if (start != string::npos && end != string::npos) {
                    flashcards = json::parse(content.substr(start, end - start + 1));
                    success = true;
                }
            } catch (...) {}
        }

        if (!success) {
            res.status = 500;
            res.set_content(json({{"error", "Failed to generate flashcards"}}).dump(), "application/json");
            return;
        }

        string label = input_desc + " (" + (max_mode ? "max" : to_string(count)) + " cards → " + to_string(flashcards.size()) + " generated)";
        stat_record_ai_call("AI Flashcards", gr.model_used, gr.tokens_used, req.remote_addr);
        discord_log_ai_tool("AI Flashcards", label, gr.model_used, gr.tokens_used, req.remote_addr, gr.tokens_remaining);
        res.set_content(json({{"flashcards", flashcards}, {"model_used", gr.model_used}, {"count", flashcards.size()}}).dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════════════
    // AI PRACTICE QUIZ
    // ══════════════════════════════════════════════════════════════════════════════
    svr.Post("/api/tools/ai-quiz", [](const httplib::Request& req, httplib::Response& res) {
        if (g_groq_key.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "AI features are not configured on this server."}}).dump(), "application/json");
            return;
        }

        bool has_text = req.has_file("text") && !req.get_file_value("text").content.empty();
        // Multi-file support
        int filecount = 0;
        if (req.has_file("filecount")) {
            try { filecount = std::stoi(req.get_file_value("filecount").content); } catch (...) {}
        }
        bool has_file = (filecount > 0) ||
                        (req.has_file("file") && !req.get_file_value("file").content.empty());

        if (!has_text && !has_file) {
            res.status = 400;
            res.set_content(json({{"error", "No content provided"}}).dump(), "application/json");
            return;
        }

        int count = 10;
        string difficulty = "medium";
        if (req.has_file("count")) {
            try { count = std::stoi(req.get_file_value("count").content); } catch (...) {}
        }
        if (req.has_file("difficulty")) {
            difficulty = req.get_file_value("difficulty").content;
        }
        // Clamp unknown difficulty values to "medium"
        if (difficulty != "easy" && difficulty != "medium" && difficulty != "hard") difficulty = "medium";
        count = std::min(std::max(count, 5), 20);

        string text;
        string proc = get_processing_dir();
        string jid = generate_job_id();

        string input_desc;
        if (has_text) {
            text = sanitize_utf8(req.get_file_value("text").content);
            input_desc = "Pasted Text (" + difficulty + ")";
        } else if (filecount > 1) {
            vector<string> parts;
            for (int fi = 0; fi < filecount && fi < 10; fi++) {
                string key = "file" + to_string(fi);
                if (!req.has_file(key)) continue;
                auto f = req.get_file_value(key);
                if (f.content.empty()) continue;
                string extracted = extract_text_from_upload(f, proc, jid + "_qz" + to_string(fi));
                if (!extracted.empty()) {
                    if (!parts.empty()) parts.push_back("\n\n--- " + f.filename + " ---\n\n");
                    parts.push_back(extracted);
                }
            }
            for (const auto& p : parts) text += p;
            text = sanitize_utf8(text); // re-sanitize combined text
            input_desc = to_string(filecount) + " files (" + difficulty + ")";
        } else {
            string key = (filecount == 1) ? "file0" : "file";
            if (!req.has_file(key)) key = "file";
            auto file = req.get_file_value(key);
            input_desc = file.filename + " (" + difficulty + ")";
            text = extract_text_from_upload(file, proc, jid + "_qz");
        }

        if (text.size() < 50) {
            res.status = 400;
            res.set_content(json({{"error", "Content too short (minimum 50 characters)"}}).dump(), "application/json");
            return;
        }
        if (text.size() > 12000) text = text.substr(0, 12000);

        string diff_instruction = difficulty == "easy" ? "basic recall and simple concepts" :
                                  difficulty == "hard" ? "complex analysis, application, and critical thinking" :
                                  "moderate difficulty requiring understanding and application";

        string system_prompt = "You are an expert quiz creator. Generate exactly " + to_string(count) + " multiple-choice questions. "
            "Difficulty: " + diff_instruction + ". "
            "Each question must have exactly 4 options with only ONE correct answer. "
            "Include a brief explanation for the correct answer. "
            "Output ONLY valid JSON array with objects containing: 'question', 'options' (array of 4 strings), 'correct' (0-3 index), 'explanation'.";

        string user_prompt = "Create " + to_string(count) + " quiz questions from this content:\n\n" + text +
            "\n\nOutput as JSON: [{\"question\": \"...\", \"options\": [\"A\", \"B\", \"C\", \"D\"], \"correct\": 0, \"explanation\": \"...\"}]";

        json payload = {
            {"model", "llama-3.3-70b-versatile"},
            {"messages", {
                {{"role", "system"}, {"content", system_prompt}},
                {{"role", "user"}, {"content", user_prompt}}
            }},
            {"temperature", 0.6},
            {"max_tokens", 4096}
        };

        auto gr = call_groq(payload, proc, jid + "_qz");

        json questions = json::array();
        bool success = false;

        if (gr.ok) {
            try {
                string content = gr.response["choices"][0]["message"]["content"].get<string>();
                size_t start = content.find('[');
                size_t end = content.rfind(']');
                if (start != string::npos && end != string::npos) {
                    questions = json::parse(content.substr(start, end - start + 1));
                    success = true;
                }
            } catch (...) {}
        }

        if (!success) {
            res.status = 500;
            res.set_content(json({{"error", "Failed to generate quiz"}}).dump(), "application/json");
            return;
        }

        stat_record_ai_call("AI Quiz", gr.model_used, gr.tokens_used, req.remote_addr);
        discord_log_ai_tool("AI Quiz", input_desc, gr.model_used, gr.tokens_used, req.remote_addr, gr.tokens_remaining);
        res.set_content(json({{"questions", questions}, {"model_used", gr.model_used}}).dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════════════
    // AI PARAPHRASER
    // ══════════════════════════════════════════════════════════════════════════════
    svr.Post("/api/tools/ai-paraphrase", [](const httplib::Request& req, httplib::Response& res) {
        if (g_groq_key.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "AI features are not configured on this server."}}).dump(), "application/json");
            return;
        }

        if (!req.has_file("text") || req.get_file_value("text").content.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "No text provided"}}).dump(), "application/json");
            return;
        }

        string text = req.get_file_value("text").content;
        string tone = req.has_file("tone") ? req.get_file_value("tone").content : "formal";

        if (text.size() < 20) {
            res.status = 400;
            res.set_content(json({{"error", "Text too short (minimum 20 characters)"}}).dump(), "application/json");
            return;
        }
        if (text.size() > 5000) text = text.substr(0, 5000);

        string tone_instruction;
        if (tone == "formal") tone_instruction = "Use professional, formal language appropriate for business or academic settings.";
        else if (tone == "casual") tone_instruction = "Use friendly, conversational language as if talking to a friend.";
        else if (tone == "simplified") tone_instruction = "Use simple words and short sentences. Make it easy to understand for everyone.";
        else if (tone == "academic") tone_instruction = "Use scholarly language with precise terminology appropriate for academic papers.";
        else tone_instruction = "Rewrite in a clear, neutral tone.";

        string system_prompt = "You are an expert writer and editor. Paraphrase the given text while preserving its meaning. " + tone_instruction +
            " Output ONLY the paraphrased text, nothing else.";

        json payload = {
            {"model", "llama-3.3-70b-versatile"},
            {"messages", {
                {{"role", "system"}, {"content", system_prompt}},
                {{"role", "user"}, {"content", "Paraphrase this text:\n\n" + text}}
            }},
            {"temperature", 0.7},
            {"max_tokens", 2048}
        };

        string proc = get_processing_dir();
        string jid = generate_job_id();
        auto gr = call_groq(payload, proc, jid + "_pp");

        if (!gr.ok) {
            res.status = 500;
            res.set_content(json({{"error", "Failed to paraphrase text"}}).dump(), "application/json");
            return;
        }

        string result = gr.response["choices"][0]["message"]["content"].get<string>();
        stat_record_ai_call("AI Paraphrase", gr.model_used, gr.tokens_used, req.remote_addr);
        discord_log_ai_tool("AI Paraphrase", tone, gr.model_used, gr.tokens_used, req.remote_addr, gr.tokens_remaining);
        res.set_content(json({{"result", result}, {"model_used", gr.model_used}}).dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════════════
    // CITATION GENERATOR
    // ══════════════════════════════════════════════════════════════════════════════
    svr.Post("/api/tools/citation-generate", [](const httplib::Request& req, httplib::Response& res) {
        string source_type = req.has_file("source_type") ? req.get_file_value("source_type").content : "url";
        string style = req.has_file("style") ? req.get_file_value("style").content : "apa";

        discord_log_tool("Citation Generator", style + " / " + source_type, req.remote_addr);

        json metadata;
        string citation;

        if (source_type == "doi") {
            string doi = req.has_file("doi") ? req.get_file_value("doi").content : "";
            if (doi.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "DOI required"}}).dump(), "application/json");
                return;
            }

            // Validate DOI format to prevent curl command injection
            // DOIs start with "10." and must not contain shell metacharacters
            if (doi.size() > 256 || doi.find("10.") != 0 ||
                doi.find_first_of(" \"'`$;&|\\!^~{}\r\n") != string::npos) {
                res.status = 400;
                res.set_content(json({{"error", "Invalid DOI format"}}).dump(), "application/json");
                return;
            }

            // Fetch DOI metadata from doi.org
            string proc = get_processing_dir();
            string jid = generate_job_id();
            string resp_file = proc + "/" + jid + "_doi.json";
            
            string accept_file = proc + "/" + jid + "_accept.txt";
            { ofstream fa(accept_file); fa << "Accept: application/vnd.citationstyles.csl+json"; }
            string doi_url = "https://doi.org/" + doi;
            string curl_cmd = "curl -s -L -H @" + escape_arg(accept_file) +
                              " " + escape_arg(doi_url) +
                              " -o " + escape_arg(resp_file);
            int rc; exec_command(curl_cmd, rc);
            
            if (fs::exists(resp_file)) {
                try {
                    ifstream f(resp_file);
                    std::ostringstream ss; ss << f.rdbuf();
                    auto doi_json = json::parse(ss.str());
                    
                    metadata["title"] = doi_json.value("title", "");
                    if (doi_json.contains("author") && doi_json["author"].is_array() && !doi_json["author"].empty()) {
                        string authors;
                        for (const auto& a : doi_json["author"]) {
                            if (!authors.empty()) authors += ", ";
                            authors += a.value("family", "") + ", " + a.value("given", "");
                        }
                        metadata["author"] = authors;
                    }
                    if (doi_json.contains("published") && doi_json["published"].contains("date-parts")) {
                        auto parts = doi_json["published"]["date-parts"][0];
                        if (parts.is_array() && !parts.empty()) {
                            metadata["date"] = to_string(parts[0].get<int>());
                        }
                    }
                    metadata["publisher"] = doi_json.value("publisher", "");
                    metadata["journal"] = doi_json.value("container-title", "");
                    metadata["doi"] = doi;
                } catch (...) {}
            }
            try { fs::remove(resp_file); fs::remove(accept_file); } catch (...) {}
        } else if (source_type == "url") {
            string url = req.has_file("url") ? req.get_file_value("url").content : "";
            if (url.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "URL required"}}).dump(), "application/json");
                return;
            }

            // Fetch webpage and extract metadata
            string proc = get_processing_dir();
            string jid = generate_job_id();
            string resp_file = proc + "/" + jid + "_page.html";
            
            string curl_cmd = "curl -s -L -A Mozilla/5.0 " + escape_arg(url) +
                              " -o " + escape_arg(resp_file);
            int rc; exec_command(curl_cmd, rc);
            
            if (fs::exists(resp_file)) {
                ifstream f(resp_file);
                std::ostringstream ss; ss << f.rdbuf();
                string html = ss.str();
                
                // Extract title
                size_t t1 = html.find("<title");
                if (t1 != string::npos) {
                    size_t t2 = html.find(">", t1);
                    size_t t3 = html.find("</title>", t2);
                    if (t2 != string::npos && t3 != string::npos) {
                        metadata["title"] = html.substr(t2 + 1, t3 - t2 - 1);
                    }
                }
                
                // Extract og:site_name
                size_t s1 = html.find("og:site_name");
                if (s1 != string::npos) {
                    size_t s2 = html.find("content=\"", s1);
                    if (s2 != string::npos) {
                        size_t s3 = html.find("\"", s2 + 9);
                        if (s3 != string::npos) {
                            metadata["site"] = html.substr(s2 + 9, s3 - s2 - 9);
                        }
                    }
                }

                // Extract author from meta tag
                size_t a1 = html.find("name=\"author\"");
                if (a1 != string::npos) {
                    size_t a2 = html.find("content=\"", a1);
                    if (a2 != string::npos && a2 - a1 < 50) {
                        size_t a3 = html.find("\"", a2 + 9);
                        if (a3 != string::npos) {
                            metadata["author"] = html.substr(a2 + 9, a3 - a2 - 9);
                        }
                    }
                }

                metadata["url"] = url;
                
                // Get current date for access date
                time_t now = time(nullptr);
                struct tm* t = localtime(&now);
                char buf[64];
                strftime(buf, sizeof(buf), "%B %d, %Y", t);
                metadata["access_date"] = string(buf);
            }
            try { fs::remove(resp_file); } catch (...) {}
        } else {
            // Manual entry
            metadata["author"] = req.has_file("author") ? req.get_file_value("author").content : "";
            metadata["title"] = req.has_file("title") ? req.get_file_value("title").content : "";
            metadata["date"] = req.has_file("year") ? req.get_file_value("year").content : "";
            metadata["publisher"] = req.has_file("publisher") ? req.get_file_value("publisher").content : "";
            metadata["url"] = req.has_file("url") ? req.get_file_value("url").content : "";
            
            time_t now = time(nullptr);
            struct tm* t = localtime(&now);
            char buf[64];
            strftime(buf, sizeof(buf), "%B %d, %Y", t);
            metadata["access_date"] = string(buf);
        }

        // Format citation based on style
        string title = metadata.value("title", "Untitled");
        string author = metadata.value("author", "");
        string date = metadata.value("date", "n.d.");
        string publisher = metadata.value("publisher", "");
        string site = metadata.value("site", publisher);
        string url = metadata.value("url", "");
        string access = metadata.value("access_date", "");

        if (style == "apa") {
            // APA 7th: Author. (Year, Month Day). Title. Site Name. URL
            citation = author.empty() ? "" : author + ". ";
            citation += "(" + date + "). ";
            citation += title + ". ";
            if (!site.empty()) citation += site + ". ";
            if (!url.empty()) citation += url;
        } else if (style == "mla") {
            // MLA 9th: Author. "Title." Site Name, Publisher, Date, URL. Accessed Day Month Year.
            citation = author.empty() ? "" : author + ". ";
            citation += "\"" + title + ".\" ";
            if (!site.empty()) citation += site + ", ";
            if (!publisher.empty() && publisher != site) citation += publisher + ", ";
            if (!date.empty()) citation += date + ", ";
            if (!url.empty()) citation += url + ". ";
            if (!access.empty()) citation += "Accessed " + access + ".";
        } else if (style == "chicago") {
            // Chicago: Author. "Title." Site Name. Publisher. Last modified/Accessed Date. URL.
            citation = author.empty() ? "" : author + ". ";
            citation += "\"" + title + ".\" ";
            if (!site.empty()) citation += site + ". ";
            if (!publisher.empty() && publisher != site) citation += publisher + ". ";
            if (!access.empty()) citation += "Accessed " + access + ". ";
            if (!url.empty()) citation += url + ".";
        } else if (style == "harvard") {
            // Harvard: Author (Year) Title, Site Name. Available at: URL (Accessed: Date).
            citation = author.empty() ? "" : author + " ";
            citation += "(" + date + ") ";
            citation += title + ", ";
            if (!site.empty()) citation += site + ". ";
            if (!url.empty()) citation += "Available at: " + url + " ";
            if (!access.empty()) citation += "(Accessed: " + access + ").";
        }

        res.set_content(json({{"citation", citation}, {"metadata", metadata}}).dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════════════
    // AI MIND MAP GENERATOR
    // ══════════════════════════════════════════════════════════════════════════════
    svr.Post("/api/mind-map", [](const httplib::Request& req, httplib::Response& res) {
        if (g_groq_key.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "AI features are not configured on this server."}}).dump(), "application/json");
            return;
        }

        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid JSON body"}}).dump(), "application/json");
            return;
        }

        string text = body.value("text", "");
        if (text.size() < 50) {
            res.status = 400;
            res.set_content(json({{"error", "Content too short (minimum 50 characters)"}}).dump(), "application/json");
            return;
        }
        if (text.size() > 12000) text = text.substr(0, 12000);

        string proc = get_processing_dir();
        string jid = generate_job_id();

        string system_prompt = "You are an expert at creating hierarchical mind maps from content. "
            "Extract the main topic and subtopics, organizing them in a tree structure. "
            "Output ONLY valid JSON with: 'central' (main topic string), 'nodes' (array of {id, label, parent}). "
            "Use 'root' as parent for first-level nodes. Keep labels concise (max 5 words). "
            "Generate 10-20 nodes covering key concepts.";

        string user_prompt = "Create a mind map from this content:\n\n" + text +
            "\n\nOutput as JSON: {\"central\": \"Main Topic\", \"nodes\": [{\"id\": \"n1\", \"label\": \"Subtopic\", \"parent\": \"root\"}, ...]}";

        json payload = {
            {"model", "llama-3.3-70b-versatile"},
            {"messages", {
                {{"role", "system"}, {"content", system_prompt}},
                {{"role", "user"}, {"content", user_prompt}}
            }},
            {"temperature", 0.5},
            {"max_tokens", 2048}
        };

        auto gr = call_groq(payload, proc, jid + "_mm");

        json result;
        bool success = false;

        if (gr.ok) {
            try {
                string content = gr.response["choices"][0]["message"]["content"].get<string>();
                size_t start = content.find('{');
                size_t end = content.rfind('}');
                if (start != string::npos && end != string::npos) {
                    result = json::parse(content.substr(start, end - start + 1));
                    success = true;
                }
            } catch (...) {}
        }

        if (!success) {
            res.status = 500;
            res.set_content(json({{"error", "Failed to generate mind map"}}).dump(), "application/json");
            return;
        }

        result["model_used"] = gr.model_used;
        stat_record_ai_call("AI Mind Map", gr.model_used, gr.tokens_used, req.remote_addr);
        discord_log_ai_tool("AI Mind Map", "Text input", gr.model_used, gr.tokens_used, req.remote_addr, gr.tokens_remaining);
        res.set_content(result.dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════════════
    // YOUTUBE SUMMARY
    // ══════════════════════════════════════════════════════════════════════════════
    svr.Post("/api/youtube-summary", [](const httplib::Request& req, httplib::Response& res) {
        if (g_groq_key.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "AI features are not configured on this server."}}).dump(), "application/json");
            return;
        }

        if (g_ytdlp_path.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "yt-dlp is not available on this server."}}).dump(), "application/json");
            return;
        }

        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid JSON body"}}).dump(), "application/json");
            return;
        }

        string video_id = body.value("videoId", "");
        if (video_id.empty() || video_id.size() != 11) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid video ID"}}).dump(), "application/json");
            return;
        }
        // Validate characters: YouTube IDs are alphanumeric, dash, or underscore only.
        // A `"` or shell metacharacter would break exec_command's outer double-quote wrapper.
        if (!std::all_of(video_id.begin(), video_id.end(),
                [](char c){ return std::isalnum((unsigned char)c) || c == '-' || c == '_'; })) {
            res.status = 400;
            res.set_content(json({{"error", "Invalid video ID characters"}}).dump(), "application/json");
            return;
        }

        string proc = get_processing_dir();
        string jid = generate_job_id();
        string base_path = proc + "/" + jid;

        // Try to fetch transcript using yt-dlp - try both manual and auto subs
        string transcript_cmd = escape_arg(g_ytdlp_path) + " --skip-download "
                               "--write-subs --write-auto-subs --sub-langs en.*,en --convert-subs srt "
                               "-o " + escape_arg(base_path) + " "
                               "\"https://www.youtube.com/watch?v=" + video_id + "\" 2>&1";
        
        cout << "[Luma Tools] YouTube transcript: " << transcript_cmd << endl;
        int code;
        string cmd_output = exec_command(transcript_cmd, code);
        cout << "[Luma Tools] yt-dlp output: " << cmd_output << endl;
        
        string transcript_text;
        
        // Look for any .srt file that was created (could be .en.srt, .en-orig.srt, etc.)
        try {
            for (const auto& entry : fs::directory_iterator(proc)) {
                string filename = entry.path().filename().string();
                if (filename.find(jid) != string::npos && filename.find(".srt") != string::npos) {
                    cout << "[Luma Tools] Found subtitle file: " << filename << endl;
                    
                    // Parse SRT to extract just the text
                    ifstream srt(entry.path());
                    string line;
                    bool reading_text = false;
                    while (getline(srt, line)) {
                        // Remove carriage returns
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        
                        // Skip empty lines - reset state
                        if (line.empty()) {
                            reading_text = false;
                            continue;
                        }
                        // Skip line numbers (pure digits)
                        if (line.find_first_not_of("0123456789") == string::npos) {
                            continue;
                        }
                        // Skip timestamps
                        if (line.find("-->") != string::npos) {
                            reading_text = true;
                            continue;
                        }
                        // Collect text
                        if (reading_text) {
                            // Remove HTML tags like <font> etc
                            string clean;
                            bool in_tag = false;
                            for (char c : line) {
                                if (c == '<') in_tag = true;
                                else if (c == '>') in_tag = false;
                                else if (!in_tag) clean += c;
                            }
                            if (!clean.empty()) {
                                transcript_text += clean + " ";
                            }
                        }
                    }
                    srt.close();
                    try { fs::remove(entry.path()); } catch (...) {}
                    break; // Use first found subtitle file
                }
            }
        } catch (...) {}
        
        cout << "[Luma Tools] Transcript length: " << transcript_text.size() << endl;
        
        if (transcript_text.size() < 100) {
            res.status = 400;
            res.set_content(json({{"error", "Could not fetch video transcript. The video may not have captions enabled, or captions are not available in English."}}).dump(), "application/json");
            return;
        }

        // Truncate if too long
        if (transcript_text.size() > 15000) transcript_text = transcript_text.substr(0, 15000);

        string system_prompt = "You are an expert at summarizing video content. "
            "Create a clear, comprehensive summary of the lecture/video. "
            "Also extract 5-7 key points as bullet points. "
            "Output ONLY valid JSON with: 'title' (inferred title), 'summary' (2-3 paragraphs), 'keyPoints' (array of strings).";

        string user_prompt = "Summarize this video transcript:\n\n" + transcript_text +
            "\n\nOutput as JSON: {\"title\": \"...\", \"summary\": \"...\", \"keyPoints\": [\"...\", ...]}";

        json payload = {
            {"model", "llama-3.3-70b-versatile"},
            {"messages", {
                {{"role", "system"}, {"content", system_prompt}},
                {{"role", "user"}, {"content", user_prompt}}
            }},
            {"response_format", {{"type", "json_object"}}},
            {"temperature", 0.5},
            {"max_tokens", 2048}
        };

        auto gr = call_groq(payload, proc, jid + "_yt");

        json result;
        bool success = false;

        if (gr.ok) {
            try {
                string content = gr.response["choices"][0]["message"]["content"].get<string>();
                size_t start = content.find('{');
                size_t end = content.rfind('}');
                if (start != string::npos && end != string::npos) {
                    result = json::parse(content.substr(start, end - start + 1));
                    success = true;
                }
            } catch (...) {}
        }

        if (!success) {
            string err_msg = "Failed to summarize video. The AI service may be unavailable.";
            if (!gr.response.is_null() && gr.response.contains("error")) {
                if (gr.response["error"].is_object() && gr.response["error"].contains("message"))
                    err_msg = "AI API error: " + gr.response["error"]["message"].get<string>();
                else if (gr.response["error"].is_string())
                    err_msg = "AI API error: " + gr.response["error"].get<string>();
            }
            res.status = 500;
            res.set_content(json({{"error", err_msg}}).dump(), "application/json");
            return;
        }

        result["model_used"] = gr.model_used;
        stat_record_ai_call("YouTube Summary", gr.model_used, gr.tokens_used, req.remote_addr);
        discord_log_ai_tool("YouTube Summary", video_id, gr.model_used, gr.tokens_used, req.remote_addr, gr.tokens_remaining);
        res.set_content(result.dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════════════
    // ARCHIVE EXTRACTOR
    // Supported: ZIP, 7Z, RAR, RAR5, TAR, GZ, BZ2, XZ, TGZ, TBZ2, TXZ, LZMA, Z,
    //            CAB, ISO, LZH, ARJ, CHM, MSI, WIM, DMG, CPIO, DEB, RPM,
    //            APK, JAR, WAR, EAR, WHL, EGG, NUPKG, CRX, XPI, CBZ, CBR, APPX
    // ══════════════════════════════════════════════════════════════════════════════
    svr.Post("/api/tools/archive-extract", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        // Locate 7-Zip
        string sevenzip = find_executable("7z", {
            "C:\\Program Files\\7-Zip\\7z.exe",
            "C:\\Program Files (x86)\\7-Zip\\7z.exe",
            "/usr/bin/7z",
            "/usr/local/bin/7z"
        });
        if (sevenzip.empty()) {
            sevenzip = find_executable("7za", {"/usr/bin/7za", "/usr/local/bin/7za"});
        }
        if (sevenzip.empty()) {
            res.status = 503;
            res.set_content(json({{"error", "7-Zip is not installed on this server. Install 7-Zip to enable archive extraction."}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        string jid = generate_job_id();
        string proc = get_processing_dir();
        string input_path = save_upload(file, jid);
        string extract_dir = proc + "/" + jid + "_extracted";
        string output_zip  = proc + "/" + jid + "_extracted.zip";

        try { fs::create_directories(extract_dir); } catch (...) {}

        // Step 1: extract into extract_dir
        string extract_cmd = escape_arg(sevenzip) + " x " + escape_arg(input_path)
            + " -o" + escape_arg(extract_dir) + " -y";
        cout << "[Luma Tools] Archive extract: " << extract_cmd << endl;
        int rc; exec_command(extract_cmd, rc);

        // Verify something was extracted
        bool has_files = false;
        uintmax_t file_count = 0;
        try {
            for (auto& e : fs::recursive_directory_iterator(extract_dir)) {
                if (fs::is_regular_file(e)) { has_files = true; ++file_count; }
            }
        } catch (...) {}

        // Sanity-check extracted file count to prevent zip-bomb DoS
        constexpr uintmax_t MAX_EXTRACT_FILES = 2000;
        if (file_count > MAX_EXTRACT_FILES) {
            try { fs::remove(input_path); fs::remove_all(extract_dir); } catch (...) {}
            res.status = 400;
            res.set_content(json({{"error", "Archive contains too many files (max 2000). Use a smaller archive."}}).dump(), "application/json");
            return;
        }

        if (!has_files) {
            try { fs::remove(input_path); fs::remove_all(extract_dir); } catch (...) {}
            res.status = 500;
            res.set_content(json({{"error", "Extraction failed \u2014 archive may be corrupt, password-protected, or unsupported."}}).dump(), "application/json");
            return;
        }

        // Step 2: re-zip the extracted contents with paths relative to extract_dir.
        // Use a wildcard path (extract_dir/*) so 7-Zip treats extract_dir as the
        // archive root, avoiding compound cd&&7z which breaks exec_command's quoting.
#ifdef _WIN32
        string wildcard_path = extract_dir + "\\*";
#else
        string wildcard_path = extract_dir + "/*";
#endif
        string zip_cmd = escape_arg(sevenzip) + " a -tzip " + escape_arg(output_zip)
            + " " + escape_arg(wildcard_path) + " -r";
        cout << "[Luma Tools] Archive rezip: " << zip_cmd << endl;
        exec_command(zip_cmd, rc);

        if (fs::exists(output_zip) && fs::file_size(output_zip) > 0) {
            discord_log_tool("Archive Extract",
                file.filename + " (" + to_string(file_count) + " files extracted)",
                req.remote_addr);
            string out_name = fs::path(file.filename).stem().string() + "_extracted.zip";
            send_file_response(res, output_zip, out_name);
        } else {
            discord_log_error("Archive Extract", "Re-zip failed for: " + mask_filename(file.filename));
            res.status = 500;
            res.set_content(json({{"error", "Extraction succeeded but packaging output failed."}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove_all(extract_dir); fs::remove(output_zip); } catch (...) {}
    });
}
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

void register_tool_routes(httplib::Server& svr, string dl_dir) {

    // ── POST /api/tools/image-compress ──────────────────────────────────────
    svr.Post("/api/tools/image-compress", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(json({{"error", "No file uploaded"}}).dump(), "application/json");
            return;
        }

        auto file = req.get_file_value("file");
        int quality = 75;

        if (req.has_file("quality")) {
            try { quality = std::stoi(req.get_file_value("quality").content); } catch (...) {}
        }

        discord_log_tool("Image Compress", file.filename, req.remote_addr);

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
            string out_name = fs::path(file.filename).stem().string() + "_compressed" + ext;
            send_file_response(res, output_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("Image Compress", "Failed for: " + mask_filename(file.filename));
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
        string width = req.has_file("width") ? req.get_file_value("width").content : "";
        string height = req.has_file("height") ? req.get_file_value("height").content : "";

        if (width.empty() && height.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "Width or height required"}}).dump(), "application/json");
            return;
        }

        discord_log_tool("Image Resize", file.filename, req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_out" + ext;

        string sw = width.empty() ? "-1" : width;
        string sh = height.empty() ? "-1" : height;
        string filter = "scale=" + sw + ":" + sh;

        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " -vf " + escape_arg(filter) + " " + escape_arg(output_path);
        cout << "[Luma Tools] Image resize: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string out_name = fs::path(file.filename).stem().string() + "_resized" + ext;
            send_file_response(res, output_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("Image Resize", "Failed for: " + mask_filename(file.filename));
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

        discord_log_tool("Image Convert", file.filename + " -> " + format, req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string out_ext = "." + format;
        string output_path = get_processing_dir() + "/" + jid + "_out" + out_ext;

        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path) + " " + escape_arg(output_path);
        cout << "[Luma Tools] Image convert: " << cmd << endl;
        int code;
        exec_command(cmd, code);

        if (fs::exists(output_path) && fs::file_size(output_path) > 0) {
            string out_name = fs::path(file.filename).stem().string() + out_ext;
            send_file_response(res, output_path, out_name);
        } else {
            res.status = 500;
            discord_log_error("Image Convert", "Failed for: " + mask_filename(file.filename));
            res.set_content(json({{"error", "Image conversion failed"}}).dump(), "application/json");
        }

        try { fs::remove(input_path); fs::remove(output_path); } catch (...) {}
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
        string dpi = req.has_file("dpi") ? req.get_file_value("dpi").content : "200";

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
        string x_s = req.has_file("x") ? req.get_file_value("x").content : "0";
        string y_s = req.has_file("y") ? req.get_file_value("y").content : "0";
        string w_s = req.has_file("w") ? req.get_file_value("w").content : "";
        string h_s = req.has_file("h") ? req.get_file_value("h").content : "";

        if (w_s.empty() || h_s.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "Crop dimensions required"}}).dump(), "application/json");
            return;
        }

        discord_log_tool("Image Crop", file.filename, req.remote_addr);

        string jid = generate_job_id();
        string input_path = save_upload(file, jid);
        string ext = fs::path(file.filename).extension().string();
        string output_path = get_processing_dir() + "/" + jid + "_cropped" + ext;

        // ffmpeg crop filter: crop=w:h:x:y
        string cmd = ffmpeg_cmd() + " -y -i " + escape_arg(input_path)
            + " -vf crop=" + w_s + ":" + h_s + ":" + x_s + ":" + y_s
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
        string method = "auto";

        if (req.has_file("method")) method = req.get_file_value("method").content;

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
}

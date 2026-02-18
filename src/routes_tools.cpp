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

        discord_log_tool("Image Compress", file.filename);

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
            discord_log_error("Image Compress", "Failed for: " + file.filename);
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

        discord_log_tool("Image Resize", file.filename);

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
            discord_log_error("Image Resize", "Failed for: " + file.filename);
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

        discord_log_tool("Image Convert", file.filename + " -> " + format);

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
            discord_log_error("Image Convert", "Failed for: " + file.filename);
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

        discord_log_tool("Audio Convert", file.filename + " -> " + format);

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
            discord_log_error("Audio Convert", "Failed for: " + file.filename);
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

        discord_log_tool("Video Compress", file.filename + " (" + preset + ")");

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
                discord_log_error("Video Compress", "Failed for: " + orig_name);
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

        discord_log_tool("Video Trim (" + mode + ")", file.filename + " [" + start + " -> " + end + "]");

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
                discord_log_error("Video Trim", "Failed for: " + orig_name);
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

        discord_log_tool("Video Convert", file.filename + " -> " + format);

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
                discord_log_error("Video Convert", "Failed for: " + orig_name);
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

        discord_log_tool("Extract Audio", file.filename + " -> " + format);

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
                discord_log_error("Extract Audio", "Failed for: " + orig_name);
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

        discord_log_tool("PDF Compress", file.filename + " (" + level + ")");

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
            discord_log_error("PDF Compress", "Failed for: " + file.filename);
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

        discord_log_tool("PDF Merge", to_string(count_val) + " files");

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

        discord_log_tool("PDF to Images", file.filename + " (" + format + ", " + dpi + " DPI)");

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
            discord_log_error("PDF to Images", "Failed for: " + file.filename);
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
}

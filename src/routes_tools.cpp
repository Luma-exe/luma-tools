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
        if (source_text.size() > 6000) source_text = source_text.substr(0, 6000);
        if (notes.size() > 6000) notes = notes.substr(0, 6000);

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
Return 8-15 key concepts. Be thorough but fair in your assessment.)";

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

        string payload_file = proc + "/" + rid + "_coverage_payload.json";
        { ofstream f(payload_file); f << payload.dump(); }

        string hdr_file = proc + "/" + rid + "_auth.txt";
        { ofstream f(hdr_file); f << "Authorization: Bearer " << g_groq_key; }

        string resp_file = proc + "/" + rid + "_coverage_resp.json";
        string curl_cmd = "curl -s -X POST https://api.groq.com/openai/v1/chat/completions"
            " -H \"Content-Type: application/json\""
            " -H @" + escape_arg(hdr_file) +
            " -d @" + escape_arg(payload_file) +
            " -o " + escape_arg(resp_file);
        int code; exec_command(curl_cmd, code);

        json result;
        bool ok = false;
        if (fs::exists(resp_file) && fs::file_size(resp_file) > 0) {
            try {
                ifstream f(resp_file); std::ostringstream ss; ss << f.rdbuf();
                auto resp_json = json::parse(ss.str());
                if (resp_json.contains("choices") && resp_json["choices"].is_array() && !resp_json["choices"].empty()) {
                    string content = resp_json["choices"][0]["message"]["content"].get<string>();
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
                    ok = true;
                } else if (resp_json.contains("error")) {
                    result = {{"error", resp_json["error"].value("message", "AI API error")}};
                }
            } catch (const std::exception& e) {
                result = {{"error", string("Failed to parse AI response: ") + e.what()}};
            }
        } else {
            result = {{"error", "AI API call failed"}};
        }

        // Cleanup temp files
        try { fs::remove(payload_file); fs::remove(hdr_file); fs::remove(resp_file); } catch (...) {}

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

        if (wm_text.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "Watermark text is required"}}).dump(), "application/json");
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
        // Convert color name to RRGGBBAA for drawtext
        string color_str = wm_color + "@" + wm_opacity;

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
        bool has_file = req.has_file("file") && !req.get_file_value("file").content.empty();
        
        if (!has_text && !has_file) {
            res.status = 400;
            res.set_content(json({{"error", "No content provided. Upload a file or paste text."}}).dump(), "application/json");
            return;
        }
        
        string format = req.has_file("format") ? req.get_file_value("format").content : "markdown";
        string jid = generate_job_id();
        string proc = get_processing_dir();
        
        // Variables for the thread
        string input_text;
        string filename;
        string input_path;
        string file_ext;
        
        if (has_text) {
            // Pasted text mode
            input_text = req.get_file_value("text").content;
            filename = "pasted_text";
            discord_log_tool("AI Study Notes", "Pasted Text (" + format + ")", req.remote_addr);
        } else {
            // File upload mode
            auto file = req.get_file_value("file");
            filename = file.filename;
            discord_log_tool("AI Study Notes", filename + " (" + format + ")", req.remote_addr);
            
            // Detect file extension
            fs::path fp(filename);
            file_ext = fp.extension().string();
            std::transform(file_ext.begin(), file_ext.end(), file_ext.begin(), ::tolower);
            
            input_path = proc + "/" + jid + "_input" + file_ext;
            { ofstream f(input_path, std::ios::binary); f.write(file.content.data(), file.content.size()); }
        }

        update_job(jid, {{"status", "processing"}, {"progress", 10}, {"stage", has_text ? "Processing pasted text..." : "Extracting text from file..."}});

        thread([jid, input_text, input_path, file_ext, format, proc, has_text, filename]() {
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

            // Truncate to ~12 000 chars to stay within token limits
            if (text.size() > 12000) text = text.substr(0, 12000) + "\n\n[... truncated ...]";

            // Store raw text in memory so the client can fetch it for comparison
            update_job_raw_text(jid, text);

            update_job(jid, {{"status","processing"},{"progress",40},{"stage","Generating study notes with AI..."}});

            // ── Step 2: call Groq ────────────────────────────────────────────
            string style_instruction = (format == "markdown")
                ? "Format the output using Markdown optimised for Obsidian: use ## headings (no numbering), bullet points for details, **bold** key terms, `code blocks` for all formulas/equations/code/worked solutions with complete variable definitions and units, and > blockquotes for instructor emphasis or exam hints."
                : "Write clear, readable plain text notes with UPPERCASE section labels and dash bullet points.";

            string system_prompt = "You are an expert academic tutor and note-taker. Extract and organise all crucial information from the provided university lecture content. "
                "Structure notes with clear headings and bullet points. Preserve all formulas, equations, code examples, and worked solutions in full detail. "
                "Include: key concepts with definitions, practical applications, real-world examples, step-by-step procedures, exam hints, and connections to other topics. "
                "Ensure the notes are comprehensive enough that someone who has not attended the lecture can fully understand the material, "
                "work through problems independently, and use the notes for study and exam review. "
                "Maintain logical flow from basic concepts to applications. Include all supporting information: references, tools mentioned, and topic connections. "
                + style_instruction;

            string user_prompt = "Please create detailed, comprehensive study notes from the following lecture content. "
                "Extract every important concept, formula, example, and procedure — do not summarise or skip details:\n\n" + text;

            json payload = {
                {"model", "llama-3.3-70b-versatile"},
                {"messages", json::array({
                    {{"role","system"}, {"content", system_prompt}},
                    {{"role","user"},   {"content", user_prompt}}
                })},
                {"max_tokens", 2048},
                {"temperature", 0.4}
            };

            string payload_str = payload.dump();
            string payload_file = proc + "/" + jid + "_payload.json";
            { ofstream f(payload_file); f << payload_str; }

            // Write auth header to a separate file to avoid shell-escaping issues
            string hdr_file = proc + "/" + jid + "_auth.txt";
            { ofstream f(hdr_file); f << "Authorization: Bearer " << g_groq_key; }

            string resp_file = proc + "/" + jid + "_resp.json";
            string curl_cmd = "curl -s -X POST https://api.groq.com/openai/v1/chat/completions"
                " -H \"Content-Type: application/json\""
                " -H @" + escape_arg(hdr_file) +
                " -d @" + escape_arg(payload_file) +
                " -o " + escape_arg(resp_file);
            int code; exec_command(curl_cmd, code);

            string notes;
            bool ai_ok = false;
            if (fs::exists(resp_file) && fs::file_size(resp_file) > 0) {
                try {
                    ifstream f(resp_file); std::ostringstream ss; ss << f.rdbuf();
                    auto resp_json = json::parse(ss.str());
                    if (resp_json.contains("choices") && resp_json["choices"].is_array()
                            && !resp_json["choices"].empty()) {
                        notes = resp_json["choices"][0]["message"]["content"].get<string>();
                        ai_ok = true;
                    } else if (resp_json.contains("error")) {
                        string msg = resp_json["error"].value("message", "OpenAI API error");
                        update_job(jid, {{"status","error"},{"error", msg}});
                        if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
                        try { fs::remove(txt_path);
                              fs::remove(payload_file); fs::remove(resp_file); fs::remove(hdr_file); } catch (...) {}
                        return;
                    }
                } catch (...) {}
            }

            if (!ai_ok) {
                update_job(jid, {{"status","error"},{"error","AI API call failed. Check server logs and Groq key."}});
                if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
                try { fs::remove(txt_path);
                      fs::remove(payload_file); fs::remove(resp_file); fs::remove(hdr_file); } catch (...) {}
                return;
            }

            // ── Step 3: write output file ─────────────────────────────────────
            string ext = (format == "markdown") ? ".md" : ".txt";
            string out_path = proc + "/" + jid + "_notes" + ext;
            { ofstream f(out_path); f << notes; }

            update_job(jid, {{"status","completed"},{"progress",100},{"filename","study_notes" + ext}}, out_path);
            if (!input_path.empty()) try { fs::remove(input_path); } catch (...) {}
            try { fs::remove(txt_path);
                  fs::remove(payload_file); fs::remove(resp_file); fs::remove(hdr_file); } catch (...) {}
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
        string api_key = get_groq_api_key();
        if (api_key.empty()) {
            res.status = 500;
            res.set_content(json({{"error", "Groq API key not configured"}}).dump(), "application/json");
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

        // Parse feedback
        json feedback;
        try {
            feedback = json::parse(feedback_json);
        } catch (...) {
            feedback = json::object();
        }
        
        auto gaps = feedback.value("gaps", json::array());
        auto tips = feedback.value("studyTips", json::array());
        auto missed = feedback.value("missed", json::array());

        // Build the improvement prompt
        string gaps_list, tips_list, missed_list;
        for (const auto& g : gaps) gaps_list += "- " + g.get<string>() + "\n";
        for (const auto& t : tips) tips_list += "- " + t.get<string>() + "\n";
        for (const auto& m : missed) missed_list += "- " + m.get<string>() + "\n";

        string system_prompt = R"(You are an expert study notes editor. Your task is to improve the user's study notes based on specific feedback. 

IMPORTANT RULES:
1. Keep the same overall structure and format (Markdown)
2. ADD content for missing topics - don't just mention them, actually explain them
3. Expand on weak areas with more detail and examples
4. Maintain the user's writing style
5. Do NOT remove existing good content
6. Output ONLY the improved notes - no explanations or meta-commentary)";

        string user_prompt = "Here are my current study notes:\n\n---\n" + current_notes + "\n---\n\n";
        
        if (!missed_list.empty()) {
            user_prompt += "MISSING TOPICS (add these with proper explanations):\n" + missed_list + "\n";
        }
        if (!gaps_list.empty()) {
            user_prompt += "AREAS TO IMPROVE:\n" + gaps_list + "\n";
        }
        if (!tips_list.empty()) {
            user_prompt += "SUGGESTIONS TO INCORPORATE:\n" + tips_list + "\n";
        }
        
        user_prompt += "\nPlease improve my notes by addressing the above feedback. Output only the improved notes in Markdown format.";

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

        string proc = g_downloads_dir + "/processing";
        if (!fs::exists(proc)) fs::create_directories(proc);
        
        string payload_file = proc + "/improve_" + job_id + "_payload.json";
        string resp_file = proc + "/improve_" + job_id + "_resp.json";
        string hdr_file = proc + "/improve_" + job_id + "_hdr.txt";
        
        { ofstream f(payload_file); f << payload.dump(); }

        string curl_cmd = "curl -s -X POST \"https://api.groq.com/openai/v1/chat/completions\" "
                          "-H \"Authorization: Bearer " + api_key + "\" "
                          "-H \"Content-Type: application/json\" "
                          "-d @" + escape_arg(payload_file) + " "
                          "-D " + escape_arg(hdr_file) + " "
                          "-o " + escape_arg(resp_file);

        int rc = system(curl_cmd.c_str());
        
        string improved_notes;
        bool success = false;
        
        if (rc == 0 && fs::exists(resp_file)) {
            try {
                ifstream f(resp_file);
                std::ostringstream ss;
                ss << f.rdbuf();
                auto resp_json = json::parse(ss.str());
                
                if (resp_json.contains("choices") && resp_json["choices"].is_array() && !resp_json["choices"].empty()) {
                    improved_notes = resp_json["choices"][0]["message"]["content"].get<string>();
                    success = true;
                } else if (resp_json.contains("error")) {
                    string msg = resp_json["error"].value("message", "AI API error");
                    res.status = 500;
                    res.set_content(json({{"error", msg}}).dump(), "application/json");
                    try { fs::remove(payload_file); fs::remove(resp_file); fs::remove(hdr_file); } catch (...) {}
                    return;
                }
            } catch (...) {}
        }

        // Cleanup temp files
        try { fs::remove(payload_file); fs::remove(resp_file); fs::remove(hdr_file); } catch (...) {}

        if (!success) {
            res.status = 500;
            res.set_content(json({{"error", "Failed to improve notes"}}).dump(), "application/json");
            return;
        }

        res.set_content(json({{"improved_notes", improved_notes}}).dump(), "application/json");
    });

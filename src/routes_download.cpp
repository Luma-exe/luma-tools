/**
 * Luma Tools — Download route handlers
 * /api/detect, /api/analyze, /api/download, /api/resolve-title, /api/status, /api/health
 */

#include "common.h"
#include "discord.h"
#include "routes.h"

void register_download_routes(httplib::Server& svr, string dl_dir) {

    // ── POST /api/detect — detect platform from URL ─────────────────────────
    svr.Post("/api/detect", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            string url = body.value("url", "");

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
            string url = body.value("url", "");

            if (url.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "URL is required"}}).dump(), "application/json");
                return;
            }

            auto platform = detect_platform(url);
            json platform_json = {
                {"id", platform.id}, {"name", platform.name},
                {"icon", platform.icon}, {"color", platform.color},
                {"supports_video", platform.supports_video},
                {"supports_audio", platform.supports_audio}
            };

            // ── Check if it's a playlist first ──────────────────────────────
            // Skip the probe entirely for URLs that are clearly single videos —
            // no list= param, no /playlist/, /sets/, /channel/ path segment.
            // This saves a full yt-dlp cold-start (~1-3s) for the common case.
            auto is_obvious_single = [&](const string& u) -> bool {
                if (u.find("list=")     != string::npos) return false;
                if (u.find("/playlist") != string::npos) return false;
                if (u.find("/sets/")    != string::npos) return false;
                if (u.find("/channel/") != string::npos) return false;
                if (u.find("/user/")    != string::npos) return false;
                if (u.find("/c/")       != string::npos) return false;
                return true;
            };

            bool is_playlist = false;
            if (!is_obvious_single(url)) {
                string probe_cmd = build_ytdlp_cmd() + " --flat-playlist --dump-single-json --no-warnings " + escape_arg(url);
                int probe_code;
                string probe_output = exec_command(probe_cmd, probe_code);

                auto json_start = probe_output.find('{');

                if (json_start != string::npos) {
                    try {
                        json probe = json::parse(probe_output.substr(json_start));
                        string ptype = json_str(probe, "_type");

                        if ((ptype == "playlist" || ptype == "multi_video") &&
                            probe.contains("entries") && probe["entries"].is_array() && probe["entries"].size() > 1) {
                            is_playlist = true;

                            json items = json::array();
                            int index = 0;

                            for (const auto& entry : probe["entries"]) {
                                index++;
                                string item_url = json_str(entry, "url");

                                if (item_url.empty()) item_url = json_str(entry, "webpage_url");
                                if (!item_url.empty() && item_url.find("http") != 0) {
                                    string extractor = json_str(entry, "ie_key", json_str(entry, "extractor"));
                                    string vid_id = item_url;

                                    if (extractor == "Youtube" || extractor == "youtube") {
                                        item_url = "https://www.youtube.com/watch?v=" + vid_id;
                                    } else {
                                        string wp = json_str(entry, "webpage_url");

                                        if (!wp.empty()) item_url = wp;
                                    }
                                }

                                string raw_title = json_str(entry, "title", "");
                                string title = sanitize_utf8(raw_title);

                                while (!title.empty() && (title.front() == '_' || title.front() == ' ')) title.erase(title.begin());
                                while (!title.empty() && (title.back() == '_' || title.back() == ' ')) title.pop_back();

                                if (title.empty() && !item_url.empty()) {
                                    string slug = item_url;
                                    auto qpos = slug.find('?');

                                    if (qpos != string::npos) slug = slug.substr(0, qpos);
                                    auto spos = slug.rfind('/');

                                    if (spos != string::npos) slug = slug.substr(spos + 1);
                                    bool all_digits = !slug.empty() && std::all_of(slug.begin(), slug.end(), ::isdigit);

                                    if (!all_digits && !slug.empty()) {
                                        string readable;
                                        bool cap_next = true;

                                        for (char c : slug) {
                                            if (c == '-' || c == '_') { readable += ' '; cap_next = true; }
                                            else if (cap_next && std::isalpha(c)) { readable += (char)std::toupper(c); cap_next = false; }
                                            else { readable += c; cap_next = false; }
                                        }

                                        while (!readable.empty() && readable.back() == ' ') readable.pop_back();
                                        if (!readable.empty()) title = readable;
                                    }
                                }

                                if (title.empty()) title = "Track " + to_string(index);

                                items.push_back({
                                    {"index", index - 1}, {"title", title}, {"url", item_url},
                                    {"duration", json_num(entry, "duration", 0)},
                                    {"thumbnail", json_str(entry, "thumbnail", "")},
                                    {"uploader", sanitize_utf8(json_str(entry, "uploader", json_str(entry, "channel", "")))},
                                });
                            }

                            json response = {
                                {"type", "playlist"},
                                {"title", sanitize_utf8(json_str(probe, "title", "Playlist"))},
                                {"uploader", sanitize_utf8(json_str(probe, "uploader", json_str(probe, "channel", "Unknown")))},
                                {"thumbnail", json_str(probe, "thumbnail", "")},
                                {"item_count", (int)items.size()},
                                {"items", items},
                                {"platform", platform_json}
                            };

                            cout << "[Luma Tools] Playlist detected: " << items.size() << " items" << endl;
                            res.set_content(response.dump(), "application/json");
                            return;
                        }
                    } catch (const json::exception& e) {
                        cout << "[Luma Tools] Playlist probe parse failed: " << e.what() << endl;
                    }
                }
            }

            // ── Single item analysis ────────────────────────────────────────
            string cmd = build_ytdlp_cmd() + " --dump-json --no-warnings --no-playlist " + escape_arg(url);
            int exit_code;
            string output = exec_command(cmd, exit_code);

            if (output.empty() || output[0] != '{') {
                auto pos = output.find('{');

                if (pos != string::npos) {
                    output = output.substr(pos);
                } else {
                    string error_msg = "Failed to analyze URL";
                    auto err_pos = output.find("ERROR:");

                    if (err_pos != string::npos) {
                        error_msg = output.substr(err_pos);
                        auto nl = error_msg.find('\n');

                        if (nl != string::npos) error_msg = error_msg.substr(0, nl);
                        error_msg.erase(std::remove(error_msg.begin(), error_msg.end(), '\r'), error_msg.end());
                    } else if (output.find("not recognized") != string::npos || output.find("not found") != string::npos) {
                        error_msg = "yt-dlp is not installed or not on PATH";
                    }

                    res.status = 500;
                    res.set_content(json({{"error", error_msg}, {"details", output}}).dump(), "application/json");
                    return;
                }
            }

            auto end_pos = output.find("}\n{");

            if (end_pos != string::npos) output = output.substr(0, end_pos + 1);

            json info = json::parse(output);

            json formats = json::array();
            set<string> seen_qualities;

            if (info.contains("formats") && info["formats"].is_array()) {
                for (const auto& fmt : info["formats"]) {
                    string format_id = json_str(fmt, "format_id");
                    string ext = json_str(fmt, "ext");
                    int height = json_num(fmt, "height", 0);
                    double filesize = json_num(fmt, "filesize", 0.0);
                    double filesize_approx = json_num(fmt, "filesize_approx", 0.0);
                    string vcodec = json_str(fmt, "vcodec", "none");
                    string acodec = json_str(fmt, "acodec", "none");
                    double tbr = json_num(fmt, "tbr", 0.0);

                    bool has_video = vcodec != "none" && !vcodec.empty();
                    bool has_audio = acodec != "none" && !acodec.empty();

                    if (has_video && height > 0) {
                        string quality = to_string(height) + "p";

                        if (seen_qualities.count(quality)) continue;
                        seen_qualities.insert(quality);

                        formats.push_back({
                            {"format_id", format_id}, {"ext", ext}, {"height", height},
                            {"quality", quality}, {"has_video", true}, {"has_audio", has_audio},
                            {"filesize", filesize > 0 ? filesize : filesize_approx}, {"tbr", tbr}
                        });
                    }
                }

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
            res.set_content(json({{"error", string("JSON parse error: ") + e.what()}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ── POST /api/download — start downloading media ────────────────────────
    svr.Post("/api/download", [dl_dir](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            string url = body.value("url", "");
            string format = body.value("format", "mp3");
            string quality = body.value("quality", "best");

            if (url.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "URL is required"}}).dump(), "application/json");
                return;
            }

            // Per-device concurrency limit
            string client_ip = req.remote_addr;

            if (req.has_header("X-Forwarded-For")) {
                client_ip = req.get_header_value("X-Forwarded-For");
                auto comma = client_ip.find(',');

                if (comma != string::npos) client_ip = client_ip.substr(0, comma);
                client_ip.erase(0, client_ip.find_first_not_of(" "));
                client_ip.erase(client_ip.find_last_not_of(" ") + 1);
            }

            if (client_ip == "::1") client_ip = "127.0.0.1";

            if (has_active_download(client_ip)) {
                res.status = 429;
                res.set_content(json({
                    {"error", "You already have an active download. Please wait for it to finish."},
                    {"code", "RATE_LIMITED"}
                }).dump(), "application/json");
                return;
            }

            string title = body.value("title", "download");
            string download_id = generate_download_id();
            string out_template = dl_dir + "/" + download_id + ".%(ext)s";

            register_active_download(client_ip, download_id);

            // Discord log
            auto plat = detect_platform(url);
            discord_log_download(title, plat.name, format, client_ip);

            update_download_status(download_id, {
                {"status", "starting"}, {"progress", 0},
                {"eta", nullptr}, {"speed", ""}, {"filesize", ""}
            });

            // Build yt-dlp command
            // --concurrent-fragments 4 : download 4 DASH/HLS segments in parallel
            // --buffer-size 1M         : 1 MB read buffer, fewer syscalls per second
            // --no-mtime               : skip setting file modification time at end
            string cmd = build_ytdlp_cmd() + " --no-warnings --newline --progress --no-playlist "
                         "--concurrent-fragments 4 --buffer-size 1M --no-mtime ";

            if (format == "mp3") {
                cmd += "-x --audio-format mp3 --audio-quality 0 ";
            } else if (format == "mp4") {
                // -c:v copy -c:a copy: stream-copy both streams — no re-encoding at all.
                // ffmpeg just wraps the existing compressed data into an mp4 container,
                // which takes ~1-2s regardless of video length vs 30-60s for audio transcode.
                // Format selector prefers mp4 video + m4a audio first (already AAC, so copy
                // is guaranteed lossless), then falls back to any best streams.
                string mp4_merge = "--merge-output-format mp4 --postprocessor-args \"ffmpeg:-c:v copy -c:a copy\" ";

                if (quality == "best") {
                    cmd += "-f \"bv*[ext=mp4]+ba[ext=m4a]/bv*+ba/b\" " + mp4_merge;
                } else {
                    string height = quality;
                    height.erase(std::remove(height.begin(), height.end(), 'p'), height.end());
                    // Validate height is digits-only to prevent yt-dlp filter injection
                    if (height.empty() || !std::all_of(height.begin(), height.end(), ::isdigit)) {
                        unregister_active_download(client_ip);
                        res.status = 400;
                        res.set_content(json({{"error","Invalid quality parameter"}}).dump(), "application/json");
                        return;
                    }
                    cmd += "-f \"bv*[ext=mp4][height<=" + height + "]+ba[ext=m4a]/bv*[height<=" + height + "]+ba/b[height<=" + height + "]\" " + mp4_merge;
                }
            }

            cmd += "-o " + escape_arg(out_template) + " " + escape_arg(url);

            cout << "[Luma Tools] Download cmd: " << cmd << endl;

            thread([cmd, download_id, dl_dir, client_ip, title, format]() {
                update_download_status(download_id, {
                    {"status", "downloading"}, {"progress", 0},
                    {"eta", nullptr}, {"speed", ""}, {"filesize", ""}
                });

                // For MP4 downloads yt-dlp fetches video and audio as separate streams.
                // Track how many streams have started so we can scale the combined progress:
                // stream 1 maps to 0-50%, stream 2 maps to 50-100%, avoiding regressions.
                bool two_streams_expected = (format == "mp4");
                int stream_count = 0;
                // Server-side monotonic cap: never emit a lower progress than already sent.
                double last_sent_pct = 0.0;

                string full_output;
                array<char, 4096> buffer;

#ifdef _WIN32
                string full_cmd = "\"" + cmd + " 2>&1\"";
                unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(full_cmd.c_str(), "r"), _pclose);
#else
                string full_cmd = cmd + " 2>&1";
                unique_ptr<FILE, decltype(&pclose)> pipe(popen(full_cmd.c_str(), "r"), pclose);
#endif

                if (pipe) {
                    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
                        string line(buffer.data());
                        full_output += line;

                        // Each "[download] Destination:" line marks the start of a new stream.
                        if (line.find("[download] Destination:") != string::npos) {
                            stream_count++;
                        }

                        if (line.find("[download]") != string::npos && line.find('%') != string::npos) {
                            double pct = 0;
                            string speed_str, size_str;

                            auto pct_pos = line.find('%');

                            if (pct_pos != string::npos) {
                                auto start = line.rfind(' ', pct_pos);

                                if (start == string::npos) start = line.rfind(']', pct_pos);
                                if (start != string::npos) {
                                    try { pct = std::stod(line.substr(start + 1, pct_pos - start - 1)); } catch (...) {}
                                }
                            }

                            auto of_pos = line.find("of");

                            if (of_pos != string::npos) {
                                auto at_pos = line.find(" at ", of_pos);

                                if (at_pos != string::npos) {
                                    size_str = line.substr(of_pos + 2, at_pos - of_pos - 2);
                                    size_str.erase(0, size_str.find_first_not_of(" ~"));
                                    size_str.erase(size_str.find_last_not_of(" \r\n") + 1);
                                }
                            }

                            auto at_pos = line.find(" at ");

                            if (at_pos != string::npos) {
                                auto eta_pos = line.find(" ETA ", at_pos);

                                if (eta_pos != string::npos) speed_str = line.substr(at_pos + 4, eta_pos - at_pos - 4);
                                else speed_str = line.substr(at_pos + 4);
                                speed_str.erase(0, speed_str.find_first_not_of(" "));
                                speed_str.erase(speed_str.find_last_not_of(" \r\n") + 1);
                            }

                            auto eta_pos = line.find("ETA ");
                            int eta_seconds = -1;

                            if (eta_pos != string::npos) {
                                string eta_str = line.substr(eta_pos + 4);
                                eta_str.erase(eta_str.find_last_not_of(" \r\n") + 1);
                                int parts[3] = {0, 0, 0};
                                int n = 0;
                                istringstream iss(eta_str);
                                string tok;

                                while (std::getline(iss, tok, ':') && n < 3) {
                                    try { parts[n++] = std::stoi(tok); } catch (...) {}
                                }

                                if (n == 2) eta_seconds = parts[0] * 60 + parts[1];
                                else if (n == 3) eta_seconds = parts[0] * 3600 + parts[1] * 60 + parts[2];
                            }

                            // Scale progress to avoid regressions when yt-dlp downloads
                            // video and audio as two separate streams for MP4.
                            double display_pct = pct;
                            if (two_streams_expected) {
                                display_pct = (stream_count <= 1) ? (pct / 2.0) : (50.0 + pct / 2.0);
                            }
                            // Monotonic cap: never send a lower value than previously sent.
                            display_pct = std::max(display_pct, last_sent_pct);
                            last_sent_pct = display_pct;

                            json st = {
                                {"status", "downloading"}, {"progress", display_pct},
                                {"speed", sanitize_utf8(speed_str)}, {"filesize", sanitize_utf8(size_str)}
                            };
                            if (eta_seconds >= 0) st["eta"] = eta_seconds;
                            else st["eta"] = nullptr;
                            update_download_status(download_id, st);
                        }
                        else if (line.find("[ExtractAudio]") != string::npos) {
                            last_sent_pct = std::max(last_sent_pct, 95.0);
                            update_download_status(download_id, {
                                {"status", "processing"}, {"progress", last_sent_pct},
                                {"eta", nullptr}, {"speed", ""}, {"filesize", ""},
                                {"processing_msg", "Converting audio..."}
                            });
                        }
                        else if (line.find("[Merger]") != string::npos) {
                            last_sent_pct = std::max(last_sent_pct, 95.0);
                            update_download_status(download_id, {
                                {"status", "processing"}, {"progress", last_sent_pct},
                                {"eta", nullptr}, {"speed", ""}, {"filesize", ""},
                                {"processing_msg", "Merging video & audio..."}
                            });
                        }
                        else if (line.find("[ffmpeg]") != string::npos) {
                            last_sent_pct = std::max(last_sent_pct, 95.0);
                            update_download_status(download_id, {
                                {"status", "processing"}, {"progress", last_sent_pct},
                                {"eta", nullptr}, {"speed", ""}, {"filesize", ""},
                                {"processing_msg", "Processing..."}
                            });
                        }
                    }
                }

                // Find & rename the downloaded file
                string found_file;
                try {
                    for (const auto& entry : fs::directory_iterator(dl_dir)) {
                        string filename;
                        try {
#ifdef _WIN32
                            filename = entry.path().filename().u8string();
#else
                            filename = entry.path().filename().string();
#endif
                        } catch (...) { continue; }

                        if (filename.rfind(download_id, 0) == 0) {
                            auto dot_pos = filename.rfind('.');
                            string ext = (dot_pos != string::npos) ? filename.substr(dot_pos) : "";
                            string clean_name = clean_filename(title) + "_LumaTools" + ext;

                            fs::path target = fs::path(dl_dir) / clean_name;

                            if (fs::exists(target)) { try { fs::remove(target); } catch (...) {} }

                            try {
                                fs::rename(entry.path(), target);
                                found_file = clean_name;
                                cout << "[Luma Tools] Renamed to: " << clean_name << endl;
                            } catch (const std::exception& rename_err) {
                                cerr << "[Luma Tools] Rename failed: " << rename_err.what() << endl;
                                found_file = sanitize_utf8(filename);

                                if (found_file != filename) {
                                    try { fs::rename(entry.path(), fs::path(dl_dir) / found_file); } catch (...) {}
                                }
                            }

                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    cerr << "[Luma Tools] Error scanning downloads: " << e.what() << endl;
                }

                unregister_active_download(client_ip);

                if (!found_file.empty()) {
                    update_download_status(download_id, {
                        {"status", "completed"}, {"progress", 100}, {"eta", 0}, {"speed", ""},
                        {"filename", found_file}, {"download_url", "/downloads/" + found_file}
                    });
                } else {
                    cerr << "[Luma Tools] Download failed. Output:\n" << sanitize_utf8(full_output) << endl;
                    discord_log_error("Download", "Failed for: " + title);
                    update_download_status(download_id, {
                        {"status", "error"}, {"progress", 0},
                        {"error", "Download failed"}, {"details", sanitize_utf8(full_output)}
                    });
                }
            }).detach();

            res.set_content(json({{"download_id", download_id}, {"status", "started"}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ── POST /api/resolve-title — fetch real title for a single URL ──────────
    svr.Post("/api/resolve-title", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            string url = body.value("url", "");

            if (url.empty()) {
                res.status = 400;
                res.set_content(json({{"error", "Missing url"}}).dump(), "application/json");
                return;
            }

            string cmd = build_ytdlp_cmd() + " --no-download --no-warnings --print title " + escape_arg(url);
            int code;
            string output = exec_command(cmd, code);

            output.erase(std::remove(output.begin(), output.end(), '\r'), output.end());
            output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());
            string title = sanitize_utf8(output);

            while (!title.empty() && (title.front() == '_' || title.front() == ' ')) title.erase(title.begin());
            while (!title.empty() && (title.back() == '_' || title.back() == ' ')) title.pop_back();

            if (title.empty() || code != 0) {
                res.set_content(json({{"title", ""}}).dump(), "application/json");
            } else {
                res.set_content(json({{"title", title}}).dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", string("Resolve failed: ") + e.what()}}).dump(), "application/json");
        }
    });

    // ── GET /api/status/:id — check download progress ───────────────────────
    svr.Get(R"(/api/status/(.+))", [](const httplib::Request& req, httplib::Response& res) {
        try {
            string id = req.matches[1];
            json status = get_download_status(id);
            res.set_content(status.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json({{"error", string("Status error: ") + e.what()}}).dump(), "application/json");
        }
    });

    // ── GET /api/health — server health check ───────────────────────────────
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        int code;
        string version = exec_command(build_ytdlp_cmd() + " --version", code);
        version.erase(std::remove(version.begin(), version.end(), '\n'), version.end());
        version.erase(std::remove(version.begin(), version.end(), '\r'), version.end());

        json response = {
            {"status", "ok"},
            {"server", "Luma Tools v2.2.2"},
            {"yt_dlp_version", version.empty() ? "not installed" : version},
            {"yt_dlp_available", !version.empty()},
            {"ffmpeg_available", !g_ffmpeg_exe.empty()},
            {"ghostscript_available", !g_ghostscript_path.empty()},
            {"git_commit", g_git_commit.empty() ? "unknown" : g_git_commit},
            {"git_branch", g_git_branch.empty() ? "unknown" : g_git_branch}
        };
        res.set_content(response.dump(), "application/json");
    });
}

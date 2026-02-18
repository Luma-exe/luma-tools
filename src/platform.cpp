/**
 * Luma Tools — Platform detection
 */

#include "common.h"

static const vector<pair<regex, PlatformInfo>> PLATFORMS = {
    { regex(R"((youtube\.com|youtu\.be))", std::regex::icase),
      { "youtube", "YouTube", "fab fa-youtube", "#FF0000", true, true } },
    { regex(R"(tiktok\.com)", std::regex::icase),
      { "tiktok", "TikTok", "fab fa-tiktok", "#00F2EA", true, true } },
    { regex(R"(instagram\.com)", std::regex::icase),
      { "instagram", "Instagram", "fab fa-instagram", "#E1306C", true, true } },
    { regex(R"(spotify\.com)", std::regex::icase),
      { "spotify", "Spotify", "fab fa-spotify", "#1DB954", false, true } },
    { regex(R"(soundcloud\.com)", std::regex::icase),
      { "soundcloud", "SoundCloud", "fab fa-soundcloud", "#FF5500", false, true } },
    { regex(R"(twitter\.com|x\.com)", std::regex::icase),
      { "twitter", "X / Twitter", "fab fa-x-twitter", "#1DA1F2", true, true } },
    { regex(R"(facebook\.com|fb\.watch)", std::regex::icase),
      { "facebook", "Facebook", "fab fa-facebook", "#1877F2", true, true } },
    { regex(R"(twitch\.tv)", std::regex::icase),
      { "twitch", "Twitch", "fab fa-twitch", "#9146FF", true, true } },
    { regex(R"(vimeo\.com)", std::regex::icase),
      { "vimeo", "Vimeo", "fab fa-vimeo-v", "#1AB7EA", true, true } },
    { regex(R"(dailymotion\.com)", std::regex::icase),
      { "dailymotion", "Dailymotion", "fas fa-play-circle", "#0066DC", true, true } },
    { regex(R"(reddit\.com)", std::regex::icase),
      { "reddit", "Reddit", "fab fa-reddit-alien", "#FF4500", true, true } },
    { regex(R"(pinterest\.com)", std::regex::icase),
      { "pinterest", "Pinterest", "fab fa-pinterest", "#E60023", true, true } },
};

PlatformInfo detect_platform(const string& url) {
    for (const auto& [pattern, info] : PLATFORMS) {
        if (std::regex_search(url, pattern)) {
            return info;
        }
    }
    return { "unknown", "Unknown", "fas fa-globe", "#888888", true, true };
}

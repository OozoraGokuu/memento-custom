#pragma once
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// This portable core is deliberately independent of the modern Qt application.
namespace xp {
struct Cue { long long start, end; std::string text; };
inline std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
inline long long timestamp(const std::string &input) {
    const auto text = trim(input);
    unsigned h, m, s, ms; char separator; int consumed = 0;
    if (std::sscanf(text.c_str(), "%u:%u:%u%c%u%n", &h, &m, &s,
                    &separator, &ms, &consumed) != 5 ||
        consumed != static_cast<int>(text.size()) || h > 999 || m > 59 ||
        s > 59 || ms > 999 || (separator != ',' && separator != '.') ||
        text.size() < 4 || text[text.size() - 4] != separator)
        return -1;
    return ((h * 60LL + m) * 60 + s) * 1000 + ms;
}
inline std::vector<Cue> parseSrt(std::string input) {
    if (input.compare(0, 3, "\xef\xbb\xbf") == 0) input.erase(0, 3);
    input.erase(std::remove(input.begin(), input.end(), '\r'), input.end());
    std::istringstream stream(input);
    std::vector<Cue> cues;
    std::string line;
    while (std::getline(stream, line)) {
        auto arrow = line.find("-->");
        if (arrow == std::string::npos) continue;
        const auto start = timestamp(line.substr(0, arrow));
        const auto end = timestamp(line.substr(arrow + 3));
        std::string text;
        while (std::getline(stream, line) && !trim(line).empty()) {
            if (!text.empty()) text += '\n';
            text += line;
        }
        if (start >= 0 && end > start && !text.empty()) cues.push_back({start, end, text});
    }
    std::stable_sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) {
        return a.start < b.start;
    });
    return cues;
}
inline std::string activeText(const std::vector<Cue> &cues, long long time) {
    std::string text;
    for (const auto &cue : cues) {
        if (cue.start > time) break;
        if (time < cue.end) {
            if (!text.empty()) text += '\n';
            text += cue.text;
        }
    }
    return text;
}
// Ordinals refer only to actual audio tracks, never VLC's Disable entry (-1).
inline int trackAtOrdinal(const std::vector<int> &ids, int ordinal) {
    if (ordinal <= 0) return -1;
    for (int id : ids) if (id >= 0 && --ordinal == 0) return id;
    return -1; // Keep the preference for a later episode with enough tracks.
}
inline std::vector<std::string> coreTests() {
    std::vector<std::string> failed;
    auto check = [&](bool pass, const char *name) { if (!pass) failed.emplace_back(name); };
    const auto cues = parseSrt("\xef\xbb\xbf" "1\r\n00:00:01,000 --> 00:00:03,000\r\n日本語の文。\r\nمرحبا\r\n\r\n2\r\n00:00:02,000 --> 00:00:04,000\r\nSecond\r\n");
    check(cues.size() == 2, "UTF-8 BOM and CRLF parsing");
    check(activeText(cues, 999).empty(), "before subtitle start");
    check(activeText(cues, 1000) == "日本語の文。\nمرحبا", "complete multiline Unicode cue");
    check(activeText(cues, 2000) == "日本語の文。\nمرحبا\nSecond", "overlapping cues");
    check(activeText(cues, 3000) == "Second" && activeText(cues, 4000).empty(), "exclusive end boundary");
    check(parseSrt("1\n00:00:05,000 --> 00:00:02,000\nBad\n").empty(), "reversed cue rejected");
    check(timestamp("00:61:00,000") < 0 && timestamp("00:00:00,01") < 0 && timestamp("00:00:00,100x") < 0, "invalid timestamps rejected");
    check(trackAtOrdinal({-1, 4, 19}, 2) == 19, "ordinal uses actual track IDs");
    check(trackAtOrdinal({-1, 4}, 2) == -1 && trackAtOrdinal({4}, 0) == -1, "unavailable audio preference preserved");
    return failed;
}
constexpr int coreTestCount = 9;
}

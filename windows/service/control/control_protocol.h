// Line protocol between the desktop UI and the engine (one JSON object per line).
//   engine -> UI : {"event":"status",...}  {"event":"ready"}  {"event":"error","code":"..."}
//   UI -> engine : {"cmd":"set","camera":true,"mic":false}   {"cmd":"quit"}
// Pure C++ (no Windows headers) so it can be unit-tested anywhere.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace phonebridge::control {

struct StatusView {
    const char* state = "disconnected";
    uint32_t session = 0;
    bool camera = false;      // camera data is arriving right now
    bool mic = false;         // audio data is arriving right now
    bool wantCamera = true;   // what the user asked for
    bool wantMic = true;
    int fpsTenths = 0;        // video fps x 10 (avoids locale decimal commas)
    std::string device;       // e.g. "Google Pixel 8 Pro" ("" until the phone says hello)
};

inline const char* boolText(bool b) { return b ? "true" : "false"; }

// Escapes text for use inside a JSON string (quotes, backslashes, control characters).
inline std::string jsonEscape(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 2);
    for (const char ch : in) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
                else out += ch;
        }
    }
    return out;
}

inline std::string formatStatus(const StatusView& s) {
    std::string o = "{\"event\":\"status\",\"state\":\"";
    o += s.state;
    o += "\",\"session\":" + std::to_string(s.session);
    o += ",\"camera\":";     o += boolText(s.camera);
    o += ",\"mic\":";        o += boolText(s.mic);
    o += ",\"wantCamera\":"; o += boolText(s.wantCamera);
    o += ",\"wantMic\":";    o += boolText(s.wantMic);
    o += ",\"fps\":" + std::to_string(s.fpsTenths / 10) + "." + std::to_string(s.fpsTenths % 10);
    o += ",\"device\":\"" + jsonEscape(s.device) + "\"";
    o += "}";
    return o;
}

namespace detail {
// Position right after `"key"` + optional spaces + ':' + optional spaces, or npos.
inline size_t valueStart(std::string_view line, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    size_t k = line.find(needle);
    if (k == std::string_view::npos) return std::string_view::npos;
    size_t i = k + needle.size();
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] != ':') return std::string_view::npos;
    ++i;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return i;
}
}  // namespace detail

// True only if the key is present with a literal true/false value; `out` is untouched otherwise.
inline bool jsonBool(std::string_view line, std::string_view key, bool& out) {
    const size_t i = detail::valueStart(line, key);
    if (i == std::string_view::npos) return false;
    const std::string_view rest = line.substr(i);
    if (rest.substr(0, 4) == "true")  { out = true;  return true; }
    if (rest.substr(0, 5) == "false") { out = false; return true; }
    return false;
}

// Simple string value (no escape handling - our commands never need it).
inline bool jsonString(std::string_view line, std::string_view key, std::string& out) {
    size_t i = detail::valueStart(line, key);
    if (i == std::string_view::npos || i >= line.size() || line[i] != '"') return false;
    ++i;
    const size_t end = line.find('"', i);
    if (end == std::string_view::npos) return false;
    out.assign(line.substr(i, end - i));
    return true;
}

// "Google" + "Pixel 8 Pro" -> "Google Pixel 8 Pro"; "samsung" + "SM-S918B" -> "Samsung SM-S918B".
// Takes the DEVICE_INFO json sent by the phone ({"manufacturer":..., "model":...}).
inline std::string deviceDisplayName(std::string_view deviceInfoJson) {
    std::string maker, model;
    jsonString(deviceInfoJson, "manufacturer", maker);
    jsonString(deviceInfoJson, "model", model);
    if (maker.empty()) return model;
    if (maker[0] >= 'a' && maker[0] <= 'z') maker[0] = static_cast<char>(maker[0] - 'a' + 'A');
    if (model.empty()) return maker;
    // many phones repeat the maker inside the model ("Xiaomi Mi 11"): don't print it twice
    auto lower = [](std::string x) { for (auto& c : x) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return x; };
    if (lower(model).rfind(lower(maker), 0) == 0) return model;
    return maker + " " + model;
}

}  // namespace phonebridge::control

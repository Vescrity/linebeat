#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

using nlohmann::json;
namespace fs = std::filesystem;

namespace lb {
namespace {

std::string home() {
    if (const char* h = std::getenv("HOME")) return h;
    return ".";
}

std::string envOr(const char* name, const std::string& fallback) {
    if (const char* v = std::getenv(name)) {
        if (*v) return v;
    }
    return fallback;
}

} // namespace

std::string userDataDir() {
    return envOr("XDG_DATA_HOME", home() + "/.local/share") + "/linebeat";
}

std::string userConfigDir() {
    return envOr("XDG_CONFIG_HOME", home() + "/.config") + "/linebeat";
}

std::string userScenesDir() { return userDataDir() + "/scenes"; }

std::string configPath() { return userConfigDir() + "/config.json"; }

Config loadConfig() {
    Config c;
    std::ifstream in(configPath());
    if (!in) return c;
    try {
        json j;
        in >> j;
        if (j.contains("recent") && j["recent"].is_array())
            for (const auto& r : j["recent"])
                if (r.is_string()) c.recent.push_back(r.get<std::string>());
        if (j.contains("advanced") && j["advanced"].is_boolean())
            c.advanced = j["advanced"].get<bool>();
        if (j.contains("language") && j["language"].is_string())
            c.language = j["language"].get<std::string>();
        if (j.contains("master_gain") && j["master_gain"].is_number()) {
            const double g = j["master_gain"].get<double>();
            if (std::isfinite(g)) c.masterGain = std::clamp(g, 0.0, 2.0);
        }
        if (j.contains("ui_scale") && j["ui_scale"].is_number()) {
            const double v = j["ui_scale"].get<double>();
            if (std::isfinite(v)) c.uiScale = std::clamp(v, 0.7, 2.5);
        }
        if (j.contains("last_project") && j["last_project"].is_string())
            c.lastProject = j["last_project"].get<std::string>();
    } catch (...) {
        // 配置坏了就用默认值，不要因此起不来
    }
    return c;
}

void saveConfig(const Config& c) {
    std::error_code ec;
    fs::create_directories(userConfigDir(), ec);

    json j;
    j["recent"] = c.recent;
    j["advanced"] = c.advanced;
    j["language"] = c.language;
    j["master_gain"] = c.masterGain;
    j["ui_scale"] = c.uiScale;
    j["last_project"] = c.lastProject;

    const std::string tmp = configPath() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << j.dump(2);
        out.flush();
        if (!out.good()) return;
    }
    fs::rename(tmp, configPath(), ec);   // 先写临时文件再换，写坏了也不会毁掉旧配置
}

std::string demoScenesDir() {
    // 只算一次：SDL_GetBasePath 的返回值不能反复 free（它内部会复用同一块内存）
    static const std::string cached = [] {
        std::error_code ec;
        if (const char* base = SDL_GetBasePath()) {
            const std::string dir(base);
            const std::string a = dir + "scenes";
            if (fs::is_directory(a, ec)) return a;
            const std::string b = dir + "../scenes";
            if (fs::is_directory(b, ec)) {
                const std::string clean = fs::weakly_canonical(b, ec).string();   // 去掉 build/../ 
                return ec ? b : clean;
            }
        }
        if (fs::is_directory("scenes", ec)) return std::string("scenes");
        return std::string();
    }();
    return cached;
}

std::vector<std::string> listProjects(const std::string& dir, int limit) {
    std::vector<std::string> out;
    if (dir.empty()) return out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() != ".lbproj") continue;
        out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    if (int(out.size()) > limit) out.resize(size_t(limit));
    return out;
}

void rememberRecent(Config& c, const std::string& path) {
    if (path.empty()) return;
    c.recent.erase(std::remove(c.recent.begin(), c.recent.end(), path), c.recent.end());
    c.recent.insert(c.recent.begin(), path);
    if (c.recent.size() > 8) c.recent.resize(8);
    c.lastProject = path;
}

} // namespace lb

#include "project_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace project_io {
namespace {

constexpr int kFormatVersion = 2;

// JSON 里不能出现 inf/nan，统一夹成 0
double sane(double v) { return std::isfinite(v) ? v : 0.0; }

json paramToJson(const Param& p) {
    json j = json::object();
    j["v"] = sane(p.constant);          // 常量始终写下来，表达式失效时还能退回
    if (p.driven() || !p.source.empty()) j["e"] = p.source;
    return j;
}

bool paramFromJson(Param& p, const json& j, double fallback, int* exprErrors) {
    p.constant = fallback;
    p.source.clear();
    p.expr.reset();
    p.state.clear();
    p.error.clear();

    if (j.is_number()) { p.constant = j.get<double>(); return true; }
    if (!j.is_object()) return true;

    if (j.contains("v") && j["v"].is_number()) p.constant = j["v"].get<double>();
    if (j.contains("e") && j["e"].is_string()) {
        p.setSource(j["e"].get<std::string>());
        if (!p.error.empty()) {
            if (exprErrors) ++*exprErrors;
            return false;
        }
    }
    return true;
}

json voiceToJson(const VoiceRef& v) {
    return json{{"type", v.type}, {"file", v.file}, {"bank", v.bank}, {"program", v.program}};
}

VoiceRef voiceFromJson(const json& j) {
    VoiceRef v;
    if (!j.is_object()) return v;
    if (j.contains("type") && j["type"].is_string()) v.type = j["type"].get<std::string>();
    if (j.contains("file") && j["file"].is_string()) v.file = j["file"].get<std::string>();
    if (j.contains("bank") && j["bank"].is_number()) v.bank = j["bank"].get<int>();
    if (j.contains("program") && j["program"].is_number()) v.program = j["program"].get<int>();
    v.bank = std::clamp(v.bank, 0, 128);   // 128 = GM 鼓组
    v.program = std::clamp(v.program, 0, 127);
    return v;
}

double num(const json& j, const char* key, double def) {
    if (!j.is_object() || !j.contains(key)) return def;
    const json& v = j[key];
    return v.is_number() ? v.get<double>() : def;
}

int inum(const json& j, const char* key, int def) {
    return int(std::lround(num(j, key, double(def))));
}

bool bval(const json& j, const char* key, bool def) {
    if (!j.is_object() || !j.contains(key)) return def;
    const json& v = j[key];
    return v.is_boolean() ? v.get<bool>() : def;
}

} // namespace

std::string toJsonString(const Project& p) {
    json root;
    root["format"] = "linebeat";
    root["version"] = kFormatVersion;
    root["name"] = p.name;
    root["nextId"] = p.nextId;

    root["world"] = {
        {"gravity", sane(p.world.gravity)},
        {"kill_y", sane(p.world.killY)},
        {"seed", p.world.seed},
    };

    json lines = json::array();
    for (const auto& l : p.lines) {
        json j;
        j["id"] = l.id;
        j["ax"] = paramToJson(l.ax);
        j["ay"] = paramToJson(l.ay);
        j["bx"] = paramToJson(l.bx);
        j["by"] = paramToJson(l.by);
        j["collide"] = l.collide;
        j["voice"] = voiceToJson(l.voice);
        j["note"] = paramToJson(l.note);
        j["vel"] = paramToJson(l.vel);
        j["gain"] = paramToJson(l.gain);
        lines.push_back(j);
    }
    root["lines"] = lines;

    json balls = json::array();
    for (const auto& b : p.balls) {
        balls.push_back(json{{"id", b.id},
                             {"x", sane(b.pos.x)}, {"y", sane(b.pos.y)},
                             {"vx", sane(b.vel.x)}, {"vy", sane(b.vel.y)},
                             {"r", sane(b.radius)}});
    }
    root["balls"] = balls;

    json emitters = json::array();
    for (const auto& e : p.emitters) {
        emitters.push_back(json{
            {"id", e.id},
            {"px", paramToJson(e.px)}, {"py", paramToJson(e.py)},
            {"dir", paramToJson(e.dir)}, {"speed", paramToJson(e.speed)},
            {"interval", paramToJson(e.interval)}, {"delay", paramToJson(e.delay)},
            {"count", paramToJson(e.count)},
            {"enabled", e.enabled},
        });
    }
    root["emitters"] = emitters;

    return root.dump(2);
}

bool fromJsonString(Project& p, const std::string& text, std::string* error,
                    int* expressionErrors) {
    if (expressionErrors) *expressionErrors = 0;
    try {
        const json root = json::parse(text);
        if (!root.is_object()) {
            if (error) *error = "不是合法的场景文件";
            return false;
        }
        if (!root.contains("format") || !root["format"].is_string() ||
            root["format"].get<std::string>() != "linebeat") {
            if (error) *error = "不是 linebeat 场景文件";
            return false;
        }
        // 空对象/陌生 JSON 不能当成“空场景”把当前工程冲掉
        if (!root.contains("lines") && !root.contains("balls") &&
            !root.contains("emitters") && !root.contains("world")) {
            if (error) *error = "文件里没有场景内容";
            return false;
        }
        if (root.contains("version") && root["version"].is_number() &&
            root["version"].get<int>() > kFormatVersion) {
            if (error) *error = "场景版本太新（" +
                                std::to_string(root["version"].get<int>()) + "）";
            return false;
        }

        Project np;
        np.name = (root.contains("name") && root["name"].is_string())
                      ? root["name"].get<std::string>()
                      : std::string("untitled");

        if (root.contains("world") && root["world"].is_object()) {
            const json& w = root["world"];
            np.world.gravity = num(w, "gravity", -9.81);
            np.world.killY = num(w, "kill_y", -8.0);
            np.world.seed = unsigned(std::max(0.0, num(w, "seed", 1.0)));
        }

        int fallbackId = 1;
        int exprErr = 0;

        if (root.contains("lines") && root["lines"].is_array()) {
            for (const auto& j : root["lines"]) {
                if (!j.is_object()) continue;
                Line l;
                l.id = j.contains("id") && j["id"].is_number() ? j["id"].get<int>() : fallbackId++;
                paramFromJson(l.ax, j.value("ax", json::object()), 0.0, &exprErr);
                paramFromJson(l.ay, j.value("ay", json::object()), 0.0, &exprErr);
                paramFromJson(l.bx, j.value("bx", json::object()), 0.0, &exprErr);
                paramFromJson(l.by, j.value("by", json::object()), 0.0, &exprErr);
                l.collide = bval(j, "collide", true);
                if (j.contains("voice")) l.voice = voiceFromJson(j["voice"]);
                paramFromJson(l.note, j.value("note", json::object()), 72.0, &exprErr);
                paramFromJson(l.vel, j.value("vel", json::object()), 90.0, &exprErr);
                paramFromJson(l.gain, j.value("gain", json::object()), 1.0, &exprErr);
                l.a = Vec2{l.ax.constant, l.ay.constant};
                l.b = Vec2{l.bx.constant, l.by.constant};
                np.lines.push_back(l);
            }
        }

        if (root.contains("balls") && root["balls"].is_array()) {
            for (const auto& j : root["balls"]) {
                if (!j.is_object()) continue;
                Ball b;
                b.id = j.contains("id") && j["id"].is_number() ? j["id"].get<int>() : fallbackId++;
                b.pos = Vec2{num(j, "x", 0.0), num(j, "y", 0.0)};
                b.vel = Vec2{num(j, "vx", 0.0), num(j, "vy", 0.0)};
                b.radius = num(j, "r", 0.0);
                np.balls.push_back(b);
            }
        }

        if (root.contains("emitters") && root["emitters"].is_array()) {
            for (const auto& j : root["emitters"]) {
                if (!j.is_object()) continue;
                Emitter e;
                e.id = j.contains("id") && j["id"].is_number() ? j["id"].get<int>() : fallbackId++;
                paramFromJson(e.px, j.value("px", json::object()), -3.0, &exprErr);
                paramFromJson(e.py, j.value("py", json::object()), 2.0, &exprErr);
                paramFromJson(e.dir, j.value("dir", json::object()), 0.0, &exprErr);
                paramFromJson(e.speed, j.value("speed", json::object()), 5.0, &exprErr);
                paramFromJson(e.interval, j.value("interval", json::object()), 1.0, &exprErr);
                paramFromJson(e.delay, j.value("delay", json::object()), 0.0, &exprErr);
                paramFromJson(e.count, j.value("count", json::object()), -1.0, &exprErr);
                e.enabled = bval(j, "enabled", true);
                np.emitters.push_back(e);
            }
        }

        if (expressionErrors) *expressionErrors = exprErr;

        // id 去重：重复或非法的重新编号，保证选择/删除只作用于一个对象
        std::vector<int> used;
        int nextFree = 1;
        auto takeId = [&used, &nextFree](int& id) {
            // 运行期球的 id 从 100000 起，工程里出现就重编号，避免两边撞号
            const bool clash = (id <= 0) || (id >= 100000) ||
                               (std::find(used.begin(), used.end(), id) != used.end());
            if (clash) {
                while (std::find(used.begin(), used.end(), nextFree) != used.end()) ++nextFree;
                id = nextFree;
            }
            used.push_back(id);
            nextFree = std::max(nextFree, id + 1);
        };
        for (auto& l : np.lines) takeId(l.id);
        for (auto& b : np.balls) takeId(b.id);
        for (auto& e : np.emitters) takeId(e.id);

        const int savedNext = root.contains("nextId") && root["nextId"].is_number()
                                  ? root["nextId"].get<int>()
                                  : 0;
        np.nextId = std::max(savedNext, nextFree);

        p = np;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool save(const Project& p, const std::string& path, std::string* error) {
    const std::string text = toJsonString(p);

    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "无法写入 " + tmp;
            return false;
        }
        out << text;
        out.flush();
        if (!out.good()) {
            if (error) *error = "写入失败（磁盘满？）";
            out.close();
            std::remove(tmp.c_str());
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (error) *error = "无法替换 " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool load(Project& p, const std::string& path, std::string* error, int* expressionErrors) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "无法读取 " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return fromJsonString(p, ss.str(), error, expressionErrors);
}

Project defaultProject() {
    Project p;
    p.name = "first loop";

    VoiceRef voice;
    voice.program = 10;   // Music Box
    Line l = makeDefaultLine(1, Vec2{-2.2, 0.0}, Vec2{2.2, 0.0}, voice);
    p.lines.push_back(l);

    Ball b;
    b.id = 2;
    b.pos = Vec2{0.0, 1.05};
    b.vel = Vec2{0.0, 0.0};
    p.balls.push_back(b);

    p.nextId = 3;
    return p;
}

} // namespace project_io

// 命令行调试入口：物理自检、表达式测试、离线导出 wav、工程存读
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "audio.h"
#include "expr.h"
#include "model.h"
#include "physics.h"
#include "project_io.h"

namespace {

bool writeWav16(const std::string& path, const std::vector<float>& samples, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t frames = uint32_t(samples.size() / 2);
    const uint32_t dataBytes = frames * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16);
    u16(1); u16(2); u32(uint32_t(rate)); u32(uint32_t(rate * 4)); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    for (float s : samples) {
        const float c = std::clamp(s, -1.0f, 1.0f);
        int16_t v = int16_t(c * 32767.0f);
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
    return true;
}

int runSelfTest() {
    Project p = project_io::defaultProject();
    World w;
    w.reset(p);

    std::vector<double> hitTimes;
    std::vector<double> apexes;
    double apex = -1e9;
    for (int i = 0; i < 240 * 60; ++i) {
        w.step();
        if (!w.balls().empty()) apex = std::max(apex, w.balls()[0].pos.y);
        for (const auto& h : w.hits()) {
            hitTimes.push_back(h.time);
            apexes.push_back(apex);
            apex = -1e9;
        }
    }
    std::printf("bounces=%zu  simTime=%.3f  balls=%zu\n", hitTimes.size(), w.time(), w.balls().size());
    if (hitTimes.size() >= 3) {
        std::printf("first hits:");
        for (size_t i = 0; i < std::min<size_t>(6, hitTimes.size()); ++i) std::printf(" %.4f", hitTimes[i]);
        std::printf("\n");
        const double t0 = hitTimes[1] - hitTimes[0];
        const double t1 = hitTimes.back() - hitTimes[hitTimes.size() - 2];
        std::printf("period first=%.6f last=%.6f drift=%.3e\n", t0, t1, t1 - t0);
    }
    if (apexes.size() > 3) {
        std::printf("apex first=%.6f last=%.6f\n", apexes[1], apexes.back());
    }
    return 0;
}

int runExpr(const std::string& src) {
    std::string err;
    auto e = Expr::compile(src, &err);
    if (!e) {
        std::printf("编译失败: %s\n", err.c_str());
        return 1;
    }
    double slots[SLOT_COUNT]{};
    std::vector<double> state(size_t(std::max(1, e->stateCount())), 0.0);
    slots[SLOT_T] = 1.0;
    slots[SLOT_DT] = 1.0 / 240.0;
    slots[SLOT_PI] = 3.14159265358979;
    slots[SLOT_E] = 2.71828182845905;
    slots[SLOT_HIT_SPEED] = 4.5;
    slots[SLOT_SELF_X] = 0.5;
    std::printf("%.6f\n", e->eval(slots, state.data()));
    if (e->stateCount() > 0) std::printf("state[0] = %.6f\n", state[0]);
    return 0;
}

int runPresets() {
    AudioEngine audio;
    if (!audio.init("/usr/share/soundfonts/FluidR3_GM.sf2", false)) {
        std::printf("音频初始化失败: %s\n", audio.error().c_str());
        return 1;
    }
    const auto& ps = audio.presets("");
    std::printf("共 %zu 个音色\n", ps.size());
    for (const auto& p : ps)
        std::printf("  bank %3d  program %3d  %s\n", p.bank, p.program, p.name.c_str());
    audio.shutdown();
    return 0;
}

int runRoundTrip(const std::string& path) {
    Project a;
    std::string err;
    int exprErr = 0;
    if (!project_io::load(a, path, &err, &exprErr)) {
        std::printf("加载失败: %s\n", err.c_str());
        return 1;
    }
    const std::string j1 = project_io::toJsonString(a);
    const std::string tmp = path + ".rt";
    // 回读用的临时文件，离开作用域即删，别把 .rt 留在场景目录里
    struct TmpFileGuard {
        std::string path;
        ~TmpFileGuard() { if (!path.empty()) std::remove(path.c_str()); }
    } guard{tmp};
    if (!project_io::save(a, tmp, &err)) {
        std::printf("保存失败: %s\n", err.c_str());
        return 1;
    }
    Project b;
    if (!project_io::load(b, tmp, &err, &exprErr)) {
        std::printf("回读失败: %s\n", err.c_str());
        return 1;
    }
    const std::string j2 = project_io::toJsonString(b);
    std::printf("lines=%zu balls=%zu emitters=%zu nextId=%d/%d exprErrors=%d %s\n",
                b.lines.size(), b.balls.size(), b.emitters.size(), a.nextId, b.nextId, exprErr,
                j1 == j2 ? "ROUNDTRIP OK" : "ROUNDTRIP DIFF");
    if (j1 != j2) {
        std::printf("--- first ---\n%s\n--- second ---\n%s\n", j1.c_str(), j2.c_str());
        return 2;
    }
    return 0;
}

int runTrace(const std::string& path, double seconds) {
    Project p;
    std::string err;
    if (!project_io::load(p, path, &err, nullptr)) { std::printf("加载失败: %s\n", err.c_str()); return 1; }
    World w;
    w.reset(p);
    const int steps = int(seconds / World::kStep);
    const int report = int(0.25 / World::kStep);
    int hits = 0;
    for (int i = 0; i < steps; ++i) {
        w.step();
        hits += int(w.hits().size());
        if (i % report == 0 || i == steps - 1) {
            std::printf("t=%6.3f  balls=%2zu  hits=%5d", w.time(), w.balls().size(), hits);
            for (size_t b = 0; b < w.balls().size() && b < 3; ++b)
                std::printf("   [%d] %.4f,%.4f v=%.3f,%.3f", w.balls()[b].id,
                            w.balls()[b].pos.x, w.balls()[b].pos.y,
                            w.balls()[b].vel.x, w.balls()[b].vel.y);
            std::printf("\n");
        }
    }
    return 0;
}

int runBench(const std::string& path, double seconds, int repeats) {
    Project p;
    std::string err;
    if (!project_io::load(p, path, &err, nullptr)) { std::printf("加载失败: %s\n", err.c_str()); return 1; }
    const int steps = int(seconds / World::kStep);
    double best = 1e9, total = 0.0;
    size_t hits = 0;
    World w;
    for (int r = 0; r < repeats; ++r) {
        w.reset(p);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < steps; ++i) {
            w.step();
            hits += w.hits().size();
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        best = std::min(best, ms);
        total += ms;
    }
    std::printf("场景 %s\n", path.c_str());
    std::printf("模拟 %.1fs x %d 次：最好 %.1f ms，平均 %.1f ms\n", seconds, repeats, best, total / repeats);
    const double nsteps = seconds / World::kStep;
    std::printf("≈ %.2f M 步/秒  %.1f 倍实时  撞击 %zu 次\n",
                nsteps / (best / 1000.0) / 1e6, seconds / (best / 1000.0), hits);
    return 0;
}

int runHits(const std::string& path, double seconds) {
    Project p;
    std::string err;
    if (!project_io::load(p, path, &err, nullptr)) { std::printf("加载失败: %s\n", err.c_str()); return 1; }
    World w;
    w.reset(p);
    const int steps = int(seconds / World::kStep);
    for (int i = 0; i < steps; ++i) {
        w.step();
        for (const auto& h : w.hits()) {
            const Line* line = nullptr;
            for (const auto& l : p.lines) if (l.id == h.lineId) line = &l;
            std::printf("%7.3f  line %2d  %-3s  v=%5.2f\n", h.time, h.lineId,
                        line ? (line->voice.bank >= 128 ? "drum" : "mel") : "?", h.speed);
        }
    }
    return 0;
}

int runRender(const std::string& out, double seconds, const std::string& projectPath) {
    Project p = project_io::defaultProject();
    if (!projectPath.empty()) {
        std::string err;
        if (!project_io::load(p, projectPath, &err)) {
            std::printf("工程加载失败: %s\n", err.c_str());
            return 1;
        }
    }

    AudioEngine audio;
    if (!audio.init("/usr/share/soundfonts/FluidR3_GM.sf2", false)) {
        std::printf("合成器启动失败: %s\n", audio.error().c_str());
        return 1;
    }
    audio.setMasterGain(0.8);

    World w;
    w.reset(p);

    const int block = 256;
    const int total = int(seconds * AudioEngine::kSampleRate);
    std::vector<float> buf(size_t(block) * 2);
    std::vector<float> all;
    all.reserve(size_t(total) * 2);

    int written = 0;
    double simAcc = 0.0;
    while (written < total) {
        simAcc += double(block) / AudioEngine::kSampleRate;
        while (simAcc >= World::kStep) {
            w.step();
            for (const auto& hit : w.hits()) {
                const Line* line = nullptr;
                for (const auto& l : p.lines) if (l.id == hit.lineId) line = &l;
                if (!line) continue;
                const int ch = audio.ensureVoice(line->voice);
                Slots s;
                s.fillWorld(p.world, w.time(), World::kStep, w.frame());
                s.fillLine(*line);
                s.fillHit(hit);
                audio.trigger(ch, int(std::lround(line->note.eval(s.v))),
                              int(std::lround(line->vel.eval(s.v))), float(line->gain.eval(s.v)));
            }
            simAcc -= World::kStep;
        }
        audio.render(buf.data(), block);
        all.insert(all.end(), buf.begin(), buf.end());
        written += block;
    }

    if (!writeWav16(out, all, AudioEngine::kSampleRate)) {
        std::printf("写 wav 失败\n");
        return 1;
    }
    std::printf("已导出 %s (%.1fs)\n", out.c_str(), seconds);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法: lb_cli --selftest | --expr \"...\" | --render out.wav --seconds N [--project file]\n");
        return 1;
    }
    const std::string mode = argv[1];
    if (mode == "--selftest") return runSelfTest();
    if (mode == "--presets") return runPresets();
    if (mode == "--roundtrip") return runRoundTrip(argc > 2 ? argv[2] : "");
    if (mode == "--bench") return runBench(argc > 2 ? argv[2] : "", argc > 3 ? std::atof(argv[3]) : 10.0, argc > 4 ? std::atoi(argv[4]) : 3);
    if (mode == "--hits") return runHits(argc > 2 ? argv[2] : "", argc > 3 ? std::atof(argv[3]) : 2.0);
    if (mode == "--trace") return runTrace(argc > 2 ? argv[2] : "", argc > 3 ? std::atof(argv[3]) : 2.0);
    if (mode == "--expr") return runExpr(argc > 2 ? argv[2] : "");

    if (mode == "--render") {
        const std::string out = argc > 2 ? argv[2] : "out.wav";
        double seconds = 8.0;
        std::string proj;
        for (int i = 3; i < argc; ++i) {
            if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) seconds = std::atof(argv[++i]);
            else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) proj = argv[++i];
        }
        return runRender(out, seconds, proj);
    }
    std::printf("未知模式: %s\n", mode.c_str());
    return 1;
}

#!/usr/bin/env python3
"""生成演示工程 scenes/demo_loop.lbproj。

节奏不是“试”出来的，是算出来的：
  · 球在两条竖线之间飞的时间 = 水平距离 / |vx|
  · 所以给定「第几拍撞第几条线」，竖线的 x 和高度就唯一确定
  · y 用自由落体 y0 - ½gt² 算，竖线只覆盖那一刻周围 0.45 m

用法：
  python3 tools/make_demo.py                # 默认 120 BPM、鼓点在第 1/2/2.5/4 拍
  python3 tools/make_demo.py --bpm 150
"""
import argparse
import json

# ---- 场景参数 -------------------------------------------------------------
GRAVITY = -3.0      # 比 9.81 缓和，音序才收得进一屏；改它要重算位置
VX = 4.0            # 球的水平速度
Y0 = 3.0            # 发射器高度
PAD_HALF = 0.45     # 竖线半高
KILL_Y = -4.5
SKIN = 1e-5        # 点球的碰撞蒙皮，摆球时要留出来


def P(v):
    return {"v": v}


def E(s):
    return {"e": s}


DRUM = {"type": "sf2", "file": "", "bank": 128, "program": 0}


def mel(prog):
    return {"type": "sf2", "file": "", "bank": 0, "program": prog}


def line(i, x0, y0, x1, y1, voice, note, vel=100, gain=1.0):
    return {"id": i, "ax": P(x0), "ay": P(y0), "bx": P(x1), "by": P(y1),
            "collide": True, "voice": voice,
            "note": note if isinstance(note, dict) else P(note),
            "vel": vel if isinstance(vel, dict) else P(vel), "gain": P(gain)}


def chord(notes):
    """按小节在四个和弦之间轮换（4536）"""
    return E(f"lut(floor(t / 2) % 4, 0,{notes[0]}, 1,{notes[1]}, 2,{notes[2]}, 3,{notes[3]})")


def drum_geometry(beat, beats, y0, vx, half):
    """beats: 每个鼓点相对发射时刻的拍数，例如 [1, 2, 2.5, 4]"""
    ts, x = [], 0.0
    direction = 1.0
    prev = 0.0
    for b in beats:
        t = b * beat
        x += direction * vx * (t - prev)   # 匀速：距离 = 速度 × 时间
        direction = -direction
        prev = t
        ts.append(t)
    ys = [y0 - 0.5 * abs(GRAVITY) * t * t for t in ts]
    xs = []
    x = 0.0
    direction = 1.0
    prev = 0.0
    for b in beats:
        t = b * beat
        x += direction * vx * (t - prev)
        direction = -direction
        prev = t
        xs.append(x)
    return ts, xs, ys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bpm", type=float, default=120.0)
    ap.add_argument("--beats", default="1,2,3.5,4", help="鼓点在第几拍")
    ap.add_argument("--out", default="scenes/demo_loop.lbproj")
    args = ap.parse_args()

    beat = 60.0 / args.bpm
    notes = [float(x) for x in args.beats.split(",")]
    ts, xs, ys = drum_geometry(beat, notes, Y0, VX, PAD_HALF)

    lines, i = [], 1
    kinds = [36, 38, 36, 38]          # kick / snare / kick / snare
    for n, (x, y) in enumerate(zip(xs, ys)):
        lines.append(line(i, x, y - PAD_HALF, x, y + PAD_HALF, DRUM, kinds[n % 4]))
        i += 1

    # 右边：踩镲 + 4536，全部在鼓组右侧，别掉到销毁线下面
    HAT_Y, CHORD_Y, BASS_Y = 0.9, 0.1, -1.9
    t16 = beat / 4.0
    hat_h = abs(GRAVITY) * t16 * t16 / 8.0
    lines.append(line(i, 3.2, HAT_Y, 5.2, HAT_Y, DRUM, 42, 100, 0.85))
    i += 1
    for x0, ns in [(3.2, (53, 55, 52, 57)), (5.6, (57, 59, 55, 60)), (8.0, (60, 62, 59, 64))]:
        lines.append(line(i, x0, CHORD_Y, x0 + 2.2, CHORD_Y, mel(11), chord(ns),
                          E("clamp(hit.speed * 8 + 40, 30, 100)"), 0.9))
        i += 1
    lines.append(line(i, 3.2, BASS_Y, 10.2, BASS_Y, mel(33), chord((41, 43, 40, 45)),
                      E("clamp(hit.speed * 6 + 50, 40, 110)"), 1.0))
    i += 1

    drop2 = abs(GRAVITY) * (2 * beat) ** 2 / 8.0     # 2 拍一次的落点高度
    drop4 = abs(GRAVITY) * (4 * beat) ** 2 / 8.0     # 4 拍一次
    balls = [
        {"id": 10, "x": 0.0, "y": Y0, "vx": VX, "vy": 0.0, "r": 0.0},
        {"id": 11, "x": 4.2, "y": HAT_Y + SKIN + hat_h, "vx": 0.0, "vy": 0.0, "r": 0.0},
        {"id": 12, "x": 4.3, "y": CHORD_Y + SKIN + drop2, "vx": 0.0, "vy": 0.0, "r": 0.0},
        {"id": 13, "x": 6.7, "y": CHORD_Y + SKIN + drop2, "vx": 0.0, "vy": 0.0, "r": 0.0},
        {"id": 14, "x": 9.1, "y": CHORD_Y + SKIN + drop2, "vx": 0.0, "vy": 0.0, "r": 0.0},
        {"id": 15, "x": 6.7, "y": BASS_Y + SKIN + drop4, "vx": 0.0, "vy": 0.0, "r": 0.0},
    ]

    emitters = [{
        "id": 20, "px": P(0.0), "py": P(Y0), "dir": P(0.0), "speed": P(VX),
        "interval": P(4 * beat), "delay": P(4 * beat), "count": P(-1), "enabled": True,
    }]

    proj = {"format": "linebeat", "version": 2, "name": "鼓组 + 4536", "nextId": 40,
            "world": {"gravity": GRAVITY, "kill_y": KILL_Y, "seed": 7},
            "lines": lines, "balls": balls, "emitters": emitters}
    json.dump(proj, open(args.out, "w"), indent=2, ensure_ascii=False)

    print(f"{args.bpm:.0f} BPM（一拍 {beat:.3f}s），鼓点拍位 {notes}")
    print("鼓点时刻:", [round(t, 3) for t in ts])
    print("竖线位置:", [(round(x, 2), round(y, 2)) for x, y in zip(xs, ys)])
    print("写入", args.out)


if __name__ == "__main__":
    main()

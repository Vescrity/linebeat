#!/usr/bin/env bash
# 弹线 Linebeat —— 需要时先编译，再运行
set -euo pipefail
cd "$(dirname "$0")"

need_build=0
[ -x ./build/linebeat ] || need_build=1
if [ "$need_build" -eq 0 ] && find src core tools -newer ./build/linebeat \( -name '*.cpp' -o -name '*.h' \) 2>/dev/null | grep -q .; then
    need_build=1
fi

if [ "$need_build" -eq 1 ]; then
    echo "[linebeat] 编译中..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
    cmake --build build -j"$(nproc)"
fi

exec ./build/linebeat "$@"

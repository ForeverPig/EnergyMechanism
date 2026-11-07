#!/usr/bin/env bash
set -euo pipefail
qt_root="$(realpath "${1:?Pass Qt 6.8.3 gcc_64 directory}")"
project="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="$project/build/linux"
export PATH="$qt_root/bin:$PATH"
export LD_LIBRARY_PATH="$qt_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S "$project" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qt_root" -DBUILD_TESTING=ON
cmake --build "$build" --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build" --output-on-failure
python3 "$project/scripts/package-linux.py" "$qt_root" "$build"

#!/usr/bin/env bash
# Builds examples/counter every way this machine supports and runs the browser tests.
#
#   tests/run.sh
#
# Needs: a C++20 compiler, Python 3, curl, Node with Playwright (npm i, then
# npx playwright install chromium). Used when present: clang++, em++ (Emscripten,
# activated), x86_64-w64-mingw32-g++-posix and wine (runs the Windows build).
set -uo pipefail
cd "$(dirname "$0")/.."
export NODE_PATH="${NODE_PATH:-$(npm root -g 2>/dev/null)}"

OUT=build/tests
rm -rf "$OUT" && mkdir -p "$OUT"
FLAGS=(-std=c++20 -Wall -Wextra -Wpedantic -Werror -g -I.)
EXAMPLE=examples/counter/main.cpp
port=8900
failed=()

have() { command -v "$1" > /dev/null 2>&1; }
step() { printf '\n== %s\n' "$1"; }
fail() { echo "FAILED: $1"; failed+=("$1"); }

wait_for() {
    for _ in $(seq 1 150); do
        curl -s -o /dev/null "$1" && return 0
        sleep 0.2
    done
    return 1
}

# Starts a development build and runs both browser tests against it.
test_native() {
    local name=$1; shift
    port=$((port + 1))
    local url="http://127.0.0.1:$port/"
    VOLT2_NO_BROWSER=1 VOLT2_PORT=$port "$@" > "$OUT/$name.log" 2>&1 &
    local pid=$!
    if wait_for "$url"; then
        node tests/counter.test.cjs "$url" || fail "$name: counter test"
        node tests/reload.test.cjs "$url" || fail "$name: reload test"
    else
        fail "$name: did not start (see $OUT/$name.log)"
    fi
    kill "$pid" 2> /dev/null
    wait "$pid" 2> /dev/null
}

build() {  # name, compiler and arguments
    local name=$1; shift
    if ! "$@" > "$OUT/$name.build.log" 2>&1; then
        cat "$OUT/$name.build.log"
        fail "$name: build"
        return 1
    fi
}

step "embedded page client is up to date"
python3 tools/embed_js.py --check || fail "embed check"

step "g++ (C++20; C++23 compile check)"
build gcc-c++23 g++ "${FLAGS[@]}" -std=c++23 -fsyntax-only "$EXAMPLE"
build gcc g++ "${FLAGS[@]}" -O1 "$EXAMPLE" -o "$OUT/counter-gcc" -pthread && test_native gcc "$OUT/counter-gcc"

if have clang++; then
    step "clang++ (C++20; C++23 compile check)"
    build clang-c++23 clang++ "${FLAGS[@]}" -std=c++23 -fsyntax-only "$EXAMPLE"
    build clang clang++ "${FLAGS[@]}" -O1 "$EXAMPLE" -o "$OUT/counter-clang" -pthread && test_native clang "$OUT/counter-clang"
fi

step "ThreadSanitizer (the bridge hands messages between threads)"
if build tsan g++ "${FLAGS[@]}" -O1 -fsanitize=thread "$EXAMPLE" -o "$OUT/counter-tsan" -pthread; then
    TSAN_OPTIONS="halt_on_error=0" test_native tsan "$OUT/counter-tsan"
    races=$(grep -c 'WARNING: ThreadSanitizer' "$OUT/tsan.log")
    echo "  data races reported: $races"
    [ "$races" = 0 ] || fail "tsan: $races reports in $OUT/tsan.log"
fi

step "AddressSanitizer + UndefinedBehaviorSanitizer"
if build asan g++ "${FLAGS[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer "$EXAMPLE" -o "$OUT/counter-asan" -pthread; then
    ASAN_OPTIONS="detect_leaks=0" test_native asan "$OUT/counter-asan"
    reports=$(grep -c -E 'ERROR: AddressSanitizer|runtime error:' "$OUT/asan.log")
    echo "  sanitizer reports: $reports"
    [ "$reports" = 0 ] || fail "asan: $reports reports in $OUT/asan.log"
fi

if have em++; then
    step "WebAssembly (em++ -m64), served as static files"
    mkdir -p "$OUT/site"
    if build wasm em++ -std=c++20 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I. "$EXAMPLE" -o "$OUT/site/app.js"; then
        cp web/index.html web/volt2.js "$OUT/site/"
        port=$((port + 1))
        python3 -m http.server "$port" --bind 127.0.0.1 --directory "$OUT/site" > "$OUT/static.log" 2>&1 &
        pid=$!
        if wait_for "http://127.0.0.1:$port/"; then
            node tests/counter.test.cjs "http://127.0.0.1:$port/" || fail "wasm: counter test"
        else
            fail "wasm: static server did not start"
        fi
        kill "$pid" 2> /dev/null
        wait "$pid" 2> /dev/null
    fi
else
    step "WebAssembly: skipped (activate Emscripten to include it)"
fi

if have x86_64-w64-mingw32-g++-posix; then
    step "Windows build (MinGW)"
    if build mingw x86_64-w64-mingw32-g++-posix "${FLAGS[@]}" -O1 "$EXAMPLE" -o "$OUT/counter.exe" -static -lws2_32; then
        if have wine; then
            export WINEPREFIX="${WINEPREFIX:-$PWD/build/wineprefix}" WINEDEBUG=-all
            test_native wine wine "$OUT/counter.exe"
            wineserver -k 2> /dev/null
        else
            echo "  built; install wine to run it"
        fi
    fi
fi

echo
if [ ${#failed[@]} -eq 0 ]; then
    echo "ALL PASSED"
else
    printf 'FAILED:\n'; printf '  %s\n' "${failed[@]}"
    exit 1
fi

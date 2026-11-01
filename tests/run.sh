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
COUNTER=examples/counter/main.cpp
LIFETIME=tests/lifetime/main.cpp
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

# Starts a development build and runs the given browser tests against it.
#   run_dev <log name> "<test> <test>..." <command...>
run_dev() {
    local name=$1 tests=$2; shift 2
    port=$((port + 1))
    local url="http://127.0.0.1:$port/"
    VOLT2_NO_BROWSER=1 VOLT2_WARN_AT=200 VOLT2_PORT=$port "$@" > "$OUT/$name.log" 2>&1 &
    local pid=$!
    if wait_for "$url"; then
        for t in $tests; do node "tests/$t.test.cjs" "$url" || fail "$name: $t test"; done
    else
        fail "$name: did not start (see $OUT/$name.log)"
    fi
    kill "$pid" 2> /dev/null
    wait "$pid" 2> /dev/null
}

# Both programs of a development build: the counter example and the lifetime test.
test_native() {  # name, command prefix (empty, or wine)
    local name=$1 prefix=$2 suffix=${3:-}
    run_dev "$name-counter" "counter reload" $prefix "$OUT/counter-$name$suffix"
    run_dev "$name-lifetime" "lifetime" $prefix "$OUT/lifetime-$name$suffix"
}

# Builds both programs: build_both <name> <suffix> <compiler> <flags and libraries...>
# The source comes right after the compiler, so libraries such as -lws2_32 follow it.
build_both() {
    local name=$1 suffix=$2 compiler=$3; shift 3
    build "$name-counter" "$compiler" "$COUNTER" "$@" -o "$OUT/counter-$name$suffix" &&
        build "$name-lifetime" "$compiler" "$LIFETIME" "$@" -o "$OUT/lifetime-$name$suffix"
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
build gcc-c++23 g++ "${FLAGS[@]}" -std=c++23 -fsyntax-only "$LIFETIME"
build_both gcc "" g++ "${FLAGS[@]}" -O1 -pthread && test_native gcc ""

if have clang++; then
    step "clang++ (C++20; C++23 compile check)"
    build clang-c++23 clang++ "${FLAGS[@]}" -std=c++23 -fsyntax-only "$LIFETIME"
    build_both clang "" clang++ "${FLAGS[@]}" -O1 -pthread && test_native clang ""
fi

step "ThreadSanitizer (the bridge hands messages between threads)"
if build_both tsan "" g++ "${FLAGS[@]}" -O1 -fsanitize=thread -pthread; then
    TSAN_OPTIONS="halt_on_error=0" test_native tsan ""
    races=$(cat "$OUT"/tsan-*.log | grep -c 'WARNING: ThreadSanitizer')
    echo "  data races reported: $races"
    [ "$races" = 0 ] || fail "tsan: $races reports in $OUT/tsan-*.log"
fi

step "AddressSanitizer + UndefinedBehaviorSanitizer"
if build_both asan "" g++ "${FLAGS[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread; then
    ASAN_OPTIONS="detect_leaks=0" test_native asan ""
    reports=$(cat "$OUT"/asan-*.log | grep -c -E 'ERROR: AddressSanitizer|runtime error:')
    echo "  sanitizer reports: $reports"
    [ "$reports" = 0 ] || fail "asan: $reports reports in $OUT/asan-*.log"
fi

# Builds a program with em++ into its own static site and runs a browser test on it.
test_wasm() {  # name, source, test
    local site="$OUT/site-$1"
    mkdir -p "$site"
    build "wasm-$1" em++ -std=c++20 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I. "$2" -o "$site/app.js" || return
    cp web/index.html web/volt2.js "$site/"
    port=$((port + 1))
    python3 -m http.server "$port" --bind 127.0.0.1 --directory "$site" > "$OUT/static-$1.log" 2>&1 &
    local pid=$!
    if wait_for "http://127.0.0.1:$port/"; then
        node "tests/$3.test.cjs" "http://127.0.0.1:$port/" || fail "wasm: $3 test"
    else
        fail "wasm: static server did not start"
    fi
    kill "$pid" 2> /dev/null
    wait "$pid" 2> /dev/null
}

if have em++; then
    step "WebAssembly (em++ -m64), served as static files"
    test_wasm counter "$COUNTER" counter
    test_wasm lifetime "$LIFETIME" lifetime
else
    step "WebAssembly: skipped (activate Emscripten to include it)"
fi

if have x86_64-w64-mingw32-g++-posix; then
    step "Windows build (MinGW)"
    if build_both mingw .exe x86_64-w64-mingw32-g++-posix "${FLAGS[@]}" -O1 -static -lws2_32; then
        if have wine; then
            export WINEPREFIX="${WINEPREFIX:-$PWD/build/wineprefix}" WINEDEBUG=-all
            test_native mingw wine .exe
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

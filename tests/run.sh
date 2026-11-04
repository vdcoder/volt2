#!/usr/bin/env bash
# Builds the example and the test programs every way this machine supports and runs
# the browser tests against each build.
#
#   tests/run.sh
#
# Needs: a C++23 compiler, Python 3, curl, Node with Playwright (npm i, then
# npx playwright install chromium). Used when present: clang++, em++ (Emscripten,
# activated), x86_64-w64-mingw32-g++-posix and wine (runs the Windows build).
set -uo pipefail
cd "$(dirname "$0")/.."
export NODE_PATH="${NODE_PATH:-$(npm root -g 2>/dev/null)}"

OUT=build/tests
rm -rf "$OUT" && mkdir -p "$OUT"
FLAGS=(-std=c++23 -Wall -Wextra -Wpedantic -Werror -g -I.)
# name, source, tests on development builds, tests on WebAssembly builds
PROGRAMS=(
    "counter examples/counter/main.cpp counter counter"
    "markup tests/markup/main.cpp markup markup"
    "bridge tests/bridge/main.cpp bridge,reload bridge"
    "lifetime tests/lifetime/main.cpp lifetime lifetime"
    "identity tests/identity/main.cpp identity identity"
)
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

build() {  # log name, compiler and arguments
    local name=$1; shift
    if ! "$@" > "$OUT/$name.build.log" 2>&1; then
        cat "$OUT/$name.build.log"
        fail "$name: build"
        return 1
    fi
}

# Builds every program: build_all <toolchain> <suffix> <compiler> <flags and libraries...>
# The source comes right after the compiler, so libraries such as -lws2_32 follow it.
build_all() {
    local name=$1 suffix=$2 compiler=$3; shift 3
    local status=0 program source
    for entry in "${PROGRAMS[@]}"; do
        read -r program source _ _ <<< "$entry"
        build "$name-$program" "$compiler" "$source" "$@" -o "$OUT/$program-$name$suffix" || status=1
    done
    return $status
}

# Starts a development build and runs the given browser tests against it.
run_dev() {  # log name, "test test...", command...
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

test_native() {  # toolchain, command prefix (empty, or wine), executable suffix
    local name=$1 prefix=$2 suffix=${3:-} program tests
    for entry in "${PROGRAMS[@]}"; do
        read -r program _ tests _ <<< "$entry"
        run_dev "$name-$program" "${tests//,/ }" $prefix "$OUT/$program-$name$suffix"
    done
}

step "embedded page client is up to date"
python3 tools/embed_js.py --check || fail "embed check"

step "C++20 is refused with a clear message"
if g++ -std=c++20 -fsyntax-only -I. examples/counter/main.cpp 2> "$OUT/cxx20.log"; then
    fail "a C++20 build was accepted"
elif grep -q 'volt2 needs C++23' "$OUT/cxx20.log"; then
    echo "  ok: $(grep -m1 -o 'volt2 needs C++23[^"]*' "$OUT/cxx20.log")"
else
    head -5 "$OUT/cxx20.log"
    fail "C++20 error message"
fi

step "= true is refused for attributes that are not booleans"
printf '#include "volt2.hpp"\nauto prop = volt2::attr::Id = true;\nint main() {}\n' > "$OUT/bool.cpp"
if g++ "${FLAGS[@]}" -fsyntax-only "$OUT/bool.cpp" 2> "$OUT/bool.log"; then
    fail "attr::Id = true was accepted"
elif grep -q 'does not take true/false' "$OUT/bool.log"; then
    echo "  ok"
else
    head -5 "$OUT/bool.log"
    fail "attr::Id = true error message"
fi

# Code that must not compile, with the message that must explain why: refused <name> <code> <message>
refused() {
    printf '#include "volt2.hpp"\nusing namespace volt2;\n%s\nint main() {}\n' "$2" > "$OUT/$1.cpp"
    if g++ "${FLAGS[@]}" -fsyntax-only "$OUT/$1.cpp" 2> "$OUT/$1.log"; then
        fail "$1 was accepted"
    elif grep -q "$3" "$OUT/$1.log"; then
        echo "  ok: $1"
    else
        head -5 "$OUT/$1.log"
        fail "$1 error message"
    fi
}

step "identity mistakes are refused with a clear message"
refused stored-element 'VirtualNode f() { auto b = tag::Div[attr::Id = "x"]; return b("a"); }' 'in one expression'
refused auto-component 'inline constexpr Component C = [](auto x) { return tag::P(x); };' 'fixed parameter types'

step "the README's example compiles"
awk '/^```cpp$/{n++; if (n == 1) {f = 1; next}} f && /^```$/{exit} f' README.md > "$OUT/readme.cpp"
build readme g++ "${FLAGS[@]}" -fsyntax-only "$OUT/readme.cpp" && echo "  ok"

step "g++"
build_all gcc "" g++ "${FLAGS[@]}" -O1 -pthread && test_native gcc ""

if have clang++; then
    step "clang++"
    build_all clang "" clang++ "${FLAGS[@]}" -O1 -pthread && test_native clang ""
fi

step "ThreadSanitizer (the bridge hands messages between threads)"
if build_all tsan "" g++ "${FLAGS[@]}" -O1 -fsanitize=thread -pthread; then
    TSAN_OPTIONS="halt_on_error=0" test_native tsan ""
    races=$(cat "$OUT"/tsan-*.log | grep -c 'WARNING: ThreadSanitizer')
    echo "  data races reported: $races"
    [ "$races" = 0 ] || fail "tsan: $races reports in $OUT/tsan-*.log"
fi

step "AddressSanitizer + UndefinedBehaviorSanitizer"
if build_all asan "" g++ "${FLAGS[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread; then
    ASAN_OPTIONS="detect_leaks=0" test_native asan ""
    reports=$(cat "$OUT"/asan-*.log | grep -c -E 'ERROR: AddressSanitizer|runtime error:')
    echo "  sanitizer reports: $reports"
    [ "$reports" = 0 ] || fail "asan: $reports reports in $OUT/asan-*.log"
fi

# Builds a program with em++ into its own static site and runs its browser tests there.
test_wasm() {  # name, source, "test,test"
    local site="$OUT/site-$1"
    mkdir -p "$site"
    build "wasm-$1" em++ -std=c++23 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I. "$2" -o "$site/app.js" || return
    cp web/index.html web/volt2.js "$site/"
    port=$((port + 1))
    python3 -m http.server "$port" --bind 127.0.0.1 --directory "$site" > "$OUT/static-$1.log" 2>&1 &
    local pid=$!
    if wait_for "http://127.0.0.1:$port/"; then
        for t in ${3//,/ }; do node "tests/$t.test.cjs" "http://127.0.0.1:$port/" || fail "wasm $1: $t test"; done
    else
        fail "wasm $1: static server did not start"
    fi
    kill "$pid" 2> /dev/null
    wait "$pid" 2> /dev/null
}

if have em++; then
    step "WebAssembly (em++ -m64), served as static files"
    for entry in "${PROGRAMS[@]}"; do
        read -r program source _ tests <<< "$entry"
        test_wasm "$program" "$source" "$tests"
    done
else
    step "WebAssembly: skipped (activate Emscripten to include it)"
fi

if have x86_64-w64-mingw32-g++-posix; then
    step "Windows build (MinGW)"
    if build_all mingw .exe x86_64-w64-mingw32-g++-posix "${FLAGS[@]}" -O1 -static -lws2_32; then
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

#!/usr/bin/env bash
# Builds the demos (demos/*.cpp) and runs their browser tests (tests/*.test.cjs) against each build.
#
#   tests/run.sh                  every toolchain this machine has
#   tests/run.sh checks gcc       only these (CI runs one per job)
#
# Toolchains:
#   checks   the embedded page client, compile-time refusals, the README example, the
#            render benchmark (g++)
#   gcc, clang, tsan (ThreadSanitizer), asan (AddressSanitizer + UndefinedBehaviorSanitizer)
#   wasm     em++ (Emscripten, activated), served as static files
#   mingw    the Windows build with MinGW g++: cross-compiled and run with wine on Linux, or
#            built and run natively on Windows (in an MSYS2 shell)
#   msvc     cl.exe, from Git Bash in a Visual Studio developer environment on Windows
#
# Needs: curl, Node with Playwright (npm install, then npx playwright install chromium), and
# Python 3 for checks and wasm.
set -uo pipefail
cd "$(dirname "$0")/.."
export NODE_PATH="${NODE_PATH:-$(npm root -g 2>/dev/null)}"

OUT=build/tests
rm -rf "$OUT" && mkdir -p "$OUT"
FLAGS=(-std=c++23 -Wall -Wextra -Wpedantic -Werror -g -I.)
MSVC=(-nologo -std:c++23preview -EHsc -W4 -WX -permissive- -utf-8 -Zc:__cplusplus -I.)
# name, source, tests on development builds, tests on WebAssembly builds
PROGRAMS=(
    "counter demos/counter.cpp counter counter"
    "markup demos/markup.cpp markup markup"
    "bridge demos/bridge.cpp bridge,reload bridge"
    "lifetime demos/lifetime.cpp lifetime lifetime"
    "identity demos/identity.cpp identity identity"
    "diff demos/diff.cpp diff diff"
    "locals demos/locals.cpp locals locals"
)
port=8900
failed=()

have() { command -v "$1" > /dev/null 2>&1; }
step() { printf '\n== %s\n' "$1"; }
fail() { echo "FAILED: $1"; failed+=("$1"); }

TOOLCHAINS=(checks gcc clang tsan asan wasm mingw msvc)
SELECTED=("$@")
windows() { [ "${OS:-}" = Windows_NT ]; }
# MinGW: the cross compiler on Linux, or on Windows the g++ of the shell (MSYS2 UCRT64).
mingw_compiler() {
    if have x86_64-w64-mingw32-g++-posix; then
        echo x86_64-w64-mingw32-g++-posix
    elif windows && have g++; then
        echo g++
    fi
}
available() {
    case $1 in
        checks | gcc | tsan | asan) ! windows && have g++ ;;
        clang) have clang++ ;;
        wasm) have em++ ;;
        mingw) [ -n "$(mingw_compiler)" ] ;;
        msvc) have cl.exe ;;
        *) return 1 ;;
    esac
}
# Whether to run a toolchain: the ones named (which must be here), or with none named, each
# one this machine has.
wanted() {
    if [ ${#SELECTED[@]} -eq 0 ]; then
        available "$1"
        return
    fi
    for name in "${SELECTED[@]}"; do
        if [ "$name" = "$1" ]; then
            available "$1" && return 0
            fail "$1: not installed on this machine"
            return 1
        fi
    done
    return 1
}
for name in "${SELECTED[@]}"; do
    [[ " ${TOOLCHAINS[*]} " == *" $name "* ]] || { echo "unknown toolchain: $name (known: ${TOOLCHAINS[*]})"; exit 2; }
done

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
    local pid=$! program=${!#}
    if wait_for "$url"; then
        for t in $tests; do node "tests/$t.test.cjs" "$url" || fail "$name: $t test"; done
    else
        fail "$name: did not start (see $OUT/$name.log)"
    fi
    stop "$pid" "$program"
}

# Stops a program started in the background. On Windows, a native program is stopped by name.
stop() {  # pid, log name
    if have taskkill.exe; then taskkill.exe //F //T //IM "$(basename "$2")" > /dev/null 2>&1; fi
    kill "$1" 2> /dev/null
    wait "$1" 2> /dev/null
}

test_native() {  # toolchain, command prefix (empty, or wine), executable suffix
    local name=$1 prefix=$2 suffix=${3:-} program tests
    for entry in "${PROGRAMS[@]}"; do
        read -r program _ tests _ <<< "$entry"
        run_dev "$name-$program" "${tests//,/ }" $prefix "$OUT/$program-$name$suffix"
    done
}

if wanted checks; then
    step "embedded page client is up to date"
    python3 tools/embed_js.py --check || fail "embed check"

    step "C++20 is refused with a clear message"
    if g++ -std=c++20 -fsyntax-only -I. demos/counter.cpp 2> "$OUT/cxx20.log"; then
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

    # On CI, where the network is open: the stylesheets the demos load exist.
    if [ -n "${CI:-}" ]; then
        step "the stylesheets the demos load exist"
        for url in $(grep -oh 'Stylesheet("https://[^"]*' demos/*.cpp | cut -d'"' -f2 | sort -u); do
            if curl -sfL --max-time 30 -o /dev/null "$url"; then echo "  ok: $url"; else fail "missing: $url"; fi
        done
    fi

    step "render speed (for information)"
    build bench g++ "${FLAGS[@]}" -O2 -pthread tests/bench/main.cpp -o "$OUT/bench" && { "$OUT/bench" || fail "bench"; }
fi

if wanted gcc; then
    step "g++"
    build_all gcc "" g++ "${FLAGS[@]}" -O1 -pthread && test_native gcc ""
fi

if wanted clang; then
    step "clang++"
    build_all clang "" clang++ "${FLAGS[@]}" -O1 -pthread && test_native clang ""
fi

if wanted tsan; then
    step "ThreadSanitizer (the bridge hands messages between threads)"
    if build_all tsan "" g++ "${FLAGS[@]}" -O1 -fsanitize=thread -pthread; then
        TSAN_OPTIONS="halt_on_error=0" test_native tsan ""
        races=$(cat "$OUT"/tsan-*.log | grep -c 'WARNING: ThreadSanitizer')
        echo "  data races reported: $races"
        [ "$races" = 0 ] || fail "tsan: $races reports in $OUT/tsan-*.log"
    fi
fi

if wanted asan; then
    step "AddressSanitizer + UndefinedBehaviorSanitizer"
    if build_all asan "" g++ "${FLAGS[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread; then
        ASAN_OPTIONS="detect_leaks=0" test_native asan ""
        reports=$(cat "$OUT"/asan-*.log | grep -c -E 'ERROR: AddressSanitizer|runtime error:')
        echo "  sanitizer reports: $reports"
        [ "$reports" = 0 ] || fail "asan: $reports reports in $OUT/asan-*.log"
    fi
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

if wanted wasm; then
    step "WebAssembly (em++ -m64), served as static files"
    em++ --version | head -1
    for entry in "${PROGRAMS[@]}"; do
        read -r program source _ tests <<< "$entry"
        test_wasm "$program" "$source" "$tests"
    done
fi

if wanted mingw; then
    step "Windows build (MinGW)"
    MINGW_CXX=$(mingw_compiler)
    "$MINGW_CXX" --version | head -1
    if build_all mingw .exe "$MINGW_CXX" "${FLAGS[@]}" -O1 -static -lws2_32; then
        if windows; then
            test_native mingw "" .exe
        elif have wine; then
            export WINEPREFIX="${WINEPREFIX:-$PWD/build/wineprefix}" WINEDEBUG=-all
            test_native mingw wine .exe
            wineserver -k 2> /dev/null
        elif [ ${#SELECTED[@]} -eq 0 ]; then
            echo "  built; install wine to run it"
        else
            fail "mingw: wine is not installed, so the build cannot run"
        fi
    fi
fi

# MSVC: a Debug build, as with F5 in Visual Studio (debug iterators and runtime checks on),
# warnings as errors. Options are written with - so Git Bash leaves them alone.
msvc_refused() {  # name, code, message: must not compile, saying why
    printf '#include "volt2.hpp"\nusing namespace volt2;\n%s\nint main() {}\n' "$2" > "$OUT/msvc-$1.cpp"
    if cl.exe "${MSVC[@]}" -Zs "$OUT/msvc-$1.cpp" > "$OUT/msvc-$1.log" 2>&1; then
        fail "msvc: $1 was accepted"
    elif grep -q "$3" "$OUT/msvc-$1.log"; then
        echo "  ok: $1"
    else
        head -5 "$OUT/msvc-$1.log"
        fail "msvc: $1 error message"
    fi
}

if wanted msvc; then
    step "MSVC (Debug)"
    cl.exe 2>&1 | head -1
    if cl.exe -nologo -std:c++20 -EHsc -Zs -I. demos/counter.cpp > "$OUT/msvc-cxx20.log" 2>&1; then
        fail "msvc: a C++20 build was accepted"
    elif grep -q 'volt2 needs C++23' "$OUT/msvc-cxx20.log"; then
        echo "  ok: C++20 is refused: $(grep -m1 -o 'volt2 needs C++23[^"]*' "$OUT/msvc-cxx20.log")"
    else
        head -5 "$OUT/msvc-cxx20.log"
        fail "msvc: C++20 error message"
    fi
    msvc_refused bool 'auto prop = attr::Id = true;' 'does not take true/false'
    msvc_refused stored-element 'VirtualNode f() { auto b = tag::Div[attr::Id = "x"]; return b("a"); }' 'in one expression'
    msvc_refused auto-component 'inline constexpr Component C = [](auto x) { return tag::P(x); };' 'fixed parameter types'
    status=0
    for entry in "${PROGRAMS[@]}"; do
        read -r program source _ _ <<< "$entry"
        build "msvc-$program" cl.exe "${MSVC[@]}" -Od -MDd -RTC1 "$source" "-Fe:$OUT/$program-msvc.exe" \
            "-Fo:$OUT/$program-msvc.obj" || status=1
    done
    [ $status = 0 ] && test_native msvc "" .exe
fi

echo
if [ ${#failed[@]} -eq 0 ]; then
    echo "ALL PASSED"
else
    printf 'FAILED:\n'; printf '  %s\n' "${failed[@]}"
    exit 1
fi

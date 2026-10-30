# ⚡ volt2

**Write a website in plain C++. Press F5 to see it in your browser. Use Emscripten only when you deploy.**

```cpp
#include "volt2.hpp"

using volt2::js::Value;

void site(volt2::Session&) {
    Value document = Value::global("document");
    Value button = document.call("createElement", "button");
    button.set("textContent", "Click me");
    button.call("addEventListener", "click", volt2::js::function([=] {
        button.set("textContent", "Clicked from C++");
    }));
    document["body"].call("appendChild", button);
}

int main() { return volt2::run(site); }
```

- **While developing,** this is a normal native program: your compiler, your debugger,
  breakpoints in UI code. `volt2::run` serves the page on `http://127.0.0.1:8080/`, opens
  your browser, and your C++ talks to the page through a local connection.
- **To deploy,** compile the same file with Emscripten. The site becomes static files
  (`index.html`, `volt2.js`, `app.js`, `app.wasm`) for any web host. No server.

## Develop

Copy `volt2.hpp` next to your code. It needs C++20.

**Visual Studio:** create a C++ Console App, set *Project Properties > C/C++ > Language >
C++ Language Standard* to ISO C++20 (or later), add your `main.cpp`, and press F5. The browser
opens on your site. Breakpoints in your handlers work; the page waits while you're stopped.

**g++ / clang:**

```bash
g++ -std=c++20 -I. examples/counter/main.cpp -o counter -pthread
./counter
```

With MinGW, add `-lws2_32`. MSVC links the socket library automatically.

Environment variables: `VOLT2_PORT=8123` picks the port (default: the first free one from 8080),
`VOLT2_NO_BROWSER=1` stops the browser opening. The same options exist in code:
`volt2::run(site, {.port = 8123, .openBrowser = false})`.

## Deploy

With [Emscripten](https://emscripten.org) 6.0 or newer activated:

```bash
em++ -std=c++20 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I. main.cpp -o site/app.js
cp web/index.html web/volt2.js site/
```

Upload `site/` to any static host. `-fexceptions` and `-lembind` are required; volt2 tells you
at compile time if exceptions are off.

## What you can do today

This first version is the foundation: C++ and the page calling each other. Rendering
(Volt's identity-preserving diff) comes next.

| C++ | JavaScript equivalent |
|---|---|
| `Value::global("document")` | `globalThis.document` |
| `v["name"]`, `v[0]` | `v.name`, `v[0]` |
| `v.set("name", x)` | `v.name = x` |
| `v.call("method", a, b)` / `v.call<int>(...)` | `v.method(a, b)`, converted to `int` |
| `f(a, b)` | `f(a, b)` for a function value |
| `v.new_(a)` | `new v(a)` |
| `v.as<bool>()`, `as<double>()`, `as<int>()`, `as<std::string>()` | truthiness and checked conversions |
| `v.typeOf()`, `v.strictlyEquals(w)` | `typeof v`, `v === w` |
| `volt2::js::function(lambda)` | a JS function that calls your C++ |

`Value` mirrors the shape of `emscripten::val`. Every call is synchronous and can nest in both
directions: a C++ handler can read `window.someFlag`, call a page function, or trigger a listener
that calls C++ again, all before it returns. A JavaScript exception becomes a `volt2::js::Error`;
a C++ exception thrown from a `js::function` becomes a JavaScript `Error` in the caller.

`js::function` takes a lambda with `(const volt2::js::Args&)`, `(const volt2::js::Value& first)` or
no parameters, returning `void` or anything that can be passed to JavaScript.

## How it works

Both builds exchange the same JSON messages, and the same `volt2.js` runs them, so conversions,
errors and handle lifetimes behave identically. Only the transport differs:

- **WebAssembly:** direct calls between the module and `volt2.js`.
- **Development:** the page waits inside a *synchronous* request to your program, and your program
  sends its questions back as the reply to that request. Whoever waits keeps serving the other
  side, which is what lets calls nest. See [the experiment that proved it](experiments/sync-bridge/README.md).

JavaScript objects that C++ holds are kept in a table in the page and released when the last C++
`Value` referring to one is destroyed.

## Current behaviour and limits

- In development every operation is a round trip to the page, about 2 ms in our tests. Batching
  writes comes with the rendering engine.
- Reloading the page runs your site again from scratch. A second tab replaces the first, which is
  told so on its next call. (One page at a time for now.)
- C++ functions passed to JavaScript live until the page is reloaded.
- Your site and every handler run on the thread that called `volt2::run`, one at a time, like
  JavaScript.
- Development relies on synchronous requests on the page's main thread, which browsers deprecate
  but still support. Only development uses them.

## Tests

`tests/run.sh` builds `examples/counter` every way the machine supports and runs the browser tests
(`tests/counter.test.cjs`, `tests/reload.test.cjs`) against each build: g++, clang, ThreadSanitizer,
AddressSanitizer with UndefinedBehaviorSanitizer, WebAssembly (with Emscripten activated), and the
Windows build via MinGW and Wine when installed. It needs Node and Playwright:

```bash
npm install
npx playwright install chromium
tests/run.sh
```

After editing `web/volt2.js`, run `python3 tools/embed_js.py` to update the copy inside `volt2.hpp`.

## Roadmap

1. ~~The bridge, on both builds~~ (this version)
2. Rendering: port Volt's tree and diff onto `js::Value`, with write batching
3. Events and inputs: event values, `preventDefault`, focus protection
4. Browser APIs: timers, fetch, storage, console, each driven by JavaScript callbacks
5. State and sessions
6. Deploy tooling and CI for both builds

## License

MIT, see [LICENSE](LICENSE).

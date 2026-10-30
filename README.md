# ⚡ volt2

**Write a website in plain C++. Press F5 to see it in your browser. Use Emscripten only when you deploy.**

```cpp
#include "volt2.hpp"

void site(volt2::Session& session) {
    // build the page
}

int main() { return volt2::run(site); }
```

- **While developing,** your C++ runs as a normal native program: your compiler, your
  debugger, breakpoints in UI code. `volt2::run` opens your real browser, and the page
  talks to your program through a local connection.
- **To deploy,** compile the same `main.cpp` with Emscripten. The site becomes static
  files (`index.html`, `volt2.js`, `app.js`, `app.wasm`) for any web host. No server.

## How it works

C++ and JavaScript call each other **synchronously** in both builds, with calls nesting
in both directions, exactly like WebAssembly:

- **WebAssembly:** direct calls.
- **Development:** the page waits inside a synchronous request to your program, and your
  program sends its questions back as the reply to that request. Whoever waits keeps
  serving the other side, so a C++ handler can read `window.someValue`, call a JS
  function, or trigger a listener that calls C++ again, all before it returns.

See [the experiment that proved it](experiments/sync-bridge/README.md).

## Status

Early. The bridge is proven; the first C++ slice (the bridge itself, on both builds)
is in progress. Rendering (porting [Volt](https://github.com/vdcoder/volt)'s
identity-preserving diff), events and inputs, browser APIs, and state come next.

## License

MIT, see [LICENSE](LICENSE).

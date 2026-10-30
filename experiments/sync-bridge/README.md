# Experiment: a synchronous bridge between a native process and a real browser

**Question.** In WebAssembly, C++ and JavaScript call each other synchronously, and
the calls can nest: JS calls C++, which reads a JS value, which runs a JS listener,
which calls C++ again. Can a native C++ process (no Emscripten) and an ordinary
browser tab do the same, so that volt2 code behaves identically during development
and after deployment?

**Answer: yes.** Each side, while waiting for the other, keeps serving the other
side's requests. The page waits inside a *synchronous* HTTP request (the one
main-thread primitive that both blocks and brings data back), and the native side
sends its questions as the reply to that pending request.

```
page: click -> native('onClick') ........ blocked in a synchronous request
native:        handler runs, needs window.peter_pan
native:        replies "tell me window.peter_pan"   (the reply to the pending request)
page:          evaluates it, sends "result: true" in a new synchronous request
native:        handler continues, finishes, replies "return: done"
page: native('onClick') returns, like an ordinary JS function
```

A WebSocket alone cannot do this: a JS loop waiting on one never receives
anything, because messages are only delivered once JS returns to the browser.

## Files

| File | What it is |
|---|---|
| `native.py` | Stand-in for the native C++ side: one thread owns all "app" logic, like the UI thread. |
| `page.html` | The page side: `native()` waits in a synchronous request and performs requested operations while waiting. |
| `run.cjs` | Nested calls in both directions, plus round-trip timing. |
| `peter.cjs` | The `window.peter_pan` scenario, optionally with a long pause in the handler (a breakpoint). |
| `floor.cjs` | Browser-side cost floor, measured against a do-nothing Node server. |

Run with Node and Playwright (`npm i playwright`, then `npx playwright install chromium`):

```bash
node run.cjs 8701
node peter.cjs 8720 0      # second argument: seconds the handler pauses first
node floor.cjs
```

## Results (Chromium 153, headless, Linux container, October 2026)

- Nested calls work in both directions: page -> native `onClick` -> native reads an
  input and writes the page -> reads layout width (66 px, so the write had landed) ->
  native clicks a second button -> its listener calls native `onInner` -> native reads
  the page again ("Hello Volt") -> both stacks unwind in order.
- `window.peter_pan` read by the native handler while the page is blocked: `true`,
  then `false` after the page changes it.
- A 150 second pause inside the handler (a developer at a breakpoint) still completes
  normally: no timeout, no console errors. Headless only; a visible Chrome may show its
  "Page unresponsive" dialog if you interact with the frozen page, and "Wait" continues.
- About 2 ms per synchronous round trip here. A do-nothing Node server measures the
  same, so the cost is in the browser, not the native side.
- Keep-alive without `TCP_NODELAY` costs about 45 ms per call (Nagle's algorithm plus
  delayed ACKs on small replies). The real server must set `TCP_NODELAY`.

## Consequences for volt2

- Writes (create elements, set properties) need no reply, so they can be queued and
  sent in order; any later read flushes them first, so read-after-write holds.
- Reads and calls with results are synchronous in both builds: direct calls in
  WebAssembly, about 2 ms round trips in development.
- JS calling C++ is synchronous too, and can nest to any depth.
- If C++ only ever runs inside a JS call (main, events, timers, fetch callbacks),
  which is also how WebAssembly works, the native side never needs to wake an idle
  page, so plain HTTP is enough.
- Synchronous requests on the page's main thread are deprecated but still supported.
  volt2 only relies on them during development; a worker plus `SharedArrayBuffer`
  wait could replace them if browsers ever remove them.

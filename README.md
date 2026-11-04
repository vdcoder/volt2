# ⚡ volt2

**Write a website in plain C++. Press F5 to see it in your browser. Use Emscripten only when you deploy.**

```cpp
#include "volt2.hpp"
using namespace volt2;

struct Counter : Session {  // one per page load: its members are the page's state
    int count = 0;
};

VirtualNode site(Counter& s) {
    return tag::Div[attr::Id = "app", attr::Style = "font-family: sans-serif; padding: 20px"](
        tag::H1("Hello from C++"),
        tag::P("Clicked ", s.count, " times"),
        tag::Button[event::OnClick([&s] { ++s.count; })]("Click me"));
}

int main() { return run(site); }
```

- **While developing,** this is a normal native program: your compiler, your debugger,
  breakpoints in UI code. `volt2::run` serves the page on `http://127.0.0.1:8080/`, opens
  your browser, and your C++ talks to the page through a local connection.
- **To deploy,** compile the same file with Emscripten. The site becomes static files
  (`index.html`, `volt2.js`, `app.js`, `app.wasm`) for any web host. No server.

See [`examples/counter`](examples/counter/main.cpp) for a counter, a greeting typed live, a panel
that opens and closes, and a list.

## Develop

Copy `volt2.hpp` next to your code. It needs C++23.

**Visual Studio:** create a C++ Console App, set *Project Properties > C/C++ > Language >
C++ Language Standard* to *Preview - ISO C++23 (/std:c++23preview)*, add your `main.cpp`, and press
F5. The browser opens on your site. Breakpoints in your handlers work; the page waits while you're
stopped. (Set the standard on the project, not on `main.cpp` alone, so every file gets it.)

**g++ / clang:**

```bash
g++ -std=c++23 -I. examples/counter/main.cpp -o counter -pthread
./counter
```

With MinGW, add `-lws2_32`; MSVC links the socket library automatically. (MinGW GCC 13 cannot link a
C++23 program built with both `-static` and no optimisation, with or without volt2: drop `-static`
or add `-O1`.) With an older standard, volt2 stops with a message saying which setting to change.

Environment variables: `VOLT2_PORT=8123` picks the port (default: the first free one from 8080),
`VOLT2_NO_BROWSER=1` stops the browser opening, `VOLT2_WARN_AT=500` sets when the leak warning
starts (see [Lifetimes](#lifetimes)). The same options exist in code:
`volt2::run(site, {.port = 8123, .openBrowser = false})`.

## Deploy

With [Emscripten](https://emscripten.org) 6.0 or newer activated:

```bash
em++ -std=c++23 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I. main.cpp -o site/app.js
cp web/index.html web/volt2.js site/
```

Upload `site/` to any static host. `-fexceptions` and `-lembind` are required; volt2 tells you
at compile time if exceptions are off.

## Sessions

Your session type derives from `volt2::Session`. volt2 creates one for each page load and
destroys it when that page goes away (reloaded, or replaced by another tab):

- its **members** are the page's state,
- its **constructor** is the one-time setup,
- its **destructor** is the cleanup.

`invalidate()` (a `Session` member) renders the page again. Event handlers do that by themselves;
call it from C++ that changes state some other way, such as a `js::function` the page calls.

`volt2::run(site)` works out the session type from `site`'s parameter. Any default-constructible
type derived from `volt2::Session` works; anything else is a compile error.

## Markup

```cpp
tag::Div[attr::Id = "main", attr::Class = "card", event::OnClick(handler)](
    tag::H2("Title"),
    tag::Br())
```

An element is `tag::Name[props...](children...)`; with no props, `tag::Name(children...)`.

- **Tags:** `tag::Div`, `tag::Button`, `tag::Input`, ... every HTML element, in PascalCase, so names
  that are C++ keywords need no underscores (`tag::Template`). Make your own with
  `inline constexpr volt2::Tag MyWidget{"my-widget"};`.
- **Attributes:** `attr::Id = "x"`, `attr::TabIndex = 3`, `attr::Data("role") = "menu"` for
  `data-role`, `attr::Aria("label") = "..."`, and `attr::Named("anything") = "..."` for the rest.
  Integers are written exactly. `= true` and `= false` follow each attribute: HTML boolean attributes
  (`attr::Disabled`, `attr::Checked`, `attr::Hidden`, ...) are written or left out; `attr::Draggable`,
  `attr::SpellCheck`, `attr::ContentEditable`, `attr::Aria(...)` and `attr::Data(...)` get `"true"` or
  `"false"`; any other attribute refuses `true`/`false` at compile time, so write the string you mean.
- **Events:** `event::OnClick`, `event::OnInput`, `event::OnSubmit`, `event::OnKeyDown`, ... and
  `event::On("my-event", handler)` for any other type. A handler takes the event (`const Value&`)
  or nothing; the event's `currentTarget` is the handler's own element. After a handler runs, volt2
  calls your site again and updates the page. `e.call("preventDefault")` and
  `e.call("stopPropagation")` work, also while developing, because every call to the page is
  synchronous.
- **Children:** elements, text, numbers, `std::optional<VirtualNode>`, and ranges of elements, such
  as a `std::vector<VirtualNode>`, `items | std::views::transform(...)` or `Map(items, ...)`.
- **Nothing:** `None` stands for no prop between the brackets and no node between the parentheses, so
  conditionals type-check in both places:

  ```cpp
  tag::Input[s.locked ? attr::ReadOnly = true : None]()
  tag::Span[s.tip.empty() ? None : attr::Title = s.tip, s.locked ? None : event::OnClick(onClick)]()
  s.open ? tag::P("details") : None
  ```

  An attribute given an empty `std::optional` (`attr::Title = s.tooltip`) or a null `const char*` is
  left out too, and a null `const char*` child renders nothing.

Text is always text: `tag::P("<b>hi</b>")` shows the angle brackets, it never makes HTML.

The page is rendered into the element with `id="volt2"`, or into a new one at the end of `<body>`.

## Components and identity

Every render calls your site again, and volt2 changes only what differs on the page. Elements keep
their DOM nodes from one render to the next, and with them the focus, the caret, typed text, scroll
positions and playing media. An element counts as the same one as last time when the same code, in
the same place, made it:

- **Call sites.** Every `tag::Name(...)` and `tag::Name[...]` knows where it is in your code, so the
  two branches of `open ? tag::P("a") : tag::P("b")` are different elements, and so are two
  `tag::Li`s on different lines. Write `tag::Name[props](children)` in one expression: a
  `tag::Name[props]` kept in a variable and used later is a compile error, because all its uses would
  be the same element.
- **Components.** Wrap a function that builds part of the page in `volt2::Component` where you define
  it, and each place that calls it gets elements of its own:

  ```cpp
  inline constexpr Component Card = [](std::string title) {
      return tag::Div[attr::Class = "card"](tag::H2(title));
  };

  tag::Main(Card("one"), Card("two"))
  ```

  A component that needs the session takes it as a parameter, like
  `[](Shop& s, const Item& item) { ... }`. Parameters can be of any kind except `auto`. A lambda that
  captures cannot be `constexpr`: make it a local, `const Component Row = [&s](std::string label) { ... };`,
  or a session member with its signature written out:
  `Component<VirtualNode(std::string)> Banner = [this](std::string text) { ... };`. Plain functions
  work too (`inline constexpr Component Badge = badge;`). A function that is not wrapped still
  works, but all its calls count as the same element, told apart only by their order.
- **Lists.** The items of a range are told apart by their position. When items can move, or come and
  go in the middle, give each one `attr::Key`, a string or an integer unique in its list, and each
  keeps its element. `Map(items, [](const T& item) { ... })`, which may also take the index, and
  `Loop(n, [](std::size_t i) { ... })` are Volt's `map` and `loop`.
- **Ids.** An element with `attr::Id` is the same element wherever it is on the page, also under
  another parent: it moves there with everything in it. A new id makes it a new element, and so does
  a new key, which is how to start an element over (`tag::Form[attr::Key = s.userId](...)`). Ids must
  be unique; volt2 warns in the console when two elements share one, and when two items of a list
  share a key.
- **Form controls.** `attr::Value`, `attr::Checked` and `attr::Selected` also set what the control
  shows, each time your rendered value changes. An `OnInput` handler that stores the text renders the
  same text back, which leaves the caret where it is.
- **Focus.** When elements reorder, the others move around the one holding the focus. An element that
  has to move anyway, such as one with an id going to another parent, gets its focus and selection
  back.

## Talking to JavaScript directly

Pages can also be built by hand, or mix in browser APIs, with `volt2::js::Value`. A session whose
constructor builds the page runs with `volt2::run<MyPage>()`; a `void site(MyPage&)` function runs
once after the constructor.

| C++ | JavaScript equivalent |
|---|---|
| `Value::global("document")` | `globalThis.document` |
| `v["name"]`, `v[0]` | `v.name`, `v[0]` |
| `v.set("name", x)` | `v.name = x` |
| `v.call("method", a, b)` / `v.call<int>(...)` | `v.method(a, b)`, converted to `int` |
| `f(a, b)` | `f(a, b)` for a function value |
| `v.new_(a)` | `new v(a)` |
| `v.as<bool>()`, `as<double>()`, `as<int>()`, `as<std::string>()` | truthiness and checked conversions |
| `v.typeOf()`, `v.strictlyEquals(w)` | `typeof v`, `v === w` (objects compare locally) |
| `v == w`, `std::hash<Value>` | JS `Map` key equality, so a `Value` can key an `unordered_map` |
| `volt2::js::function(lambda)` | a `js::Function`: a JS function that calls your C++, while you hold it |
| `volt2::stats()` | how many page objects and functions C++ holds |

`Value` mirrors the shape of `emscripten::val`. Every call is synchronous and can nest in both
directions: a C++ handler can read `window.someFlag`, call a page function, or trigger a listener
that calls C++ again, all before it returns. A JavaScript exception becomes a `volt2::js::Error`;
a C++ exception thrown from a `js::function` becomes a JavaScript `Error` in the caller.

`js::function` takes a lambda with `(const volt2::js::Args&)`, `(const volt2::js::Value& first)` or
no parameters, returning `void` or anything that can be passed to JavaScript.

## Lifetimes

- **Page objects.** A JavaScript object stays alive while any `Value` refers to it, and is released
  when the last one is destroyed. Each object has exactly one handle, so `strictlyEquals` and `==`
  compare objects without asking the page, and a `Value` can be a map key (for example by element).
- **C++ functions.** `volt2::js::function` returns a `js::Function`, which works for as long as you
  hold it. Copies share it; when the last copy is destroyed the C++ function (and everything its
  lambda captured) is released, and calling it from JavaScript throws. Pass it to JavaScript by
  name, from a variable that lives long enough, typically a member of your session. Passing a
  temporary, such as `call("addEventListener", "click", volt2::js::function(...))`, is a compile
  error, because it would be released when the statement ends. Markup event handlers need none of
  this: they live in the rendered tree.
- **Threads.** Values, functions and `volt2::stats()` belong to the thread that called `volt2::run`;
  using them on another thread throws a `js::Error`. Destroying one on another thread is safe: the
  release is queued and applied on volt2's thread before its next operation or call, so a released
  function can never be called again.
- **Leaks.** `volt2::stats()` reports how many page objects and functions C++ holds. While developing,
  volt2 warns in your console and the browser console when either passes 10,000 (`VOLT2_WARN_AT`),
  then again each time the count doubles.
- **Reloads.** A page reload destroys that page's session and releases everything that belonged to
  it. A session destructor cannot reach JavaScript anymore: such calls do nothing.

## How it works

Both builds exchange the same JSON messages, and the same `volt2.js` runs them, so conversions,
errors and handle lifetimes behave identically. Only the transport differs:

- **WebAssembly:** direct calls between the module and `volt2.js`.
- **Development:** the page waits inside a *synchronous* request to your program, and your program
  sends its questions back as the reply to that request. Whoever waits keeps serving the other
  side, which is what lets calls nest. See [the experiment that proved it](experiments/sync-bridge/README.md).

Rendering compares the new tree with the last one and sends only the differences, in one message,
or none when nothing changed; `volt2.js` applies them, moving as few elements as it can. Events use
one listener per event type on the root element, which sends C++ the elements with handlers, from
the target upward, in one call; C++ runs their handlers and renders once.

## Current behaviour and limits

- In development every operation is a round trip to the page, about 2 ms in our tests; an event
  and the render after it are one or two.
- Reloading the page runs your site again from scratch with a new session. A second tab replaces the
  first, which is told so on its next call. (One page at a time for now.)
- Your site and every handler run on the thread that called `volt2::run`, one at a time, like
  JavaScript.
- Development relies on synchronous requests on the page's main thread, which browsers deprecate
  but still support. Only development uses them.

## Tests

`tests/run.sh` builds five programs every way the machine supports and runs their browser tests
against each build:

| Program | Browser tests |
|---|---|
| `examples/counter` | `counter`: rendering, events, conditionals, lists |
| `tests/markup` | `markup`: attributes, text, bubbling, `preventDefault`, event values, sessions |
| `tests/bridge` | `bridge`, `reload`: calls in both directions, errors, page reloads and tabs |
| `tests/lifetime` | `lifetime`: functions, object identity, threads, leak warnings |
| `tests/identity` | `identity`: typing, keys, ids, components, focus, form controls, minimal updates |

The builds are g++, clang, ThreadSanitizer, AddressSanitizer with UndefinedBehaviorSanitizer,
WebAssembly (with Emscripten activated), and the Windows build via MinGW and Wine when installed.
It also checks that C++20, and the mistakes described above, are refused with a clear message. It
needs Node and Playwright:

```bash
npm install
npx playwright install chromium
tests/run.sh
```

After editing `web/volt2.js`, run `python3 tools/embed_js.py` to update the copy inside `volt2.hpp`.

## Roadmap

1. ~~The bridge, on both builds~~, with ~~lifetimes~~
2. ~~Markup and sessions~~
3. ~~Identity: renders keep elements, focus and typed text~~ (this version)
4. Async: awaiting JavaScript promises with C++20 coroutines, and JavaScript awaiting C++
5. Browser APIs: timers, fetch, storage, console, each driven by JavaScript callbacks
6. State: change tracking, state per component (`volt2::local`), and shared memory for sessions
7. Deploy tooling and CI for both builds

## License

MIT, see [LICENSE](LICENSE).

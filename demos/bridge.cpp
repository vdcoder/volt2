// The bridge demo: a page built by hand with volt2::js::Value, no markup. It shows C++ state,
// reading page values and calling page functions in the middle of a handler, nested calls,
// errors in both directions, and C++ functions that page JavaScript can call.
// tests/bridge.test.cjs and tests/reload.test.cjs drive it; each card says what it checks.
#include "volt2.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using volt2::js::Function;
using volt2::js::Value;

namespace {

// The look shared by the demos: Pico CSS, the Inter font, and a few rules of our own.
const std::vector<volt2::head::Item> kLook = {
    volt2::head::Pico,
    volt2::head::GoogleFont("Inter:wght@400;600;700"),
    volt2::head::Css(R"(
        :root { --pico-font-family: Inter, system-ui, sans-serif; }
        main.container { max-width: 52rem; padding-block: 2rem; }
        .about { margin-bottom: 0.4rem; font-size: 0.92rem; }
        .about strong { color: var(--pico-primary); }
        .live { margin-top: 1rem; padding: 1rem 1rem 0.25rem; border: 1px dashed var(--pico-muted-border-color);
                border-radius: var(--pico-border-radius); }
        .live button { margin: 0 0.5rem 0.75rem 0; }
        code { font-size: 0.85em; }
    )"),
};

// Adds an element with an optional id and text to `parent`, and returns it.
Value add(const Value& parent, const char* tag, const char* id, const std::string& text) {
    Value document = parent["ownerDocument"];
    Value element = document.call("createElement", tag);
    if (*id) element.set("id", id);
    if (!text.empty()) element.set("textContent", text);
    parent.call("appendChild", element);
    return element;
}

// A card like the other demos': what it checks, how to try it, what should happen; returns
// the box where the live elements go.
Value card(const Value& page, const std::string& title, const std::string& checks, const std::string& tryIt,
           const std::string& expect) {
    Value article = add(page, "article", "", "");
    add(add(article, "header", "", ""), "strong", "", title);
    auto line = [&](const char* label, const std::string& text) {
        if (text.empty()) return;
        Value p = add(article, "p", "", "");
        p.set("className", "about");
        add(p, "strong", "", label);
        p.call("append", text);
    };
    line("Checks: ", checks);
    line("Try it: ", tryIt);
    line("Expect: ", expect);
    Value live = add(article, "div", "", "");
    live.set("className", "live");
    return live;
}

// The session: one per page load, and its constructor builds the page. A js::Function works
// for as long as it is held, so the session keeps the functions it gives JavaScript as members.
class BridgePage : public volt2::Session {
public:
    BridgePage();

private:
    Value document = Value::global("document");
    Value window = Value::global("window");
    int count = 0;
    Value counter, answer, log, inner, outer, errors, stress;
    Function onCounter, onAsk, onInner, onOuter, onFail, onStress, cppAdd, cppThrow;
};

BridgePage::BridgePage() {
    document.set("title", "volt2 bridge");
    Value page = add(document["body"], "main", "", "");
    page.set("className", "container");
    Value header = add(page, "header", "", "");
    add(header, "h1", "title", "Bridge: a page built by hand");
    add(header, "p", "",
        "This page has no markup: the constructor of BridgePage (demos/bridge.cpp) builds every element with "
        "volt2::js::Value calls, the same calls JavaScript would make, and wires up C++ listeners. While "
        "developing, each call travels to the page and back synchronously.");

    // 1. C++ state, updated by a click.
    Value box = card(page, "C++ state",
                     "a C++ listener (a js::Function the session holds) changes C++ state and writes it into the page.",
                     "click the button a few times; reload the page.",
                     "the count goes up with each click; a reload starts again from 0, with a new session.");
    counter = add(box, "button", "counter", "Clicked 0 times");
    onCounter = volt2::js::function([this] {
        ++count;
        counter.set("textContent", "Clicked " + std::to_string(count) + " times");
    });
    counter.call("addEventListener", "click", onCounter);

    // 2. In the middle of a handler, read a page global and call a page function.
    box = card(page, "Asking the page, mid-handler",
               "a C++ handler reads the page global peter_pan and calls the page function twice() before it "
               "returns: every call to the page is synchronous.",
               "click Ask; then in the browser console type peter_pan = true; twice = x => 2 * x; and click again.",
               "first \"peter_pan is false\", then \"peter_pan is true, twice(21) is 42\".");
    Value ask = add(box, "button", "ask", "Ask the page");
    answer = add(box, "p", "answer", "");
    onAsk = volt2::js::function([this] {
        std::string text = std::string("peter_pan is ") + (Value::global("peter_pan").as<bool>() ? "true" : "false");
        Value twice = Value::global("twice");
        if (twice.typeOf() == "function") text += ", twice(21) is " + std::to_string(twice(21).as<int>());
        answer.set("textContent", text);
    });
    ask.call("addEventListener", "click", onAsk);

    // 3. Nested calls: clicking `inner` from C++ runs its C++ listener before click() returns.
    box = card(page, "Nested calls",
               "Outer's C++ listener calls inner.click(); inner's C++ listener runs inside that call, before it "
               "returns. Calls nest in both directions.",
               "click Outer.", "outer-start;inner;outer-end, in that order.");
    inner = add(box, "button", "inner", "Inner");
    outer = add(box, "button", "outer", "Outer");
    log = add(box, "p", "log", "");
    onInner = volt2::js::function([this] { log.set("textContent", log["textContent"].as<std::string>() + "inner;"); });
    inner.call("addEventListener", "click", onInner);
    onOuter = volt2::js::function([this] {
        log.set("textContent", "outer-start;");
        inner.call("click");
        log.set("textContent", log["textContent"].as<std::string>() + "outer-end");
    });
    outer.call("addEventListener", "click", onOuter);

    // 4. A JavaScript error becomes a C++ exception.
    box = card(page, "JavaScript errors in C++",
               "calling a function the page does not have throws a TypeError in the page, which reaches C++ as a "
               "volt2::js::Error that C++ catches.",
               "click the button.", "\"C++ caught: TypeError: ... is not a function\".");
    Value fail = add(box, "button", "fail", "Call a missing function");
    errors = add(box, "p", "errors", "");
    onFail = volt2::js::function([this] {
        try {
            Value::global("doesNotExist")();
        } catch (const volt2::js::Error& e) {
            errors.set("textContent", std::string("C++ caught: ") + e.what());
        }
    });
    fail.call("addEventListener", "click", onFail);

    // 5. C++ functions that page JavaScript can call; a C++ exception becomes a JS Error.
    box = card(page, "C++ functions for JavaScript",
               "window.cppAdd and window.cppThrow are C++ functions page JavaScript can call. Wrong argument types "
               "are reported; a C++ exception becomes a JavaScript Error.",
               "in the browser console: cppAdd(2, 3.5), then cppAdd('a', 1), then cppThrow().",
               "5.5; then an error \"expected a number, got string\"; then an Error \"thrown in C++\".");
    add(box, "p", "", "Nothing to click here: use the browser console.");
    cppAdd = volt2::js::function([](const volt2::js::Args& args) {
        return args.at(0).as<double>() + args.at(1).as<double>();
    });
    window.set("cppAdd", cppAdd);
    cppThrow = volt2::js::function([]() -> Value { throw std::runtime_error("thrown in C++"); });
    window.set("cppThrow", cppThrow);

    // 6. Short-lived values: each read creates a page-side handle that must be released.
    box = card(page, "Short-lived values",
               "each read of document.body gives C++ a handle to a page object; when the Value goes, the handle is "
               "released. 300 reads must leave nothing behind.",
               "in the browser console type volt2.handleCount(), click the button, then type it again.",
               "the button says done, and the count is the same before and after (give or take one).");
    stress = add(box, "button", "stress", "Read document.body 300 times");
    onStress = volt2::js::function([this] {
        for (int i = 0; i < 300; ++i) {
            Value body = document["body"];
        }
        stress.set("textContent", "done");
    });
    stress.call("addEventListener", "click", onStress);
}

}  // namespace

int main() { return volt2::run<BridgePage>({.head = kLook}); }

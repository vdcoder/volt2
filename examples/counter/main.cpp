// A page built by hand with volt2::js::Value; no rendering engine yet.
// Exercises what the bridge must get right: C++ state, reading page values and
// calling page functions mid-handler, nested calls, errors in both directions,
// and C++ functions that page JavaScript can call.
//
// Develop:  build natively and run, then use the page that opens.
// Deploy:   em++ -std=c++20 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I../.. main.cpp -o site/app.js
//           and copy web/index.html and web/volt2.js next to it.
#include "volt2.hpp"

#include <memory>
#include <stdexcept>
#include <string>

using volt2::js::Function;
using volt2::js::Value;

namespace {

Value add(const Value& document, const char* tag, const char* id, const std::string& text) {
    Value element = document.call("createElement", tag);
    element.set("id", id);
    element.set("textContent", text);
    document["body"].call("appendChild", element);
    return element;
}

// Everything the page uses. A js::Function works for as long as it is held, so the
// page object keeps the functions it gives JavaScript as members.
class CounterPage {
public:
    CounterPage();

private:
    Value document = Value::global("document");
    Value window = Value::global("window");
    int count = 0;
    Value counter, answer, log, inner, outer, errors, stress;
    Function onCounter, onAsk, onInner, onOuter, onFail, onStress, cppAdd, cppThrow;
};

CounterPage::CounterPage() {
    document.set("title", "volt2 counter");
    add(document, "h1", "title", "Hello from C++");

    // 1. C++ state, updated by a click.
    counter = add(document, "button", "counter", "Clicked 0 times");
    onCounter = volt2::js::function([this] {
        ++count;
        counter.set("textContent", "Clicked " + std::to_string(count) + " times");
    });
    counter.call("addEventListener", "click", onCounter);

    // 2. In the middle of a handler, read a page global and call a page function.
    answer = add(document, "p", "answer", "");
    Value ask = add(document, "button", "ask", "Ask the page");
    onAsk = volt2::js::function([this] {
        std::string text = std::string("peter_pan is ") + (Value::global("peter_pan").as<bool>() ? "true" : "false");
        Value twice = Value::global("twice");
        if (twice.typeOf() == "function") text += ", twice(21) is " + std::to_string(twice(21).as<int>());
        answer.set("textContent", text);
    });
    ask.call("addEventListener", "click", onAsk);

    // 3. Nested calls: clicking `inner` from C++ runs its C++ listener before click() returns.
    log = add(document, "p", "log", "");
    inner = add(document, "button", "inner", "Inner");
    onInner = volt2::js::function([this] {
        log.set("textContent", log["textContent"].as<std::string>() + "inner;");
    });
    inner.call("addEventListener", "click", onInner);
    outer = add(document, "button", "outer", "Outer");
    onOuter = volt2::js::function([this] {
        log.set("textContent", "outer-start;");
        inner.call("click");
        log.set("textContent", log["textContent"].as<std::string>() + "outer-end");
    });
    outer.call("addEventListener", "click", onOuter);

    // 4. A JavaScript error becomes a C++ exception.
    errors = add(document, "p", "errors", "");
    Value fail = add(document, "button", "fail", "Call a missing function");
    onFail = volt2::js::function([this] {
        try {
            Value::global("doesNotExist")();
        } catch (const volt2::js::Error& e) {
            errors.set("textContent", std::string("C++ caught: ") + e.what());
        }
    });
    fail.call("addEventListener", "click", onFail);

    // 5. C++ functions that page JavaScript can call; a C++ exception becomes a JS Error.
    cppAdd = volt2::js::function([](const volt2::js::Args& args) {
        return args.at(0).as<double>() + args.at(1).as<double>();
    });
    window.set("cppAdd", cppAdd);
    cppThrow = volt2::js::function([]() -> Value { throw std::runtime_error("thrown in C++"); });
    window.set("cppThrow", cppThrow);

    // 6. Short-lived values: each read creates a page-side handle that must be released.
    stress = add(document, "button", "stress", "Read document.body 300 times");
    onStress = volt2::js::function([this] {
        for (int i = 0; i < 300; ++i) {
            Value body = document["body"];
        }
        stress.set("textContent", "done");
    });
    stress.call("addEventListener", "click", onStress);
}

}  // namespace

void site(volt2::Session&) {
    // Each page load builds a fresh page object; a reload drops the previous one here.
    static std::unique_ptr<CounterPage> page;
    page = std::make_unique<CounterPage>();
}

int main() { return volt2::run(site); }

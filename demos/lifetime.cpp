// The lifetime demo: js::Function ownership, one handle per page object, Value as a map key,
// the thread check, stats and the development leak warning. tests/lifetime.test.cjs drives it
// through the functions this session puts on window; the page lists each one, what it checks,
// and how to run it by hand in the browser console.
#include "volt2.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
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
        article header code { font-size: 1rem; }
    )"),
};

// Adds an element with optional text to `parent`, and returns it.
Value add(const Value& parent, const char* tag, const std::string& text) {
    Value element = parent["ownerDocument"].call("createElement", tag);
    if (!text.empty()) element.set("textContent", text);
    parent.call("appendChild", element);
    return element;
}

// One card per check: its console call, what it checks, how to run it, what should happen.
void card(const Value& page, const std::string& call, const std::string& checks, const std::string& tryIt,
          const std::string& expect) {
    Value article = add(page, "article", "");
    add(add(article, "header", ""), "code", call);
    for (auto [label, text] : {std::pair{"Checks: ", checks}, std::pair{"Try it: ", tryIt}, std::pair{"Expect: ", expect}}) {
        Value p = add(article, "p", "");
        p.set("className", "about");
        add(p, "strong", label);
        p.call("append", text);
    }
}

// The page: built once, in the constructor, by hand.
void describe(const Value& document) {
    document.set("title", "volt2 lifetime");
    Value page = add(document["body"], "main", "");
    page.set("className", "container");
    Value header = add(page, "header", "");
    add(header, "h1", "Lifetimes");
    add(header, "p",
        "How long JavaScript objects and C++ functions live when C++ holds them. Each check is a C++ function "
        "this page puts on window: open the browser console (F12) and call them. cppStats() shows how many page "
        "objects (values) and C++ functions C++ holds right now; most checks are about it returning to where it was.");
    card(page, "cppStats()", "volt2::stats(): the page objects and C++ functions C++ holds.", "cppStats()",
         "{values: ..., functions: ...}: small numbers, the same each time nothing happens in between.");
    card(page, "churn(100)",
         "100 buttons, each with a C++ click listener that captures its button, created and removed. Each "
         "listener's js::Function is released at the end of its loop iteration, and the button with it.",
         "cppStats(), then churn(100) a few times, then cppStats() again.", "the same numbers before and after.");
    card(page, "holdTwice()",
         "the same page object (document.body) fetched twice is one handle, and two Values for it compare equal in "
         "C++ without asking the page.",
         "holdTwice()", "{same: true, equal: true, added: 1}.");
    card(page, "keyed()",
         "Value as a std::unordered_map key: objects by identity, NaN equal to NaN, as JavaScript's Map does.",
         "keyed()", "true.");
    card(page, "releasedFunction()",
         "a js::Function works while C++ holds it; once released, calling it from JavaScript throws, and leaks "
         "nothing even when given objects.",
         "releasedFunction(), then temporary()", "\"still here\", then an Error saying the function was released.");
    card(page, "holdMany(250) and dropAll()",
         "holding many page objects and functions: while developing, volt2 warns in both consoles once C++ holds "
         "more than VOLT2_WARN_AT (default 10,000; the tests use 200), then each time the count doubles.",
         "holdMany(250), cppStats(), dropAll(), cppStats(). Run the program with VOLT2_WARN_AT=200 to see the warning.",
         "250 more of each, then back to the numbers before; with the low threshold, warnings \"C++ now holds ...\".");
    card(page, "rvalueArguments()",
         "temporaries passed straight into call() and set() stay alive until the page has used them.", "rvalueArguments()",
         "\"SPAN\".");
    card(page, "freshAfterGrowth(900)",
         "an object returned by a C++ function reaches the page even when a leak warning is sent in the same call.",
         "freshAfterGrowth(900).tagName, then dropAll()", "\"B\".");
    card(page, "fromThread()",
         "Values and stats() belong to the thread that called volt2::run: another thread gets an error; a Value "
         "destroyed on another thread is still released.",
         "fromThread()",
         "two \"only be used on the thread that called volt2::run\" messages (WebAssembly: \"no threads in this build\").");
    card(page, "dropFunctionOnThread() and droppedOnThread()",
         "a js::Function destroyed on another thread, with what its lambda points at: JavaScript must be told it was "
         "released, never call into freed memory.",
         "dropFunctionOnThread(), then droppedOnThread()", "true, then an Error saying the function was released.");
}

class LifetimePage : public volt2::Session {
public:
    LifetimePage() {
        describe(document);

        expose(stats, "cppStats", [] {
            volt2::Stats now = volt2::stats();  // before creating the result object
            Value result = Value::object();
            result.set("values", now.values);
            result.set("functions", now.functions);
            return result;
        });

        // n buttons, each with a listener capturing its own button, removed again. Each
        // listener's js::Function is released at the end of its iteration, and with it
        // the captured button, so nothing should remain on either side.
        expose(churn, "churn", [this](const Value& count) {
            Value body = document["body"];
            for (int i = 0, n = count.as<int>(); i < n; ++i) {
                Value button = document.call("createElement", "button");
                Function onClick = volt2::js::function([button] { button.set("textContent", "clicked"); });
                button.call("addEventListener", "click", onClick);
                body.call("appendChild", button);
                button.call("remove");
            }
        });

        // The same page object fetched twice is one handle, compared locally.
        expose(holdTwice, "holdTwice", [this] {
            std::size_t before = volt2::stats().values;
            first = document["body"];
            second = document["body"];
            std::size_t added = volt2::stats().values - before;
            Value result = Value::object();
            result.set("same", first.strictlyEquals(second));
            result.set("equal", first == second);
            result.set("added", added);
            return result;
        });

        // Values as unordered_map keys: objects by identity, NaN equal to NaN (like JS Map keys).
        expose(keyed, "keyed", [this] {
            std::unordered_map<Value, std::string> names;
            names[document["body"]] = "body";
            names[document["head"]] = "head";
            names[Value(std::nan(""))] = "nan";
            names[Value("text")] = "text";
            return names.size() == 4 && names.at(document["body"]) == "body" && names.at(document["head"]) == "head" &&
                   names.count(Value(std::nan(""))) == 1 && names.count(Value(0.0)) == 0 &&
                   names.count(Value("text")) == 1 && !Value(std::nan("")).strictlyEquals(Value(std::nan("")));
        });

        // A js::Function works while held; once released, calling it from JS throws.
        expose(releasedFunction, "releasedFunction", [this] {
            Function temporary = volt2::js::function([] { return "still here"; });
            window.set("temporary", temporary);
            return window.call("temporary");
        });  // `temporary` is released when this returns

        // Hold many page objects and functions: the development build warns past VOLT2_WARN_AT.
        expose(holdMany, "holdMany", [this](const Value& count) {
            for (int i = 0, n = count.as<int>(); i < n; ++i) {
                heldValues.push_back(document.call("createElement", "span"));
                heldFunctions.push_back(volt2::js::function([] {}));
            }
        });
        expose(dropAll, "dropAll", [this] {
            heldValues.clear();
            heldFunctions.clear();
        });

        // Temporaries passed straight into call and set stay alive until the page has used them.
        expose(rvalueArguments, "rvalueArguments", [this] {
            document["body"].call("appendChild", document.call("createElement", "div"));
            Value holder = Value::object();
            holder.set("child", document.call("createElement", "span"));
            return holder["child"]["tagName"].as<std::string>();
        });

        // A fresh object returned in the same call as a leak warning (development) reaches the page.
        expose(freshAfterGrowth, "freshAfterGrowth", [this](const Value& count) {
            for (int i = 0, n = count.as<int>(); i < n; ++i) heldValues.push_back(document.call("createElement", "span"));
            return document.call("createElement", "b");  // C++ holds nothing of it once returned
        });

        // Values belong to the thread that called volt2::run.
        expose(fromThread, "fromThread", [this] {
#ifdef __EMSCRIPTEN__
            return std::string("no threads in this build");
#else
            std::string message = "no error", statsMessage = "no error";
            Value copy = document;
            std::thread([&] {
                try {
                    (void)copy["title"];
                } catch (const volt2::js::Error& e) {
                    message = e.what();
                }
                try {
                    (void)volt2::stats();
                } catch (const volt2::js::Error& e) {
                    statsMessage = e.what();
                }
            }).join();
            // The last copy of a Value destroyed on another thread is released safely.
            Value head = document["head"];
            std::thread([moved = std::move(head)]() mutable { Value gone = std::move(moved); }).join();
            return message + " | " + statsMessage;
#endif
        });

        // A js::Function destroyed on another thread, together with what its lambda points at:
        // JavaScript must get "released", never a call into freed memory.
        expose(dropFunctionOnThread, "dropFunctionOnThread", [this] {
#ifdef __EMSCRIPTEN__
            return false;
#else
            struct Holder {
                int value = 42;
                Function fn;
            };
            auto holder = std::make_unique<Holder>();
            holder->fn = volt2::js::function([data = holder.get()] { return data->value; });
            window.set("droppedOnThread", holder->fn);
            std::thread([moved = std::move(holder)]() mutable { moved.reset(); }).join();
            return true;
#endif
        });
    }

private:
    template <class F>
    void expose(Function& slot, const char* name, F&& fn) {
        slot = volt2::js::function(std::forward<F>(fn));
        window.set(name, slot);
    }

    Value document = Value::global("document");
    Value window = Value::global("window");
    Value first, second;
    std::vector<Value> heldValues;
    std::vector<Function> heldFunctions;
    Function stats, churn, holdTwice, keyed, releasedFunction, holdMany, dropAll, rvalueArguments, freshAfterGrowth,
        fromThread, dropFunctionOnThread;
};

}  // namespace

int main() { return volt2::run<LifetimePage>({.head = kLook}); }

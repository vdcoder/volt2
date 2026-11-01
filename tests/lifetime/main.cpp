// Test program for lifetimes: js::Function ownership, one handle per page object,
// Value as a map key, the thread check, stats and the development leak warning.
// Driven by tests/lifetime.test.cjs through the functions it puts on window.
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

class LifetimePage {
public:
    LifetimePage() {
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

void site(volt2::Session&) {
    static std::unique_ptr<LifetimePage> page;
    page = std::make_unique<LifetimePage>();
}

int main() { return volt2::run(site); }

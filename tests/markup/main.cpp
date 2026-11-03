// Test program for the markup language and rendering, driven by tests/markup.test.cjs.
#include "volt2.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace volt2;

// A 1x1 GIF: loads instantly, and fires load again each time a render recreates the image.
constexpr const char* kPixel = "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==";

struct Markup : Session {
    static inline int constructed = 0;
    static inline int destroyed = 0;
    int renders = 0;
    std::vector<std::string> log;
    std::string typed;
    std::string targets;
    int loads = 0;
    bool imageLoaded = false;
    int bumps = 0;
    bool locked = false;
    std::optional<std::string> tooltip;
    Value document = Value::global("document");
    js::Function bump;

    Markup() {
        ++constructed;
        // C++ that page JavaScript calls outside any event: invalidate() renders.
        bump = js::function([this] {
            ++bumps;
            invalidate();
        });
        document["defaultView"].set("cppBump", bump);
    }
    ~Markup() override {
        ++destroyed;
        // Runs after this session's page is gone (development), through a stored Value:
        // must neither crash nor reach the new page.
        document.set("title", "set by a destroyed session");
    }
};

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) out += (out.empty() ? "" : ",") + item;
    return out;
}

VirtualNode site(Markup& s) {
    ++s.renders;
    return tag::Div[attr::Id = "root"](
        tag::P[attr::Id = "lifecycle"]("constructed ", Markup::constructed, ", destroyed ", Markup::destroyed,
                                       ", renders ", s.renders),

        // Attribute values: strings, booleans (false leaves the attribute out), numbers, data- and aria-.
        tag::Input[attr::Id = "box", attr::Type = "checkbox", attr::Checked = true, attr::Disabled = false,
                   attr::TabIndex = 3, attr::Data("role") = "demo", attr::Aria("label") = "A box"](),

        // Text is always text, never HTML.
        tag::P[attr::Id = "text"]("<b>not bold</b> & \"quoted\""),
        tag::P[attr::Id = "numbers"](42, " ", 2.5, " ", -0.125, " ", 1e21),

        // Bubbling runs the target's handler, then its ancestors'; stopPropagation stops it.
        tag::Div[attr::Id = "outer", event::OnClick([&s] { s.log.push_back("outer"); })](
            tag::Button[attr::Id = "inner", event::OnClick([&s] { s.log.push_back("inner"); })]("inner"),
            tag::Button[attr::Id = "stop", event::OnClick([&s](const Value& e) {
                s.log.push_back("stop");
                e.call("stopPropagation");
            })]("stop")),
        tag::P[attr::Id = "log"](joined(s.log)),

        // preventDefault works mid-dispatch, so the form does not navigate.
        tag::Form[attr::Id = "form", attr::Action = "/elsewhere", event::OnSubmit([&s](const Value& e) {
            e.call("preventDefault");
            s.log.push_back("submit");
        })](tag::Button[attr::Id = "send", attr::Type = "submit"]("Send")),

        // Event values: what was typed.
        tag::Input[attr::Id = "name",
                   event::OnInput([&s](const Value& e) { s.typed = e["target"]["value"].as<std::string>(); })](),
        tag::P[attr::Id = "typed"]("typed: ", s.typed),

        // Events that do not bubble reach only their target.
        tag::Div[attr::Id = "wrap", event::OnFocus([&s] { s.log.push_back("wrap-focus"); })](
            tag::Input[attr::Id = "focusme", event::OnFocus([&s] { s.log.push_back("focus"); })]()),

        // Children: a range, optionals, and nothing.
        tag::Ul[attr::Id = "list"](std::vector<VirtualNode>{tag::Li("a"), tag::Li("b")}, std::optional<VirtualNode>(),
                                   std::optional<VirtualNode>(tag::Li("c")), VirtualNode()),

        // A handler that throws: the page gets the error and still shows the new state.
        tag::Button[attr::Id = "throw", event::OnClick([&s] {
            s.log.push_back("throw");
            throw std::runtime_error("handler failed");
        })]("throw"),

        // An element's own events (here load) do not render by themselves, so they cannot
        // loop; invalidate() renders when the state really changed.
        tag::Img[attr::Id = "img", attr::Src = kPixel, event::OnLoad([&s] {
            ++s.loads;
            if (!s.imageLoaded) {
                s.imageLoaded = true;
                s.invalidate();
            }
        })](),
        tag::P[attr::Id = "image"](s.imageLoaded ? "image loaded" : "image loading"),
        tag::P[attr::Id = "bumps"]("bumps ", s.bumps),

        // currentTarget is each handler's own element.
        tag::Div[attr::Id = "ct-outer",
                 event::OnClick([&s](const Value& e) { s.targets += e["currentTarget"]["id"].as<std::string>() + ";"; })](
            tag::Span[attr::Id = "ct-inner", event::OnClick([&s](const Value& e) {
                s.targets += e["currentTarget"]["id"].as<std::string>() + ";";
            })]("x")),
        tag::P[attr::Id = "targets"](s.targets),

        // A template's children are its content.
        tag::Template[attr::Id = "tpl"](tag::P("inside")),

        // Integers stay exact; true/false follows each attribute's rules.
        tag::P[attr::Id = "big", attr::Data("n") = 9007199254740993LL](9007199254740993ULL, " ", -9007199254740993LL),
        tag::Div[attr::Id = "flags", attr::Draggable = true, attr::SpellCheck = false, attr::Hidden = false,
                 attr::Aria("expanded") = true, attr::Data("on") = false](),

        // None: nothing, wherever a prop or a node can go. std::optional values and null
        // const char* leave attributes out too. The lock button flips everything below.
        tag::Button[attr::Id = "lock", event::OnClick([&s] {
            s.locked = !s.locked;
            s.tooltip = s.locked ? std::optional<std::string>("locked") : std::nullopt;
        })]("lock"),
        tag::Input[attr::Id = "maybe-ro", s.locked ? attr::ReadOnly = true : None, attr::Title = s.tooltip,
                   attr::Data("x") = std::optional<int>(), attr::Placeholder = static_cast<const char*>(nullptr)](),
        tag::Span[attr::Id = "maybe-click", s.locked ? None : event::OnClick([&s] { s.log.push_back("unlocked-click"); })](
            "click me"),
        s.locked ? tag::P[attr::Id = "locked-note"]("locked") : None,
        tag::P[attr::Id = "nones", None](None, "text", static_cast<const char*>(nullptr), None),

        // A handler that clicks another element: both run, and the page renders once.
        tag::Button[attr::Id = "relay", event::OnClick([&s](const Value& e) {
            s.log.push_back("relay");
            e["target"]["ownerDocument"].call("getElementById", "inner").call("click");
        })]("relay"));
}

int main() { return run(site); }

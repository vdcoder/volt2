// The markup demo: attributes, text, events, children, None and sessions, one card each.
// tests/markup.test.cjs drives it; every card says what it checks, how to try it by hand, and
// what should happen. Build it like the counter (see demos/counter.cpp).
#include "volt2.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace volt2;

// The look shared by the demos: Pico CSS, the Inter font, and a few rules of our own.
const std::vector<head::Item> kLook = {
    head::Pico,
    head::GoogleFont("Inter:wght@400;600;700"),
    head::Css(R"(
        :root { --pico-font-family: Inter, system-ui, sans-serif; }
        main.container { max-width: 52rem; padding-block: 2rem; }
        .about { margin-bottom: 0.4rem; font-size: 0.92rem; }
        .about strong { color: var(--pico-primary); }
        .live { margin-top: 1rem; padding: 1rem 1rem 0.25rem; border: 1px dashed var(--pico-muted-border-color);
                border-radius: var(--pico-border-radius); }
        .live button { margin: 0 0.5rem 0.75rem 0; }
        .log { font-family: ui-monospace, monospace; font-size: 0.85rem; }
        #outer, #ct-outer, #wrap { padding: 0.75rem; border: 1px solid var(--pico-muted-border-color);
                                   border-radius: var(--pico-border-radius); margin-bottom: 0.75rem; }
    )"),
};

// One card of the page: what it checks, how to try it, what should happen, then the live part.
inline constexpr Component Topic = [](std::string title, std::string checks, std::string tryIt, std::string expect,
                                      VirtualNode live) {
    return tag::Article(tag::Header(tag::Strong(title)),
                        tag::P[attr::Class = "about"](tag::Strong("Checks: "), checks),
                        tryIt.empty() ? None : tag::P[attr::Class = "about"](tag::Strong("Try it: "), tryIt),
                        tag::P[attr::Class = "about"](tag::Strong("Expect: "), expect),
                        tag::Div[attr::Class = "live"](live));
};

// A 1x1 GIF: loads instantly.
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
    return tag::Main[attr::Id = "root", attr::Class = "container"](
        tag::Header(tag::H1("Markup"),
                    tag::P("How volt2's markup becomes a page: attributes, text, events, the kinds of children, None, and "
                           "sessions. Several cards add to the same event log, shown in the \"Bubbling\" card.")),

        Topic("Sessions",
              "one Session object per page load: constructed once, destroyed when the page goes away. The counts are "
              "static members, so they survive sessions; renders counts this session's renders.",
              "reload the page (F5).",
              "constructed goes up by one and destroyed catches up with it while developing (the old page's "
              "session is destroyed). As WebAssembly, a reload starts a new program: \"constructed 1, destroyed 0\".",
              tag::P[attr::Id = "lifecycle"]("constructed ", Markup::constructed, ", destroyed ", Markup::destroyed,
                                             ", renders ", s.renders)),

        Topic("Attribute values",
              "strings, true (written as an empty attribute), false (left out), integers, data- and aria- attributes.",
              "inspect the checkbox in your browser's developer tools.",
              "type=\"checkbox\" checked tabindex=\"3\" data-role=\"demo\" aria-label=\"A box\", and no disabled "
              "attribute: it is checked and clickable.",
              tag::Input[attr::Id = "box", attr::Type = "checkbox", attr::Checked = true, attr::Disabled = false,
                         attr::TabIndex = 3, attr::Data("role") = "demo", attr::Aria("label") = "A box"]()),

        Topic("Text and numbers",
              "text is always text, never HTML; numbers are written as JavaScript shows them.",
              "",
              "the first line shows the angle brackets and quotes literally, not bold text; the second reads "
              "\"42 2.5 -0.125 1e+21\".",
              tag::Div(tag::P[attr::Id = "text"]("<b>not bold</b> & \"quoted\""),
                       tag::P[attr::Id = "numbers"](42, " ", 2.5, " ", -0.125, " ", 1e21))),

        Topic("Bubbling (and the event log)",
              "a click runs the handler of the element clicked, then its ancestors'; stopPropagation() stops it. "
              "Below is the log several cards write to.",
              "click \"inner\", then \"stop\".",
              "\"inner\" adds inner,outer (the button's handler, then the box's); \"stop\" adds only stop.",
              tag::Div(tag::Div[attr::Id = "outer", event::OnClick([&s] { s.log.push_back("outer"); })](
                           tag::Button[attr::Id = "inner", event::OnClick([&s] { s.log.push_back("inner"); })]("inner"),
                           tag::Button[attr::Id = "stop", attr::Class = "secondary", event::OnClick([&s](const Value& e) {
                               s.log.push_back("stop");
                               e.call("stopPropagation");
                           })]("stop")),
                       tag::P(tag::Strong("Event log: "), tag::Span[attr::Id = "log", attr::Class = "log"](joined(s.log))))),

        Topic("preventDefault",
              "e.call(\"preventDefault\") works in the middle of the event, also while developing, because every call "
              "to the page is synchronous.",
              "click Send.",
              "the page stays here (the form would go to /elsewhere) and the log gets submit.",
              tag::Form[attr::Id = "form", attr::Action = "/elsewhere", event::OnSubmit([&s](const Value& e) {
                  e.call("preventDefault");
                  s.log.push_back("submit");
              })](tag::Button[attr::Id = "send", attr::Type = "submit"]("Send"))),

        Topic("Event values",
              "a handler reads the event: e[\"target\"][\"value\"] is what the input holds.",
              "type in the box.",
              "the line below repeats what you type, as you type.",
              tag::Div(tag::Input[attr::Id = "name", attr::Placeholder = "Type here",
                                  event::OnInput([&s](const Value& e) { s.typed = e["target"]["value"].as<std::string>(); })](),
                       tag::P[attr::Id = "typed"]("typed: ", s.typed))),

        Topic("Events that do not bubble",
              "focus does not bubble: only the input's own handler runs, not the one on the box around it.",
              "click into the input.",
              "the log gets focus, never wrap-focus.",
              tag::Div[attr::Id = "wrap", event::OnFocus([&s] { s.log.push_back("wrap-focus"); })](
                  tag::Input[attr::Id = "focusme", attr::Placeholder = "Focus me",
                             event::OnFocus([&s] { s.log.push_back("focus"); })]())),

        Topic("Kinds of children",
              "a std::vector of nodes, an empty std::optional, a full one, and an empty VirtualNode as children.",
              "",
              "exactly three items: a, b, c (the empty ones render nothing).",
              tag::Ul[attr::Id = "list"](std::vector<VirtualNode>{tag::Li("a"), tag::Li("b")}, std::optional<VirtualNode>(),
                                         std::optional<VirtualNode>(tag::Li("c")), VirtualNode())),

        Topic("A handler that throws",
              "an exception from a handler reaches the page as an error, and the state it changed before throwing "
              "is still rendered.",
              "click the button and look at the browser console.",
              "the console shows \"handler failed\", and the log still gets throw.",
              tag::Button[attr::Id = "throw", attr::Class = "contrast", event::OnClick([&s] {
                  s.log.push_back("throw");
                  throw std::runtime_error("handler failed");
              })]("throw")),

        Topic("Events elements fire themselves",
              "an image's load event renders like any other event, without looping (a render does not recreate "
              "the image, so it does not load again). Also invalidate() from C++ that page JavaScript calls "
              "(window.cppBump) outside any event.",
              "type cppBump() in the browser console, a few times.",
              "\"image loaded\" as soon as the page shows; bumps counts each cppBump().",
              tag::Div(tag::Img[attr::Id = "img", attr::Src = kPixel, attr::Alt = "", event::OnLoad([&s] {
                           ++s.loads;
                           if (!s.imageLoaded) {
                               s.imageLoaded = true;
                               s.invalidate();  // inside a handler: still one render
                           }
                       })](),
                       tag::P[attr::Id = "image"](s.imageLoaded ? "image loaded" : "image loading"),
                       tag::P[attr::Id = "bumps"]("bumps ", s.bumps))),

        Topic("currentTarget",
              "each handler sees its own element as e[\"currentTarget\"], even though volt2 listens in one place "
              "for the whole page.",
              "click x.",
              "ct-inner;ct-outer; (the inner handler first, each naming its own element).",
              tag::Div(tag::Div[attr::Id = "ct-outer", event::OnClick([&s](const Value& e) {
                           s.targets += e["currentTarget"]["id"].as<std::string>() + ";";
                       })](tag::Button[attr::Id = "ct-inner", attr::Class = "outline", event::OnClick([&s](const Value& e) {
                           s.targets += e["currentTarget"]["id"].as<std::string>() + ";";
                       })]("x")),
                       tag::P[attr::Id = "targets"](s.targets))),

        Topic("Template",
              "a <template>'s children go into its content, as HTML defines, not into the element.",
              "",
              "nothing visible: a template is never shown. (The test checks its content holds one paragraph.)",
              tag::Template[attr::Id = "tpl"](tag::P("inside"))),

        Topic("Big integers and true/false",
              "integers stay exact past JavaScript's 2^53; attributes write true and false their own way: draggable "
              "and spellcheck as \"true\"/\"false\", hidden left out when false, aria- and data- as words.",
              "inspect the empty box below this line.",
              "9007199254740993 -9007199254740993 (not ...992), data-n=\"9007199254740993\"; on the box "
              "draggable=\"true\" spellcheck=\"false\" aria-expanded=\"true\" data-on=\"false\", no hidden.",
              tag::Div(tag::P[attr::Id = "big", attr::Data("n") = 9007199254740993LL](9007199254740993ULL, " ",
                                                                                  -9007199254740993LL),
                       tag::Div[attr::Id = "flags", attr::Draggable = true, attr::SpellCheck = false, attr::Hidden = false,
                                attr::Aria("expanded") = true, attr::Data("on") = false]())),

        Topic("None: nothing, wherever a prop or a node can go",
              "None in conditionals, between brackets and parentheses; an empty std::optional attribute value and "
              "a null const char* also leave things out.",
              "click \"click me\", then lock, then \"click me\" again.",
              "unlocked: the input is editable, has no title, and \"click me\" adds unlocked-click to the log. "
              "Locked: the input is read-only with the title \"locked\", \"locked\" appears below, and \"click me\" "
              "does nothing. The last line reads \"text\" either way.",
              tag::Div(tag::Button[attr::Id = "lock", event::OnClick([&s] {
                           s.locked = !s.locked;
                           s.tooltip = s.locked ? std::optional<std::string>("locked") : std::nullopt;
                       })](s.locked ? "unlock" : "lock"),
                       tag::Input[attr::Id = "maybe-ro", s.locked ? attr::ReadOnly = true : None, attr::Title = s.tooltip,
                                  attr::Data("x") = std::optional<int>(), attr::Placeholder = static_cast<const char*>(nullptr)](),
                       tag::P(tag::Span[attr::Id = "maybe-click", s.locked ? None : event::OnClick([&s] {
                                  s.log.push_back("unlocked-click");
                              })](tag::Mark("click me"))),
                       s.locked ? tag::P[attr::Id = "locked-note"]("locked") : None,
                       tag::P[attr::Id = "nones", None](None, "text", static_cast<const char*>(nullptr), None))),

        Topic("A handler that fires another event",
              "a handler can make another event happen (here, clicking \"inner\" from C++): its handlers run "
              "inside, and the page still renders once, after both.",
              "click relay.",
              "the log gets relay,inner,outer, and renders (in the Sessions card) goes up by one.",
              tag::Button[attr::Id = "relay", event::OnClick([&s](const Value& e) {
                  s.log.push_back("relay");
                  e["target"]["ownerDocument"].call("getElementById", "inner").call("click");
              })]("relay")));
}

int main() { return run(site, {.head = kLook}); }

// The volt2 starter: a counter, a greeting typed live, a panel that opens and closes, and a list.
// It is also the page tests/counter.test.cjs drives.
//
// Develop:  build natively with C++23 and run; your browser opens the page.
//           g++ -std=c++23 -I.. counter.cpp -o counter -pthread     (Visual Studio: see the README)
// Deploy:   em++ -std=c++23 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I.. counter.cpp -o site/app.js
//           and copy web/index.html and web/volt2.js next to it.
#include "volt2.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace volt2;

// The look: Pico CSS styles plain HTML (headings, buttons, inputs, cards), Inter is the font,
// and a few rules of our own. head:: items go into every page's <head>, see run() below; any
// stylesheet, script or CSS works, volt2 only puts them there.
const std::vector<head::Item> kLook = {
    head::Stylesheet("https://cdn.jsdelivr.net/npm/@picocss/pico@2.1.1/css/pico.min.css"),  // Pico CSS (picocss.com)
    head::Stylesheet("https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap"),  // the Inter font
    head::Css(R"(
        :root { --pico-font-family: Inter, system-ui, sans-serif; }
        main.container { max-width: 52rem; padding-block: 2rem; }
        .about { margin-bottom: 0.4rem; font-size: 0.92rem; }
        .about strong { color: var(--pico-primary); }
        .live { margin-top: 1rem; padding: 1rem 1rem 0.25rem; border: 1px dashed var(--pico-muted-border-color);
                border-radius: var(--pico-border-radius); }
        .live button { margin: 0 0.5rem 0.75rem 0; }
        .live li button { padding: 0.1rem 0.6rem; margin: 0 0 0 0.5rem; font-size: 0.8rem; }
    )"),
};

// One card of the page: what it shows, how to try it, what should happen, then the live part.
inline constexpr Component Topic = [](std::string title, std::string shows, std::string tryIt, std::string expect,
                                      VirtualNode live) {
    return tag::Article(tag::Header(tag::Strong(title)),
                        tag::P[attr::Class = "about"](tag::Strong("Shows: "), shows),
                        tag::P[attr::Class = "about"](tag::Strong("Try it: "), tryIt),
                        tag::P[attr::Class = "about"](tag::Strong("Expect: "), expect),
                        tag::Div[attr::Class = "live"](live));
};

// One per page load. Its members are the page's state; its constructor would be the place for
// one-time setup.
struct Counter : Session {
    int count = 0;
    std::string name;
    bool panelOpen = true;
    std::vector<std::string> fruits{"Apple", "Banana", "Cherry"};
    int added = 0;
};

// A component: each place that calls it gets elements of its own.
inline constexpr Component FruitItem = [](Counter& s, const std::string& fruit) {
    return tag::Li(fruit, " ", tag::Button[attr::Class = "outline", event::OnClick([&s, fruit] { std::erase(s.fruits, fruit); })](
                                   "Remove"));
};

// Builds the page from the session. volt2 calls it again after every event handler and updates
// only what changed, so the input below keeps its focus while you type.
VirtualNode site(Counter& s) {
    return tag::Main[attr::Class = "container"](
        tag::Header(tag::H1("Hello from C++"),
                    tag::P("This page is a C++ program: every element below is made by site() in demos/counter.cpp, and "
                           "every click runs C++. The cards say what each part shows and what should happen.")),

        Topic("Counter",
              "state kept in your session (Counter::count), changed by click handlers, and a conditional element.",
              "click Increment a few times, then Decrement; then click Increment until the count passes 10.",
              "the text follows every click. Past 10, a red warning appears under the buttons; below 11 it goes away.",
              tag::Div(tag::P[attr::Id = "count"]("Clicked ", s.count, " times"),
                       tag::Button[attr::Id = "increment", event::OnClick([&s] { ++s.count; })]("Increment"),
                       tag::Button[attr::Id = "decrement", attr::Class = "secondary", event::OnClick([&s] { --s.count; })](
                           "Decrement"),
                       s.count > 10 ? tag::P[attr::Id = "warning", attr::Style = "color: #d93526"]("That's a lot of clicks!")
                                    : None)),

        Topic("Greeting, typed live",
              "an input whose OnInput handler stores the text in the session; the page renders after every key.",
              "type your name in the box.",
              "the greeting changes with every key, and the box keeps the focus and the caret: rendering never "
              "replaces the input, it only updates what changed.",
              tag::Div(tag::Input[attr::Id = "name", attr::Placeholder = "Your name", attr::Value = s.name,
                                  event::OnInput([&s](const Value& e) { s.name = e["target"]["value"].as<std::string>(); })](),
                       tag::P[attr::Id = "greeting"]("Hello, ", s.name.empty() ? "stranger" : s.name, "!"))),

        Topic("Panel",
              "an element that comes and goes (cond ? element : None), and a button whose label follows the state.",
              "click the button twice.",
              "the panel disappears and the button says \"Show the panel\"; then both come back.",
              tag::Div(tag::Button[attr::Id = "toggle", event::OnClick([&s] { s.panelOpen = !s.panelOpen; })](
                           s.panelOpen ? "Hide the panel" : "Show the panel"),
                       s.panelOpen ? tag::P[attr::Id = "panel"]("Rendered by C++, both while developing and as WebAssembly.")
                                   : None)),

        Topic("Fruit",
              "a list made with Map and a key per item (the fruit's name), each item made by the FruitItem component.",
              "remove Banana, then add a fruit or two.",
              "Banana goes and the other items keep their elements (the key tells them apart); new fruits are "
              "added at the end as \"Fruit 1\", \"Fruit 2\"...",
              tag::Div(tag::Ul[attr::Id = "fruits"](Map(s.fruits, [](const std::string& fruit) { return fruit; },
                                                         [&s](const std::string& fruit) { return FruitItem(s, fruit); })),
                       tag::Button[attr::Id = "add",
                                   event::OnClick([&s] { s.fruits.push_back("Fruit " + std::to_string(++s.added)); })](
                           "Add a fruit"))));
}

int main() { return run(site, {.head = kLook}); }

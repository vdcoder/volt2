// The volt2 starter: a counter, a greeting typed live, a panel that opens and closes, and a list.
//
// Develop:  build natively with C++23 and run; your browser opens the page.
// Deploy:   em++ -std=c++23 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I../.. main.cpp -o site/app.js
//           and copy web/index.html and web/volt2.js next to it.
#include "volt2.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace volt2;

// One per page load. Its members are the page's state; its constructor would be the
// place for one-time setup.
struct Counter : Session {
    int count = 0;
    std::string name;
    bool panelOpen = true;
    std::vector<std::string> fruits{"Apple", "Banana", "Cherry"};
    int added = 0;
};

// A component: each place that calls it gets elements of its own.
inline constexpr Component FruitItem = [](Counter& s, const std::string& fruit) {
    return tag::Li[attr::Key = fruit](  // the key keeps each item's element when others go
        fruit, " ", tag::Button[event::OnClick([&s, fruit] { std::erase(s.fruits, fruit); })]("Remove"));
};

// Builds the page from the session. volt2 calls it again after every event handler and
// updates only what changed, so the input below keeps its focus while you type.
VirtualNode site(Counter& s) {
    return tag::Main[attr::Style = "font-family: sans-serif; max-width: 600px; padding: 20px"](
        tag::H1("Hello from C++"),

        tag::P[attr::Id = "count"]("Clicked ", s.count, " times"),
        tag::Button[attr::Id = "increment", event::OnClick([&s] { ++s.count; })]("Increment"),
        tag::Button[attr::Id = "decrement", event::OnClick([&s] { --s.count; })]("Decrement"),
        s.count > 10 ? tag::P[attr::Id = "warning", attr::Style = "color: #c00"]("That's a lot of clicks!") : None,

        tag::H2("Greeting"),
        tag::Input[attr::Id = "name", attr::Placeholder = "Your name", attr::Value = s.name,
                   event::OnInput([&s](const Value& e) { s.name = e["target"]["value"].as<std::string>(); })](),
        tag::P[attr::Id = "greeting"]("Hello, ", s.name.empty() ? "stranger" : s.name, "!"),

        tag::H2("Panel"),
        tag::Button[attr::Id = "toggle", event::OnClick([&s] { s.panelOpen = !s.panelOpen; })](
            s.panelOpen ? "Hide the panel" : "Show the panel"),
        s.panelOpen ? tag::Div[attr::Id = "panel"]("Rendered by C++, both while developing and as WebAssembly.")
                    : None,

        tag::H2("Fruit"),
        tag::Ul[attr::Id = "fruits"](Map(s.fruits, [&s](const std::string& fruit) { return FruitItem(s, fruit); })),
        tag::Button[attr::Id = "add", event::OnClick([&s] { s.fruits.push_back("Fruit " + std::to_string(++s.added)); })](
            "Add a fruit"));
}

int main() { return run(site); }

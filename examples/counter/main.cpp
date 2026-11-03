// The volt2 starter: a counter, a panel that opens and closes, and a list.
//
// Develop:  build natively with C++23 and run; your browser opens the page.
// Deploy:   em++ -std=c++23 -O2 -m64 -fexceptions -lembind -sALLOW_MEMORY_GROWTH -I../.. main.cpp -o site/app.js
//           and copy web/index.html and web/volt2.js next to it.
#include "volt2.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

using namespace volt2;

// One per page load. Its members are the page's state; its constructor would be the
// place for one-time setup.
struct Counter : Session {
    int count = 0;
    bool panelOpen = true;
    std::vector<std::string> fruits{"Apple", "Banana", "Cherry"};
    int added = 0;
};

// Builds the page from the session. volt2 calls it again after every event handler.
VirtualNode site(Counter& s) {
    return tag::Main[attr::Style = "font-family: sans-serif; max-width: 600px; padding: 20px"](
        tag::H1("Hello from C++"),

        tag::P[attr::Id = "count"]("Clicked ", s.count, " times"),
        tag::Button[attr::Id = "increment", event::OnClick([&s] { ++s.count; })]("Increment"),
        tag::Button[attr::Id = "decrement", event::OnClick([&s] { --s.count; })]("Decrement"),
        s.count > 10 ? tag::P[attr::Id = "warning", attr::Style = "color: #c00"]("That's a lot of clicks!") : None,

        tag::H2("Panel"),
        tag::Button[attr::Id = "toggle", event::OnClick([&s] { s.panelOpen = !s.panelOpen; })](
            s.panelOpen ? "Hide the panel" : "Show the panel"),
        s.panelOpen ? tag::Div[attr::Id = "panel"]("Rendered by C++, both while developing and as WebAssembly.")
                    : None,

        tag::H2("Fruit"),
        tag::Ul[attr::Id = "fruits"](s.fruits | std::views::transform([&s](const std::string& fruit) {
                                         return tag::Li(fruit, " ",
                                                        tag::Button[event::OnClick([&s, fruit] { std::erase(s.fruits, fruit); })](
                                                            "Remove"));
                                     })),
        tag::Button[attr::Id = "add", event::OnClick([&s] { s.fruits.push_back("Fruit " + std::to_string(++s.added)); })](
            "Add a fruit"));
}

int main() { return run(site); }

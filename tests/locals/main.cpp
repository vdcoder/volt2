// Test program for state inside components (volt2::local), driven by tests/locals.test.cjs.
#include "volt2.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace volt2;

struct Locals : Session {
    std::vector<std::string> items{"a", "b", "c"};
    std::vector<std::string> plain{"x", "y"};
    bool showFirst = true;
    bool hideCached = false;
    bool repeat = false;
};

// A component with state: each call site has its own.
inline constexpr Component Toggle = [](std::string label) {
    bool& open = local(false);
    int& clicks = local(0);
    return tag::Div[attr::Class = "toggle"](
        tag::Button[event::OnClick([&open, &clicks] {
            open = !open;
            ++clicks;
        })](label, " ", clicks),
        open ? tag::Span(" open") : None);
};

// Components inside a component, and a state of another type.
inline constexpr Component Pair = [](std::string name) {
    std::string& note = local<std::string>("note");
    return tag::Div[attr::Class = "pair"](
        tag::Input[attr::Value = note, event::OnInput([&note](const Value& e) { note = e["target"]["value"].as<std::string>(); })](),
        Toggle(name + "-1"), Toggle(name + "-2"));
};

// The same call made twice by a plain loop: the state is shared, and volt2 says so.
VirtualNode repeated() {
    std::vector<VirtualNode> twice;
    for (int i = 0; i < 2; ++i) twice.push_back(Toggle("same"));
    return tag::Div[attr::Id = "repeat"](twice);
}

VirtualNode site(Locals& s) {
    int& count = local(0);                 // state of site() itself
    VirtualNode cached = Toggle("cached");  // called on every render, shown only sometimes
    return tag::Div[attr::Id = "root"](
        tag::Button[attr::Id = "site", event::OnClick([&count] { ++count; })]("site ", count),
        tag::Div[attr::Id = "first"](s.showFirst ? Toggle("first") : None),
        tag::Div[attr::Id = "second"](Toggle("second")),
        tag::Div[attr::Id = "cached"](s.hideCached ? None : cached),
        tag::Div[attr::Id = "keyed"](Map(s.items, [](const std::string& item) { return item; },
                                          [](const std::string& item) { return Pair(item); })),
        tag::Div[attr::Id = "plain"](Map(s.plain, [](const std::string& item) { return Toggle(item); })),
        tag::Div[attr::Id = "looped"](Loop(2, [](std::size_t i) { return Toggle("loop" + std::to_string(i)); })),
        s.repeat ? repeated() : None,
        tag::Button[attr::Id = "toggle-first", event::OnClick([&s] { s.showFirst = !s.showFirst; })]("first"),
        tag::Button[attr::Id = "toggle-cached", event::OnClick([&s] { s.hideCached = !s.hideCached; })]("cached"),
        tag::Button[attr::Id = "reverse", event::OnClick([&s] { std::ranges::reverse(s.items); })]("reverse"),
        tag::Button[attr::Id = "remove-first", event::OnClick([&s] { s.items.erase(s.items.begin()); })]("remove"),
        tag::Button[attr::Id = "add-c", event::OnClick([&s] { s.items.push_back("c"); })]("add c"),
        tag::Button[attr::Id = "reverse-plain", event::OnClick([&s] { std::ranges::reverse(s.plain); })]("reverse plain"),
        tag::Button[attr::Id = "repeat-on", event::OnClick([&s] { s.repeat = true; })]("repeat"),
        tag::Button[attr::Id = "misuse", event::OnClick([] { ++local(0); })]("misuse"));
}

int main() { return run(site); }

// Randomized test of the diff, driven by tests/diff.test.cjs: the page's data changes at random
// (the same way on every run), and after each render the page must be exactly the tree C++
// rendered. Exercises keyed and unkeyed lists, nested lists, conditionals, attributes that come
// and go, a container whose tag changes, components and plain helper functions.
#include "volt2.hpp"

#include <algorithm>
#include <random>
#include <ranges>
#include <string>
#include <vector>

using namespace volt2;

#ifndef VOLT2_TEST_SEED
#define VOLT2_TEST_SEED 12345  // other seeds: -DVOLT2_TEST_SEED=7
#endif

struct Item {
    int key;
    std::string text;
    bool flag;
    std::vector<int> kids;
};

struct Fuzz : Session {
    std::mt19937 random{VOLT2_TEST_SEED};
    std::vector<Item> items;
    std::vector<std::string> plain{"p0", "p1"};
    bool show = true;
    bool wide = false;
    int counter = 0;
    int nextKey = 0;
    std::string title = "title";
    js::Function step, expected;

    int pick(int n) { return n <= 0 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(random); }
    std::string word() { return std::string(1, static_cast<char>('a' + pick(26))) + std::to_string(pick(100)); }
    Item fresh() { return {nextKey++, word(), pick(2) == 1, {}}; }

    void mutate() {
        int n = static_cast<int>(items.size());
        switch (pick(14)) {
            case 0: case 1: items.insert(items.begin() + pick(n + 1), fresh()); break;
            case 2: if (n) items.erase(items.begin() + pick(n)); break;
            case 3: if (n > 1) { Item moved = items[static_cast<std::size_t>(pick(n))]; std::erase_if(items, [&](const Item& i) { return i.key == moved.key; }); items.insert(items.begin() + pick(n), moved); } break;
            case 4: if (n) items[static_cast<std::size_t>(pick(n))].flag ^= true; break;
            case 5: if (n) items[static_cast<std::size_t>(pick(n))].text = word(); break;
            case 6: if (n) { auto& kids = items[static_cast<std::size_t>(pick(n))].kids; if (pick(2) && !kids.empty()) kids.pop_back(); else kids.insert(kids.begin() + pick(static_cast<int>(kids.size()) + 1), pick(50)); } break;
            case 7: std::ranges::reverse(items); break;
            case 8: if (pick(2) && !plain.empty()) plain.erase(plain.begin() + pick(static_cast<int>(plain.size()))); else plain.insert(plain.begin() + pick(static_cast<int>(plain.size()) + 1), word()); break;
            case 9: std::ranges::reverse(plain); break;
            case 10: show = !show; break;
            case 11: wide = !wide; break;
            case 12: title = pick(3) ? word() : "<&\"quoted\">"; break;
            default: ++counter;
        }
    }
};

VirtualNode tree(Fuzz& s);

Fuzz* current = nullptr;
struct FuzzPage : Fuzz {
    FuzzPage() {
        current = this;
        step = js::function([this](const Value& times) {
            for (int i = 0, n = times.as<int>(); i < n; ++i) mutate();
            invalidate();
        });
        expected = js::function([this] { return detail::outline(tree(*this)); });
        Value window = Value::global("window");
        window.set("fuzzStep", step);
        window.set("fuzzExpected", expected);
    }
};

// A component, and the same as a plain function.
inline constexpr Component Card = [](const Item& item) {
    return tag::Div[attr::Class = item.flag ? "on" : "off"](
        tag::B(item.text), item.flag ? tag::I("flag") : None,
        tag::Ul(Map(item.kids, [](int kid) { return tag::Li(kid); })));
};
VirtualNode badge(const std::string& text) { return tag::Span[attr::Class = "badge"](text); }

VirtualNode tree(Fuzz& s) {
    return tag::Div[attr::Id = "fuzz"](
        tag::P[s.counter % 3 == 0 ? attr::Title = "three" : None]("counter ", s.counter),
        s.show ? tag::Section(tag::B("shown"), badge(s.title)) : None,
        (s.wide ? tag::Section : tag::Div)[attr::Class = "frame"](tag::Input[attr::Value = s.title](), s.title),
        tag::Ul[attr::Class = "keyed"](Map(s.items, [](const Item& item) {
            return tag::Li[attr::Key = item.key, attr::Data("n") = item.text, item.flag ? attr::Title = item.text : None](
                item.flag ? Card(item) : tag::Span("off ", item.text), item.key % 3 == 0 ? badge("three") : None);
        })),
        tag::Ol(s.plain | std::views::transform([](const std::string& text) { return tag::Li(text); })),
        tag::Div(badge("one"), s.counter % 2 ? badge("odd") : None, badge("two")));
}

VirtualNode site(FuzzPage& s) { return tree(s); }

int main() { return run(site); }

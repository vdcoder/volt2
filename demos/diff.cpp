// The diff demo: a randomized test of how volt2 updates the page. The page's data changes at
// random (the same way on every run), and after each render the page must be exactly the tree
// C++ rendered: keyed and unkeyed lists, nested lists, conditionals, attributes that come and
// go, a container whose tag changes, components and plain helper functions.
// tests/diff.test.cjs drives it through window.fuzzStep and window.fuzzExpected; the buttons on
// the page make the same random changes by hand.
#include "volt2.hpp"

#include <algorithm>
#include <random>
#include <ranges>
#include <string>
#include <vector>

using namespace volt2;

// The look shared by the demos: Pico CSS (it styles plain HTML), the Inter font from Google Fonts,
// and a few rules of our own. Any URLs work: volt2 only puts them in the page's <head>.
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
        .on { color: var(--pico-primary); } .off { color: var(--pico-muted-color); }
        .badge { display: inline-block; font-size: 0.75rem; padding: 0 0.4rem; margin: 0 0.25rem;
                 border-radius: 1rem; background: var(--pico-secondary-background); color: var(--pico-secondary-inverse); }
    )"),
};

// One card of the page: what it checks, how to try it, what should happen, then the live part.
inline constexpr Component Topic = [](std::string title, std::string checks, std::string tryIt, std::string expect,
                                      VirtualNode live) {
    return tag::Article(tag::Header(tag::Strong(title)),
                        tag::P[attr::Class = "about"](tag::Strong("Checks: "), checks),
                        tag::P[attr::Class = "about"](tag::Strong("Try it: "), tryIt),
                        tag::P[attr::Class = "about"](tag::Strong("Expect: "), expect),
                        tag::Div[attr::Class = "live"](live));
};

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
    int changes = 0;  // made so far
    std::string title = "title";
    js::Function step, expected;

    int pick(int n) { return n <= 0 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(random); }
    std::string word() { return std::string(1, static_cast<char>('a' + pick(26))) + std::to_string(pick(100)); }
    Item fresh() { return {nextKey++, word(), pick(2) == 1, {}}; }

    void mutate() {
        ++changes;
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

// The whole page: the test compares it, explanation included, with what the browser shows.
VirtualNode tree(Fuzz& s) {
    auto steps = [&s](int count) {
        return event::OnClick([&s, count] {
            for (int i = 0; i < count; ++i) s.mutate();
        });
    };
    return tag::Main[attr::Id = "fuzz", attr::Class = "container"](
        tag::Header(tag::H1("Diff"),
                    tag::P("After each render volt2 sends the page only what changed. This page checks that it gets "
                           "that right, whatever the change: its data changes at random, and the test compares the page "
                           "with the tree C++ rendered after every change.")),
        Topic("Random changes",
              "keyed items inserted, removed, moved, reversed; their contents and attributes changed; an unkeyed list "
              "growing and shrinking; sections coming and going; a container switching between div and section; "
              "components and plain helper functions. The test makes 400 such changes and, after each one, compares "
              "the page with detail::outline(tree()) in C++, and checks volt2 tracks exactly the nodes shown.",
              "click the buttons a few times; to check it like the test does, type "
              "fuzzStep(1); document.querySelector('#fuzz').outerHTML in the browser console and compare.",
              "the content below changes in all those ways, and never shows leftovers or duplicates.",
              tag::Div(tag::Button[attr::Id = "one", steps(1)]("1 random change"),
                       tag::Button[attr::Id = "ten", attr::Class = "secondary", steps(10)]("10 random changes"),
                       tag::P[attr::Id = "changes"]("changes so far: ", s.changes))),
        tag::Article(
        tag::Header(tag::Strong("The page that changes")),
        tag::P[s.counter % 3 == 0 ? attr::Title = "three" : None]("counter ", s.counter),
        s.show ? tag::Section(tag::B("shown"), badge(s.title)) : None,
        (s.wide ? tag::Section : tag::Div)[attr::Class = "frame"](tag::Input[attr::Value = s.title](), s.title),
        tag::Ul[attr::Class = "keyed"](Map(s.items, [](const Item& item) {
            return tag::Li[attr::Key = item.key, attr::Data("n") = item.text, item.flag ? attr::Title = item.text : None](
                item.flag ? Card(item) : tag::Span("off ", item.text), item.key % 3 == 0 ? badge("three") : None);
        })),
        tag::Ol(s.plain | std::views::transform([](const std::string& text) { return tag::Li(text); })),
        tag::Div(badge("one"), s.counter % 2 ? badge("odd") : None, badge("two"))));
}

VirtualNode site(FuzzPage& s) { return tree(s); }

int main() { return run(site, {.head = kLook}); }

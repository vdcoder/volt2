// The locals demo: state inside components (volt2::local). Each component call keeps its own
// state, found while the component runs; it lives as long as the component is called, and in
// lists it follows the item's key. tests/locals.test.cjs drives it; every card says what it
// checks, how to try it by hand, and what should happen.
#include "volt2.hpp"

#include <algorithm>
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
        .toggle { display: inline-block; margin-right: 0.5rem; }
        .toggle span { color: var(--pico-primary); font-weight: 600; margin-right: 0.75rem; }
        .pair { padding: 0.5rem 0.75rem 0; border-left: 3px solid var(--pico-primary); margin-bottom: 0.75rem; }
        .pair input { margin-bottom: 0.5rem; }
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

struct Locals : Session {
    std::vector<std::string> items{"a", "b", "c"};
    std::vector<std::string> plain{"x", "y"};
    bool showFirst = true;
    bool hideCached = false;
    bool repeat = false;
};

// A component with state: each call site has its own. The button counts its clicks and opens
// and closes; both live in local() values, not in the session.
inline constexpr Component Toggle = [](std::string label) {
    bool& open = local(false);
    int& clicks = local(0);
    return tag::Div[attr::Class = "toggle"](tag::Button[event::OnClick([&open, &clicks] {
                                                open = !open;
                                                ++clicks;
                                            })](label, " ", clicks),
                                            open ? tag::Span(" open") : None);
};

// Components inside a component, and a state of another type (a std::string).
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
    return tag::Main[attr::Id = "root", attr::Class = "container"](
        tag::Header(tag::H1("State inside components"),
                    tag::P("A component can keep state of its own: bool& open = volt2::local(false);. It belongs to that "
                           "call of the component, is found while the component runs, and lives as long as the "
                           "component is called. Every button below keeps its count and its \"open\" in local() "
                           "values, none in the session.")),

        Topic("Each call has its own",
              "two calls of the Toggle component, on two lines, each with its own local() values; site() itself can "
              "keep state too (the site button).",
              "click first once, second twice, and site once.",
              "first shows 1 and \"open\", second shows 2 and is closed, site shows 1: none of them share.",
              tag::Div(tag::Button[attr::Id = "site", attr::Class = "secondary", event::OnClick([&count] { ++count; })](
                           "site ", count),
                       tag::Div[attr::Id = "first"](s.showFirst ? Toggle("first") : None),
                       tag::Div[attr::Id = "second"](Toggle("second")))),

        Topic("Keyed lists: state follows the item",
              "Map with a key (each item's name) gives each item a call of its own: its components' state, and its "
              "element, go with it when the list reorders. A removed item's state is dropped; added again, it starts "
              "over. Each item is a Pair component with a text note (local<std::string>) and two Toggles.",
              "in b: click b-1 and type in its box. Then click reverse, then remove first, then add c.",
              "after reverse, b keeps its count, \"open\" and your text. remove first drops c; add c brings it back "
              "fresh, with 0 and \"note\".",
              tag::Div(tag::Div[attr::Id = "keyed"](Map(s.items, [](const std::string& item) { return item; },
                                                        [](const std::string& item) { return Pair(item); })),
                       tag::Button[attr::Id = "reverse", event::OnClick([&s] { std::ranges::reverse(s.items); })]("reverse"),
                       tag::Button[attr::Id = "remove-first", attr::Class = "secondary",
                                   event::OnClick([&s] { s.items.erase(s.items.begin()); })]("remove first"),
                       tag::Button[attr::Id = "add-c", attr::Class = "secondary", event::OnClick([&s] { s.items.push_back("c"); })](
                           "add c"))),

        Topic("Lists without keys, and Loop",
              "Map without a key, and Loop, tell items apart by position: state stays at its position.",
              "click x, then reverse these. Click loop1.",
              "after reversing, the first one reads \"y 1 open\": the state stayed first. loop1 counts on its own.",
              tag::Div(tag::Div[attr::Id = "plain"](Map(s.plain, [](const std::string& item) { return Toggle(item); })),
                       tag::Button[attr::Id = "reverse-plain", event::OnClick([&s] { std::ranges::reverse(s.plain); })](
                           "reverse these"),
                       tag::Div[attr::Id = "looped"](Loop(2, [](std::size_t i) { return Toggle("loop" + std::to_string(i)); })))),

        Topic("When state is kept, and dropped",
              "state lives as long as the component is called. first is called only while shown, so hiding it drops "
              "its state. cached is called on every render (made ahead in a variable) and only shown sometimes, so it "
              "keeps its state while hidden.",
              "click first and cached; then click \"first\" twice and \"cached\" twice below.",
              "first comes back at 0; cached comes back with its count and \"open\".",
              tag::Div(tag::Div[attr::Id = "cached"](s.hideCached ? None : cached),
                       tag::Button[attr::Id = "toggle-first", attr::Class = "outline",
                                   event::OnClick([&s] { s.showFirst = !s.showFirst; })]("first"),
                       tag::Button[attr::Id = "toggle-cached", attr::Class = "outline",
                                   event::OnClick([&s] { s.hideCached = !s.hideCached; })]("cached"))),

        Topic("Mistakes volt2 reports",
              "a plain for loop calling the same component repeats one call, so its items share their state: volt2 "
              "warns once, pointing to Map. local() outside a render (here, in a click handler) is an error.",
              "click repeat, then misuse, with the browser console open.",
              "two \"same\" toggles appear, and the console warns \"the same component call ran twice in one render, "
              "... use Map\"; misuse reports \"volt2::local works while the page renders\".",
              tag::Div(s.repeat ? repeated() : None,
                       tag::Button[attr::Id = "repeat-on", attr::Class = "secondary", event::OnClick([&s] { s.repeat = true; })](
                           "repeat"),
                       tag::Button[attr::Id = "misuse", attr::Class = "contrast", event::OnClick([] { ++local(0); })](
                           "misuse"))));
}

int main() { return run(site, {.head = kLook}); }

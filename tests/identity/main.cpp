// Test program for identity preservation, driven by tests/identity.test.cjs.
#include "volt2.hpp"

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

using namespace volt2;

constexpr const char* kPixel = "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==";

struct Identity : Session {
    int renders = 0;
    std::string name;
    std::string query;
    std::vector<std::string> keyed{"a", "b", "c", "d"};
    std::vector<std::string> plain{"p1", "p2", "p3"};
    bool right = false;
    bool wide = false;
    bool flip = false;
    bool duplicate = false;
    bool agree = false;
    std::string color = "green";
    bool loaded = false;
    bool branch = false;
    std::vector<std::string> options;  // none at first

    // A member component, which can use the session's state.
    Component<VirtualNode(std::string)> Badge = [this](std::string text) {
        return tag::Span[attr::Class = "badge"](text, " after ", renders, " renders");
    };
};

// A component: each call site is its own element.
inline constexpr Component Card = [](std::string title) {
    return tag::Div[attr::Class = "card"](tag::H3(title), tag::Input[attr::Class = "card-input"]());
};

// The same, as a plain function: every call is the same element, told apart only by order.
VirtualNode plainCard(std::string title) {
    return tag::Div[attr::Class = "plain-card"](tag::H3(title), tag::Input[attr::Class = "card-input"]());
}

VirtualNode site(Identity& s) {
    ++s.renders;
    auto enter = [](const Value& e) { return e["key"].as<std::string>() == "Enter"; };

    // A local component may capture.
    const Component Player = [&s, enter] {
        return tag::Div[attr::Id = "player"]("player ", tag::Input[attr::Class = "player-input", event::OnKeyDown([&s, enter](const Value& e) {
            if (enter(e)) s.right = !s.right;
        })]());
    };

    return tag::Div[attr::Id = "root"](
        tag::P[attr::Id = "renders"]("renders ", s.renders),

        // Typing renders on every keystroke; the input keeps its node, the focus and the caret.
        tag::Input[attr::Id = "name", attr::Value = s.name,
                   event::OnInput([&s](const Value& e) { s.name = e["target"]["value"].as<std::string>(); })](),
        tag::P[attr::Id = "greeting"]("Hello, ", s.name),
        tag::Button[attr::Id = "clear", event::OnClick([&s] { s.name.clear(); })]("clear"),

        // An element appearing above the focused input.
        s.query.size() >= 3 ? tag::P[attr::Id = "banner"]("searching for ", s.query) : None,
        tag::Input[attr::Id = "query",
                   event::OnInput([&s](const Value& e) { s.query = e["target"]["value"].as<std::string>(); })](),

        // Keys: reordering keeps each item's node. Enter in an item's input rotates the list.
        tag::Ul[attr::Id = "keyed"](Map(s.keyed, [&s, enter](const std::string& item) {
            return tag::Li[attr::Key = item](item, tag::Input[attr::Class = "item-input", event::OnKeyDown([&s, enter](const Value& e) {
                                                 if (enter(e)) std::ranges::rotate(s.keyed, s.keyed.begin() + 1);
                                             })]());
        })),
        // No keys: items are told apart by position, so nodes stay and their text changes.
        tag::Ul[attr::Id = "plain"](s.plain | std::views::transform([](const std::string& item) { return tag::Li(item); })),
        tag::Button[attr::Id = "reverse", event::OnClick([&s] {
            std::ranges::reverse(s.keyed);
            std::ranges::reverse(s.plain);
        })]("reverse"),

        // A key says which element it is, whichever line made it, and must be unique among siblings.
        tag::Ul[attr::Id = "branches"](s.branch ? tag::Li[attr::Key = "same"]("from one line")
                                                : tag::Li[attr::Key = "same"]("from another line")),
        tag::Button[attr::Id = "branch", event::OnClick([&s] { s.branch = !s.branch; })]("branch"),
        tag::Div[attr::Id = "dup-keys"](tag::Span[attr::Key = "k"]("one"), tag::Span[attr::Key = "k"]("two")),

        // An element under another parent is another element, even with an id: ids are
        // ordinary attributes.
        tag::Div[attr::Id = "left"](s.right ? None : Player()),
        tag::Div[attr::Id = "right"](s.right ? Player() : None),

        // A container whose tag changes is a new element, but its children keep their nodes,
        // and a focused input among them gets its focus and selection back after the move.
        (s.wide ? tag::Section : tag::Div)[attr::Id = "frame"](tag::Input[event::OnKeyDown([&s, enter](const Value& e) {
            if (enter(e)) s.wide = !s.wide;
        })]()),

        // Components: each call site is its own element; a plain function's calls are not.
        tag::Div[attr::Id = "cards"](s.flip ? Card("b") : Card("a")),
        tag::Div[attr::Id = "plain-cards"](s.flip ? plainCard("b") : plainCard("a")),
        tag::Button[attr::Id = "flip", event::OnClick([&s] { s.flip = !s.flip; })]("flip"),
        tag::P[attr::Id = "badge"](s.Badge("member")),

        // Duplicate ids are reported, and both elements still render.
        tag::Div[attr::Id = "dups"](tag::Span[attr::Id = "dup"]("one"),
                                    s.duplicate ? tag::Span[attr::Id = "dup"]("two") : None),
        tag::Button[attr::Id = "duplicate", event::OnClick([&s] { s.duplicate = true; })]("duplicate"),

        // Form controls show the state: checked, and a select's value.
        tag::Input[attr::Id = "agree", attr::Type = "checkbox", attr::Checked = s.agree,
                   event::OnChange([&s](const Value& e) { s.agree = e["target"]["checked"].as<bool>(); })](),
        tag::Button[attr::Id = "disagree", event::OnClick([&s] { s.agree = false; })]("disagree"),
        tag::Select[attr::Id = "color", attr::Value = s.color,
                    event::OnChange([&s](const Value& e) { s.color = e["target"]["value"].as<std::string>(); })](
            tag::Option[attr::Value = "red"]("red"), tag::Option[attr::Value = "green"]("green"),
            tag::Option[attr::Value = "blue"]("blue")),
        tag::Button[attr::Id = "blue", event::OnClick([&s] { s.color = "blue"; })]("blue"),
        tag::P[attr::Id = "color-state"](s.color),
        // A select keeps showing its value while its options change around it.
        tag::Select[attr::Id = "fixed", attr::Value = "green"](
            Map(s.options, [](const std::string& option) { return tag::Option[attr::Value = option](option); })),
        tag::Button[attr::Id = "options", event::OnClick([&s] {
            s.options = s.options.empty() ? std::vector<std::string>{"red", "green"} : std::vector<std::string>{"green", "blue"};
        })]("options"),

        // An element's own events render like any other, and do not loop.
        tag::Img[attr::Id = "img", attr::Src = kPixel, event::OnLoad([&s] { s.loaded = true; })](),
        tag::P[attr::Id = "loaded"](s.loaded ? "loaded" : "loading"),

        // A handler that changes nothing: the render only updates the render count.
        tag::Button[attr::Id = "noop", event::OnClick([] {})]("noop"),

        // Map with the index, and Loop.
        tag::P[attr::Id = "mapped"](Map(s.plain, [](const std::string& item, std::size_t i) { return tag::Span(i, ":", item, " "); })),
        tag::P[attr::Id = "looped"](Loop(3, [](std::size_t i) { return tag::Span(i * 10, " "); })));
}

int main() { return run(site); }

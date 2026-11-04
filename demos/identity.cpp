// The identity demo: why renders keep elements (and with them focus, typed text and the caret),
// and how volt2 tells elements apart: where the code made them, keys, components, positions in a
// list. tests/identity.test.cjs drives it; every card says what it checks, how to try it by
// hand, and what should happen.
#include "volt2.hpp"

#include <algorithm>
#include <ranges>
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
        .live li input { display: inline-block; width: 12rem; margin: 0 0 0 0.75rem; padding: 0.2rem 0.5rem; height: auto; }
        #left, #right { min-height: 4rem; padding: 0.75rem; border: 1px solid var(--pico-muted-border-color);
                        border-radius: var(--pico-border-radius); margin-bottom: 0.75rem; }
        #left::before { content: "left"; } #right::before { content: "right"; }
        #left::before, #right::before { display: block; font-size: 0.75rem; color: var(--pico-muted-color); }
        .card, .plain-card { padding: 0.5rem 0.75rem; border-left: 3px solid var(--pico-primary); margin-bottom: 0.75rem; }
        .card h3, .plain-card h3 { margin-bottom: 0.5rem; }
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
    return tag::Div[attr::Class = "card"](tag::H3(title), tag::Input[attr::Class = "card-input", attr::Placeholder = "type here"]());
};

// The same, as a plain function: every call is the same element, told apart only by order.
VirtualNode plainCard(std::string title) {
    return tag::Div[attr::Class = "plain-card"](tag::H3(title),
                                                tag::Input[attr::Class = "card-input", attr::Placeholder = "type here"]());
}

VirtualNode site(Identity& s) {
    ++s.renders;
    auto enter = [](const Value& e) { return e["key"].as<std::string>() == "Enter"; };

    // A local component may capture.
    const Component Player = [&s, enter] {
        return tag::Div[attr::Id = "player"]("player ", tag::Input[attr::Class = "player-input", attr::Placeholder = "type, then Enter",
                                                                   event::OnKeyDown([&s, enter](const Value& e) {
                                                                       if (enter(e)) s.right = !s.right;
                                                                   })]());
    };

    return tag::Main[attr::Id = "root", attr::Class = "container"](
        tag::Header(tag::H1("Identity"),
                    tag::P("Every handler renders the page again, and volt2 changes only what differs: elements keep "
                           "their DOM nodes, so the focus, the caret, typed text and scroll positions stay. These cards "
                           "show how volt2 decides that an element is the same one as last time."),
                    tag::P[attr::Id = "renders"]("renders ", s.renders)),

        Topic("Typing while every key renders",
              "an input whose OnInput handler stores the text: every key renders, and the input is updated in place, "
              "never replaced. Its value is set only when the rendered value changes, which keeps the caret.",
              "type a word, press Home, type two letters; then click clear.",
              "the greeting follows each key, the caret stays where you put it (the letters go to the start), and "
              "clear empties the box: C++ can set the value too.",
              tag::Div(tag::Input[attr::Id = "name", attr::Value = s.name, attr::Placeholder = "Your name",
                                  event::OnInput([&s](const Value& e) { s.name = e["target"]["value"].as<std::string>(); })](),
                       tag::P[attr::Id = "greeting"]("Hello, ", s.name),
                       tag::Button[attr::Id = "clear", attr::Class = "secondary", event::OnClick([&s] { s.name.clear(); })](
                           "clear"))),

        Topic("An element appearing above the focused input",
              "after three letters a line appears above the input; the input must stay where it is in the tree.",
              "type four letters in the box.",
              "\"searching for ...\" appears above it after the third, and the box keeps the focus and the text.",
              tag::Div(s.query.size() >= 3 ? tag::P[attr::Id = "banner"]("searching for ", s.query) : None,
                       tag::Input[attr::Id = "query", attr::Placeholder = "Search",
                                  event::OnInput([&s](const Value& e) { s.query = e["target"]["value"].as<std::string>(); })]())),

        Topic("Lists: with keys, and without",
              "the first list gives each item a key (attr::Key = its letter), so items move with their elements; the "
              "second has none, so its items are told apart by position: elements stay put and their text changes. "
              "When a list reorders, the others move around the element holding the focus.",
              "type something into item d's box, click reverse; then put the cursor in the first item's box and "
              "press Enter (it rotates the list).",
              "with keys, d moves to the top with your text in it; without keys, the second list's text reverses in "
              "place. On Enter, the list rotates and the box you are in keeps the focus and the text.",
              tag::Div(tag::Ul[attr::Id = "keyed"](Map(s.keyed, [&s, enter](const std::string& item) {
                           return tag::Li[attr::Key = item](item, tag::Input[attr::Class = "item-input", event::OnKeyDown([&s, enter](const Value& e) {
                                                                if (enter(e)) std::ranges::rotate(s.keyed, s.keyed.begin() + 1);
                                                            })]());
                       })),
                       tag::Ul[attr::Id = "plain"](
                           s.plain | std::views::transform([](const std::string& item) { return tag::Li(item); })),
                       tag::Button[attr::Id = "reverse", event::OnClick([&s] {
                           std::ranges::reverse(s.keyed);
                           std::ranges::reverse(s.plain);
                       })]("reverse"))),

        Topic("A key names the element, whichever line made it",
              "the item below is drawn by one branch of a conditional or the other; both give it the key \"same\", so "
              "it stays the same element. Keys must be unique among siblings: the two spans under it share \"k\", and "
              "volt2 says so in the console.",
              "click branch; look at the browser console.",
              "the text changes between \"from one line\" and \"from another line\" in the same element; the console "
              "has a warning about attr::Key = \"k\".",
              tag::Div(tag::Ul[attr::Id = "branches"](s.branch ? tag::Li[attr::Key = "same"]("from one line")
                                                                : tag::Li[attr::Key = "same"]("from another line")),
                       tag::Button[attr::Id = "branch", event::OnClick([&s] { s.branch = !s.branch; })]("branch"),
                       tag::Div[attr::Id = "dup-keys"](tag::Span[attr::Key = "k"]("one "), tag::Span[attr::Key = "k"]("two")))),

        Topic("Elements stay under their parent",
              "an element under another parent is another element, even with an id (ids are ordinary attributes). "
              "The player is a local component, called from the left box or the right one.",
              "type in the player's box, then press Enter (it moves the player to the other box).",
              "the player appears in the other box as a new element: its box is empty.",
              tag::Div(tag::Div[attr::Id = "left"](s.right ? None : Player()),
                       tag::Div[attr::Id = "right"](s.right ? Player() : None))),

        Topic("A container whose tag changes",
              "when an element's tag changes (here between div and section), it is a new element, but its children "
              "keep their nodes and move into it; a focused input among them gets its focus and selection back.",
              "type in the box, select some of it, press Enter (it switches the tag).",
              "the box keeps its text, the focus and the selection.",
              (s.wide ? tag::Section : tag::Div)[attr::Id = "frame"](tag::Input[attr::Placeholder = "type, select, then Enter",
                                                                               event::OnKeyDown([&s, enter](const Value& e) {
                                                                                   if (enter(e)) s.wide = !s.wide;
                                                                               })]())),

        Topic("Components and plain functions",
              "volt2::Component gives each call site its own elements: Card(\"a\") and Card(\"b\") on two lines are "
              "two elements. A plain function's calls all count as one element. The badge is a session member "
              "component, reading the session's state.",
              "type in both boxes, then click flip.",
              "the component's box (first) is a new, empty element; the plain function's box (second) is the same "
              "element with your text still in it, under the new title.",
              tag::Div(tag::Div[attr::Id = "cards"](s.flip ? Card("b") : Card("a")),
                       tag::Div[attr::Id = "plain-cards"](s.flip ? plainCard("b") : plainCard("a")),
                       tag::Button[attr::Id = "flip", event::OnClick([&s] { s.flip = !s.flip; })]("flip"),
                       tag::P[attr::Id = "badge"](s.Badge("member")))),

        Topic("Duplicate ids",
              "two elements with the same id are both rendered, and volt2 warns in the console (HTML wants ids "
              "unique).",
              "click duplicate; look at the browser console.",
              "a second span appears, and the console warns that two elements have id=\"dup\".",
              tag::Div(tag::Div[attr::Id = "dups"](tag::Span[attr::Id = "dup"]("one "),
                                                   s.duplicate ? tag::Span[attr::Id = "dup"]("two") : None),
                       tag::Button[attr::Id = "duplicate", event::OnClick([&s] { s.duplicate = true; })]("duplicate"))),

        Topic("Form controls follow the state",
              "attr::Checked and attr::Value also set what the control shows when the rendered value changes; a "
              "select keeps showing its value while its options change around it.",
              "check the box, then click disagree. Click blue; pick red in the first select. Click options twice.",
              "disagree unchecks the box; blue selects blue and red shows red below it. The second select shows "
              "green both after its options arrive and after they change.",
              tag::Div(tag::Label(tag::Input[attr::Id = "agree", attr::Type = "checkbox", attr::Checked = s.agree,
                                             event::OnChange([&s](const Value& e) { s.agree = e["target"]["checked"].as<bool>(); })](),
                                  " I agree"),
                       tag::Button[attr::Id = "disagree", attr::Class = "secondary", event::OnClick([&s] { s.agree = false; })](
                           "disagree"),
                       tag::Select[attr::Id = "color", attr::Value = s.color,
                                   event::OnChange([&s](const Value& e) { s.color = e["target"]["value"].as<std::string>(); })](
                           tag::Option[attr::Value = "red"]("red"), tag::Option[attr::Value = "green"]("green"),
                           tag::Option[attr::Value = "blue"]("blue")),
                       tag::Button[attr::Id = "blue", event::OnClick([&s] { s.color = "blue"; })]("blue"),
                       tag::P[attr::Id = "color-state"](s.color),
                       tag::Select[attr::Id = "fixed", attr::Value = "green"](
                           Map(s.options, [](const std::string& option) { return tag::Option[attr::Value = option](option); })),
                       tag::Button[attr::Id = "options", event::OnClick([&s] {
                           s.options = s.options.empty() ? std::vector<std::string>{"red", "green"}
                                                         : std::vector<std::string>{"green", "blue"};
                       })]("options"))),

        Topic("Events elements fire themselves",
              "an image's load event renders like any other event, and does not loop: the render keeps the image, "
              "so it does not load again.",
              "",
              "\"loaded\" right away, and the render count at the top stays still.",
              tag::Div(tag::Img[attr::Id = "img", attr::Src = kPixel, attr::Alt = "", event::OnLoad([&s] { s.loaded = true; })](),
                       tag::P[attr::Id = "loaded"](s.loaded ? "loaded" : "loading"))),

        Topic("Only what changed",
              "a handler that changes nothing still renders, but the page only gets what differs: here, the two "
              "texts that show the render count.",
              "click noop with the developer tools' Elements panel open.",
              "only the render count at the top and in the badge flash as changed.",
              tag::Button[attr::Id = "noop", attr::Class = "secondary", event::OnClick([] {})]("noop")),

        Topic("Map and Loop",
              "Map may pass each item's index; Loop makes n items.",
              "",
              "\"0:p1 1:p2 2:p3\" (or reversed after reverse above), then \"0 10 20\".",
              tag::Div(tag::P[attr::Id = "mapped"](
                           Map(s.plain, [](const std::string& item, std::size_t i) { return tag::Span(i, ":", item, " "); })),
                       tag::P[attr::Id = "looped"](Loop(3, [](std::size_t i) { return tag::Span(i * 10, " "); })))));
}

int main() { return run(site, {.head = kLook}); }

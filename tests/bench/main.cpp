// Render speed: how long volt2 takes to build, compare and write one render of a large page
// (1,000 table rows, about 9,000 nodes), without a browser. tests/run.sh prints it; or:
//   g++ -std=c++23 -O2 -I. tests/bench/main.cpp -o bench -pthread && ./bench
#include "volt2.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace volt2;
using Clock = std::chrono::steady_clock;

struct Table : Session {
    std::vector<int> rows;
    int tick = 0;
};

inline constexpr Component Row = [](Table& s, int row) {
    return tag::Tr[attr::Key = row](tag::Td(row), tag::Td[attr::Class = row % 2 ? "odd" : "even"]("row ", row),
                                    tag::Td(tag::Button[event::OnClick([&s, row] { s.tick += row; })]("x")));
};

VirtualNode site(Table& s) {
    return tag::Div[attr::Id = "root"](tag::P("tick ", s.tick),
                                       tag::Table(tag::Tbody(Map(s.rows, [&s](int row) { return Row(s, row); }))));
}

double milliseconds(Clock::time_point since) { return std::chrono::duration<double, std::milli>(Clock::now() - since).count(); }

int main() {
    Table s;
    for (int i = 0; i < 1000; ++i) s.rows.push_back(i);
    detail::Renderer renderer([&s] { return site(s); });

    // Each scenario: the average of `times` renders, each after `change`, and the site alone
    // (run right after, warm like the render) to tell building the tree from comparing it.
    auto measure = [&](const char* what, int times, auto change, bool fresh = false) {
        double total = 0, build = 0;
        std::size_t bytes = 0;
        for (int i = 0; i < times; ++i) {
            change();
            detail::Renderer first([&s] { return site(s); });
            detail::Renderer& r = fresh ? first : renderer;
            auto start = Clock::now();
            detail::Renderer::Patch patch = r.diff();
            total += milliseconds(start);
            bytes = patch.steps.size() + patch.unused.size();
            start = Clock::now();
            VirtualNode tree = site(s);
            build += milliseconds(start);
        }
        std::printf("  %-28s %6.2f ms  (site %5.2f, compare and write %5.2f)  %7zu bytes\n", what, total / times,
                    build / times, (total - build) / times, bytes);
    };
    std::printf("  one render of %zu rows, averaged (g++ -O2 on this machine):\n", s.rows.size());
    for (int i = 0; i < 3; ++i) site(s);  // warm up
    measure("first render, all new", 20, [] {}, true);
    for (int i = 0; i < 5; ++i) renderer.diff();
    measure("one text changed", 50, [&] { ++s.tick; });
    measure("a class on every row", 20, [&] { for (int& row : s.rows) row += 2; });
    measure("insert a row at the front", 20, [&] { s.rows.insert(s.rows.begin(), 100000 + s.tick++); });
    measure("remove a row in the middle", 20, [&] { s.rows.erase(s.rows.begin() + static_cast<long>(s.rows.size() / 2)); });
    measure("reverse all rows", 20, [&] { std::ranges::reverse(s.rows); });
    measure("nothing changed", 50, [] {});
}

# Limit Order Book Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the single-threaded C++ limit order book simulator specified in `docs/superpowers/specs/2026-08-09-limit-order-book-design.md`.

**Architecture:** A pure-data-structure `OrderBook` (sorted maps of FIFO price levels + hash cancel index) driven by a `MatchingEngine` (all crossing logic). A seeded `MarketSimulator` and a step-based REPL both feed the engine; a `--benchmark` mode times the engine alone. Prices are `int64_t` ticks (1 tick = $0.01) — no floating point anywhere.

**Tech Stack:** C++20, standard library only. CMake ≥ 3.16 + CTest. Hand-rolled test harness (`tests/test_framework.h`). Apple clang on macOS.

**Workflow rules (apply to every task):**
- **Pair-per-component:** every task ends with a walkthrough checkpoint — STOP, present the component to Renzo with the listed talking points, wait for approval, then `git push` (that's the milestone push he authorized).
- Commit messages end with `-m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"` as a second `-m` paragraph.
- Dev builds use `cmake -B build` (no `CMAKE_BUILD_TYPE` → asserts active). Benchmarks use a separate `build-release`.
- Run tests with `./build/orderbook_tests` (per-test output); `ctest --test-dir build` must also pass before each commit.

---

## File structure

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Both targets, all files listed up front (stubs first, filled in per task) — never edited again |
| `src/types.h` | `Price`, `Quantity`, `OrderId`, `Side`, `OrderType`, `Order`, `Trade` |
| `src/order_book.{h,cpp}` | Resting-order storage, price-time priority, O(1) cancel. No matching decisions |
| `src/matching_engine.{h,cpp}` | Crossing algorithm, `SubmitResult`, never-crossed invariant |
| `src/display.{h,cpp}` | `formatPrice`/`parsePrice`/`formatMid`, book + trade rendering |
| `src/simulator.{h,cpp}` | Seeded random order flow, initial liquidity, own-order cancels |
| `src/repl.{h,cpp}` | `parseCommand` (pure, tested) + interactive loop |
| `src/benchmark.{h,cpp}` | Pre-generate N orders, time `submit*` only, report throughput |
| `src/main.cpp` | CLI dispatch: REPL (default), `--benchmark [N]`, `--seed N` |
| `tests/test_framework.h` | `TEST`, `CHECK`, `CHECK_EQ`, runner |
| `tests/test_main.cpp` | `main()` → `run_all()` |
| `tests/test_order_book.cpp` | Book unit tests |
| `tests/test_matching.cpp` | The spec's 12 engine scenarios |
| `tests/test_display.cpp` | Formatting/parsing tests |
| `tests/test_simulator.cpp` | Determinism + validity tests |
| `tests/test_repl.cpp` | Command-parsing tests |

---

### Task 0: Install CMake

**Files:** none.

- [ ] **Step 1: Install**

Run: `brew install cmake`

- [ ] **Step 2: Verify**

Run: `cmake --version`
Expected: `cmake version 3.x` (or 4.x) — anything ≥ 3.16.

---

### Task 1: Scaffolding, core types, test framework

**Files:**
- Create: `CMakeLists.txt`, `src/types.h`, `tests/test_framework.h`, `tests/test_main.cpp`
- Create (stubs): `src/order_book.{h,cpp}`, `src/matching_engine.{h,cpp}`, `src/display.{h,cpp}`, `src/simulator.{h,cpp}`, `src/repl.{h,cpp}`, `src/benchmark.{h,cpp}`, `src/main.cpp`, `tests/test_order_book.cpp`, `tests/test_matching.cpp`, `tests/test_display.cpp`, `tests/test_simulator.cpp`, `tests/test_repl.cpp`

- [ ] **Step 1: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(limit_order_book CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

set(CORE_SOURCES
    src/order_book.cpp
    src/matching_engine.cpp
    src/display.cpp
    src/simulator.cpp
    src/repl.cpp
    src/benchmark.cpp
)

add_executable(orderbook src/main.cpp ${CORE_SOURCES})
target_include_directories(orderbook PRIVATE src)

add_executable(orderbook_tests
    tests/test_main.cpp
    tests/test_order_book.cpp
    tests/test_matching.cpp
    tests/test_display.cpp
    tests/test_simulator.cpp
    tests/test_repl.cpp
    ${CORE_SOURCES}
)
target_include_directories(orderbook_tests PRIVATE src tests)

enable_testing()
add_test(NAME all_tests COMMAND orderbook_tests)
```

- [ ] **Step 2: Write `src/types.h`**

```cpp
#pragma once
#include <cstdint>

namespace lob {

// Prices are integer ticks: 1 tick = $0.01, so $100.10 == 10010.
// Doubles are never used for money — 100.10 has no exact binary
// representation, which breaks map keys and equality.
using Price = std::int64_t;
using Quantity = std::int64_t;
using OrderId = std::uint64_t;

constexpr Price TICKS_PER_DOLLAR = 100;

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };

struct Order {
    OrderId id;
    Side side;
    OrderType type;
    Price price;        // ignored for Market orders
    Quantity quantity;
};

struct Trade {
    OrderId takerId;    // the incoming order
    OrderId makerId;    // the resting order
    Price price;        // always the maker's price
    Quantity quantity;
};

}  // namespace lob
```

- [ ] **Step 3: Write `tests/test_framework.h`**

```cpp
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

inline int run_all() {
    int passed = 0;
    for (const auto& test : registry()) {
        int before = failure_count();
        test.fn();
        if (failure_count() == before) {
            std::printf("PASS  %s\n", test.name.c_str());
            ++passed;
        } else {
            std::printf("FAIL  %s\n", test.name.c_str());
        }
    }
    std::printf("\n%d/%zu tests passed\n", passed, registry().size());
    return failure_count() == 0 ? 0 : 1;
}

}  // namespace testfw

#define TEST(name)                                                  \
    static void test_##name();                                      \
    static testfw::Registrar registrar_##name(#name, test_##name);  \
    static void test_##name()

// CHECK works for any boolean expression (including optional == value).
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++testfw::failure_count();                                    \
            std::printf("  CHECK failed: %s (%s:%d)\n", #cond, __FILE__,  \
                        __LINE__);                                        \
        }                                                                 \
    } while (0)

// CHECK_EQ is for integral values only (prints both sides on failure).
// Both sides are cast to long long so size_t-vs-int comparisons don't
// trigger -Wsign-compare.
#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        long long va = (long long)(a);                                        \
        long long vb = (long long)(b);                                        \
        if (!(va == vb)) {                                                    \
            ++testfw::failure_count();                                        \
            std::printf("  CHECK_EQ failed: %s == %s, got %lld vs %lld "      \
                        "(%s:%d)\n",                                          \
                        #a, #b, (long long)va, (long long)vb, __FILE__,       \
                        __LINE__);                                            \
        }                                                                     \
    } while (0)
```

- [ ] **Step 4: Write `tests/test_main.cpp` with a deliberately failing smoke test**

```cpp
#include "test_framework.h"

TEST(framework_smoke) {
    CHECK_EQ(1 + 1, 3);  // deliberately wrong: prove failures are reported
}

int main() { return testfw::run_all(); }
```

- [ ] **Step 5: Create all stub files**

Every stub `.cpp` in `src/` gets exactly this content (valid, empty translation unit):

```cpp
namespace lob {}  // implemented in a later task
```

Files: `src/order_book.cpp`, `src/matching_engine.cpp`, `src/display.cpp`, `src/simulator.cpp`, `src/repl.cpp`, `src/benchmark.cpp`.

Every stub header in `src/` gets:

```cpp
#pragma once
// implemented in a later task
```

Files: `src/order_book.h`, `src/matching_engine.h`, `src/display.h`, `src/simulator.h`, `src/repl.h`.

`src/main.cpp` stub:

```cpp
#include <cstdio>

int main() {
    std::printf("orderbook: components not wired yet (see plan tasks)\n");
    return 0;
}
```

Every stub test file gets exactly this content (they gain TESTs in later tasks):

```cpp
#include "test_framework.h"
```

Files: `tests/test_order_book.cpp`, `tests/test_matching.cpp`, `tests/test_display.cpp`, `tests/test_simulator.cpp`, `tests/test_repl.cpp`.

- [ ] **Step 6: Configure, build, watch the smoke test fail**

Run: `cmake -B build && cmake --build build && ./build/orderbook_tests`
Expected: build succeeds; output contains `CHECK_EQ failed: 1 + 1 == 3` and `FAIL  framework_smoke`, `0/1 tests passed`; exit code 1. This proves the harness detects and reports failures.

- [ ] **Step 7: Fix the smoke test**

In `tests/test_main.cpp`, change the check to:

```cpp
    CHECK_EQ(1 + 1, 2);
```

- [ ] **Step 8: Run tests to verify pass**

Run: `cmake --build build && ./build/orderbook_tests && ctest --test-dir build`
Expected: `PASS  framework_smoke`, `1/1 tests passed`, exit 0; ctest reports `100% tests passed`.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "feat: project scaffolding, core types, hand-rolled test framework" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 10: Pair walkthrough (STOP), then push**

Walk Renzo through: why integer ticks (float keys break maps); the `Order`/`Trade` fields; how `TEST`/`Registrar` self-registration works (static objects run before `main`); why `CHECK_EQ` is integral-only. Wait for approval. Then run: `git push`

---

### Task 2: OrderBook

**Files:**
- Modify: `src/order_book.h`, `src/order_book.cpp` (replace stubs)
- Test: `tests/test_order_book.cpp`

- [ ] **Step 1: Write the failing tests** (replace `tests/test_order_book.cpp` content)

```cpp
#include "test_framework.h"

#include "order_book.h"

using namespace lob;

namespace {
Order limit(OrderId id, Side side, Price price, Quantity qty) {
    return {id, side, OrderType::Limit, price, qty};
}
}  // namespace

TEST(book_add_and_best_prices) {
    OrderBook book;
    CHECK(book.bestBid() == std::nullopt);
    CHECK(book.bestAsk() == std::nullopt);

    book.addOrder(limit(1, Side::Buy, 9990, 30));
    book.addOrder(limit(2, Side::Buy, 10000, 20));
    book.addOrder(limit(3, Side::Sell, 10010, 50));

    CHECK(book.bestBid() == 10000);   // highest bid wins
    CHECK(book.bestAsk() == 10010);   // lowest ask wins
    CHECK_EQ(book.orderCount(), 3);
}

TEST(book_depth_aggregates_levels) {
    OrderBook book;
    book.addOrder(limit(1, Side::Buy, 10000, 20));
    book.addOrder(limit(2, Side::Buy, 10000, 30));
    book.addOrder(limit(3, Side::Buy, 9990, 10));

    auto bids = book.depth(Side::Buy, 10);
    CHECK_EQ(bids.size(), 2);
    CHECK_EQ(bids[0].price, 10000);      // best level first
    CHECK_EQ(bids[0].totalQty, 50);      // 20 + 30 aggregated
    CHECK_EQ(bids[0].orderCount, 2);
    CHECK_EQ(bids[1].price, 9990);

    auto one = book.depth(Side::Buy, 1); // truncates to N levels
    CHECK_EQ(one.size(), 1);
    CHECK(book.depth(Side::Sell, 10).empty());
}

TEST(book_cancel) {
    OrderBook book;
    book.addOrder(limit(1, Side::Buy, 10000, 20));
    CHECK(book.contains(1));

    CHECK(book.cancel(1));
    CHECK(!book.contains(1));
    CHECK(book.bestBid() == std::nullopt);  // emptied level removed
    CHECK_EQ(book.orderCount(), 0);

    CHECK(!book.cancel(1));    // already gone
    CHECK(!book.cancel(999));  // never existed
}

TEST(book_peek_front_is_fifo) {
    OrderBook book;
    book.addOrder(limit(1, Side::Sell, 10010, 30));
    book.addOrder(limit(2, Side::Sell, 10010, 20));  // same price, later

    const Order* front = book.peekFront(Side::Sell);
    CHECK(front != nullptr);
    CHECK_EQ(front->id, 1);  // first in, first out
    CHECK(book.peekFront(Side::Buy) == nullptr);
}

TEST(book_fill_front_partial_and_full) {
    OrderBook book;
    book.addOrder(limit(1, Side::Sell, 10010, 30));
    book.addOrder(limit(2, Side::Sell, 10010, 20));

    book.fillFront(Side::Sell, 10);              // partial fill of order 1
    const Order* front = book.peekFront(Side::Sell);
    CHECK_EQ(front->id, 1);                      // keeps queue position
    CHECK_EQ(front->quantity, 20);
    CHECK_EQ(book.depth(Side::Sell, 1)[0].totalQty, 40);

    book.fillFront(Side::Sell, 20);              // finishes order 1
    CHECK(!book.contains(1));
    CHECK_EQ(book.peekFront(Side::Sell)->id, 2);

    book.fillFront(Side::Sell, 20);              // finishes order 2 and level
    CHECK(book.bestAsk() == std::nullopt);
    CHECK_EQ(book.orderCount(), 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build 2>&1 | tail -5`
Expected: compile error — `order_book.h` is still the Task 1 stub, so `OrderBook` is undeclared. A compile failure is this step's "red".

- [ ] **Step 3: Write `src/order_book.h`** (replace stub)

```cpp
#pragma once
#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "types.h"

namespace lob {

struct LevelView {
    Price price;
    Quantity totalQty;
    int orderCount;
};

// Pure data structure: stores resting limit orders in price-time priority.
// It never decides whether a trade happens — that's the MatchingEngine.
class OrderBook {
public:
    void addOrder(const Order& order);   // rest at its level, back of queue
    bool cancel(OrderId id);             // false if unknown / already gone
    bool contains(OrderId id) const;

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;

    // Front order of the best level on `side`; nullptr if side is empty.
    const Order* peekFront(Side side) const;
    // Reduce that front order by qty; remove it (and an emptied level) when
    // fully filled. Caller guarantees 0 < qty <= front order's quantity.
    void fillFront(Side side, Quantity qty);

    std::vector<LevelView> depth(Side side, int levels) const;
    std::size_t orderCount() const;

private:
    struct PriceLevel {
        std::list<Order> orders;  // FIFO: front = oldest = first to fill
        Quantity totalQty = 0;    // cached for O(1) depth display
    };

    // Bids sort descending, asks ascending, so begin() is always the best
    // price on either side.
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;

    struct OrderLocation {
        Side side;
        Price price;
        std::list<Order>::iterator it;  // stays valid: std::list iterators
    };                                  // survive other insertions/erasures

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, OrderLocation> locations_;
};

}  // namespace lob
```

- [ ] **Step 4: Write `src/order_book.cpp`** (replace stub)

```cpp
#include "order_book.h"

#include <cassert>

namespace lob {

namespace {

// Bids and asks are different map types (opposite comparators); these
// templates share the logic between them.

template <typename Map>
void eraseFromLevel(Map& map, Price price, std::list<Order>::iterator it) {
    auto levelIt = map.find(price);
    assert(levelIt != map.end());
    levelIt->second.totalQty -= it->quantity;
    levelIt->second.orders.erase(it);
    if (levelIt->second.orders.empty()) {
        map.erase(levelIt);
    }
}

template <typename Map>
std::vector<LevelView> depthOf(const Map& map, int levels) {
    std::vector<LevelView> out;
    for (auto it = map.begin();
         it != map.end() && static_cast<int>(out.size()) < levels; ++it) {
        out.push_back({it->first, it->second.totalQty,
                       static_cast<int>(it->second.orders.size())});
    }
    return out;
}

}  // namespace

void OrderBook::addOrder(const Order& order) {
    assert(order.type == OrderType::Limit);  // market orders never rest
    assert(order.quantity > 0);
    assert(!contains(order.id));
    std::list<Order>::iterator it;
    if (order.side == Side::Buy) {
        PriceLevel& level = bids_[order.price];
        level.orders.push_back(order);
        level.totalQty += order.quantity;
        it = std::prev(level.orders.end());
    } else {
        PriceLevel& level = asks_[order.price];
        level.orders.push_back(order);
        level.totalQty += order.quantity;
        it = std::prev(level.orders.end());
    }
    locations_[order.id] = {order.side, order.price, it};
}

bool OrderBook::cancel(OrderId id) {
    auto found = locations_.find(id);
    if (found == locations_.end()) return false;
    const OrderLocation& loc = found->second;
    if (loc.side == Side::Buy) {
        eraseFromLevel(bids_, loc.price, loc.it);
    } else {
        eraseFromLevel(asks_, loc.price, loc.it);
    }
    locations_.erase(found);
    return true;
}

bool OrderBook::contains(OrderId id) const {
    return locations_.find(id) != locations_.end();
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

const Order* OrderBook::peekFront(Side side) const {
    if (side == Side::Buy) {
        if (bids_.empty()) return nullptr;
        return &bids_.begin()->second.orders.front();
    }
    if (asks_.empty()) return nullptr;
    return &asks_.begin()->second.orders.front();
}

void OrderBook::fillFront(Side side, Quantity qty) {
    // Symmetric branches kept explicit for readability; the map types differ
    // so a shared helper would need more template machinery than it saves.
    if (side == Side::Buy) {
        assert(!bids_.empty());
        auto levelIt = bids_.begin();
        Order& front = levelIt->second.orders.front();
        assert(qty > 0 && qty <= front.quantity);
        front.quantity -= qty;
        levelIt->second.totalQty -= qty;
        if (front.quantity == 0) {
            locations_.erase(front.id);
            levelIt->second.orders.pop_front();
            if (levelIt->second.orders.empty()) bids_.erase(levelIt);
        }
    } else {
        assert(!asks_.empty());
        auto levelIt = asks_.begin();
        Order& front = levelIt->second.orders.front();
        assert(qty > 0 && qty <= front.quantity);
        front.quantity -= qty;
        levelIt->second.totalQty -= qty;
        if (front.quantity == 0) {
            locations_.erase(front.id);
            levelIt->second.orders.pop_front();
            if (levelIt->second.orders.empty()) asks_.erase(levelIt);
        }
    }
}

std::vector<LevelView> OrderBook::depth(Side side, int levels) const {
    return side == Side::Buy ? depthOf(bids_, levels) : depthOf(asks_, levels);
}

std::size_t OrderBook::orderCount() const { return locations_.size(); }

}  // namespace lob
```

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build build && ./build/orderbook_tests`
Expected: all 6 tests pass (`6/6 tests passed`, exit 0).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: OrderBook with price-time priority and O(1) cancel index" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Pair walkthrough (STOP), then push**

Talking points: why two map comparators make `begin()` the best price on both sides; why `std::list` (iterators survive unrelated insert/erase → the cancel index stays valid; splice-free FIFO); the complexity table from the spec §5; why `fillFront` only ever touches the front of the best level. Wait for approval. Then: `git push`

---

### Task 3: MatchingEngine (the 12 spec scenarios)

**Files:**
- Modify: `src/matching_engine.h`, `src/matching_engine.cpp` (replace stubs)
- Test: `tests/test_matching.cpp`

- [ ] **Step 1: Write the failing tests** (replace `tests/test_matching.cpp` content — these are the spec §10 scenarios, numbered)

```cpp
#include "test_framework.h"

#include <random>

#include "matching_engine.h"

using namespace lob;

// Scenario 1: a resting limit order appears in the book.
TEST(s01_resting_order_appears) {
    MatchingEngine engine;
    SubmitResult r = engine.submitLimit(Side::Buy, 10000, 30);
    CHECK(r.trades.empty());
    CHECK_EQ(r.restedQty, 30);
    CHECK_EQ(r.cancelledQty, 0);
    CHECK(engine.book().bestBid() == 10000);
    CHECK(engine.book().bestAsk() == std::nullopt);
    CHECK_EQ(engine.book().depth(Side::Buy, 10)[0].totalQty, 30);
}

// Scenario 2: exact cross fully fills both orders.
TEST(s02_exact_cross_full_fill) {
    MatchingEngine engine;
    SubmitResult buy = engine.submitLimit(Side::Buy, 10000, 30);
    SubmitResult sell = engine.submitLimit(Side::Sell, 10000, 30);
    CHECK_EQ(sell.trades.size(), 1);
    CHECK_EQ(sell.trades[0].quantity, 30);
    CHECK_EQ(sell.trades[0].price, 10000);
    CHECK_EQ(sell.trades[0].makerId, buy.id);
    CHECK_EQ(sell.trades[0].takerId, sell.id);
    CHECK_EQ(sell.restedQty, 0);
    CHECK_EQ(engine.book().orderCount(), 0);
    CHECK(engine.book().bestBid() == std::nullopt);
}

// Scenario 3: incoming order partially filled; remainder rests at its limit.
TEST(s03_incoming_partial_fill_rests) {
    MatchingEngine engine;
    engine.submitLimit(Side::Sell, 10050, 10);
    SubmitResult buy = engine.submitLimit(Side::Buy, 10050, 25);
    CHECK_EQ(buy.trades.size(), 1);
    CHECK_EQ(buy.trades[0].quantity, 10);
    CHECK_EQ(buy.restedQty, 15);
    CHECK(engine.book().bestBid() == 10050);
    CHECK_EQ(engine.book().depth(Side::Buy, 1)[0].totalQty, 15);
    CHECK(engine.book().bestAsk() == std::nullopt);
}

// Scenario 4: a partially filled resting order keeps its queue position.
TEST(s04_resting_partial_keeps_position) {
    MatchingEngine engine;
    SubmitResult a = engine.submitLimit(Side::Sell, 10050, 30);
    SubmitResult b = engine.submitLimit(Side::Sell, 10050, 20);
    SubmitResult first = engine.submitLimit(Side::Buy, 10050, 10);
    CHECK_EQ(first.trades[0].makerId, a.id);      // A hit first (FIFO)
    SubmitResult second = engine.submitLimit(Side::Buy, 10050, 25);
    CHECK_EQ(second.trades.size(), 2);
    CHECK_EQ(second.trades[0].makerId, a.id);     // A still front after
    CHECK_EQ(second.trades[0].quantity, 20);      // partial fill: 30-10 left
    CHECK_EQ(second.trades[1].makerId, b.id);
    CHECK_EQ(second.trades[1].quantity, 5);
}

// Scenario 5: price improvement — taker fills at the maker's better price.
TEST(s05_price_improvement) {
    MatchingEngine engine;
    engine.submitLimit(Side::Sell, 10000, 40);
    SubmitResult buy = engine.submitLimit(Side::Buy, 10010, 40);
    CHECK_EQ(buy.trades.size(), 1);
    CHECK_EQ(buy.trades[0].price, 10000);  // maker's price, not 10010
    CHECK_EQ(buy.restedQty, 0);
}

// Scenario 6: market order sweeps levels (slippage); remainder cancelled.
TEST(s06_market_sweep_and_remainder_cancelled) {
    MatchingEngine engine;
    engine.submitLimit(Side::Sell, 10000, 30);
    engine.submitLimit(Side::Sell, 10010, 20);
    SubmitResult mkt = engine.submitMarket(Side::Buy, 100);
    CHECK_EQ(mkt.trades.size(), 2);
    CHECK_EQ(mkt.trades[0].price, 10000);  // best level first
    CHECK_EQ(mkt.trades[0].quantity, 30);
    CHECK_EQ(mkt.trades[1].price, 10010);  // slippage: worse second price
    CHECK_EQ(mkt.trades[1].quantity, 20);
    CHECK_EQ(mkt.cancelledQty, 50);        // unfilled remainder dropped
    CHECK_EQ(mkt.restedQty, 0);            // market orders never rest
    CHECK(engine.book().bestAsk() == std::nullopt);
}

// Scenario 7: FIFO time priority within a price level.
TEST(s07_fifo_time_priority) {
    MatchingEngine engine;
    SubmitResult a = engine.submitLimit(Side::Buy, 10000, 20);
    SubmitResult b = engine.submitLimit(Side::Buy, 10000, 30);
    SubmitResult s1 = engine.submitLimit(Side::Sell, 10000, 20);
    CHECK_EQ(s1.trades[0].makerId, a.id);
    SubmitResult s2 = engine.submitLimit(Side::Sell, 10000, 30);
    CHECK_EQ(s2.trades[0].makerId, b.id);
}

// Scenario 8: price priority across levels.
TEST(s08_price_priority) {
    MatchingEngine engine;
    engine.submitLimit(Side::Buy, 10000, 10);
    SubmitResult better = engine.submitLimit(Side::Buy, 10010, 10);
    SubmitResult mkt = engine.submitMarket(Side::Sell, 10);
    CHECK_EQ(mkt.trades[0].makerId, better.id);  // higher bid fills first
    CHECK_EQ(mkt.trades[0].price, 10010);
}

// Scenario 9: cancellation semantics.
TEST(s09_cancel_semantics) {
    MatchingEngine engine;
    SubmitResult buy = engine.submitLimit(Side::Buy, 10000, 20);
    CHECK(engine.cancel(buy.id));
    CHECK(!engine.cancel(buy.id));   // second cancel fails
    CHECK(!engine.cancel(999999));   // unknown id fails
    // A cancelled order never matches:
    SubmitResult sell = engine.submitLimit(Side::Sell, 10000, 20);
    CHECK(sell.trades.empty());
    CHECK_EQ(sell.restedQty, 20);
}

// Scenario 10: a crossing limit order rests its leftover at its own limit.
TEST(s10_crossing_limit_rests_leftover) {
    MatchingEngine engine;
    engine.submitLimit(Side::Sell, 10000, 30);
    SubmitResult buy = engine.submitLimit(Side::Buy, 10000, 50);
    CHECK_EQ(buy.trades.size(), 1);
    CHECK_EQ(buy.trades[0].quantity, 30);
    CHECK_EQ(buy.restedQty, 20);
    CHECK(engine.book().bestBid() == 10000);  // leftover at its own limit
}

// Scenario 11: empty-book queries are well-defined.
TEST(s11_empty_book_queries) {
    MatchingEngine engine;
    CHECK(engine.book().bestBid() == std::nullopt);
    CHECK(engine.book().bestAsk() == std::nullopt);
    CHECK(engine.book().depth(Side::Buy, 10).empty());
    SubmitResult mkt = engine.submitMarket(Side::Buy, 50);  // no liquidity
    CHECK(mkt.trades.empty());
    CHECK_EQ(mkt.cancelledQty, 50);
}

// Scenario 12: seeded random burst — the book is never crossed.
TEST(s12_random_burst_never_crossed) {
    MatchingEngine engine;
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> kind(1, 100);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<Price> priceDist(10000 - 20, 10000 + 20);
    std::uniform_int_distribution<Quantity> qtyDist(1, 100);
    for (int i = 0; i < 10000; ++i) {
        Side side = sideDist(rng) == 0 ? Side::Buy : Side::Sell;
        if (kind(rng) <= 80) {
            engine.submitLimit(side, priceDist(rng), qtyDist(rng));
        } else {
            engine.submitMarket(side, qtyDist(rng));
        }
        auto bid = engine.book().bestBid();
        auto ask = engine.book().bestAsk();
        if (bid && ask) CHECK(*bid < *ask);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build 2>&1 | tail -5`
Expected: compile error (`MatchingEngine` / `SubmitResult` undeclared) — the red step.

- [ ] **Step 3: Write `src/matching_engine.h`** (replace stub)

```cpp
#pragma once
#include <optional>
#include <vector>

#include "order_book.h"
#include "types.h"

namespace lob {

// Accounts for every share of a submitted order:
// traded + restedQty + cancelledQty == requested quantity.
struct SubmitResult {
    OrderId id = 0;
    std::vector<Trade> trades;
    Quantity restedQty = 0;      // remainder now resting (limit orders only)
    Quantity cancelledQty = 0;   // remainder dropped (market orders only)
};

// The algorithm layer: decides when orders trade. Owns the book.
class MatchingEngine {
public:
    SubmitResult submitLimit(Side side, Price limit, Quantity qty);
    SubmitResult submitMarket(Side side, Quantity qty);
    bool cancel(OrderId id);
    const OrderBook& book() const { return book_; }

private:
    // Fills against the opposite side while the taker is willing to trade.
    // `limit` empty = market order (no price bound). Returns unfilled qty.
    Quantity matchAgainstBook(Side takerSide, OrderId takerId, Quantity qty,
                              std::optional<Price> limit,
                              std::vector<Trade>& trades);
    void assertNotCrossed() const;

    OrderBook book_;
    OrderId nextId_ = 1;  // single monotonic counter: user + simulator orders
};

}  // namespace lob
```

- [ ] **Step 4: Write `src/matching_engine.cpp`** (replace stub)

```cpp
#include "matching_engine.h"

#include <algorithm>
#include <cassert>

namespace lob {

namespace {

// Is a taker limited at `limit` willing to trade at maker price `makerPrice`?
bool crosses(Side takerSide, Price limit, Price makerPrice) {
    return takerSide == Side::Buy ? makerPrice <= limit : makerPrice >= limit;
}

Side opposite(Side side) {
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

}  // namespace

Quantity MatchingEngine::matchAgainstBook(Side takerSide, OrderId takerId,
                                          Quantity qty,
                                          std::optional<Price> limit,
                                          std::vector<Trade>& trades) {
    Side makerSide = opposite(takerSide);
    Quantity remaining = qty;
    while (remaining > 0) {
        const Order* maker = book_.peekFront(makerSide);
        if (maker == nullptr) break;  // no liquidity left on that side
        if (limit && !crosses(takerSide, *limit, maker->price)) break;
        Quantity fillQty = std::min(remaining, maker->quantity);
        // Trades always execute at the maker's price: price improvement
        // goes to the incoming (taker) order.
        trades.push_back({takerId, maker->id, maker->price, fillQty});
        book_.fillFront(makerSide, fillQty);
        remaining -= fillQty;
    }
    return remaining;
}

SubmitResult MatchingEngine::submitLimit(Side side, Price limit,
                                         Quantity qty) {
    assert(limit > 0 && qty > 0);  // REPL/simulator validate before calling
    SubmitResult result;
    result.id = nextId_++;
    Quantity remaining =
        matchAgainstBook(side, result.id, qty, limit, result.trades);
    if (remaining > 0) {
        book_.addOrder({result.id, side, OrderType::Limit, limit, remaining});
        result.restedQty = remaining;
    }
    assertNotCrossed();
    return result;
}

SubmitResult MatchingEngine::submitMarket(Side side, Quantity qty) {
    assert(qty > 0);
    SubmitResult result;
    result.id = nextId_++;
    Quantity remaining = matchAgainstBook(side, result.id, qty, std::nullopt,
                                          result.trades);
    result.cancelledQty = remaining;  // market remainders never rest
    assertNotCrossed();
    return result;
}

bool MatchingEngine::cancel(OrderId id) { return book_.cancel(id); }

void MatchingEngine::assertNotCrossed() const {
#ifndef NDEBUG
    // Invariant: after any submit completes, the book is never crossed —
    // matching ran to completion before anything rested.
    auto bid = book_.bestBid();
    auto ask = book_.bestAsk();
    assert(!(bid && ask) || *bid < *ask);
#endif
}

}  // namespace lob
```

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build build && ./build/orderbook_tests`
Expected: all 18 tests pass (`18/18 tests passed`, exit 0). Scenario 12 runs 10k orders in well under a second.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: matching engine with price-time priority and full share accounting" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Pair walkthrough (STOP), then push**

Talking points: the matching loop (peek → crosses? → fill → repeat); why trades execute at maker price; why market remainders cancel instead of resting; the `SubmitResult` share-accounting identity; the never-crossed invariant and why it must hold. Wait for approval. Then: `git push`

---

### Task 4: Display formatting and rendering

**Files:**
- Modify: `src/display.h`, `src/display.cpp` (replace stubs)
- Test: `tests/test_display.cpp`

- [ ] **Step 1: Write the failing tests** (replace `tests/test_display.cpp` content)

```cpp
#include "test_framework.h"

#include "display.h"

using namespace lob;

TEST(format_price) {
    CHECK(formatPrice(10010) == "100.10");
    CHECK(formatPrice(10000) == "100.00");
    CHECK(formatPrice(5) == "0.05");
    CHECK(formatPrice(9990) == "99.90");
}

TEST(parse_price_valid) {
    CHECK(parsePrice("100.10") == 10010);
    CHECK(parsePrice("100.1") == 10010);   // tenths scale to ticks
    CHECK(parsePrice("100") == 10000);
    CHECK(parsePrice("0.05") == 5);
}

TEST(parse_price_invalid) {
    CHECK(parsePrice("100.005") == std::nullopt);  // sub-penny
    CHECK(parsePrice("abc") == std::nullopt);
    CHECK(parsePrice("") == std::nullopt);
    CHECK(parsePrice("-5") == std::nullopt);
    CHECK(parsePrice("0") == std::nullopt);        // price must be positive
    CHECK(parsePrice("100.") == std::nullopt);     // dot with no digits
    CHECK(parsePrice(".50") == std::nullopt);      // no whole part
    CHECK(parsePrice("10 0") == std::nullopt);     // embedded junk
}

TEST(format_mid_handles_half_ticks) {
    CHECK(formatMid(10000, 10010) == "100.05");
    CHECK(formatMid(10000, 10001) == "100.005");  // half-tick midpoint
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build 2>&1 | tail -5`
Expected: compile error (`formatPrice` undeclared).

- [ ] **Step 3: Write `src/display.h`** (replace stub)

```cpp
#pragma once
#include <optional>
#include <string>

#include "order_book.h"
#include "types.h"

namespace lob {

std::string formatPrice(Price ticks);              // 10010 -> "100.10"
std::string formatMid(Price bid, Price ask);       // may end ".xx5"
// "100.10" -> 10010. Rejects sub-penny, non-numeric, and <= 0.
std::optional<Price> parsePrice(const std::string& text);

void printBook(const OrderBook& book, int levels);
void printTrade(const Trade& trade, bool userInvolved);

}  // namespace lob
```

- [ ] **Step 4: Write `src/display.cpp`** (replace stub)

```cpp
#include "display.h"

#include <cassert>
#include <cctype>
#include <cstdio>

namespace lob {

std::string formatPrice(Price ticks) {
    assert(ticks >= 0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld.%02lld",
                  (long long)(ticks / TICKS_PER_DOLLAR),
                  (long long)(ticks % TICKS_PER_DOLLAR));
    return buf;
}

std::string formatMid(Price bid, Price ask) {
    Price sum = bid + ask;
    std::string s = formatPrice(sum / 2);
    if (sum % 2 != 0) s += "5";  // half-tick midpoints end in .xx5
    return s;
}

std::optional<Price> parsePrice(const std::string& text) {
    auto allDigits = [](const std::string& s) {
        if (s.empty()) return false;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        return true;
    };

    std::string whole = text;
    std::string frac;
    auto dot = text.find('.');
    if (dot != std::string::npos) {
        whole = text.substr(0, dot);
        frac = text.substr(dot + 1);
        if (frac.empty() || frac.size() > 2) return std::nullopt;  // sub-penny
        if (!allDigits(frac)) return std::nullopt;
    }
    if (!allDigits(whole) || whole.size() > 12) return std::nullopt;

    Price ticks = std::stoll(whole) * TICKS_PER_DOLLAR;
    if (frac.size() == 1) ticks += (frac[0] - '0') * 10;
    if (frac.size() == 2) ticks += (frac[0] - '0') * 10 + (frac[1] - '0');
    if (ticks <= 0) return std::nullopt;
    return ticks;
}

void printBook(const OrderBook& book, int levels) {
    auto asks = book.depth(Side::Sell, levels);
    auto bids = book.depth(Side::Buy, levels);

    std::printf("================ ORDER BOOK ================\n");
    std::printf("        ASKS\n");
    // Asks print worst-first so the best ask sits nearest the spread.
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::printf("%9s | %6lld  (%d)\n", formatPrice(it->price).c_str(),
                    (long long)it->totalQty, it->orderCount);
    }
    std::printf("--------------------------------------------\n");
    auto bid = book.bestBid();
    auto ask = book.bestAsk();
    if (ask) std::printf(" Best Ask: %s", formatPrice(*ask).c_str());
    if (bid && ask) {
        std::printf("   Mid: %s   Spread: %s", formatMid(*bid, *ask).c_str(),
                    formatPrice(*ask - *bid).c_str());
    }
    if (ask) std::printf("\n");
    if (bid) std::printf(" Best Bid: %s\n", formatPrice(*bid).c_str());
    if (!bid && !ask) std::printf(" (book is empty)\n");
    std::printf("--------------------------------------------\n");
    for (const auto& level : bids) {
        std::printf("%9s | %6lld  (%d)\n", formatPrice(level.price).c_str(),
                    (long long)level.totalQty, level.orderCount);
    }
    std::printf("        BIDS\n");
    std::printf("============================================\n");
}

void printTrade(const Trade& trade, bool userInvolved) {
    std::printf("TRADE  %6lld @ %s  (taker #%llu, maker #%llu)%s\n",
                (long long)trade.quantity, formatPrice(trade.price).c_str(),
                (unsigned long long)trade.takerId,
                (unsigned long long)trade.makerId,
                userInvolved ? "  (you)" : "");
}

}  // namespace lob
```

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build build && ./build/orderbook_tests`
Expected: all 22 tests pass (`22/22 tests passed`).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: price formatting/parsing and book rendering" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Pair walkthrough (STOP), then push**

Talking points: tick↔dollar conversion is pure integer math (`/100`, `%100`); the half-tick midpoint trick in `formatMid`; where parse validation lives (boundary layer, so the engine can assert instead of validate). Wait for approval. Then: `git push`

---

### Task 5: MarketSimulator

**Files:**
- Modify: `src/simulator.h`, `src/simulator.cpp` (replace stubs)
- Test: `tests/test_simulator.cpp`

- [ ] **Step 1: Write the failing tests** (replace `tests/test_simulator.cpp` content)

```cpp
#include "test_framework.h"

#include "simulator.h"

using namespace lob;

TEST(sim_seeds_initial_liquidity) {
    MatchingEngine engine;
    MarketSimulator sim(engine, 42);
    sim.seedInitialLiquidity();
    CHECK_EQ(engine.book().orderCount(), 20);
    CHECK(engine.book().bestBid().has_value());
    CHECK(engine.book().bestAsk().has_value());
    CHECK(*engine.book().bestBid() < *engine.book().bestAsk());
}

TEST(sim_draw_order_requests_are_valid) {
    MatchingEngine engine;
    MarketSimulator sim(engine, 42);
    for (int i = 0; i < 1000; ++i) {
        OrderRequest req = sim.drawOrderRequest();
        CHECK(req.quantity > 0);
        if (req.type == OrderType::Limit) {
            CHECK(req.price > 0);
        }
    }
}

TEST(sim_is_deterministic_for_same_seed) {
    MatchingEngine e1, e2;
    MarketSimulator s1(e1, 123), s2(e2, 123);
    s1.seedInitialLiquidity();
    s2.seedInitialLiquidity();
    std::size_t trades1 = 0, trades2 = 0;
    for (int i = 0; i < 200; ++i) trades1 += s1.step().size();
    for (int i = 0; i < 200; ++i) trades2 += s2.step().size();
    CHECK_EQ(trades1, trades2);
    CHECK_EQ(e1.book().orderCount(), e2.book().orderCount());
    CHECK(e1.book().bestBid() == e2.book().bestBid());
    CHECK(e1.book().bestAsk() == e2.book().bestAsk());
}

TEST(sim_steps_keep_book_sane) {
    MatchingEngine engine;
    MarketSimulator sim(engine, 42);
    sim.seedInitialLiquidity();
    for (int i = 0; i < 500; ++i) {
        sim.step();
        auto bid = engine.book().bestBid();
        auto ask = engine.book().bestAsk();
        if (bid && ask) CHECK(*bid < *ask);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build 2>&1 | tail -5`
Expected: compile error (`MarketSimulator` / `OrderRequest` undeclared).

- [ ] **Step 3: Write `src/simulator.h`** (replace stub)

```cpp
#pragma once
#include <random>
#include <vector>

#include "matching_engine.h"
#include "types.h"

namespace lob {

// An order the simulator wants to submit (no id yet — the engine assigns).
struct OrderRequest {
    OrderType type;
    Side side;
    Price price;  // 0 for market orders
    Quantity quantity;
};

// Naive random order flow around a reference price. Not a market model —
// its only job is to make the book move plausibly (documented assumption).
class MarketSimulator {
public:
    static constexpr unsigned kDefaultSeed = 42;
    static constexpr Price kInitialReference = 10000;  // $100.00

    MarketSimulator(MatchingEngine& engine, unsigned seed = kDefaultSeed);

    // 10 bids and 10 asks spread over 1-10 ticks around the reference.
    void seedInitialLiquidity();

    // One event: ~85% limit order, ~10% market order, ~5% cancel one of the
    // simulator's own resting orders. Returns any trades that resulted.
    std::vector<Trade> step();

    // Random limit (90%) or market (10%) order around the current reference,
    // without applying it. Used by the benchmark to pre-generate load.
    OrderRequest drawOrderRequest();

    Price referencePrice() const { return referencePrice_; }

private:
    std::vector<Trade> submitRequest(const OrderRequest& req);
    void cancelRandomOwnOrder();

    MatchingEngine& engine_;
    std::mt19937 rng_;
    Price referencePrice_ = kInitialReference;
    std::vector<OrderId> restingIds_;  // sim-owned resting orders (lazily
                                       // pruned: filled ids drop when picked)
};

}  // namespace lob
```

- [ ] **Step 4: Write `src/simulator.cpp`** (replace stub)

```cpp
#include "simulator.h"

#include <algorithm>

namespace lob {

MarketSimulator::MarketSimulator(MatchingEngine& engine, unsigned seed)
    : engine_(engine), rng_(seed) {}

void MarketSimulator::seedInitialLiquidity() {
    std::uniform_int_distribution<Price> offset(1, 10);
    std::uniform_int_distribution<Quantity> qty(10, 100);
    for (int i = 0; i < 10; ++i) {
        SubmitResult buy = engine_.submitLimit(
            Side::Buy, referencePrice_ - offset(rng_), qty(rng_));
        if (buy.restedQty > 0) restingIds_.push_back(buy.id);
        SubmitResult sell = engine_.submitLimit(
            Side::Sell, referencePrice_ + offset(rng_), qty(rng_));
        if (sell.restedQty > 0) restingIds_.push_back(sell.id);
    }
}

OrderRequest MarketSimulator::drawOrderRequest() {
    std::uniform_int_distribution<int> roll(1, 100);
    std::uniform_int_distribution<int> coin(0, 1);
    Side side = coin(rng_) == 0 ? Side::Buy : Side::Sell;
    if (roll(rng_) <= 90) {
        std::uniform_int_distribution<Price> offset(1, 10);
        std::uniform_int_distribution<Quantity> qty(10, 100);
        Price price = side == Side::Buy ? referencePrice_ - offset(rng_)
                                        : referencePrice_ + offset(rng_);
        price = std::max<Price>(price, 1);  // never a non-positive price
        return {OrderType::Limit, side, price, qty(rng_)};
    }
    std::uniform_int_distribution<Quantity> qty(10, 50);
    return {OrderType::Market, side, 0, qty(rng_)};
}

std::vector<Trade> MarketSimulator::step() {
    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(rng_) > 95 && !restingIds_.empty()) {
        cancelRandomOwnOrder();
        return {};
    }
    return submitRequest(drawOrderRequest());
}

std::vector<Trade> MarketSimulator::submitRequest(const OrderRequest& req) {
    SubmitResult result =
        req.type == OrderType::Limit
            ? engine_.submitLimit(req.side, req.price, req.quantity)
            : engine_.submitMarket(req.side, req.quantity);
    if (result.restedQty > 0) restingIds_.push_back(result.id);
    if (!result.trades.empty()) {
        referencePrice_ = result.trades.back().price;  // drift with trades
    }
    return result.trades;
}

void MarketSimulator::cancelRandomOwnOrder() {
    std::uniform_int_distribution<std::size_t> pick(0, restingIds_.size() - 1);
    std::size_t i = pick(rng_);
    OrderId id = restingIds_[i];
    restingIds_[i] = restingIds_.back();  // swap-pop removal
    restingIds_.pop_back();
    // false just means the order already filled — the id was stale.
    engine_.cancel(id);
}

}  // namespace lob
```

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build build && ./build/orderbook_tests`
Expected: all 26 tests pass (`26/26 tests passed`).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: seeded market simulator with reference-price drift" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Pair walkthrough (STOP), then push**

Talking points: why the reference price follows the last trade (it's what lets one-sided limit prices eventually cross); determinism from seeding; lazy stale-id pruning in `cancelRandomOwnOrder`; why the sim never touches user orders (it only cancels ids it recorded). Wait for approval. Then: `git push`

---

### Task 6: REPL

**Files:**
- Modify: `src/repl.h`, `src/repl.cpp` (replace stubs)
- Test: `tests/test_repl.cpp`

- [ ] **Step 1: Write the failing tests** (replace `tests/test_repl.cpp` content)

```cpp
#include "test_framework.h"

#include "repl.h"

using namespace lob;

TEST(parse_limit_order) {
    Command c = parseCommand("buy 50 @ 100.10");
    CHECK(c.kind == Command::Kind::LimitOrder);
    CHECK(c.side == Side::Buy);
    CHECK_EQ(c.quantity, 50);
    CHECK_EQ(c.price, 10010);

    Command s = parseCommand("sell 25 @ 100.50");
    CHECK(s.kind == Command::Kind::LimitOrder);
    CHECK(s.side == Side::Sell);
    CHECK_EQ(s.price, 10050);
}

TEST(parse_market_order) {
    Command c = parseCommand("buy 50");
    CHECK(c.kind == Command::Kind::MarketOrder);
    CHECK(c.side == Side::Buy);
    CHECK_EQ(c.quantity, 50);
}

TEST(parse_cancel_step_book_trades) {
    Command c = parseCommand("cancel 12");
    CHECK(c.kind == Command::Kind::Cancel);
    CHECK_EQ(c.orderId, 12);

    CHECK_EQ(parseCommand("step").count, 1);      // default
    CHECK_EQ(parseCommand("step 20").count, 20);
    CHECK(parseCommand("book").kind == Command::Kind::Book);
    CHECK_EQ(parseCommand("book 5").count, 5);
    CHECK(parseCommand("trades").kind == Command::Kind::Trades);
    CHECK(parseCommand("help").kind == Command::Kind::Help);
    CHECK(parseCommand("quit").kind == Command::Kind::Quit);
}

TEST(parse_rejects_invalid_input) {
    CHECK(parseCommand("").kind == Command::Kind::Invalid);
    CHECK(parseCommand("hold 50").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy fifty").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy 0 @ 100").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy -5 @ 100").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy 50 @ 100.005").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy 50 @ abc").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy 50 100.10").kind == Command::Kind::Invalid);
    CHECK(parseCommand("cancel").kind == Command::Kind::Invalid);
    CHECK(parseCommand("step -3").kind == Command::Kind::Invalid);
    CHECK(!parseCommand("buy 0 @ 100").error.empty());  // errors carry text
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build 2>&1 | tail -5`
Expected: compile error (`Command` / `parseCommand` undeclared).

- [ ] **Step 3: Write `src/repl.h`** (replace stub)

```cpp
#pragma once
#include <string>

#include "matching_engine.h"
#include "simulator.h"
#include "types.h"

namespace lob {

// Parsed user input. All validation happens here — invalid input never
// reaches the engine (which asserts instead of validating).
struct Command {
    enum class Kind {
        LimitOrder, MarketOrder, Cancel, Step, Book, Trades, Help, Quit,
        Invalid
    };
    Kind kind = Kind::Invalid;
    Side side = Side::Buy;
    Price price = 0;
    Quantity quantity = 0;
    OrderId orderId = 0;
    int count = 1;       // step/book/trades argument
    std::string error;   // set when kind == Invalid
};

Command parseCommand(const std::string& line);

// Interactive loop: reads commands until quit/EOF.
void runRepl(MatchingEngine& engine, MarketSimulator& sim);

}  // namespace lob
```

- [ ] **Step 4: Write `src/repl.cpp`** (replace stub)

```cpp
#include "repl.h"

#include <cctype>
#include <cstdio>
#include <deque>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "display.h"

namespace lob {

namespace {

constexpr int kDefaultBookLevels = 10;
constexpr int kDefaultTradeCount = 10;
constexpr std::size_t kTradeHistoryLimit = 50;

std::optional<long long> parsePositiveInt(const std::string& text) {
    if (text.empty() || text.size() > 12) return std::nullopt;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    long long value = std::stoll(text);
    if (value <= 0) return std::nullopt;
    return value;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens;
}

Command invalid(const std::string& message) {
    Command c;
    c.kind = Command::Kind::Invalid;
    c.error = message;
    return c;
}

Command parseCount(Command::Kind kind, const std::vector<std::string>& tokens,
                   int defaultCount) {
    Command c;
    c.kind = kind;
    c.count = defaultCount;
    if (tokens.size() > 2) return invalid("too many arguments");
    if (tokens.size() == 2) {
        auto n = parsePositiveInt(tokens[1]);
        if (!n) return invalid("expected a positive count");
        c.count = static_cast<int>(*n);
    }
    return c;
}

}  // namespace

Command parseCommand(const std::string& line) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return invalid("empty command (try 'help')");
    const std::string& verb = tokens[0];

    if (verb == "buy" || verb == "sell") {
        Command c;
        c.side = verb == "buy" ? Side::Buy : Side::Sell;
        if (tokens.size() != 2 && tokens.size() != 4) {
            return invalid("usage: buy 50 @ 100.10  (limit)  or  buy 50  (market)");
        }
        auto qty = parsePositiveInt(tokens[1]);
        if (!qty) return invalid("quantity must be a positive integer");
        c.quantity = *qty;
        if (tokens.size() == 2) {
            c.kind = Command::Kind::MarketOrder;
            return c;
        }
        if (tokens[2] != "@") return invalid("expected '@' before the price");
        auto price = parsePrice(tokens[3]);
        if (!price) {
            return invalid("bad price (positive, at most 2 decimals — no sub-penny)");
        }
        c.kind = Command::Kind::LimitOrder;
        c.price = *price;
        return c;
    }
    if (verb == "cancel") {
        if (tokens.size() != 2) return invalid("usage: cancel <order-id>");
        auto id = parsePositiveInt(tokens[1]);
        if (!id) return invalid("order id must be a positive integer");
        Command c;
        c.kind = Command::Kind::Cancel;
        c.orderId = static_cast<OrderId>(*id);
        return c;
    }
    if (verb == "step") return parseCount(Command::Kind::Step, tokens, 1);
    if (verb == "book") {
        return parseCount(Command::Kind::Book, tokens, kDefaultBookLevels);
    }
    if (verb == "trades") {
        return parseCount(Command::Kind::Trades, tokens, kDefaultTradeCount);
    }
    if (verb == "help" && tokens.size() == 1) {
        Command c;
        c.kind = Command::Kind::Help;
        return c;
    }
    if (verb == "quit" && tokens.size() == 1) {
        Command c;
        c.kind = Command::Kind::Quit;
        return c;
    }
    return invalid("unknown command '" + verb + "' (try 'help')");
}

namespace {

void printHelp() {
    std::printf(
        "  buy 50 @ 100.10   limit buy      step [N]    run N sim events\n"
        "  sell 25 @ 100.50  limit sell     book [N]    top N levels/side\n"
        "  buy 50            market buy     trades [N]  last N trades\n"
        "  sell 25           market sell    help        this text\n"
        "  cancel 12         cancel by id   quit        exit\n");
}

class ReplSession {
public:
    ReplSession(MatchingEngine& engine, MarketSimulator& sim)
        : engine_(engine), sim_(sim) {}

    // Returns false when the loop should exit.
    bool handle(const Command& c) {
        switch (c.kind) {
            case Command::Kind::LimitOrder:
                report(engine_.submitLimit(c.side, c.price, c.quantity));
                return true;
            case Command::Kind::MarketOrder:
                report(engine_.submitMarket(c.side, c.quantity));
                return true;
            case Command::Kind::Cancel:
                if (engine_.cancel(c.orderId)) {
                    std::printf("cancelled order #%llu\n",
                                (unsigned long long)c.orderId);
                } else {
                    std::printf("order #%llu not found (filled or never existed)\n",
                                (unsigned long long)c.orderId);
                }
                return true;
            case Command::Kind::Step: {
                int tradeCount = 0;
                for (int i = 0; i < c.count; ++i) {
                    for (const Trade& t : sim_.step()) {
                        remember(t);
                        printTrade(t, involvesUser(t));
                        ++tradeCount;
                    }
                }
                std::printf("%d sim event(s), %d trade(s)\n", c.count,
                            tradeCount);
                printBook(engine_.book(), kDefaultBookLevels);
                return true;
            }
            case Command::Kind::Book:
                printBook(engine_.book(), c.count);
                return true;
            case Command::Kind::Trades: {
                int shown = 0;
                for (auto it = history_.rbegin();
                     it != history_.rend() && shown < c.count; ++it, ++shown) {
                    printTrade(*it, involvesUser(*it));
                }
                if (shown == 0) std::printf("no trades yet\n");
                return true;
            }
            case Command::Kind::Help:
                printHelp();
                return true;
            case Command::Kind::Quit:
                return false;
            case Command::Kind::Invalid:
                std::printf("error: %s\n", c.error.c_str());
                return true;
        }
        return true;
    }

private:
    void report(const SubmitResult& result) {
        userIds_.insert(result.id);
        for (const Trade& t : result.trades) {
            remember(t);
            printTrade(t, true);
        }
        if (result.restedQty > 0) {
            std::printf("order #%llu resting: %lld remaining\n",
                        (unsigned long long)result.id,
                        (long long)result.restedQty);
        }
        if (result.cancelledQty > 0) {
            std::printf("unfilled remainder cancelled: %lld (no liquidity)\n",
                        (long long)result.cancelledQty);
        }
        if (result.trades.empty() && result.restedQty == 0 &&
            result.cancelledQty == 0) {
            std::printf("nothing happened (empty book?)\n");
        }
        printBook(engine_.book(), kDefaultBookLevels);
    }

    void remember(const Trade& t) {
        history_.push_back(t);
        if (history_.size() > kTradeHistoryLimit) history_.pop_front();
    }

    bool involvesUser(const Trade& t) const {
        return userIds_.count(t.takerId) > 0 || userIds_.count(t.makerId) > 0;
    }

    MatchingEngine& engine_;
    MarketSimulator& sim_;
    std::deque<Trade> history_;          // last 50 trades
    std::unordered_set<OrderId> userIds_;
};

}  // namespace

void runRepl(MatchingEngine& engine, MarketSimulator& sim) {
    std::printf("Limit Order Book Simulator — type 'help' for commands.\n");
    printBook(engine.book(), kDefaultBookLevels);
    ReplSession session(engine, sim);
    std::string line;
    while (std::printf("> "), std::fflush(stdout),
           std::getline(std::cin, line)) {
        if (!session.handle(parseCommand(line))) break;
    }
    std::printf("bye\n");
}

}  // namespace lob
```

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build build && ./build/orderbook_tests`
Expected: all 30 tests pass (`30/30 tests passed`).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: step-based REPL with validated command parsing" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Pair walkthrough (STOP), then push**

Talking points: parse/execute separation (pure `parseCommand` is unit-testable; the loop is thin glue); all validation at the boundary; how `(you)` tagging works; the trade ring buffer. Note the REPL isn't wired into `main` until Task 8. Wait for approval. Then: `git push`

---

### Task 7: Benchmark

**Files:**
- Modify: `src/benchmark.h`, `src/benchmark.cpp` (replace stubs)

Benchmark has no unit test (it's a timing harness); its correctness check is determinism, verified manually in Step 3.

- [ ] **Step 1: Write `src/benchmark.h`** (replace stub)

```cpp
#pragma once
#include <cstddef>

namespace lob {

// Pre-generates numOrders with the simulator's generator, then times the
// engine.submit* loop alone (RNG cost excluded). Reports throughput plus a
// determinism checksum (trade count + final resting orders).
void runBenchmark(std::size_t numOrders, unsigned seed);

}  // namespace lob
```

- [ ] **Step 2: Write `src/benchmark.cpp`** (replace stub)

```cpp
#include "benchmark.h"

#include <chrono>
#include <cstdio>
#include <vector>

#include "matching_engine.h"
#include "simulator.h"

namespace lob {

void runBenchmark(std::size_t numOrders, unsigned seed) {
    MatchingEngine engine;
    MarketSimulator sim(engine, seed);

    // Phase 1: generate the full load up front so RNG cost isn't timed.
    // The reference price stays static here (nothing is applied), which is
    // fine for a throughput measurement.
    std::vector<OrderRequest> requests;
    requests.reserve(numOrders);
    for (std::size_t i = 0; i < numOrders; ++i) {
        requests.push_back(sim.drawOrderRequest());
    }

    // Phase 2: time only the engine.
    std::size_t tradeCount = 0;
    auto start = std::chrono::steady_clock::now();
    for (const OrderRequest& req : requests) {
        SubmitResult result =
            req.type == OrderType::Limit
                ? engine.submitLimit(req.side, req.price, req.quantity)
                : engine.submitMarket(req.side, req.quantity);
        tradeCount += result.trades.size();
    }
    auto end = std::chrono::steady_clock::now();

    double seconds = std::chrono::duration<double>(end - start).count();
    std::printf("Orders processed:  %zu\n", numOrders);
    std::printf("Execution time:    %.3f seconds\n", seconds);
    std::printf("Throughput:        %.2f million orders/sec\n",
                numOrders / seconds / 1e6);
    std::printf("Trades executed:   %zu\n", tradeCount);
    std::printf("Resting orders:    %zu\n", engine.book().orderCount());
    std::printf("(seed %u — identical seeds give identical numbers)\n", seed);
}

}  // namespace lob
```

- [ ] **Step 3: Build Release and verify determinism manually**

`main.cpp` isn't wired yet, so verify via a throwaway driver: temporarily replace `src/main.cpp` content with:

```cpp
#include "benchmark.h"

int main() { lob::runBenchmark(1000000, 42); return 0; }
```

Run: `cmake -B build-release -DCMAKE_BUILD_TYPE=Release && cmake --build build-release && ./build-release/orderbook && ./build-release/orderbook`
Expected: two runs print identical `Trades executed` and `Resting orders` lines; throughput comfortably above 1 million orders/sec. Also run `cmake --build build && ./build/orderbook_tests` — still all 30 passing.

- [ ] **Step 4: Restore the `main.cpp` stub**

Restore `src/main.cpp` to the Task 1 stub content (real wiring happens in Task 8):

```cpp
#include <cstdio>

int main() {
    std::printf("orderbook: components not wired yet (see plan tasks)\n");
    return 0;
}
```

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: throughput benchmark with pre-generated load and determinism checksum" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Pair walkthrough (STOP), then push**

Talking points: why generation is excluded from timing; `steady_clock` vs `system_clock`; why Release build for numbers, Debug for asserts; what the checksum lines prove; record the actual measured throughput for the README. Wait for approval. Then: `git push`

---

### Task 8: main wiring, README, end-to-end check

**Files:**
- Modify: `src/main.cpp` (replace stub), `README.md` (replace placeholder if present, else create)

- [ ] **Step 1: Write `src/main.cpp`**

```cpp
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include "benchmark.h"
#include "matching_engine.h"
#include "repl.h"
#include "simulator.h"

namespace {

void printUsage() {
    std::printf(
        "usage: orderbook [--benchmark [N]] [--seed S]\n"
        "  (no args)        interactive REPL with a simulated market\n"
        "  --benchmark [N]  process N generated orders (default 1000000)\n"
        "  --seed S         RNG seed (default 42)\n");
}

// Returns nullopt on parse failure.
std::optional<long long> parseNumber(const char* text) {
    std::string s(text);
    if (s.empty()) return std::nullopt;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    return std::stoll(s);
}

}  // namespace

int main(int argc, char** argv) {
    bool benchmark = false;
    std::size_t benchmarkOrders = 1000000;
    unsigned seed = lob::MarketSimulator::kDefaultSeed;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--benchmark") == 0) {
            benchmark = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                auto n = parseNumber(argv[++i]);
                if (!n || *n <= 0) { printUsage(); return 1; }
                benchmarkOrders = static_cast<std::size_t>(*n);
            }
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            auto s = parseNumber(argv[++i]);
            if (!s) { printUsage(); return 1; }
            seed = static_cast<unsigned>(*s);
        } else {
            printUsage();
            return std::strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }

    if (benchmark) {
        lob::runBenchmark(benchmarkOrders, seed);
        return 0;
    }

    lob::MatchingEngine engine;
    lob::MarketSimulator sim(engine, seed);
    sim.seedInitialLiquidity();
    lob::runRepl(engine, sim);
    return 0;
}
```

- [ ] **Step 2: Build and run the end-to-end demo script**

Run: `cmake --build build && ./build/orderbook_tests` — expect 30/30.
Then run `./build/orderbook` and execute this manual script, confirming each behavior:

1. Startup shows a populated book (20 resting orders around $100).
2. `buy 30 @ <best ask price>` → immediate trade tagged `(you)` at the ask price (price-time priority + maker pricing).
3. `sell 10 @ <price above best bid>` → rests; `book` shows it; `cancel <its id>` → confirmed; `cancel <same id>` → "not found".
4. `buy 500` (market) → sweeps multiple ask levels — visible slippage; if it exhausts the asks, the cancelled-remainder message appears.
5. `step 50` → sim trades print, book moves, spread/mid update.
6. `trades` → recent history with `(you)` tags. `buy 50 @ 100.005` → sub-penny rejected. `help`, then `quit`.

- [ ] **Step 3: Run the Release benchmark for README numbers**

Run: `cmake --build build-release && ./build-release/orderbook --benchmark`
Record the throughput/trades/resting numbers for the README.

- [ ] **Step 4: Write `README.md`**

```markdown
# Limit Order Book Simulator

A single-threaded C++20 limit order book with price-time priority matching,
market/limit orders, cancellation, a seeded market simulator, an interactive
REPL, and a throughput benchmark. Built as a learning project in market
microstructure and C++ data structures — standard library only.

## Build & run

    brew install cmake                # once
    cmake -B build && cmake --build build
    ./build/orderbook_tests           # 30 tests
    ./build/orderbook                 # interactive REPL

    # benchmark (use an optimized build for numbers)
    cmake -B build-release -DCMAKE_BUILD_TYPE=Release
    cmake --build build-release
    ./build-release/orderbook --benchmark

## REPL commands

    buy 50 @ 100.10   limit buy      step [N]    run N sim events
    sell 25 @ 100.50  limit sell     book [N]    top N levels/side
    buy 50            market buy     trades [N]  last N trades
    sell 25           market sell    help / quit
    cancel 12         cancel by id

## Design

Prices are int64 ticks (1 tick = $0.01) — floating point is never used for
money, because 100.10 has no exact binary representation and would break map
keys and equality.

The `OrderBook` is a pure data structure; the `MatchingEngine` is the
algorithm that runs against it:

- Bids: `std::map<Price, PriceLevel, std::greater<>>` — best bid is `begin()`
- Asks: `std::map<Price, PriceLevel, std::less<>>` — best ask is `begin()`
- Each `PriceLevel`: `std::list<Order>` in FIFO arrival order + cached total
- Cancel index: `unordered_map<OrderId, {side, price, list iterator}>` —
  `std::list` iterators stay valid under other insertions/erasures

| Operation | Complexity (L = price levels/side) |
|---|---|
| Add resting order | O(log L) |
| Best bid / best ask | O(1) |
| One fill during matching | O(1) amortized (+O(log L) when a level empties) |
| Cancel | O(1) average (+O(log L) when a level empties) |
| Depth snapshot, top N | O(N) |

Matching rules: trades execute at the resting (maker) order's price, so price
improvement goes to the incoming order; partially filled resting orders keep
their queue position; an unfilled market-order remainder is cancelled, never
rested. Invariant (asserted in debug builds): after any submit completes the
book is never crossed.

## Benchmark

    Orders processed:  1000000
    Execution time:    <measured> seconds
    Throughput:        <measured> million orders/sec
    Trades executed:   <measured>
    Resting orders:    <measured>

(Apple M-series, Release build, seed 42 — identical seeds give identical
trade/resting counts, demonstrating determinism.)

## Simplifying assumptions

Single security; strictly sequential order arrival; no latency, fees, or
persistence; no self-trade prevention (one manual user + an anonymous
simulator); no order modification; no hidden liquidity; the simulator is
naive random flow around a drifting reference price, not a market model;
only limit and market orders.

## Known extensions (deliberately not built)

Cache-friendly flat-vector book; pre-allocated price-indexed array book
(the HFT direction); IOC/FOK/stop orders; order modification; multiple
instruments; concurrency.
```

Replace the four `<measured>` fields with the actual numbers from Step 3 — the plan's only permitted fill-in, since they are machine-measured.

- [ ] **Step 5: Full verification**

Run: `./build/orderbook_tests && ctest --test-dir build && ./build-release/orderbook --benchmark`
Expected: 30/30 tests, ctest 100%, benchmark output matching the README.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: CLI wiring and README with design notes and benchmark results" -m "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Final pair walkthrough (STOP), then push**

Walk the whole flow end to end against the spec's Definition of Done (§14): every REPL behavior, the demo script results, the benchmark numbers, and where each of the 12 scenarios lives in the tests. Confirm Renzo can answer: why ticks, why map+list+hash, complexity of each op, why maker pricing, why market remainders cancel, what the never-crossed invariant proves, and how this differs from a real exchange (assumptions list). Then: `git push`

---

## Verification against spec (run at the end)

- Spec §4 types → Task 1. §5 book + complexities → Task 2. §6 matching + SubmitResult → Task 3. §7 simulator → Task 5. §8 REPL/display/validation → Tasks 4, 6. §9 benchmark → Task 7. §10 tests → Tasks 1–6 (12 scenarios in Task 3). §11 error handling → Tasks 3 (asserts), 6 (boundary validation). §12 build → Tasks 0–1. §13 assumptions + §15 extensions → Task 8 README. §14 definition of done → Task 8 Step 7 walkthrough.

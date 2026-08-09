# Limit Order Book Simulator — Design Spec

**Date:** 2026-08-09
**Status:** Approved design (brainstorming complete; implementation plan next)

## 1. Purpose

A single-threaded C++ limit order book simulator that models the core mechanics of an electronic exchange: price-time priority matching, market and limit orders, partial fills, cancellation, a simple market-activity simulator, a terminal REPL, and a throughput benchmark.

The project is a quant-developer recruiting portfolio piece. Its success criterion is that the owner can explain and defend every component — data structures, matching logic, complexity bounds, and simplifying assumptions — in an interview. Explainability trumps sophistication. The repo contains zero third-party code; the only tool dependency is CMake.

**Non-goals** (unchanged from the project outline): networking, FIX or any exchange protocol, live market data, multithreading, lock-free structures, advanced order types (stop, iceberg, IOC, FOK, pegged), multiple securities or venues, persistence, GUI, trading strategies, and deep performance optimization (custom allocators, SIMD, cache tuning).

## 2. Decisions made during design

| Decision | Choice | Rationale |
|---|---|---|
| Price representation | `int64_t` ticks; 1 tick = $0.01 | `double` prices break map keys and equality (100.10 is not exactly representable). Integer math is exact; dollars exist only at the parse/display boundary. |
| Book structure | Sorted `std::map` per side + `std::list` per level + `unordered_map` cancel index | O(1) best price, O(1) cancel, FIFO time priority for free. Every structure has a one-line interview justification. |
| Interaction model | Step-based REPL | A live-refreshing display needs threads or non-blocking IO, contradicting the single-threaded scope. `step N` advances the simulator on demand. |
| Testing | Hand-rolled ~60-line harness + 12 scenario tests | Zero dependencies; keeps the "I wrote every line" story airtight. |
| Build system | CMake, C++20, `-Wall -Wextra` | Industry standard. Prerequisite: `brew install cmake` (not yet installed at design time). |
| Randomness | Seeded `std::mt19937`, `--seed N` flag | Reproducible demos and benchmarks; determinism is itself a demo feature. |
| Workflow | Pair-per-component | Implement one component, walk through it together, approve, continue. |

## 3. Architecture

```
limit-order-book/
├── CMakeLists.txt
├── README.md                      # usage + assumptions + complexity table
├── docs/superpowers/specs/        # this spec
├── src/
│   ├── types.h                    # Side, OrderType, Price, Order, Trade, constants
│   ├── order_book.{h,cpp}         # pure data structure — no matching logic
│   ├── matching_engine.{h,cpp}    # crossing algorithm; owns the OrderBook
│   ├── simulator.{h,cpp}          # seeded random market activity
│   ├── display.{h,cpp}            # book rendering, trade lines, tick↔dollar formatting
│   ├── repl.{h,cpp}               # command parsing + interactive loop
│   ├── benchmark.{h,cpp}          # --benchmark mode
│   └── main.cpp                   # CLI dispatch: REPL (default), --benchmark [N], --seed N
└── tests/
    ├── test_framework.h           # TEST macro, CHECK/CHECK_EQ, runner (~60 lines)
    └── test_matching.cpp          # 12 scenario tests
```

**Data flow:** the REPL and the Simulator are both just order sources — each calls `engine.submitLimit/submitMarket/cancel`. The engine mutates the book and returns results; trades go to the display and a recent-trades ring buffer (last 50). Everything is sequential on one thread.

**Boundary rule:** the OrderBook is the data structure; the MatchingEngine is the algorithm that runs against it. The book never decides whether a trade happens; the engine never touches map internals. They are testable and explainable separately.

## 4. Core types (`types.h`)

- `using Price = int64_t;` — ticks. `TICKS_PER_DOLLAR = 100`. $100.10 = `10010`.
- `using Quantity = int64_t;` `using OrderId = uint64_t;`
- `enum class Side { Buy, Sell };` `enum class OrderType { Limit, Market };`
- `struct Order { OrderId id; Side side; OrderType type; Price price; Quantity quantity; };`
  No timestamp field: FIFO position within a price level's list *is* arrival order. IDs come from a single monotonic counter in the engine (shared by user and simulator orders).
- `struct Trade { OrderId takerId; OrderId makerId; Price price; Quantity quantity; };`

## 5. OrderBook

```cpp
struct LevelView { Price price; Quantity totalQty; int orderCount; };

class OrderBook {
public:
    void addOrder(const Order& order);              // rest at its price level (back of FIFO queue)
    bool cancel(OrderId id);                        // false if unknown/already gone
    std::optional<Price> bestBid() const;           // begin() of bids map
    std::optional<Price> bestAsk() const;           // begin() of asks map
    const Order* peekFront(Side side) const;        // front order of best level; nullptr if side empty
    void fillFront(Side side, Quantity qty);        // reduce front order; remove it (and empty level) when filled
    std::vector<LevelView> depth(Side side, int levels) const;
    size_t orderCount() const;
};
```

**Internals:**

- Bids: `std::map<Price, PriceLevel, std::greater<Price>>` — best bid is `begin()`.
- Asks: `std::map<Price, PriceLevel, std::less<Price>>` — best ask is `begin()`.
- `PriceLevel { std::list<Order> orders; Quantity totalQty; }` — FIFO arrival order; cached total for O(1) depth display.
- Cancel index: `std::unordered_map<OrderId, OrderLocation>` where `OrderLocation { Side side; Price price; std::list<Order>::iterator it; }`. `std::list` iterators remain valid under insertion/erasure elsewhere — the justification for `list` over `vector`.

**Complexity (L = number of price levels on a side):**

| Operation | Cost | Why |
|---|---|---|
| Add resting order | O(log L) | map find-or-insert of the level; list push_back O(1) |
| Best bid / best ask | O(1) | `begin()` of a sorted map |
| One fill during matching | O(1) amortized | front of best level; + O(log L) map erase when a level empties |
| Cancel | O(1) average | hash lookup + `list::erase(it)`; + O(log L) if the level empties |
| Depth snapshot (top N) | O(N) | walk from `begin()` |

## 6. Matching semantics (MatchingEngine)

```cpp
struct SubmitResult { OrderId id; std::vector<Trade> trades; Quantity restedQty; Quantity cancelledQty; };

class MatchingEngine {
public:
    SubmitResult submitLimit(Side side, Price limit, Quantity qty);
    SubmitResult submitMarket(Side side, Quantity qty);
    bool cancel(OrderId id);
    const OrderBook& book() const;
};
```

**Incoming limit order** (buy with limit P; sell is symmetric):

1. While remaining qty > 0 **and** best ask exists **and** best ask ≤ P: fill against the front order of the best ask level. Price priority across levels; FIFO within a level.
2. Fill quantity = `min(remaining, restingQty)`. **Trade price = the resting (maker) order's price** — price improvement goes to the incoming (taker) order. Equal prices cross.
3. Fully filled resting orders are removed from level and cancel index; partially filled resting orders are reduced and keep their queue position; emptied levels are erased.
4. Any remainder rests in the book at price P (`restedQty`), and the taker becomes a maker.

**Incoming market order:** same loop with no price bound; walks levels best-first until filled or the opposite side is empty. Any unfilled remainder is **cancelled, never rested** (`cancelledQty`) — a market order has no price to sit at. Multi-level sweeps are how the project demonstrates slippage.

**Cancel:** full cancellation only; no modify/amend. Unknown or already-filled IDs return `false` (no exception).

**Invariant (asserted in debug builds):** after any `submit*` returns, the book is never crossed — `bestBid < bestAsk` whenever both exist — because matching runs to completion before anything rests. Additional asserted invariants: cached level totals equal the sum of their orders; the cancel index and the book agree.

## 7. MarketSimulator

- Owns a `std::mt19937` seeded with a fixed default of `42` (overridable via `--seed N`).
- Tracks a reference price: starts at $100.00 (10000 ticks); updated to the last trade price after each trade.
- `step()` emits one event:
  - **~85% limit order:** random side (50/50), price = reference ± uniform 1–10 ticks (bids below, asks above), quantity uniform 10–100.
  - **~10% market order:** random side, quantity uniform 10–50.
  - **~5% cancel** of a randomly chosen still-resting simulator order.
- Tracks the IDs of its own resting orders; it never cancels user orders.
- On startup, seeds the book with ~20 limit orders spread over ±1–10 ticks around the reference so the first `book` command shows a populated market.

The simulator is deliberately naive — its only job is to make the book move plausibly. This is a documented assumption, not a flaw.

## 8. REPL and display

**Commands:**

| Command | Meaning |
|---|---|
| `buy 50 @ 100.10` / `sell 25 @ 100.50` | limit order |
| `buy 50` / `sell 25` | market order |
| `cancel 12` | cancel by order ID |
| `step [N]` | run N simulator events (default 1) |
| `book [N]` | show top N levels per side (default 10) |
| `trades [N]` | show last N trades (default 10) |
| `help`, `quit` | self-explanatory |

- After every user order or `step`, print resulting trades and redraw the book.
- The REPL tracks user-submitted IDs; trades involving them are tagged `(you)`.
- **Input validation (all of it lives here):** quantity must be a positive integer; price must be positive and tick-aligned — at most 2 decimals, so `100.005` is rejected with a message; malformed commands print usage hints. Invalid input never reaches the engine and never crashes the program.

**Display layout** (asks worst→best on top, summary bar, bids best→worst below):

```
================ ORDER BOOK ================
        ASKS
   100.30 |     70  (2)
   100.20 |     40  (1)
   100.10 |     55  (3)
--------------------------------------------
 Best Ask: 100.10   Mid: 100.05   Spread: 0.10
 Best Bid: 100.00
--------------------------------------------
   100.00 |     60  (2)
    99.90 |     30  (1)
        BIDS
============================================
```

Tick→dollar formatting is integer math (`ticks / 100`, `ticks % 100`) — no floating point anywhere in the program.

## 9. Benchmark

`./orderbook --benchmark [N]` (default 1,000,000; `--seed` honored):

1. Pre-generate all N orders with the simulator's generator (RNG cost excluded from timing).
2. Time only the `engine.submit*` loop with `std::chrono::steady_clock`.
3. Report: orders processed, elapsed seconds, orders/sec, total trades executed, final resting-order count. The last two act as a checksum — identical seed, identical numbers — demonstrating determinism.

Results are quoted from a Release (`-O2`) build. Expected order of magnitude for this design: >1M orders/sec.

## 10. Testing

`tests/test_framework.h`: a `TEST(name)` macro that registers a function, `CHECK(cond)` / `CHECK_EQ(a, b)` assertions that record failures with file/line, and a `main` runner printing pass/fail counts and returning non-zero on failure. Roughly 60 lines, no dependencies.

`tests/test_matching.cpp` — 12 scenarios through the engine's public API:

1. A resting limit order appears in the book; best bid/ask report correctly.
2. Exact cross → full fill, both orders gone.
3. Incoming order partially filled → remainder rests at its limit.
4. Resting order partially filled → reduced in place, keeps queue position.
5. Price improvement: buy limit above best ask fills at the ask price.
6. Market order sweeps multiple levels (slippage); remainder cancelled when the side empties.
7. FIFO time priority: same-price orders fill in arrival order.
8. Price priority: better-priced levels fill first.
9. Cancel removes an order; cancelling an unknown ID returns false; a cancelled order never matches.
10. A crossing limit order rests its leftover at its own limit price.
11. Empty-book queries return `nullopt`; spread/mid only defined when both sides exist.
12. Seeded random burst (~10k orders): the never-crossed invariant and index-consistency invariants hold throughout.

Run via `ctest` or the `orderbook_tests` binary directly.

## 11. Error handling

- **Parse layer (REPL/CLI):** validates everything; friendly messages; never crashes.
- **Engine/book:** no exceptions. `cancel` returns `bool`; `submit*` returns a `SubmitResult` that accounts for every share (traded + rested + cancelled = requested). Internal invariants are `assert`ed (active in Debug builds, compiled out in Release).
- **Rationale:** invalid input is a user problem handled at the boundary; an invariant violation is a programmer bug that should stop a debug run loudly.

## 12. Build & tooling

- `CMakeLists.txt`: CMake ≥ 3.16, C++20 (`CMAKE_CXX_STANDARD 20`), `-Wall -Wextra`, targets `orderbook` and `orderbook_tests`, CTest integration.
- **Prerequisite:** `brew install cmake` (machine check on 2026-08-09: Apple clang 21 present, CMake absent).
- Standard flow: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build`.

## 13. Simplifying assumptions (to be documented in README)

Single security; strictly sequential order arrival; no latency, fees, or persistence; no self-trade prevention (one manual user plus an anonymous simulator); no order modification; no hidden liquidity; simulator activity is naive random flow, not a market model; only limit and market orders.

## 14. Definition of done

Everything in the project outline's Section 10, specifically: working REPL with all commands; limit and market orders matching under price-time priority; partial fills; cancellation; book display with best bid/ask, spread, mid, and depth; simulator-driven activity; observable multi-level slippage; benchmark reporting throughput; **plus** all 12 scenario tests passing via `ctest`, and a README covering usage, data-structure choices with complexity table, and the assumptions above.

## 15. Future extensions (explicitly not in scope)

Documented in README as known next steps, not built: cache-friendly flat-vector book (design B), price-indexed array book (design C), IOC/FOK/stop orders, order modification, multiple instruments, concurrency. Mentioning the road not taken is itself interview material.

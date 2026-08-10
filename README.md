# Limit Order Book Simulator

A single-threaded C++20 implementation of the core data structure behind
every electronic exchange: a limit order book with price-time priority
matching. Orders arrive one at a time — limit orders rest in the book at
their price, market orders sweep the best available levels — and a matching
engine crosses them the way a real venue does: trades execute at the resting
order's price, earlier orders at the same price fill first, and every
submitted share is accounted for as traded, resting, or cancelled.

The program runs in two modes: an interactive REPL where you trade against a
seeded market simulator and watch the book, spread, and trade tape react to
every order, and a benchmark mode that measures raw engine throughput.
Standard library only — no external dependencies.

## What it looks like

Running `./build/orderbook` drops you into a REPL against a simulated market
(seed 42, so this exact book is reproducible):

    ================ ORDER BOOK ================
            ASKS
       100.09 |     70  (2)
       100.08 |    109  (2)
       100.07 |    114  (2)
       100.05 |     42  (1)
       100.03 |     95  (2)
       100.02 |     13  (1)
    --------------------------------------------
     Best Ask: 100.02   Mid: 99.99   Spread: 0.06
     Best Bid: 99.96
    --------------------------------------------
        99.96 |     73  (1)
        99.95 |    163  (3)
        99.94 |     11  (1)
        99.93 |     61  (1)
        99.92 |     33  (1)
        99.91 |     48  (1)
        99.90 |    100  (2)
            BIDS
    ============================================

Each row is a price level: aggregate resting quantity and (order count).
Asks print worst-first so the best ask sits nearest the spread; your fills
are tagged `(you)` in the trade tape.

## REPL commands

    buy 50 @ 100.10   limit buy      step [N]    run N sim events
    sell 25 @ 100.50  limit sell     book [N]    top N levels/side
    buy 50            market buy     trades [N]  last N trades
    sell 25           market sell    help / quit
    cancel 12         cancel by id

## Performance

The matching engine sustains **16 million orders per second on a single
core — roughly 62 nanoseconds per order** — including matching, resting,
and all cancel-index bookkeeping:

    Orders processed:  1000000
    Execution time:    0.062 seconds
    Throughput:        16.05 million orders/sec
    Trades executed:   152723
    Resting orders:    845416

(Apple M-series, Release build, seed 42.) Order generation is pre-computed
so the timed loop measures the engine alone, and identical seeds give
identical trade/resting counts on every run — the checksum lines above
double as a determinism proof.

## Design

Prices are int64 ticks (1 tick = $0.01) — floating point is never used for
money, because 100.10 has no exact binary representation and would break map
keys and equality. All price arithmetic (mid, spread, tick offsets) is exact
integer math; dollar strings exist only at the display boundary.

The core split: the `OrderBook` is a pure data structure that stores resting
orders and answers queries, while the `MatchingEngine` owns it and makes
every trading decision. The book never decides whether a trade happens.

- Bids: `std::map<Price, PriceLevel, std::greater<>>` — best bid is `begin()`
- Asks: `std::map<Price, PriceLevel, std::less<>>` — best ask is `begin()`
- Each `PriceLevel`: `std::list<Order>` in FIFO arrival order + cached total
- Cancel index: `unordered_map<OrderId, {side, price, list iterator}>` —
  `std::list` iterators stay valid under other insertions/erasures

The two opposite map comparators mean "best price" is always `begin()` on
either side, so the engine never searches. Within a level, arrival order is
time priority: new orders push to the back, fills consume from the front.
`std::list` is the piece that makes O(1) cancellation work — its iterators
survive insertions and erasures of other elements, so the cancel index can
jump straight to any resting order without scanning its level. Each level
also caches its total quantity, so rendering depth never walks the queues.

Matching itself is one loop: peek the front order of the best opposite
level; if the incoming order still crosses it, fill `min(remaining, maker
quantity)` at the maker's price and repeat. Trades execute at the resting
(maker) order's price, so price improvement goes to the incoming order;
partially filled resting orders keep their queue position; an unfilled
market-order remainder is cancelled, never rested.

| Operation | Complexity (L = price levels/side) |
|---|---|
| Add resting order | O(log L) |
| Best bid / best ask | O(1) |
| One fill during matching | O(1) amortized (+O(log L) when a level empties) |
| Cancel | O(1) average (+O(log L) when a level empties) |
| Depth snapshot, top N | O(N) |

Error handling is layered: the REPL and price parser validate all user input
at the boundary, the engine asserts its preconditions, and the book trusts
its caller. Invariant (asserted in debug builds after every submit): the
book is never crossed — matching runs to completion before any remainder
rests.

## Structure

    src/
      types.h                 Price/Quantity/OrderId, Side, OrderType, Order, Trade
      order_book.{h,cpp}      resting-order storage: price-time priority, O(1) cancel
      matching_engine.{h,cpp} crossing algorithm, per-order share accounting
      display.{h,cpp}         price formatting/parsing, book + trade rendering
      simulator.{h,cpp}       seeded random order flow around a drifting reference
      repl.{h,cpp}            command parsing (pure, unit-tested) + interactive loop
      benchmark.{h,cpp}       pre-generated load, engine-only timing
      main.cpp                CLI dispatch: REPL (default), --benchmark [N], --seed S
    tests/
      test_framework.h        hand-rolled TEST/CHECK/CHECK_EQ harness
      test_*.cpp              30 tests: book operations, 12 matching scenarios,
                              price parsing, simulator determinism, REPL parsing
    CMakeLists.txt            two targets: orderbook, orderbook_tests (CTest)

## Build & run

    brew install cmake                # once
    cmake -B build && cmake --build build
    ./build/orderbook_tests           # 30 tests
    ./build/orderbook                 # interactive REPL

    # benchmark (use an optimized build for numbers)
    cmake -B build-release -DCMAKE_BUILD_TYPE=Release
    cmake --build build-release
    ./build-release/orderbook --benchmark

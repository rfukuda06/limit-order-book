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
    Execution time:    0.062 seconds
    Throughput:        16.05 million orders/sec
    Trades executed:   152723
    Resting orders:    845416

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

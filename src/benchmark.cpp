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

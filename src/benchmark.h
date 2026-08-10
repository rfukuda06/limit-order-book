#pragma once
#include <cstddef>

namespace lob {

// Pre-generates numOrders with the simulator's generator, then times the
// engine.submit* loop alone (RNG cost excluded). Reports throughput plus a
// determinism checksum (trade count + final resting orders).
void runBenchmark(std::size_t numOrders, unsigned seed);

}  // namespace lob

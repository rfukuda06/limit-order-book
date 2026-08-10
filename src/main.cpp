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
    if (s.empty() || s.size() > 12) return std::nullopt;
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

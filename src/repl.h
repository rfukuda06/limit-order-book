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

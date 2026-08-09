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

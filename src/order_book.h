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

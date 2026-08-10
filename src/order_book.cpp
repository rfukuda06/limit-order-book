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

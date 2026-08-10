#include "simulator.h"

#include <algorithm>

namespace lob {

MarketSimulator::MarketSimulator(MatchingEngine& engine, unsigned seed)
    : engine_(engine), rng_(seed) {}

void MarketSimulator::seedInitialLiquidity() {
    std::uniform_int_distribution<Price> offset(1, 10);
    std::uniform_int_distribution<Quantity> qty(10, 100);
    for (int i = 0; i < 10; ++i) {
        SubmitResult buy = engine_.submitLimit(
            Side::Buy, referencePrice_ - offset(rng_), qty(rng_));
        if (buy.restedQty > 0) restingIds_.push_back(buy.id);
        SubmitResult sell = engine_.submitLimit(
            Side::Sell, referencePrice_ + offset(rng_), qty(rng_));
        if (sell.restedQty > 0) restingIds_.push_back(sell.id);
    }
}

OrderRequest MarketSimulator::drawOrderRequest() {
    std::uniform_int_distribution<int> roll(1, 100);
    std::uniform_int_distribution<int> coin(0, 1);
    Side side = coin(rng_) == 0 ? Side::Buy : Side::Sell;
    if (roll(rng_) <= 90) {
        std::uniform_int_distribution<Price> offset(1, 10);
        std::uniform_int_distribution<Quantity> qty(10, 100);
        Price price = side == Side::Buy ? referencePrice_ - offset(rng_)
                                        : referencePrice_ + offset(rng_);
        price = std::max<Price>(price, 1);  // never a non-positive price
        return {OrderType::Limit, side, price, qty(rng_)};
    }
    std::uniform_int_distribution<Quantity> qty(10, 50);
    return {OrderType::Market, side, 0, qty(rng_)};
}

std::vector<Trade> MarketSimulator::step() {
    std::uniform_int_distribution<int> roll(1, 100);
    if (roll(rng_) > 95 && !restingIds_.empty()) {
        cancelRandomOwnOrder();
        return {};
    }
    return submitRequest(drawOrderRequest());
}

std::vector<Trade> MarketSimulator::submitRequest(const OrderRequest& req) {
    SubmitResult result =
        req.type == OrderType::Limit
            ? engine_.submitLimit(req.side, req.price, req.quantity)
            : engine_.submitMarket(req.side, req.quantity);
    if (result.restedQty > 0) restingIds_.push_back(result.id);
    if (!result.trades.empty()) {
        referencePrice_ = result.trades.back().price;  // drift with trades
    }
    return result.trades;
}

void MarketSimulator::cancelRandomOwnOrder() {
    std::uniform_int_distribution<std::size_t> pick(0, restingIds_.size() - 1);
    std::size_t i = pick(rng_);
    OrderId id = restingIds_[i];
    restingIds_[i] = restingIds_.back();  // swap-pop removal
    restingIds_.pop_back();
    // false just means the order already filled — the id was stale.
    engine_.cancel(id);
}

}  // namespace lob

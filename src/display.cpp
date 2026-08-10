#include "display.h"

#include <cassert>
#include <cctype>
#include <cstdio>

namespace lob {

std::string formatPrice(Price ticks) {
    assert(ticks >= 0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld.%02lld",
                  (long long)(ticks / TICKS_PER_DOLLAR),
                  (long long)(ticks % TICKS_PER_DOLLAR));
    return buf;
}

std::string formatMid(Price bid, Price ask) {
    Price sum = bid + ask;
    std::string s = formatPrice(sum / 2);
    if (sum % 2 != 0) s += "5";  // half-tick midpoints end in .xx5
    return s;
}

std::optional<Price> parsePrice(const std::string& text) {
    auto allDigits = [](const std::string& s) {
        if (s.empty()) return false;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        return true;
    };

    std::string whole = text;
    std::string frac;
    auto dot = text.find('.');
    if (dot != std::string::npos) {
        whole = text.substr(0, dot);
        frac = text.substr(dot + 1);
        if (frac.empty() || frac.size() > 2) return std::nullopt;  // sub-penny
        if (!allDigits(frac)) return std::nullopt;
    }
    if (!allDigits(whole) || whole.size() > 12) return std::nullopt;

    Price ticks = std::stoll(whole) * TICKS_PER_DOLLAR;
    if (frac.size() == 1) ticks += (frac[0] - '0') * 10;
    if (frac.size() == 2) ticks += (frac[0] - '0') * 10 + (frac[1] - '0');
    if (ticks <= 0) return std::nullopt;
    return ticks;
}

void printBook(const OrderBook& book, int levels) {
    auto asks = book.depth(Side::Sell, levels);
    auto bids = book.depth(Side::Buy, levels);

    std::printf("================ ORDER BOOK ================\n");
    std::printf("        ASKS\n");
    // Asks print worst-first so the best ask sits nearest the spread.
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::printf("%9s | %6lld  (%d)\n", formatPrice(it->price).c_str(),
                    (long long)it->totalQty, it->orderCount);
    }
    std::printf("--------------------------------------------\n");
    auto bid = book.bestBid();
    auto ask = book.bestAsk();
    if (ask) std::printf(" Best Ask: %s", formatPrice(*ask).c_str());
    if (bid && ask) {
        std::printf("   Mid: %s   Spread: %s", formatMid(*bid, *ask).c_str(),
                    formatPrice(*ask - *bid).c_str());
    }
    if (ask) std::printf("\n");
    if (bid) std::printf(" Best Bid: %s\n", formatPrice(*bid).c_str());
    if (!bid && !ask) std::printf(" (book is empty)\n");
    std::printf("--------------------------------------------\n");
    for (const auto& level : bids) {
        std::printf("%9s | %6lld  (%d)\n", formatPrice(level.price).c_str(),
                    (long long)level.totalQty, level.orderCount);
    }
    std::printf("        BIDS\n");
    std::printf("============================================\n");
}

void printTrade(const Trade& trade, bool userInvolved) {
    std::printf("TRADE  %6lld @ %s  (taker #%llu, maker #%llu)%s\n",
                (long long)trade.quantity, formatPrice(trade.price).c_str(),
                (unsigned long long)trade.takerId,
                (unsigned long long)trade.makerId,
                userInvolved ? "  (you)" : "");
}

}  // namespace lob

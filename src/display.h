#pragma once
#include <optional>
#include <string>

#include "order_book.h"
#include "types.h"

namespace lob {

std::string formatPrice(Price ticks);              // 10010 -> "100.10"
std::string formatMid(Price bid, Price ask);       // may end ".xx5"
// "100.10" -> 10010. Rejects sub-penny, non-numeric, and <= 0.
std::optional<Price> parsePrice(const std::string& text);

void printBook(const OrderBook& book, int levels);
void printTrade(const Trade& trade, bool userInvolved);

}  // namespace lob

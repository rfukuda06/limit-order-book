#include "test_framework.h"

#include "order_book.h"

using namespace lob;

namespace {
Order limit(OrderId id, Side side, Price price, Quantity qty) {
    return {id, side, OrderType::Limit, price, qty};
}
}  // namespace

TEST(book_add_and_best_prices) {
    OrderBook book;
    CHECK(book.bestBid() == std::nullopt);
    CHECK(book.bestAsk() == std::nullopt);

    book.addOrder(limit(1, Side::Buy, 9990, 30));
    book.addOrder(limit(2, Side::Buy, 10000, 20));
    book.addOrder(limit(3, Side::Sell, 10010, 50));

    CHECK(book.bestBid() == 10000);   // highest bid wins
    CHECK(book.bestAsk() == 10010);   // lowest ask wins
    CHECK_EQ(book.orderCount(), 3);
}

TEST(book_depth_aggregates_levels) {
    OrderBook book;
    book.addOrder(limit(1, Side::Buy, 10000, 20));
    book.addOrder(limit(2, Side::Buy, 10000, 30));
    book.addOrder(limit(3, Side::Buy, 9990, 10));

    auto bids = book.depth(Side::Buy, 10);
    CHECK_EQ(bids.size(), 2);
    CHECK_EQ(bids[0].price, 10000);      // best level first
    CHECK_EQ(bids[0].totalQty, 50);      // 20 + 30 aggregated
    CHECK_EQ(bids[0].orderCount, 2);
    CHECK_EQ(bids[1].price, 9990);

    auto one = book.depth(Side::Buy, 1); // truncates to N levels
    CHECK_EQ(one.size(), 1);
    CHECK(book.depth(Side::Sell, 10).empty());
}

TEST(book_cancel) {
    OrderBook book;
    book.addOrder(limit(1, Side::Buy, 10000, 20));
    CHECK(book.contains(1));

    CHECK(book.cancel(1));
    CHECK(!book.contains(1));
    CHECK(book.bestBid() == std::nullopt);  // emptied level removed
    CHECK_EQ(book.orderCount(), 0);

    CHECK(!book.cancel(1));    // already gone
    CHECK(!book.cancel(999));  // never existed
}

TEST(book_peek_front_is_fifo) {
    OrderBook book;
    book.addOrder(limit(1, Side::Sell, 10010, 30));
    book.addOrder(limit(2, Side::Sell, 10010, 20));  // same price, later

    const Order* front = book.peekFront(Side::Sell);
    CHECK(front != nullptr);
    CHECK_EQ(front->id, 1);  // first in, first out
    CHECK(book.peekFront(Side::Buy) == nullptr);
}

TEST(book_fill_front_partial_and_full) {
    OrderBook book;
    book.addOrder(limit(1, Side::Sell, 10010, 30));
    book.addOrder(limit(2, Side::Sell, 10010, 20));

    book.fillFront(Side::Sell, 10);              // partial fill of order 1
    const Order* front = book.peekFront(Side::Sell);
    CHECK_EQ(front->id, 1);                      // keeps queue position
    CHECK_EQ(front->quantity, 20);
    CHECK_EQ(book.depth(Side::Sell, 1)[0].totalQty, 40);

    book.fillFront(Side::Sell, 20);              // finishes order 1
    CHECK(!book.contains(1));
    CHECK_EQ(book.peekFront(Side::Sell)->id, 2);

    book.fillFront(Side::Sell, 20);              // finishes order 2 and level
    CHECK(book.bestAsk() == std::nullopt);
    CHECK_EQ(book.orderCount(), 0);
}

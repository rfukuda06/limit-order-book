#include "test_framework.h"

#include "repl.h"

using namespace lob;

TEST(parse_limit_order) {
    Command c = parseCommand("buy 50 @ 100.10");
    CHECK(c.kind == Command::Kind::LimitOrder);
    CHECK(c.side == Side::Buy);
    CHECK_EQ(c.quantity, 50);
    CHECK_EQ(c.price, 10010);

    Command s = parseCommand("sell 25 @ 100.50");
    CHECK(s.kind == Command::Kind::LimitOrder);
    CHECK(s.side == Side::Sell);
    CHECK_EQ(s.price, 10050);
}

TEST(parse_market_order) {
    Command c = parseCommand("buy 50");
    CHECK(c.kind == Command::Kind::MarketOrder);
    CHECK(c.side == Side::Buy);
    CHECK_EQ(c.quantity, 50);
}

TEST(parse_cancel_step_book_trades) {
    Command c = parseCommand("cancel 12");
    CHECK(c.kind == Command::Kind::Cancel);
    CHECK_EQ(c.orderId, 12);

    CHECK_EQ(parseCommand("step").count, 1);      // default
    CHECK_EQ(parseCommand("step 20").count, 20);
    CHECK(parseCommand("book").kind == Command::Kind::Book);
    CHECK_EQ(parseCommand("book 5").count, 5);
    CHECK(parseCommand("trades").kind == Command::Kind::Trades);
    CHECK(parseCommand("help").kind == Command::Kind::Help);
    CHECK(parseCommand("quit").kind == Command::Kind::Quit);
}

TEST(parse_rejects_invalid_input) {
    CHECK(parseCommand("").kind == Command::Kind::Invalid);
    CHECK(parseCommand("hold 50").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy fifty").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy 0 @ 100").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy -5 @ 100").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy 50 @ 100.005").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy 50 @ abc").kind == Command::Kind::Invalid);
    CHECK(parseCommand("buy 50 100.10").kind == Command::Kind::Invalid);
    CHECK(parseCommand("cancel").kind == Command::Kind::Invalid);
    CHECK(parseCommand("step -3").kind == Command::Kind::Invalid);
    CHECK(parseCommand("step 999999999999").kind == Command::Kind::Invalid);
    CHECK(!parseCommand("buy 0 @ 100").error.empty());  // errors carry text
}

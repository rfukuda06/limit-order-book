#include "test_framework.h"

#include "simulator.h"

using namespace lob;

TEST(sim_seeds_initial_liquidity) {
    MatchingEngine engine;
    MarketSimulator sim(engine, 42);
    sim.seedInitialLiquidity();
    CHECK_EQ(engine.book().orderCount(), 20);
    CHECK(engine.book().bestBid().has_value());
    CHECK(engine.book().bestAsk().has_value());
    CHECK(*engine.book().bestBid() < *engine.book().bestAsk());
}

TEST(sim_draw_order_requests_are_valid) {
    MatchingEngine engine;
    MarketSimulator sim(engine, 42);
    for (int i = 0; i < 1000; ++i) {
        OrderRequest req = sim.drawOrderRequest();
        CHECK(req.quantity > 0);
        if (req.type == OrderType::Limit) {
            CHECK(req.price > 0);
        }
    }
}

TEST(sim_is_deterministic_for_same_seed) {
    MatchingEngine e1, e2;
    MarketSimulator s1(e1, 123), s2(e2, 123);
    s1.seedInitialLiquidity();
    s2.seedInitialLiquidity();
    std::size_t trades1 = 0, trades2 = 0;
    for (int i = 0; i < 200; ++i) trades1 += s1.step().size();
    for (int i = 0; i < 200; ++i) trades2 += s2.step().size();
    CHECK_EQ(trades1, trades2);
    CHECK_EQ(e1.book().orderCount(), e2.book().orderCount());
    CHECK(e1.book().bestBid() == e2.book().bestBid());
    CHECK(e1.book().bestAsk() == e2.book().bestAsk());
}

TEST(sim_steps_keep_book_sane) {
    MatchingEngine engine;
    MarketSimulator sim(engine, 42);
    sim.seedInitialLiquidity();
    for (int i = 0; i < 500; ++i) {
        sim.step();
        auto bid = engine.book().bestBid();
        auto ask = engine.book().bestAsk();
        if (bid && ask) CHECK(*bid < *ask);
    }
}

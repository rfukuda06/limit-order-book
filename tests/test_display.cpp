#include "test_framework.h"

#include "display.h"

using namespace lob;

TEST(format_price) {
    CHECK(formatPrice(10010) == "100.10");
    CHECK(formatPrice(10000) == "100.00");
    CHECK(formatPrice(5) == "0.05");
    CHECK(formatPrice(9990) == "99.90");
}

TEST(parse_price_valid) {
    CHECK(parsePrice("100.10") == 10010);
    CHECK(parsePrice("100.1") == 10010);   // tenths scale to ticks
    CHECK(parsePrice("100") == 10000);
    CHECK(parsePrice("0.05") == 5);
}

TEST(parse_price_invalid) {
    CHECK(parsePrice("100.005") == std::nullopt);  // sub-penny
    CHECK(parsePrice("abc") == std::nullopt);
    CHECK(parsePrice("") == std::nullopt);
    CHECK(parsePrice("-5") == std::nullopt);
    CHECK(parsePrice("0") == std::nullopt);        // price must be positive
    CHECK(parsePrice("100.") == std::nullopt);     // dot with no digits
    CHECK(parsePrice(".50") == std::nullopt);      // no whole part
    CHECK(parsePrice("10 0") == std::nullopt);     // embedded junk
}

TEST(format_mid_handles_half_ticks) {
    CHECK(formatMid(10000, 10010) == "100.05");
    CHECK(formatMid(10000, 10001) == "100.005");  // half-tick midpoint
}

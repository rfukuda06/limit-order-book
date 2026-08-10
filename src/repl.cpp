#include "repl.h"

#include <cctype>
#include <climits>
#include <cstdio>
#include <deque>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "display.h"

namespace lob {

namespace {

constexpr int kDefaultBookLevels = 10;
constexpr int kDefaultTradeCount = 10;
constexpr std::size_t kTradeHistoryLimit = 50;

std::optional<long long> parsePositiveInt(const std::string& text) {
    if (text.empty() || text.size() > 12) return std::nullopt;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    long long value = std::stoll(text);
    if (value <= 0) return std::nullopt;
    return value;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens;
}

Command invalid(const std::string& message) {
    Command c;
    c.kind = Command::Kind::Invalid;
    c.error = message;
    return c;
}

Command parseCount(Command::Kind kind, const std::vector<std::string>& tokens,
                   int defaultCount) {
    Command c;
    c.kind = kind;
    c.count = defaultCount;
    if (tokens.size() > 2) return invalid("too many arguments");
    if (tokens.size() == 2) {
        auto n = parsePositiveInt(tokens[1]);
        if (!n) return invalid("expected a positive count");
        if (*n > INT_MAX) return invalid("count too large");
        c.count = static_cast<int>(*n);
    }
    return c;
}

}  // namespace

Command parseCommand(const std::string& line) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return invalid("empty command (try 'help')");
    const std::string& verb = tokens[0];

    if (verb == "buy" || verb == "sell") {
        Command c;
        c.side = verb == "buy" ? Side::Buy : Side::Sell;
        if (tokens.size() != 2 && tokens.size() != 4) {
            return invalid("usage: buy 50 @ 100.10  (limit)  or  buy 50  (market)");
        }
        auto qty = parsePositiveInt(tokens[1]);
        if (!qty) return invalid("quantity must be a positive integer");
        c.quantity = *qty;
        if (tokens.size() == 2) {
            c.kind = Command::Kind::MarketOrder;
            return c;
        }
        if (tokens[2] != "@") return invalid("expected '@' before the price");
        auto price = parsePrice(tokens[3]);
        if (!price) {
            return invalid("bad price (positive, at most 2 decimals — no sub-penny)");
        }
        c.kind = Command::Kind::LimitOrder;
        c.price = *price;
        return c;
    }
    if (verb == "cancel") {
        if (tokens.size() != 2) return invalid("usage: cancel <order-id>");
        auto id = parsePositiveInt(tokens[1]);
        if (!id) return invalid("order id must be a positive integer");
        Command c;
        c.kind = Command::Kind::Cancel;
        c.orderId = static_cast<OrderId>(*id);
        return c;
    }
    if (verb == "step") return parseCount(Command::Kind::Step, tokens, 1);
    if (verb == "book") {
        return parseCount(Command::Kind::Book, tokens, kDefaultBookLevels);
    }
    if (verb == "trades") {
        return parseCount(Command::Kind::Trades, tokens, kDefaultTradeCount);
    }
    if (verb == "help" && tokens.size() == 1) {
        Command c;
        c.kind = Command::Kind::Help;
        return c;
    }
    if (verb == "quit" && tokens.size() == 1) {
        Command c;
        c.kind = Command::Kind::Quit;
        return c;
    }
    return invalid("unknown command '" + verb + "' (try 'help')");
}

namespace {

void printHelp() {
    std::printf(
        "  buy 50 @ 100.10   limit buy      step [N]    run N sim events\n"
        "  sell 25 @ 100.50  limit sell     book [N]    top N levels/side\n"
        "  buy 50            market buy     trades [N]  last N trades\n"
        "  sell 25           market sell    help        this text\n"
        "  cancel 12         cancel by id   quit        exit\n");
}

class ReplSession {
public:
    ReplSession(MatchingEngine& engine, MarketSimulator& sim)
        : engine_(engine), sim_(sim) {}

    // Returns false when the loop should exit.
    bool handle(const Command& c) {
        switch (c.kind) {
            case Command::Kind::LimitOrder:
                report(engine_.submitLimit(c.side, c.price, c.quantity));
                return true;
            case Command::Kind::MarketOrder:
                report(engine_.submitMarket(c.side, c.quantity));
                return true;
            case Command::Kind::Cancel:
                if (engine_.cancel(c.orderId)) {
                    std::printf("cancelled order #%llu\n",
                                (unsigned long long)c.orderId);
                } else {
                    std::printf("order #%llu not found (filled or never existed)\n",
                                (unsigned long long)c.orderId);
                }
                return true;
            case Command::Kind::Step: {
                int tradeCount = 0;
                for (int i = 0; i < c.count; ++i) {
                    for (const Trade& t : sim_.step()) {
                        remember(t);
                        printTrade(t, involvesUser(t));
                        ++tradeCount;
                    }
                }
                std::printf("%d sim event(s), %d trade(s)\n", c.count,
                            tradeCount);
                printBook(engine_.book(), kDefaultBookLevels);
                return true;
            }
            case Command::Kind::Book:
                printBook(engine_.book(), c.count);
                return true;
            case Command::Kind::Trades: {
                int shown = 0;
                for (auto it = history_.rbegin();
                     it != history_.rend() && shown < c.count; ++it, ++shown) {
                    printTrade(*it, involvesUser(*it));
                }
                if (shown == 0) std::printf("no trades yet\n");
                return true;
            }
            case Command::Kind::Help:
                printHelp();
                return true;
            case Command::Kind::Quit:
                return false;
            case Command::Kind::Invalid:
                std::printf("error: %s\n", c.error.c_str());
                return true;
        }
        return true;
    }

private:
    void report(const SubmitResult& result) {
        userIds_.insert(result.id);
        for (const Trade& t : result.trades) {
            remember(t);
            printTrade(t, true);
        }
        if (result.restedQty > 0) {
            std::printf("order #%llu resting: %lld remaining\n",
                        (unsigned long long)result.id,
                        (long long)result.restedQty);
        }
        if (result.cancelledQty > 0) {
            std::printf("unfilled remainder cancelled: %lld (no liquidity)\n",
                        (long long)result.cancelledQty);
        }
        if (result.trades.empty() && result.restedQty == 0 &&
            result.cancelledQty == 0) {
            std::printf("nothing happened (empty book?)\n");
        }
        printBook(engine_.book(), kDefaultBookLevels);
    }

    void remember(const Trade& t) {
        history_.push_back(t);
        if (history_.size() > kTradeHistoryLimit) history_.pop_front();
    }

    bool involvesUser(const Trade& t) const {
        return userIds_.count(t.takerId) > 0 || userIds_.count(t.makerId) > 0;
    }

    MatchingEngine& engine_;
    MarketSimulator& sim_;
    std::deque<Trade> history_;          // last 50 trades
    std::unordered_set<OrderId> userIds_;
};

}  // namespace

void runRepl(MatchingEngine& engine, MarketSimulator& sim) {
    std::printf("Limit Order Book Simulator — type 'help' for commands.\n");
    printBook(engine.book(), kDefaultBookLevels);
    ReplSession session(engine, sim);
    std::string line;
    while (std::printf("> "), std::fflush(stdout),
           std::getline(std::cin, line)) {
        if (!session.handle(parseCommand(line))) break;
    }
    std::printf("bye\n");
}

}  // namespace lob

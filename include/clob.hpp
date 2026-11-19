#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clob {

enum class Side { Buy, Sell };

struct Trade {
    uint64_t taker;
    uint64_t maker;
    int64_t price;
    int64_t qty;
    bool operator==(const Trade& o) const {
        return taker == o.taker && maker == o.maker && price == o.price && qty == o.qty;
    }
};

struct Level {
    int64_t price;
    int64_t qty;
    bool operator==(const Level& o) const { return price == o.price && qty == o.qty; }
};

class Error : public std::invalid_argument {
public:
    explicit Error(const std::string& m) : std::invalid_argument(m) {}
};

// Price-time priority. Prices are positive integer ticks.
// The trade price is the resting order's price. A market order does not rest.
// One thread. No network.
class Book {
public:
    Book() = default;
    Book(const Book&) = delete;
    Book& operator=(const Book&) = delete;
    Book(Book&&) = default;
    Book& operator=(Book&&) = default;

    std::vector<Trade> limit(uint64_t id, Side side, int64_t price, int64_t qty);
    std::vector<Trade> market(uint64_t id, Side side, int64_t qty);
    bool cancel(uint64_t id);

    std::optional<int64_t> best_bid() const;
    std::optional<int64_t> best_ask() const;
    int64_t qty_at(Side side, int64_t price) const;
    std::vector<Level> levels(Side side) const;
    bool contains(uint64_t id) const;
    std::size_t resting() const;

private:
    struct Order {
        uint64_t id;
        int64_t qty;
    };
    struct Loc {
        Side side;
        int64_t price;
        std::list<Order>::iterator it;
    };

    std::vector<Trade> hit(uint64_t taker, Side side, const int64_t* limit, int64_t& qty);
    void rest(uint64_t id, Side side, int64_t price, int64_t qty);
    void require_fresh(uint64_t id) const;
    static void require_qty(int64_t qty);
    static int64_t sum_qty(const std::list<Order>& level);

    std::map<int64_t, std::list<Order>, std::greater<int64_t>> bids_;
    std::map<int64_t, std::list<Order>> asks_;
    std::unordered_map<uint64_t, Loc> index_;
};

}  // namespace clob

#include "clob.hpp"

#include <iterator>
#include <limits>

namespace clob {

void Book::require_qty(int64_t qty) {
    if (qty <= 0) {
        throw Error("qty");
    }
}

void Book::require_fresh(uint64_t id) const {
    if (index_.find(id) != index_.end()) {
        throw Error("duplicate id");
    }
}

int64_t Book::sum_qty(const std::list<Order>& level) {
    int64_t s = 0;
    for (const auto& o : level) {
        if (o.qty > std::numeric_limits<int64_t>::max() - s) {
            throw Error("qty");
        }
        s += o.qty;
    }
    return s;
}

std::vector<Trade> Book::hit(uint64_t taker, Side side, const int64_t* limit, int64_t& qty) {
    std::vector<Trade> trades;
    if (side == Side::Buy) {
        while (qty > 0 && !asks_.empty()) {
            auto lvl = asks_.begin();
            if (limit != nullptr && lvl->first > *limit) {
                break;
            }
            auto& level = lvl->second;
            auto& front = level.front();
            int64_t fill = front.qty < qty ? front.qty : qty;
            uint64_t maker = front.id;
            trades.push_back(Trade{taker, maker, lvl->first, fill});
            qty -= fill;
            front.qty -= fill;
            if (front.qty == 0) {
                index_.erase(maker);
                level.pop_front();
                if (level.empty()) {
                    asks_.erase(lvl);
                }
            }
        }
    } else {
        while (qty > 0 && !bids_.empty()) {
            auto lvl = bids_.begin();
            if (limit != nullptr && lvl->first < *limit) {
                break;
            }
            auto& level = lvl->second;
            auto& front = level.front();
            int64_t fill = front.qty < qty ? front.qty : qty;
            uint64_t maker = front.id;
            trades.push_back(Trade{taker, maker, lvl->first, fill});
            qty -= fill;
            front.qty -= fill;
            if (front.qty == 0) {
                index_.erase(maker);
                level.pop_front();
                if (level.empty()) {
                    bids_.erase(lvl);
                }
            }
        }
    }
    return trades;
}

void Book::rest(uint64_t id, Side side, int64_t price, int64_t qty) {
    if (side == Side::Buy) {
        auto& level = bids_[price];
        level.push_back(Order{id, qty});
        index_.emplace(id, Loc{Side::Buy, price, std::prev(level.end())});
    } else {
        auto& level = asks_[price];
        level.push_back(Order{id, qty});
        index_.emplace(id, Loc{Side::Sell, price, std::prev(level.end())});
    }
}

std::vector<Trade> Book::limit(uint64_t id, Side side, int64_t price, int64_t qty) {
    require_qty(qty);
    if (price <= 0) {
        throw Error("price");
    }
    require_fresh(id);
    auto trades = hit(id, side, &price, qty);
    if (qty > 0) {
        rest(id, side, price, qty);
    }
    return trades;
}

std::vector<Trade> Book::market(uint64_t id, Side side, int64_t qty) {
    require_qty(qty);
    require_fresh(id);
    return hit(id, side, nullptr, qty);
}

bool Book::cancel(uint64_t id) {
    auto it = index_.find(id);
    if (it == index_.end()) {
        return false;
    }
    Loc loc = it->second;
    index_.erase(it);
    if (loc.side == Side::Buy) {
        auto lvl = bids_.find(loc.price);
        lvl->second.erase(loc.it);
        if (lvl->second.empty()) {
            bids_.erase(lvl);
        }
    } else {
        auto lvl = asks_.find(loc.price);
        lvl->second.erase(loc.it);
        if (lvl->second.empty()) {
            asks_.erase(lvl);
        }
    }
    return true;
}

std::optional<int64_t> Book::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<int64_t> Book::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

int64_t Book::qty_at(Side side, int64_t price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) {
            return 0;
        }
        return sum_qty(it->second);
    }
    auto it = asks_.find(price);
    if (it == asks_.end()) {
        return 0;
    }
    return sum_qty(it->second);
}

std::vector<Level> Book::levels(Side side) const {
    std::vector<Level> out;
    if (side == Side::Buy) {
        for (const auto& kv : bids_) {
            out.push_back(Level{kv.first, sum_qty(kv.second)});
        }
    } else {
        for (const auto& kv : asks_) {
            out.push_back(Level{kv.first, sum_qty(kv.second)});
        }
    }
    return out;
}

bool Book::contains(uint64_t id) const { return index_.find(id) != index_.end(); }

std::size_t Book::resting() const { return index_.size(); }

}  // namespace clob

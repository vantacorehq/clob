#include "clob.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <vector>

static int checks;

static void check(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL " << msg << "\n";
        std::exit(1);
    }
    ++checks;
}

template <typename F>
static void fails(const char* msg, F&& fn) {
    try {
        fn();
    } catch (const clob::Error&) {
        ++checks;
        return;
    }
    std::cerr << "FAIL expected " << msg << "\n";
    std::exit(1);
}

static void eq_trades(const std::vector<clob::Trade>& got, const std::vector<clob::Trade>& want,
                      const char* msg) {
    if (got != want) {
        std::cerr << "FAIL " << msg << " trades " << got.size() << " vs " << want.size() << "\n";
        std::exit(1);
    }
    ++checks;
}

int main() {
    using clob::Side;
    clob::Book book;
    check(!book.best_bid() && !book.best_ask(), "empty top");
    check(book.market(1, Side::Buy, 5).empty(), "empty market");
    check(!book.contains(1), "market did not rest");

    eq_trades(book.limit(1, Side::Buy, 100, 5), {}, "rest bid");
    check(book.best_bid() == 100 && book.qty_at(Side::Buy, 100) == 5, "bid 100");
    check(book.resting() == 1, "one resting");

    eq_trades(book.limit(2, Side::Buy, 101, 1), {}, "better bid");
    check(book.best_bid() == 101, "best is 101");

    eq_trades(book.market(9, Side::Sell, 2),
              {{9, 2, 101, 1}, {9, 1, 100, 1}}, "price priority");
    check(book.best_bid() == 100 && book.qty_at(Side::Buy, 100) == 4, "residual bid");
    check(!book.contains(2) && book.contains(1), "maker gone");

    clob::Book ladder;
    eq_trades(ladder.limit(3, Side::Sell, 100, 5), {}, "ask 100");
    eq_trades(ladder.limit(4, Side::Sell, 101, 5), {}, "ask 101");
    check(ladder.best_ask() == 100, "best ask");
    eq_trades(ladder.market(10, Side::Buy, 8),
              {{10, 3, 100, 5}, {10, 4, 101, 3}}, "sweep two asks");
    check(ladder.qty_at(Side::Sell, 101) == 2 && !ladder.contains(3), "ask residual");
    check(!ladder.contains(10), "taker not resting");

    eq_trades(ladder.limit(5, Side::Buy, 99, 4), {}, "no cross");
    check(ladder.best_bid() == 99 && ladder.best_ask() == 101, "no cross top");
    eq_trades(ladder.limit(6, Side::Buy, 101, 2), {{6, 4, 101, 2}}, "lift the ask");
    check(!ladder.best_ask() && !ladder.contains(4) && !ladder.contains(6), "ask cleared");
    check(ladder.contains(5) && ladder.qty_at(Side::Buy, 99) == 4, "bid stayed");

    clob::Book fifo;
    fifo.limit(1, Side::Buy, 50, 5);
    fifo.limit(2, Side::Buy, 50, 5);
    fifo.limit(3, Side::Buy, 50, 5);
    eq_trades(fifo.limit(4, Side::Sell, 50, 3), {{4, 1, 50, 3}}, "fifo front");
    check(fifo.qty_at(Side::Buy, 50) == 12, "fifo qty");
    check(fifo.cancel(1), "cancel front");
    check(!fifo.cancel(1), "cancel missing");
    eq_trades(fifo.limit(5, Side::Sell, 50, 6), {{5, 2, 50, 5}, {5, 3, 50, 1}}, "next in line");
    check(fifo.qty_at(Side::Buy, 50) == 4 && fifo.contains(3), "tail remains");

    clob::Book sweep;
    sweep.limit(1, Side::Sell, 10, 2);
    sweep.limit(2, Side::Sell, 11, 2);
    eq_trades(sweep.limit(3, Side::Buy, 12, 5),
              {{3, 1, 10, 2}, {3, 2, 11, 2}}, "limit sweeps and rests");
    check(sweep.contains(3) && sweep.best_bid() == 12 && sweep.qty_at(Side::Buy, 12) == 1, "remainder");
    check(sweep.levels(Side::Buy).size() == 1 && sweep.levels(Side::Buy)[0].price == 12, "levels");

    clob::Book cross;
    cross.limit(1, Side::Buy, 100, 3);
    eq_trades(cross.limit(2, Side::Sell, 101, 3), {}, "sell above bid");
    check(cross.best_ask() == 101 && cross.best_bid() == 100, "uncrossed");
    eq_trades(cross.limit(3, Side::Sell, 100, 1), {{3, 1, 100, 1}}, "sell hits bid");

    fails("qty", [&] { cross.limit(9, Side::Buy, 1, 0); });
    fails("neg qty", [&] { cross.market(9, Side::Buy, -1); });
    fails("price", [&] { cross.limit(9, Side::Buy, 0, 1); });
    fails("dup", [&] { cross.limit(1, Side::Sell, 50, 1); });
    check(cross.contains(1), "dup did not remove");

    clob::Book moved = std::move(cross);
    check(moved.contains(1) && moved.cancel(1), "move keeps queue");
    check(!moved.contains(1), "cancelled after move");

    // Brute-force book. Same price-time rules, vector instead of a ladder.
    struct Rest {
        uint64_t id;
        Side side;
        int64_t price;
        int64_t qty;
        int seq;
    };
    std::vector<Rest> live;
    int seq = 0;
    clob::Book fuzz;
    auto same = [&] {
        std::vector<std::tuple<uint64_t, int, int64_t, int64_t>> a, b;
        for (const auto& r : live) {
            if (r.qty > 0) {
                a.emplace_back(r.id, r.side == Side::Buy ? 0 : 1, r.price, r.qty);
            }
        }
        // Reconstruct from the real book via cancel-free observation: levels + contains
        // is not a full order list. Compare aggregates and walk ids we still track.
        std::sort(a.begin(), a.end());
        for (const auto& r : live) {
            if (r.qty <= 0) {
                continue;
            }
            if (!fuzz.contains(r.id) || fuzz.qty_at(r.side, r.price) <= 0) {
                std::cerr << "FAIL shadow id " << r.id << "\n";
                std::exit(1);
            }
            b.emplace_back(r.id, r.side == Side::Buy ? 0 : 1, r.price, r.qty);
        }
        std::sort(b.begin(), b.end());
        check(a == b, "shadow ids");
        check(fuzz.resting() == a.size(), "shadow count");
        auto bb = fuzz.best_bid();
        auto ba = fuzz.best_ask();
        std::optional<int64_t> ebb, eba;
        for (const auto& r : live) {
            if (r.qty <= 0) {
                continue;
            }
            if (r.side == Side::Buy && (!ebb || r.price > *ebb)) {
                ebb = r.price;
            }
            if (r.side == Side::Sell && (!eba || r.price < *eba)) {
                eba = r.price;
            }
        }
        check(bb == ebb && ba == eba, "shadow top");
        for (int px = 1; px <= 12; ++px) {
            int64_t qb = 0, qa = 0;
            for (const auto& r : live) {
                if (r.qty <= 0 || r.price != px) {
                    continue;
                }
                if (r.side == Side::Buy) {
                    qb += r.qty;
                } else {
                    qa += r.qty;
                }
            }
            check(fuzz.qty_at(Side::Buy, px) == qb && fuzz.qty_at(Side::Sell, px) == qa, "shadow qty");
        }
    };
    auto apply = [&](uint64_t id, Side side, const int64_t* limit, int64_t qty) {
        std::vector<int> idx;
        for (int i = 0; i < static_cast<int>(live.size()); ++i) {
            if (live[i].qty <= 0 || live[i].side == side) {
                continue;
            }
            if (limit != nullptr) {
                if (side == Side::Buy && live[i].price > *limit) {
                    continue;
                }
                if (side == Side::Sell && live[i].price < *limit) {
                    continue;
                }
            }
            idx.push_back(i);
        }
        std::sort(idx.begin(), idx.end(), [&](int a, int c) {
            if (live[a].price != live[c].price) {
                if (side == Side::Buy) {
                    return live[a].price < live[c].price;
                }
                return live[a].price > live[c].price;
            }
            return live[a].seq < live[c].seq;
        });
        std::vector<clob::Trade> trades;
        for (int i : idx) {
            if (qty == 0 || live[i].qty == 0) {
                continue;
            }
            int64_t fill = live[i].qty < qty ? live[i].qty : qty;
            trades.push_back(clob::Trade{id, live[i].id, live[i].price, fill});
            qty -= fill;
            live[i].qty -= fill;
        }
        if (limit != nullptr && qty > 0) {
            live.push_back(Rest{id, side, *limit, qty, seq++});
        }
        return trades;
    };

    std::mt19937 rng(20261119);
    std::uniform_int_distribution<int> kind(0, 9);
    std::uniform_int_distribution<int> price(1, 12);
    std::uniform_int_distribution<int> qty(1, 6);
    std::uniform_int_distribution<int> ident(1, 40);
    std::uniform_int_distribution<int> coin(0, 1);
    for (int step = 0; step < 2000; ++step) {
        int id = ident(rng);
        Side side = coin(rng) == 0 ? Side::Buy : Side::Sell;
        bool exists = false;
        for (const auto& r : live) {
            if (r.id == static_cast<uint64_t>(id) && r.qty > 0) {
                exists = true;
            }
        }
        int k = kind(rng);
        if (k < 6) {
            int64_t px = price(rng);
            int64_t q = qty(rng);
            if (exists) {
                fails("fuzz dup", [&] { fuzz.limit(static_cast<uint64_t>(id), side, px, q); });
            } else {
                auto got = fuzz.limit(static_cast<uint64_t>(id), side, px, q);
                auto want = apply(static_cast<uint64_t>(id), side, &px, q);
                if (got != want) {
                    std::cerr << "FAIL fuzz limit step " << step << "\n";
                    std::exit(1);
                }
                ++checks;
            }
        } else if (k < 8) {
            int64_t q = qty(rng);
            if (exists) {
                fails("fuzz mdup", [&] { fuzz.market(static_cast<uint64_t>(id), side, q); });
            } else {
                auto got = fuzz.market(static_cast<uint64_t>(id), side, q);
                auto want = apply(static_cast<uint64_t>(id), side, nullptr, q);
                if (got != want) {
                    std::cerr << "FAIL fuzz market step " << step << "\n";
                    std::exit(1);
                }
                ++checks;
            }
        } else {
            bool ok = fuzz.cancel(static_cast<uint64_t>(id));
            bool want = false;
            for (auto& r : live) {
                if (r.id == static_cast<uint64_t>(id) && r.qty > 0) {
                    r.qty = 0;
                    want = true;
                }
            }
            check(ok == want, "fuzz cancel");
        }
        same();
    }

    std::cout << "clob: " << checks << " checks passed\n";
    return 0;
}

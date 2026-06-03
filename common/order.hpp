#pragma once

// Domain types for a single LOB snapshot (one row of the Kaggle CSV).

#include "config.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace uds {

struct BookLevel {
    double bid_distance{0.0};
    double ask_distance{0.0};
    double bid_notional{0.0};
    double ask_notional{0.0};
    double bid_cancel_notional{0.0};
    double ask_cancel_notional{0.0};
    double bid_limit_notional{0.0};
    double ask_limit_notional{0.0};
};

struct MarketSnapshot {
    std::uint64_t sequence{0};
    std::uint64_t system_time{0};
    std::string symbol;
    double midpoint{0.0};
    double spread{0.0};
    std::uint32_t buys{0};
    std::uint32_t sells{0};
    std::array<BookLevel, config::kOrderBookLevels> levels{};
};

}  // namespace uds

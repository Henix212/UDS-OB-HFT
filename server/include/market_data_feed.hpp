#pragma once

// Loads Kaggle *_1sec.csv rows into memory and replays them in time order.

#include "order.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace uds {

class MarketDataFeed {
public:
    explicit MarketDataFeed(std::string symbol);

    bool loadFromCsv(const std::string& csv_path);
    bool hasData() const { return !snapshots_.empty(); }
    std::size_t size() const { return snapshots_.empty() ? 0 : snapshots_.size() - 1; }

    bool next(MarketSnapshot& out_snapshot);
    void reset();
    bool isFinished() const { return cursor_ >= snapshots_.size(); }

    const std::string& symbol() const { return symbol_; }
    std::uint64_t peekNextDelayMicros() const;

private:
    struct ColumnMap {
        int system_time{-1};
        int midpoint{-1};
        int spread{-1};
        int buys{-1};
        int sells{-1};
        int level[config::kOrderBookLevels][8]{};
    };

    bool buildColumnMap(const std::vector<std::string>& header, ColumnMap& columns) const;
    MarketSnapshot parseRow(const std::vector<std::string>& fields,
                          const ColumnMap& columns) const;

    std::string symbol_;
    std::vector<MarketSnapshot> snapshots_;
    std::size_t cursor_{0};
};

}  // namespace uds

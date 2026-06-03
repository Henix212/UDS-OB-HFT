// CSV parser for Kaggle LOB snapshots (1 row = 1 second).

#include "include/market_data_feed.hpp"

#include "config.hpp"
#include "utilities.hpp"


namespace uds {

namespace {

int columnIndex(const std::unordered_map<std::string, int>& index, const std::string& name) {
    const auto it = index.find(name);
    return it == index.end() ? -1 : it->second;
}

}  // namespace

MarketDataFeed::MarketDataFeed(std::string symbol) : symbol_(std::move(symbol)) {}

bool MarketDataFeed::buildColumnMap(const std::vector<std::string>& header,
                                    ColumnMap& columns) const {
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < static_cast<int>(header.size()); ++i) {
        index[util::trim(header[static_cast<std::size_t>(i)])] = i;
    }

    columns.system_time = columnIndex(index, "system_time");
    columns.midpoint = columnIndex(index, "midpoint");
    columns.spread = columnIndex(index, "spread");
    columns.buys = columnIndex(index, "buys");
    columns.sells = columnIndex(index, "sells");

    if (columns.system_time < 0 || columns.midpoint < 0) {
        util::logError("CSV " + symbol_ + ": missing system_time/midpoint columns");
        return false;
    }

    for (std::size_t level = 0; level < config::kOrderBookLevels; ++level) {
        const std::string suffix = std::to_string(level);
        columns.level[level][0] = columnIndex(index, "bids_distance_" + suffix);
        columns.level[level][1] = columnIndex(index, "asks_distance_" + suffix);
        columns.level[level][2] = columnIndex(index, "bids_notional_" + suffix);
        columns.level[level][3] = columnIndex(index, "asks_notional_" + suffix);
        columns.level[level][4] = columnIndex(index, "bids_cancel_notional_" + suffix);
        columns.level[level][5] = columnIndex(index, "asks_cancel_notional_" + suffix);
        columns.level[level][6] = columnIndex(index, "bids_limit_notional_" + suffix);
        columns.level[level][7] = columnIndex(index, "asks_limit_notional_" + suffix);
    }

    return true;
}

MarketSnapshot MarketDataFeed::parseRow(const std::vector<std::string>& fields,
                                        const ColumnMap& columns) const {
    auto fieldAt = [&](int idx) -> const std::string& {
        static const std::string empty;
        if (idx < 0 || idx >= static_cast<int>(fields.size())) {
            return empty;
        }
        return fields[static_cast<std::size_t>(idx)];
    };

    MarketSnapshot snapshot;
    snapshot.symbol = symbol_;
    snapshot.system_time = util::parseUint64(fieldAt(columns.system_time));
    snapshot.midpoint = util::parseDouble(fieldAt(columns.midpoint));
    snapshot.spread = util::parseDouble(fieldAt(columns.spread));
    snapshot.buys = util::parseUint32(fieldAt(columns.buys));
    snapshot.sells = util::parseUint32(fieldAt(columns.sells));

    for (std::size_t level = 0; level < config::kOrderBookLevels; ++level) {
        BookLevel& book = snapshot.levels[level];
        book.bid_distance = util::parseDouble(fieldAt(columns.level[level][0]));
        book.ask_distance = util::parseDouble(fieldAt(columns.level[level][1]));
        book.bid_notional = util::parseDouble(fieldAt(columns.level[level][2]));
        book.ask_notional = util::parseDouble(fieldAt(columns.level[level][3]));
        book.bid_cancel_notional = util::parseDouble(fieldAt(columns.level[level][4]));
        book.ask_cancel_notional = util::parseDouble(fieldAt(columns.level[level][5]));
        book.bid_limit_notional = util::parseDouble(fieldAt(columns.level[level][6]));
        book.ask_limit_notional = util::parseDouble(fieldAt(columns.level[level][7]));
    }

    return snapshot;
}

bool MarketDataFeed::loadFromCsv(const std::string& csv_path) {
    std::ifstream input(csv_path);
    if (!input.is_open()) {
        util::logError("Failed to open " + csv_path);
        return false;
    }

    std::string header_line;
    if (!std::getline(input, header_line)) {
        util::logError("Empty CSV: " + csv_path);
        return false;
    }

    ColumnMap columns;
    if (!buildColumnMap(util::splitCsvLine(header_line), columns)) {
        return false;
    }

    snapshots_.clear();
    cursor_ = 0;

    std::string line;
    std::uint64_t sequence = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        auto fields = util::splitCsvLine(line);
        if (fields.empty()) {
            continue;
        }

        MarketSnapshot snapshot = parseRow(fields, columns);
        snapshot.sequence = sequence++;
        snapshots_.push_back(snapshot);
    }

    util::logInfo(symbol_ + ": loaded " + std::to_string(snapshots_.size()) + " snapshots");
    return !snapshots_.empty();
}

bool MarketDataFeed::next(MarketSnapshot& out_snapshot) {
    if (cursor_ >= snapshots_.size()) {
        return false;
    }
    out_snapshot = snapshots_[cursor_++];
    return true;
}

void MarketDataFeed::reset() {
    cursor_ = 0;
}

// Inter-row delay from system_time (microseconds); used for replay pacing.
std::uint64_t MarketDataFeed::peekNextDelayMicros() const {
    if (cursor_ == 0 || cursor_ >= snapshots_.size()) {
        return 0;
    }
    const std::uint64_t prev = snapshots_[cursor_ - 1].system_time;
    const std::uint64_t next = snapshots_[cursor_].system_time;
    if (next <= prev) {
        return 0;
    }
    return next - prev;
}

}  // namespace uds

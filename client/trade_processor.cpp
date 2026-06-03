// Paper trading engine: signals, position management, session PnL report.

#include "include/trade_processor.hpp"

#include "utilities.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace uds {

namespace {

double envOrDefaultDouble(const char* name, double fallback) {
    if (const char* value = std::getenv(name)) {
        try {
            return std::stod(value);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::uint64_t envOrDefaultUint64(const char* name, std::uint64_t fallback) {
    if (const char* value = std::getenv(name)) {
        try {
            return std::stoull(value);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

}  // namespace

namespace strategy {

// Tunable strategy thresholds (not env vars). Simulation uses UDS_* in loadConfig().
constexpr int kFastMomentumTicks = 5;
constexpr int kSlowMomentumTicks = 30;
constexpr double kExitScore = 0.02;
constexpr double kMinBookImb = 0.15;
constexpr double kMinFlowImb = 0.15;
constexpr double kStopLossPct = 0.0015;
constexpr double kTakeProfitPct = 0.0030;
constexpr double kScoreMomWeight = 0.40;
constexpr double kScoreBookWeight = 0.35;
constexpr double kScoreFlowWeight = 0.25;
constexpr double kMaxEntrySpread = 0.05;
constexpr double kSlowMomLongRegime = 0.0005;
constexpr double kSlowMomShortRegime = -0.0005;

}  // namespace strategy

void TradeProcessor::pushMidPrice(double mid) {
    trading_.mid_ring[trading_.mid_ring_head] = mid;
    trading_.mid_ring_head = (trading_.mid_ring_head + 1) % TradingState::kMidWindow;
    trading_.mid_ring_count =
        std::min(trading_.mid_ring_count + 1, TradingState::kMidWindow);
}

double TradeProcessor::momentumAtLookback(int lookback_ticks) const {
    if (trading_.mid_ring_count <= static_cast<std::size_t>(lookback_ticks)) {
        return 0.0;
    }

    const std::size_t oldest_index =
        (trading_.mid_ring_head + TradingState::kMidWindow -
         static_cast<std::size_t>(lookback_ticks)) %
        TradingState::kMidWindow;
    const std::size_t newest_index =
        (trading_.mid_ring_head + TradingState::kMidWindow - 1) % TradingState::kMidWindow;
    const double old_mid = trading_.mid_ring[oldest_index];
    const double new_mid = trading_.mid_ring[newest_index];

    if (old_mid <= 0.0) {
        return 0.0;
    }
    return (new_mid - old_mid) / old_mid;
}

void TradeProcessor::loadConfig() {
    trading_.initial_cash = envOrDefaultDouble("UDS_INITIAL_CASH", 1'000'000.0);
    trading_.cash = trading_.initial_cash;
    trading_.position_size = envOrDefaultDouble("UDS_POSITION_SIZE", 0.1);
    trading_.signal_threshold = envOrDefaultDouble("UDS_SIGNAL_THRESHOLD", 0.16);
    trading_.fee_rate = envOrDefaultDouble("UDS_FEE_RATE", 0.000088);
    trading_.max_spread = envOrDefaultDouble("UDS_MAX_SPREAD", 2.0);
    trading_.momentum_weight = envOrDefaultDouble("UDS_MOMENTUM_WEIGHT", 0.55);
    trading_.imbalance_weight = envOrDefaultDouble("UDS_IMBALANCE_WEIGHT", 0.45);
    trading_.lookback_ticks =
        static_cast<int>(envOrDefaultUint64("UDS_LOOKBACK_TICKS", 5));
    trading_.min_hold_ticks =
        static_cast<int>(envOrDefaultUint64("UDS_MIN_HOLD_TICKS", 40));
    progress_log_interval_ = envOrDefaultUint64("UDS_LOG_INTERVAL", 0);
}

void TradeProcessor::resetTrading() {
    trading_ = TradingState{};
    loadConfig();
    trading_.peak_equity = trading_.cash;
    trading_.last_equity = trading_.cash;
}

void TradeProcessor::resetSession() {
    stats_ = SessionStats{};
    stats_.active = true;
    latest_.reset();
    resetTrading();
}

double TradeProcessor::markEquity(double mid) const {
    return trading_.cash + trading_.position * mid;
}

void TradeProcessor::updateDrawdown(double equity) {
    trading_.peak_equity = std::max(trading_.peak_equity, equity);
    if (trading_.peak_equity > 0.0) {
        const double dd = (trading_.peak_equity - equity) / trading_.peak_equity;
        trading_.max_drawdown = std::max(trading_.max_drawdown, dd);
    }
    trading_.last_equity = equity;
}

// Bid vs ask notional imbalance over top 3 book levels (-1 .. +1).
double TradeProcessor::deepBookImbalance(const MarketSnapshot& snapshot) const {
    double bid = 0.0;
    double ask = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        bid += snapshot.levels[i].bid_notional;
        ask += snapshot.levels[i].ask_notional;
    }
    const double denom = bid + ask + 1e-12;
    return (bid - ask) / denom;
}

double TradeProcessor::flowImbalance(const MarketSnapshot& snapshot) const {
    const double denom =
        static_cast<double>(snapshot.buys + snapshot.sells) + 1.0;
    return (static_cast<double>(snapshot.buys) - static_cast<double>(snapshot.sells)) /
           denom;
}

double TradeProcessor::compositeScore(const MarketSnapshot& snapshot) const {
    const double mom_fast = momentumAtLookback(strategy::kFastMomentumTicks);
    const double mom_slow = momentumAtLookback(strategy::kSlowMomentumTicks);
    const double mom = 0.6 * mom_fast + 0.4 * mom_slow;
    const double book = deepBookImbalance(snapshot);
    const double flow = flowImbalance(snapshot);
    return strategy::kScoreMomWeight * mom + strategy::kScoreBookWeight * book +
           strategy::kScoreFlowWeight * flow;
}

double TradeProcessor::unrealizedReturnPct(double mid) const {
    if (trading_.position_side == 0 || trading_.entry_mid <= 0.0) {
        return 0.0;
    }
    if (trading_.position_side > 0) {
        return (mid - trading_.entry_mid) / trading_.entry_mid;
    }
    return (trading_.entry_mid - mid) / trading_.entry_mid;
}

// All entry filters must align for a long (same idea for short).
bool entryLongOk(double mom_fast, double mom_slow, double book, double flow, double score,
                 double entry_threshold) {
    return score > entry_threshold && mom_fast > 0.0 &&
           mom_slow > strategy::kSlowMomLongRegime && book > strategy::kMinBookImb &&
           flow > strategy::kMinFlowImb;
}

bool entryShortOk(double mom_fast, double mom_slow, double book, double flow, double score,
                  double entry_threshold) {
    return score < -entry_threshold && mom_fast < 0.0 &&
           mom_slow < strategy::kSlowMomShortRegime && book < -strategy::kMinBookImb &&
           flow < -strategy::kMinFlowImb;
}

// Target position: -1 short, 0 flat, +1 long (exit rules + hysteresis).
int TradeProcessor::computeDesiredSide(const MarketSnapshot& snapshot) const {
    const double mid = snapshot.midpoint;
    const double entry = trading_.signal_threshold;
    const double mom_fast = momentumAtLookback(strategy::kFastMomentumTicks);
    const double mom_slow = momentumAtLookback(strategy::kSlowMomentumTicks);
    const double book = deepBookImbalance(snapshot);
    const double flow = flowImbalance(snapshot);
    const double score = compositeScore(snapshot);
    const double ur = unrealizedReturnPct(mid);

    if (trading_.position_side == 0 && snapshot.spread > strategy::kMaxEntrySpread) {
        return 0;
    }

    if (trading_.position_side == 1) {
        if (mom_slow < strategy::kSlowMomShortRegime) {
            return 0;
        }
        if (ur <= -strategy::kStopLossPct || ur >= strategy::kTakeProfitPct) {
            return 0;
        }
        if (entryShortOk(mom_fast, mom_slow, book, flow, score, entry)) {
            return -1;
        }
        if (score < strategy::kExitScore || mom_slow < 0.0 || book < 0.0) {
            return 0;
        }
        return 1;
    }

    if (trading_.position_side == -1) {
        if (mom_slow > strategy::kSlowMomLongRegime) {
            return 0;
        }
        if (ur <= -strategy::kStopLossPct || ur >= strategy::kTakeProfitPct) {
            return 0;
        }
        if (entryLongOk(mom_fast, mom_slow, book, flow, score, entry)) {
            return 1;
        }
        if (score > -strategy::kExitScore || mom_slow > 0.0 || book > 0.0) {
            return 0;
        }
        return -1;
    }

    if (entryLongOk(mom_fast, mom_slow, book, flow, score, entry)) {
        return 1;
    }
    if (entryShortOk(mom_fast, mom_slow, book, flow, score, entry)) {
        return -1;
    }
    return 0;
}

void TradeProcessor::chargeFee(double notional) {
    const double fee = std::abs(notional) * trading_.fee_rate;
    trading_.cash -= fee;
    trading_.total_fees += fee;
}

bool TradeProcessor::canAffordLeg(double mid, int side) const {
    if (side == 0) {
        return true;
    }
    const double qty = static_cast<double>(side) * trading_.position_size;
    const double notional = std::abs(qty * mid);
    const double fee = notional * trading_.fee_rate;
    if (side > 0) {
        return trading_.cash >= notional + fee;
    }
    return trading_.cash + notional >= fee;
}

bool TradeProcessor::canChangePosition() const {
    return trading_.position_side == 0 ||
           trading_.ticks_in_position >= trading_.min_hold_ticks;
}

void TradeProcessor::setPosition(int target_side, double mid) {
    if (target_side == trading_.position_side) {
        return;
    }

    if (trading_.position_side != 0) {
        const double qty = trading_.position;
        const double notional = std::abs(qty * mid);
        const double pnl = qty * (mid - trading_.entry_mid);
        trading_.cash += qty * mid;
        chargeFee(notional);
        trading_.position = 0.0;
        trading_.position_side = 0;
        trading_.realized_pnl += pnl;
        trading_.last_trade_pnl = pnl - std::abs(notional) * trading_.fee_rate;
        ++trading_.trades;
        if (trading_.last_trade_pnl > 0.0) {
            ++trading_.winning_trades;
        }
    }

    if (target_side != 0) {
        const double qty = static_cast<double>(target_side) * trading_.position_size;
        const double notional = std::abs(qty * mid);
        trading_.cash -= qty * mid;
        chargeFee(notional);
        trading_.position = qty;
        trading_.position_side = target_side;
        trading_.entry_mid = mid;
        trading_.ticks_in_position = 0;
    }

    updateDrawdown(markEquity(mid));

    if (markEquity(mid) <= 0.0) {
        trading_.trading_halted = true;
    }
}

void TradeProcessor::finalizePosition(double mid) {
    if (trading_.position_side != 0) {
        setPosition(0, mid);
    }
}

// Execute one tick: update ring buffer, apply desired side, flip long<->short via flat.
void TradeProcessor::runStrategy(const MarketSnapshot& snapshot) {
    const double mid = snapshot.midpoint;
    if (mid <= 0.0) {
        return;
    }

    if (trading_.position_side != 0) {
        ++trading_.ticks_in_position;
    }

    if (trading_.trading_halted) {
        if (trading_.position_side != 0 && canChangePosition()) {
            setPosition(0, mid);
        }
        return;
    }

    pushMidPrice(mid);
    updateDrawdown(markEquity(mid));

    if (snapshot.spread > trading_.max_spread) {
        return;
    }

    if (trading_.position_side == 0 && trading_.queued_side != 0) {
        if (snapshot.spread <= strategy::kMaxEntrySpread &&
            canAffordLeg(mid, trading_.queued_side)) {
            setPosition(trading_.queued_side, mid);
        }
        trading_.queued_side = 0;
    }

    const int desired = computeDesiredSide(snapshot);

    if (desired != 0) {
        ++trading_.ticks_with_signal;
    }

    if (desired == 0) {
        trading_.queued_side = 0;
        if (canChangePosition()) {
            setPosition(0, mid);
        }
        return;
    }

    // Opposite signal: close first, enter on next tick (queued_side).
    if (trading_.position_side != 0 && desired != trading_.position_side) {
        if (!canChangePosition()) {
            return;
        }
        setPosition(0, mid);
        trading_.queued_side = desired;
        return;
    }

    if (desired != trading_.position_side) {
        if (!canChangePosition()) {
            return;
        }
        if (!canAffordLeg(mid, desired)) {
            return;
        }
        setPosition(desired, mid);
    }
    trading_.queued_side = 0;
}

void TradeProcessor::updateStats(const MarketSnapshot& snapshot) {
    if (stats_.symbol.empty()) {
        stats_.symbol = snapshot.symbol;
    }

    ++stats_.tick_count;

    if (stats_.tick_count == 1) {
        stats_.first_system_time = snapshot.system_time;
    }
    stats_.last_system_time = snapshot.system_time;

    stats_.min_midpoint = std::min(stats_.min_midpoint, snapshot.midpoint);
    stats_.max_midpoint = std::max(stats_.max_midpoint, snapshot.midpoint);
    stats_.min_spread = std::min(stats_.min_spread, snapshot.spread);
    stats_.max_spread = std::max(stats_.max_spread, snapshot.spread);
}

void TradeProcessor::printTradingReport(const char* title) const {
    const double mid = latest_ ? latest_->midpoint : 0.0;
    const double equity = trading_.cash + trading_.position * mid;
    const double net_pnl = equity - trading_.initial_cash;
    const double win_rate = trading_.trades > 0
                                ? 100.0 * static_cast<double>(trading_.winning_trades) /
                                      static_cast<double>(trading_.trades)
                                : 0.0;

    std::ostringstream oss;
    oss << '\n'
        << "========== SESSION REPORT: " << title << " ==========\n"
        << std::fixed << std::setprecision(4)
        << "Symbol           : " << stats_.symbol << '\n'
        << "Ticks processed  : " << stats_.tick_count << '\n'
        << "Time range       : " << stats_.first_system_time << " .. "
        << stats_.last_system_time << '\n'
        << "Mid range        : " << stats_.min_midpoint << " .. " << stats_.max_midpoint
        << '\n'
        << "Spread range     : " << stats_.min_spread << " .. " << stats_.max_spread
        << '\n'
        << "-------------------------------------------\n"
        << "Initial cash     : " << trading_.initial_cash
        << '\n'
        << "Final equity     : " << equity << '\n'
        << "Net PnL          : " << net_pnl << '\n'
        << "Gross trade PnL  : " << trading_.realized_pnl << '\n'
        << "Total fees       : " << trading_.total_fees << " (rate "
        << (trading_.fee_rate * 100.0) << "%/leg)\n"
        << "Net PnL (gross-fees): " << (trading_.realized_pnl - trading_.total_fees)
        << '\n'
        << "Final net PnL    : " << net_pnl << '\n'
        << "Trading halted   : " << (trading_.trading_halted ? "yes" : "no") << '\n'
        << "Min hold ticks   : " << trading_.min_hold_ticks << '\n'
        << "Open position    : " << trading_.position << " BTC (side "
        << trading_.position_side << ")\n"
        << "Trades           : " << trading_.trades << '\n'
        << "Winning trades   : " << trading_.winning_trades << " (" << win_rate
        << "%)\n"
        << "Signal ticks     : " << trading_.ticks_with_signal << '\n'
        << "Max drawdown     : " << (trading_.max_drawdown * 100.0) << "%\n"
        << "Strategy         : dual mom + book + flow; spread<="
        << strategy::kMaxEntrySpread << " entry; slow-mom regime\n"
        << "Threshold        : " << trading_.signal_threshold
        << " | max spread: " << trading_.max_spread
        << " | flip via flat: yes\n"
        << "============================================\n";
    util::logInfo(oss.str());
}

void TradeProcessor::onConnected(const std::vector<std::string>& available_symbols) {
    resetSession();

    util::logInfo(
        "Strategy: trend+flow entry, spread<=" +
        std::to_string(strategy::kMaxEntrySpread) +
        ", long if slow mom>0.05%, short if slow mom<-0.05%.");
    util::logInfo("Available symbols:");
    for (const auto& symbol : available_symbols) {
        util::logInfo("  - " + symbol);
    }
    util::logInfo("Faster replay: UDS_REPLAY_SPEED=50 ./build/server");
}

void TradeProcessor::onMarketUpdate(const MarketSnapshot& snapshot) {
    latest_ = snapshot;
    updateStats(snapshot);
    runStrategy(snapshot);

    if (progress_log_interval_ > 0 &&
        (stats_.tick_count == 1 || stats_.tick_count % progress_log_interval_ == 0)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << '[' << stats_.symbol << "] tick "
            << stats_.tick_count << " mid=" << snapshot.midpoint
            << " pos=" << trading_.position_side;
        util::logInfo(oss.str());
    }
}

void TradeProcessor::onReplayDone(const std::string& symbol, std::uint64_t total_updates) {
    if (latest_) {
        finalizePosition(latest_->midpoint);
    }
    printTradingReport(("REPLAY " + symbol).c_str());
    util::logInfo("Server replay total for " + symbol + ": " + std::to_string(total_updates) +
                  " updates");

    stats_.tick_count = 0;
    resetTrading();
}

void TradeProcessor::onError(const std::string& message) {
    ++stats_.error_count;
    util::logError("Server error (" + std::to_string(stats_.error_count) + "): " + message);
}

void TradeProcessor::onDisconnected() {
    if (latest_) {
        finalizePosition(latest_->midpoint);
    }
    if (stats_.tick_count > 0) {
        printTradingReport("DISCONNECTED");
    }
    stats_.active = false;
}

}  // namespace uds

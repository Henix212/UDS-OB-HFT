#pragma once

#include "order.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace uds {

// Paper-trading strategy (signal logic in trade_processor.cpp).
class TradeProcessor {
public:
    void onConnected(const std::vector<std::string>& available_symbols);
    void onMarketUpdate(const MarketSnapshot& snapshot);
    void onReplayDone(const std::string& symbol, std::uint64_t total_updates);
    void onError(const std::string& message);
    void onDisconnected();

    const std::optional<MarketSnapshot>& latestSnapshot() const { return latest_; }

private:
    struct SessionStats {
        bool active{false};
        std::string symbol;
        std::uint64_t tick_count{0};
        std::uint64_t first_system_time{0};
        std::uint64_t last_system_time{0};
        double min_midpoint{std::numeric_limits<double>::max()};
        double max_midpoint{std::numeric_limits<double>::lowest()};
        double min_spread{std::numeric_limits<double>::max()};
        double max_spread{std::numeric_limits<double>::lowest()};
        std::uint32_t error_count{0};
    };

    struct TradingState {
        // Paper account and open position (BTC qty; side -1/0/+1).
        double initial_cash{1'000'000.0};
        double cash{1'000'000.0};
        double position{0.0};
        double position_size{1.0};
        double entry_mid{0.0};
        int position_side{0};

        std::uint64_t ticks_with_signal{0};
        std::uint64_t trades{0};
        std::uint64_t winning_trades{0};
        double realized_pnl{0.0};
        double total_fees{0.0};
        double fee_rate{0.0005};
        double last_trade_pnl{0.0};
        double peak_equity{0.0};
        double max_drawdown{0.0};
        double last_equity{0.0};
        int queued_side{0};  // deferred entry after flat (long<->short flip)
        int ticks_in_position{0};
        int min_hold_ticks{30};
        bool trading_halted{false};

        static constexpr std::size_t kMidWindow = 64;
        std::array<double, kMidWindow> mid_ring{};
        std::size_t mid_ring_head{0};
        std::size_t mid_ring_count{0};

        double momentum_weight{0.55};
        double imbalance_weight{0.45};
        double signal_threshold{0.15};
        double max_spread{2.0};
        int lookback_ticks{5};
    };

    void loadConfig();
    void resetSession();
    void resetTrading();
    void updateStats(const MarketSnapshot& snapshot);
    void runStrategy(const MarketSnapshot& snapshot);

    void pushMidPrice(double mid);
    double momentumAtLookback(int lookback_ticks) const;
    double deepBookImbalance(const MarketSnapshot& snapshot) const;
    double flowImbalance(const MarketSnapshot& snapshot) const;
    double compositeScore(const MarketSnapshot& snapshot) const;
    int computeDesiredSide(const MarketSnapshot& snapshot) const;
    double unrealizedReturnPct(double mid) const;

    void chargeFee(double notional);
    bool canAffordLeg(double mid, int side) const;
    bool canChangePosition() const;
    void setPosition(int target_side, double mid);
    double markEquity(double mid) const;
    void updateDrawdown(double equity);
    void finalizePosition(double mid);
    void printTradingReport(const char* title) const;

    std::optional<MarketSnapshot> latest_;
    SessionStats stats_;
    TradingState trading_;
    std::uint64_t progress_log_interval_{0};
};

}  // namespace uds

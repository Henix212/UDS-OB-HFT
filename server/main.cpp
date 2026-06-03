// Market data replay server: load CSVs, stream LOB updates over TCP.

#include "include/market_data_feed.hpp"
#include "include/session_manager.hpp"

#include "config.hpp"
#include "utilities.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) {
    g_running = false;
}

}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const std::string data_dir =
        uds::util::envOrDefault("UDS_DATA_DIR", uds::config::kDefaultDataDir);
    const std::uint16_t port =
        uds::util::envOrDefaultPort("UDS_SERVER_PORT", uds::config::kDefaultPort);
    const double replay_speed = uds::util::envOrDefaultDouble(
        "UDS_REPLAY_SPEED", uds::config::kDefaultReplaySpeed);
    const bool loop_forever = uds::util::envOrDefault("UDS_LOOP", "1") != "0";

    // Listen before CSV load so clients can connect while data is loading.
    uds::SessionManager sessions;
    if (!sessions.start(port)) {
        return 1;
    }

    const std::vector<std::string> symbols = {
        uds::config::kSymbolBtc,
        uds::config::kSymbolEth,
        uds::config::kSymbolAda,
    };

    uds::util::logInfo("Loading market data from " + data_dir + "...");

    std::vector<uds::MarketDataFeed> feeds;
    feeds.reserve(symbols.size());

    for (const auto& symbol : symbols) {
        feeds.emplace_back(symbol);
        const std::string csv_path = uds::config::csvPathForSymbol(symbol, data_dir);
        if (!feeds.back().loadFromCsv(csv_path)) {
            uds::util::logError("Feed " + symbol + " unavailable (" + csv_path + ")");
        }
    }

    const bool has_any_feed =
        std::any_of(feeds.begin(), feeds.end(), [](const uds::MarketDataFeed& feed) {
            return feed.hasData();
        });
    if (!has_any_feed) {
        uds::util::logError(
            "No data loaded. Run data_fetcher.py and place CSV files in " +
            data_dir);
        sessions.stop();
        return 1;
    }

    for (auto& feed : feeds) {
        if (feed.hasData()) {
            sessions.registerFeed(feed.symbol(), &feed);
        }
    }

    sessions.runReplayLoop(replay_speed, loop_forever);
    uds::util::logInfo("Market data replay started (speed x" + std::to_string(replay_speed) +
                       ")");

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    uds::util::logInfo("Shutting down server...");
    sessions.stop();
    return 0;
}

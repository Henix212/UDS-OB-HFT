// Paper-trading client: connect, subscribe, run strategy on each tick.

#include "include/client_config.hpp"
#include "include/market_client.hpp"
#include "include/trade_processor.hpp"

#include "utilities.hpp"

#include <csignal>
#include <memory>

namespace {

std::unique_ptr<uds::client::MarketClient> g_client;

void handleSignal(int) {
    if (g_client) {
        g_client->stop();
    }
}

}  // namespace

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const auto config = uds::client::loadFromEnv();
    uds::TradeProcessor processor;

    g_client = std::make_unique<uds::client::MarketClient>(config, processor);

    if (!g_client->connect()) {
        return 1;
    }

    uds::util::logInfo("Waiting for server welcome on " + config.symbol + "...");
    g_client->run();
    return 0;
}

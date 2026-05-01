#include "include/market_data_feed.hpp"
#include <iostream>
#include <csignal>

static uds_hft::MarketDataFeed* g_feed = nullptr;

void signal_handler(int) {
    std::cout << "\n[main] Signal reçu, arrêt propre...\n";
    if (g_feed) {
        g_feed->print_stats();
        g_feed->stop();
    }
    std::exit(0);
}

int main() {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        uds_hft::MarketDataFeed feed("/tmp/uds_ob_hft.sock");
        g_feed = &feed;

        feed.start();

        std::cout << "[main] Serveur actif. Ctrl+C pour arrêter.\n";

        while (true)
            std::this_thread::sleep_for(std::chrono::seconds(1));

    } catch (const std::exception& e) {
        std::cerr << "[main] Erreur fatale : " << e.what() << "\n";
        return 1;
    }
    return 0;
}
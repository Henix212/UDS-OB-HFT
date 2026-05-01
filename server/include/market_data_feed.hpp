#pragma once
#include <string>
#include <thread>
#include <atomic>
#include "session_manager.hpp"
#include "../../common/protocol.hpp"

namespace uds_hft {

class MarketDataFeed {
public:
    explicit MarketDataFeed(const std::string& socket_path,
                            int heartbeat_interval_ms = 1000);
    ~MarketDataFeed();

    void start();
    void stop();

    // Point d'entrée pour le feeder Python ou tout producteur interne
    void push_snapshot(const MarketSnapshot& snap);

    void print_stats() const { session_mgr_.print_stats(); }

private:
    void accept_loop();

    std::string      socket_path_;
    int              server_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread      accept_thread_;

    SessionManager   session_mgr_;
};

} // namespace uds_hft
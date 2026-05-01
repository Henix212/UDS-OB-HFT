#pragma once
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include "../../common/protocol.hpp"

namespace uds_hft {

enum class Symbol    : uint8_t { ADA=0, BTC=1, ETH=2, ALL=255 };
enum class Timeframe : uint8_t { S1=0, MIN1=1, MIN5=2, ALL=255 };

struct Session {
    int      fd;
    Symbol   symbol;
    Timeframe timeframe;

    uint64_t snapshots_sent{0};
    uint64_t snapshots_dropped{0};  // client trop lent
    std::chrono::steady_clock::time_point last_heartbeat;

    bool ready{false};
};

class SessionManager {
public:
    explicit SessionManager(int heartbeat_interval_ms = 1000);
    ~SessionManager();

    void on_connect(int fd);

    int  dispatch(const MarketSnapshot& snap);

    void on_disconnect(int fd);

    void print_stats() const;

private:
    void heartbeat_loop();
    bool do_handshake(Session& sess);
    bool send_all(int fd, const void* buf, size_t len);

    std::unordered_map<int, Session> sessions_;
    mutable std::mutex               sessions_mtx_;

    std::thread      heartbeat_thread_;
    std::atomic<bool> running_{true};
    int              heartbeat_interval_ms_;
};

} // namespace uds_hft
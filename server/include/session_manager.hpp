#pragma once

// TCP server: accept clients, subscriptions, broadcast LOB ticks, replay threads.

#include "market_data_feed.hpp"
#include "protocol.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace uds {

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    void registerFeed(const std::string& symbol, MarketDataFeed* feed);
    bool start(std::uint16_t port);
    void stop();

    void runReplayLoop(double replay_speed, bool loop_forever);

private:
    struct ClientSession {
        int socket_fd{-1};
        std::string subscribed_symbol;  // active symbol filter for broadcast
        std::string pending_symbol;     // set before CSV feed is registered
    };

    void acceptLoop();
    void handleClient(int client_fd);
    bool readExact(int fd, std::uint8_t* buffer, std::size_t size);
    bool writeAll(int fd, const std::uint8_t* buffer, std::size_t size);
    void sendPacket(int fd, protocol::MessageType type, const void* payload, std::uint32_t size);
    void removeClient(int client_fd);
    void broadcast(const MarketSnapshot& snapshot);
    bool subscribeClient(int client_fd, const std::string& symbol);
    void unsubscribeClient(int client_fd);
    void activatePendingSubscriptions(const std::string& symbol);
    static bool isKnownSymbol(const std::string& symbol);

    std::uint16_t port_{0};
    int listen_fd_{-1};
    std::atomic<bool> running_{false};

    std::thread accept_thread_;
    std::thread replay_thread_;

    std::mutex clients_mutex_;
    std::vector<ClientSession> clients_;

    std::mutex feeds_mutex_;
    std::unordered_map<std::string, MarketDataFeed*> feeds_;
};

}  // namespace uds

// TCP session handling and per-symbol replay worker threads.

#include "include/session_manager.hpp"

#include "utilities.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace uds {

SessionManager::SessionManager() = default;

SessionManager::~SessionManager() {
    stop();
}

bool SessionManager::isKnownSymbol(const std::string& symbol) {
    return symbol == config::kSymbolBtc || symbol == config::kSymbolEth ||
           symbol == config::kSymbolAda;
}

void SessionManager::registerFeed(const std::string& symbol, MarketDataFeed* feed) {
    {
        std::lock_guard<std::mutex> lock(feeds_mutex_);
        feeds_[symbol] = feed;
    }
    activatePendingSubscriptions(symbol);
}

// Clients may subscribe before CSV load finishes; activate when feed registers.
void SessionManager::activatePendingSubscriptions(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
        if (client.pending_symbol == symbol) {
            client.subscribed_symbol = symbol;
            client.pending_symbol.clear();
        }
    }
}

bool SessionManager::start(std::uint16_t port) {
    if (running_.exchange(true)) {
        return true;
    }

    port_ = port;
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        util::logError("socket() failed");
        running_ = false;
        return false;
    }

    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        util::logError("bind() failed on port " + std::to_string(port_));
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }

    if (::listen(listen_fd_, 16) < 0) {
        util::logError("listen() failed");
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }

    accept_thread_ = std::thread(&SessionManager::acceptLoop, this);
    util::logInfo("TCP server listening on port " + std::to_string(port_));
    return true;
}

void SessionManager::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    if (replay_thread_.joinable()) {
        replay_thread_.join();
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
        if (client.socket_fd >= 0) {
            ::shutdown(client.socket_fd, SHUT_RDWR);
            ::close(client.socket_fd);
            client.socket_fd = -1;
        }
    }
    clients_.clear();
}

void SessionManager::acceptLoop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd =
            ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (running_.load()) {
                util::logError("accept() failed");
            }
            break;
        }

        std::thread(&SessionManager::handleClient, this, client_fd).detach();
    }
}

bool SessionManager::readExact(int fd, std::uint8_t* buffer, std::size_t size) {
    std::size_t received = 0;
    while (received < size) {
        const ssize_t n = ::recv(fd, buffer + received, size - received, 0);
        if (n <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(n);
    }
    return true;
}

bool SessionManager::writeAll(int fd, const std::uint8_t* buffer, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd, buffer + sent, size - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void SessionManager::sendPacket(int fd, protocol::MessageType type, const void* payload,
                              std::uint32_t size) {
    const auto packet = protocol::encodePacket(type, payload, size);
    writeAll(fd, packet.data(), packet.size());
}

void SessionManager::removeClient(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
                       [client_fd](const ClientSession& session) {
                           if (session.socket_fd == client_fd) {
                               if (session.socket_fd >= 0) {
                                   ::close(session.socket_fd);
                               }
                               return true;
                           }
                           return false;
                       }),
        clients_.end());
}

bool SessionManager::subscribeClient(int client_fd, const std::string& symbol) {
    if (!isKnownSymbol(symbol)) {
        return false;
    }

    bool feed_ready = false;
    {
        std::lock_guard<std::mutex> feeds_lock(feeds_mutex_);
        feed_ready = feeds_.find(symbol) != feeds_.end();
    }

    std::lock_guard<std::mutex> clients_lock(clients_mutex_);
    for (auto& client : clients_) {
        if (client.socket_fd == client_fd) {
            if (feed_ready) {
                client.subscribed_symbol = symbol;
                client.pending_symbol.clear();
            } else {
                client.pending_symbol = symbol;
                client.subscribed_symbol.clear();
            }
            return true;
        }
    }
    return false;
}

void SessionManager::unsubscribeClient(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
        if (client.socket_fd == client_fd) {
            client.subscribed_symbol.clear();
            client.pending_symbol.clear();
            return;
        }
    }
}

void SessionManager::handleClient(int client_fd) {
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.push_back(ClientSession{client_fd, {}, {}});
    }

    protocol::WelcomePayload welcome{};
    welcome.port = port_;
    std::strncpy(welcome.symbols[0], config::kSymbolBtc, config::kMaxSymbolLen - 1);
    std::strncpy(welcome.symbols[1], config::kSymbolEth, config::kMaxSymbolLen - 1);
    std::strncpy(welcome.symbols[2], config::kSymbolAda, config::kMaxSymbolLen - 1);
    sendPacket(client_fd, protocol::MessageType::Welcome, &welcome, sizeof(welcome));

    while (running_.load()) {
        protocol::PacketHeader header{};
        if (!readExact(client_fd, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
            break;
        }
        if (std::memcmp(header.magic, protocol::kMagic, sizeof(protocol::kMagic)) != 0) {
            break;
        }

        std::vector<std::uint8_t> payload(header.payload_size);
        if (header.payload_size > 0 &&
            !readExact(client_fd, payload.data(), header.payload_size)) {
            break;
        }

        switch (header.type) {
            case protocol::MessageType::Subscribe: {
                if (payload.size() < sizeof(protocol::SubscribePayload)) {
                    break;
                }
                protocol::SubscribePayload request{};
                std::memcpy(&request, payload.data(), sizeof(request));
                const std::string symbol(request.symbol);
                if (!subscribeClient(client_fd, symbol)) {
                    protocol::ErrorPayload error{};
                    const std::string msg = "Unknown symbol: " + symbol;
                    std::strncpy(error.message, msg.c_str(), sizeof(error.message) - 1);
                    sendPacket(client_fd, protocol::MessageType::Error, &error, sizeof(error));
                }
                break;
            }
            case protocol::MessageType::Unsubscribe:
                unsubscribeClient(client_fd);
                break;
            case protocol::MessageType::Heartbeat:
                sendPacket(client_fd, protocol::MessageType::Heartbeat, nullptr, 0);
                break;
            default:
                break;
        }
    }

    removeClient(client_fd);
}

// Send one MarketUpdate to every client subscribed to snapshot.symbol.
void SessionManager::broadcast(const MarketSnapshot& snapshot) {
    const auto payload = protocol::snapshotToPayload(snapshot);
    const auto packet =
        protocol::encodePacket(protocol::MessageType::MarketUpdate, &payload, sizeof(payload));

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
        if (client.subscribed_symbol != snapshot.symbol) {
            continue;
        }
        if (!writeAll(client.socket_fd, packet.data(), packet.size())) {
            ::close(client.socket_fd);
            client.socket_fd = -1;
        }
    }

    clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                  [](const ClientSession& session) {
                                      return session.socket_fd < 0;
                                  }),
                   clients_.end());
}

void SessionManager::runReplayLoop(double replay_speed, bool loop_forever) {
    replay_thread_ = std::thread([this, replay_speed, loop_forever]() {
        const double speed = replay_speed > 0.0 ? replay_speed : 1.0;

        std::vector<MarketDataFeed*> active_feeds;
        {
            std::lock_guard<std::mutex> lock(feeds_mutex_);
            for (auto& [symbol, feed] : feeds_) {
                (void)symbol;
                if (feed != nullptr && feed->hasData()) {
                    active_feeds.push_back(feed);
                }
            }
        }

        if (active_feeds.empty()) {
            util::logError("No market data feeds registered");
            return;
        }

        std::vector<std::thread> workers;
        workers.reserve(active_feeds.size());

        for (MarketDataFeed* feed : active_feeds) {
            workers.emplace_back([this, feed, speed, loop_forever]() {
                do {
                    feed->reset();
                    while (running_.load()) {
                        const std::uint64_t delay = feed->peekNextDelayMicros();
                        if (delay > 0) {
                            util::sleepMicros(static_cast<std::uint64_t>(
                                static_cast<double>(delay) / speed));
                        }

                        MarketSnapshot snapshot;
                        if (!feed->next(snapshot)) {
                            protocol::ReplayDonePayload done{};
                            std::strncpy(done.symbol, feed->symbol().c_str(),
                                         sizeof(done.symbol) - 1);
                            done.total_updates = feed->size();

                            std::lock_guard<std::mutex> lock(clients_mutex_);
                            for (auto& client : clients_) {
                                if (client.subscribed_symbol != feed->symbol()) {
                                    continue;
                                }
                                sendPacket(client.socket_fd, protocol::MessageType::ReplayDone,
                                           &done, sizeof(done));
                            }
                            break;
                        }
                        broadcast(snapshot);
                    }
                } while (loop_forever && running_.load());
            });
        }

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    });
}

}  // namespace uds

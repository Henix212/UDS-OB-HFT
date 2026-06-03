// Blocking read loop: decode packets and invoke TradeProcessor callbacks.

#include "include/market_client.hpp"

#include "protocol.hpp"
#include "utilities.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <vector>

namespace uds::client {

MarketClient::MarketClient(ClientConfig config, TradeProcessor& processor)
    : config_(std::move(config)), processor_(processor) {}

bool MarketClient::connect() {
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        util::logError("socket() failed");
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);

    if (::inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) <= 0) {
        util::logError("Invalid host: " + config_.host);
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        util::logError("connect() failed to " + config_.host + ":" +
                       std::to_string(config_.port));
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    util::logInfo("Connected to " + config_.host + ":" + std::to_string(config_.port));
    running_ = true;
    return true;
}

bool MarketClient::subscribe(const std::string& symbol) {
    protocol::SubscribePayload payload{};
    std::strncpy(payload.symbol, symbol.c_str(), sizeof(payload.symbol) - 1);
    sendPacket(protocol::MessageType::Subscribe, &payload, sizeof(payload));
    util::logInfo("Subscribed to " + symbol);
    return true;
}

void MarketClient::stop() {
    running_ = false;
    if (socket_fd_ >= 0) {
        ::shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool MarketClient::readExact(std::uint8_t* buffer, std::size_t size) {
    std::size_t received = 0;
    while (received < size && running_.load()) {
        const ssize_t n = ::recv(socket_fd_, buffer + received, size - received, 0);
        if (n <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(n);
    }
    return received == size;
}

bool MarketClient::writeAll(const std::uint8_t* buffer, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(socket_fd_, buffer + sent, size - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void MarketClient::sendPacket(protocol::MessageType type, const void* payload,
                            std::uint32_t size) {
    const auto packet = protocol::encodePacket(type, payload, size);
    writeAll(packet.data(), packet.size());
}

bool MarketClient::handlePacket(const protocol::PacketHeader& header,
                                const std::uint8_t* payload) {
    switch (header.type) {
        case protocol::MessageType::Welcome: {
            if (header.payload_size < sizeof(protocol::WelcomePayload)) {
                return false;
            }
            protocol::WelcomePayload welcome{};
            std::memcpy(&welcome, payload, sizeof(welcome));

            std::vector<std::string> symbols;
            for (const auto& entry : welcome.symbols) {
                const std::string symbol(entry);
                if (!symbol.empty()) {
                    symbols.push_back(symbol);
                }
            }
            processor_.onConnected(symbols);
            // Subscribe after Welcome so server knows available symbols.
            subscribe(config_.symbol);
            return true;
        }
        case protocol::MessageType::MarketUpdate: {
            if (header.payload_size < sizeof(protocol::MarketUpdatePayload)) {
                return false;
            }
            protocol::MarketUpdatePayload update{};
            std::memcpy(&update, payload, sizeof(update));
            processor_.onMarketUpdate(protocol::payloadToSnapshot(update));
            return true;
        }
        case protocol::MessageType::ReplayDone: {
            if (header.payload_size < sizeof(protocol::ReplayDonePayload)) {
                return false;
            }
            protocol::ReplayDonePayload done{};
            std::memcpy(&done, payload, sizeof(done));
            processor_.onReplayDone(done.symbol, done.total_updates);
            return true;
        }
        case protocol::MessageType::Error: {
            if (header.payload_size < sizeof(protocol::ErrorPayload)) {
                return false;
            }
            protocol::ErrorPayload error{};
            std::memcpy(&error, payload, sizeof(error));
            processor_.onError(error.message);
            return true;
        }
        case protocol::MessageType::Heartbeat:
            return true;
        default:
            return true;
    }
}

void MarketClient::run() {
    while (running_.load()) {
        protocol::PacketHeader header{};
        if (!readExact(reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) {
            break;
        }
        if (std::memcmp(header.magic, protocol::kMagic, sizeof(protocol::kMagic)) != 0) {
            util::logError("Invalid packet magic");
            break;
        }
        if (header.version != protocol::kProtocolVersion) {
            util::logError("Unsupported protocol version");
            break;
        }

        std::vector<std::uint8_t> payload(header.payload_size);
        if (header.payload_size > 0 && !readExact(payload.data(), header.payload_size)) {
            break;
        }

        if (!handlePacket(header, payload.data())) {
            util::logError("Failed to handle packet");
            break;
        }
    }

    running_ = false;
    processor_.onDisconnected();
}

}  // namespace uds::client

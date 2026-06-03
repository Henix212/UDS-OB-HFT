#pragma once

// TCP client: read UDSO packets and forward events to TradeProcessor.

#include "client_config.hpp"
#include "protocol.hpp"
#include "trade_processor.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace uds::client {

class MarketClient {
public:
    explicit MarketClient(ClientConfig config, TradeProcessor& processor);

    bool connect();
    bool subscribe(const std::string& symbol);
    void run();
    void stop();

private:
    bool readExact(std::uint8_t* buffer, std::size_t size);
    bool writeAll(const std::uint8_t* buffer, std::size_t size);
    void sendPacket(uds::protocol::MessageType type, const void* payload, std::uint32_t size);
    bool handlePacket(const uds::protocol::PacketHeader& header, const std::uint8_t* payload);

    ClientConfig config_;
    TradeProcessor& processor_;
    int socket_fd_{-1};
    std::atomic<bool> running_{false};
};

}  // namespace uds::client

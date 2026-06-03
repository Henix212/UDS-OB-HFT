#pragma once

// Binary wire format between server and client (magic "UDSO", version 1).

#include "order.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace uds::protocol {

inline constexpr char kMagic[4] = {'U', 'D', 'S', 'O'};
inline constexpr std::uint8_t kProtocolVersion = 1;

// Message types on the TCP stream (header + payload).
enum class MessageType : std::uint8_t {
    Welcome = 0,
    Subscribe = 1,
    Unsubscribe = 2,
    MarketUpdate = 3,
    Heartbeat = 4,
    ReplayDone = 5,
    Error = 255,
};

#pragma pack(push, 1)

struct PacketHeader {
    char magic[4]{kMagic[0], kMagic[1], kMagic[2], kMagic[3]};
    std::uint8_t version{kProtocolVersion};
    MessageType type{MessageType::Heartbeat};
    std::uint32_t payload_size{0};
};

struct WelcomePayload {
    std::uint16_t port{0};
    char symbols[3][config::kMaxSymbolLen]{};
};

struct SubscribePayload {
    char symbol[config::kMaxSymbolLen]{};
};

struct MarketUpdatePayload {
    std::uint64_t sequence{0};
    std::uint64_t system_time{0};
    char symbol[config::kMaxSymbolLen]{};
    double midpoint{0.0};
    double spread{0.0};
    std::uint32_t buys{0};
    std::uint32_t sells{0};
    BookLevel levels[config::kOrderBookLevels]{};
};

struct ReplayDonePayload {
    char symbol[config::kMaxSymbolLen]{};
    std::uint64_t total_updates{0};
};

struct ErrorPayload {
    char message[256]{};
};

#pragma pack(pop)

inline std::vector<std::uint8_t> encodePacket(MessageType type,
                                              const void* payload,
                                              std::uint32_t payload_size) {
    PacketHeader header{};
    header.type = type;
    header.payload_size = payload_size;

    std::vector<std::uint8_t> buffer(sizeof(PacketHeader) + payload_size);
    std::memcpy(buffer.data(), &header, sizeof(PacketHeader));
    if (payload != nullptr && payload_size > 0) {
        std::memcpy(buffer.data() + sizeof(PacketHeader), payload, payload_size);
    }
    return buffer;
}

inline bool decodeHeader(const std::uint8_t* data, std::size_t size, PacketHeader& header) {
    if (size < sizeof(PacketHeader)) {
        return false;
    }
    std::memcpy(&header, data, sizeof(PacketHeader));
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    if (header.version != kProtocolVersion) {
        return false;
    }
    return true;
}

inline MarketSnapshot payloadToSnapshot(const MarketUpdatePayload& payload) {
    MarketSnapshot snapshot;
    snapshot.sequence = payload.sequence;
    snapshot.system_time = payload.system_time;
    snapshot.symbol = payload.symbol;
    snapshot.midpoint = payload.midpoint;
    snapshot.spread = payload.spread;
    snapshot.buys = payload.buys;
    snapshot.sells = payload.sells;
    for (std::size_t i = 0; i < config::kOrderBookLevels; ++i) {
        snapshot.levels[i] = payload.levels[i];
    }
    return snapshot;
}

inline MarketUpdatePayload snapshotToPayload(const MarketSnapshot& snapshot) {
    MarketUpdatePayload payload{};
    payload.sequence = snapshot.sequence;
    payload.system_time = snapshot.system_time;
    std::strncpy(payload.symbol, snapshot.symbol.c_str(), sizeof(payload.symbol) - 1);
    payload.midpoint = snapshot.midpoint;
    payload.spread = snapshot.spread;
    payload.buys = snapshot.buys;
    payload.sells = snapshot.sells;
    for (std::size_t i = 0; i < config::kOrderBookLevels; ++i) {
        payload.levels[i] = snapshot.levels[i];
    }
    return payload;
}

}  // namespace uds::protocol

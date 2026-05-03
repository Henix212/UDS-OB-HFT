#pragma once
#include <cstdint>
#include <cstring>

namespace uds_hft
{

    static constexpr int DEPTH = 15;

    static constexpr uint8_t MSG_SNAPSHOT = 0x01;
    static constexpr uint8_t MSG_ORDER = 0x02;
    static constexpr uint8_t MSG_HANDSHAKE = 0xAA;
    static constexpr uint8_t MSG_HEARTBEAT = 0xBB;
    static constexpr uint8_t MSG_ACK = 0xCC;

    enum class Symbol : uint8_t
    {
        ADA = 0,
        BTC = 1,
        ETH = 2,
        ALL = 255
    };
    enum class Timeframe : uint8_t
    {
        S1 = 0,
        MIN1 = 1,
        MIN5 = 2,
        ALL = 255
    };

    struct BookSide
    {
        float distance[DEPTH];
        float notional[DEPTH];
        float cancel_notional[DEPTH];
        float limit_notional[DEPTH];
        float market_notional[DEPTH];
    };

    struct MarketSnapshot
    {
        uint8_t msg_type;
        uint8_t symbol;
        uint8_t timeframe;
        uint8_t _pad;
        int64_t system_time_us;
        double midpoint;
        double spread;
        double buys;
        double sells;
        BookSide bids;
        BookSide asks;
    };

    struct OrderRequest
    {
        uint8_t msg_type;
        uint8_t side;
        uint8_t _pad[2];
        double price;
        double quantity;
        int64_t timestamp_us;
    };

    struct HandshakeMsg
    {
        uint8_t msg_type;
        uint8_t symbol;
        uint8_t timeframe;
        uint8_t _pad;
    };

    struct MsgHeader
    {
        uint8_t msg_type;
        uint32_t payload_len;
    } __attribute__((packed));

} // namespace uds_hft
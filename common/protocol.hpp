#pragma once
#include <cstdint>
#include <cstring>

namespace uds_hft {

static constexpr int DEPTH = 15;

struct BookSide {
    float distance[DEPTH];        // bids/asks_distance_0..14
    float notional[DEPTH];        // bids/asks_notional_0..14
    float cancel_notional[DEPTH]; // bids/asks_cancel_notional_0..14
    float limit_notional[DEPTH];  // bids/asks_limit_notional_0..14
    float market_notional[DEPTH]; // bids/asks_market_notional_0..14
};

struct MarketSnapshot {
    uint8_t  msg_type;      // 0x01 = snapshot
    uint8_t  symbol;        // 0=ADA, 1=BTC, 2=ETH
    uint8_t  timeframe;     // 0=1s, 1=1min, 2=5min
    uint8_t  _pad;
    int64_t  system_time_us; // timestamp en microsecondes
    double   midpoint;
    double   spread;
    double   buys;
    double   sells;
    BookSide bids;
    BookSide asks;
};

struct OrderRequest {
    uint8_t  msg_type;   // 0x02 = order
    uint8_t  side;       // 0=buy, 1=sell
    uint8_t  _pad[2];
    double   price;
    double   quantity;
    int64_t  timestamp_us;
};

struct MsgHeader {
    uint8_t msg_type;
    uint32_t payload_len; 
} __attribute__((packed));

} // namespace uds_hft
#pragma once

// Client connection settings (host, port, symbol) from environment.

#include "config.hpp"
#include "utilities.hpp"

#include <cstdint>
#include <string>

namespace uds::client {

struct ClientConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{config::kDefaultPort};
    std::string symbol{config::kSymbolBtc};
};

inline ClientConfig loadFromEnv() {
    ClientConfig cfg;
    cfg.host = uds::util::envOrDefault("UDS_SERVER_HOST", cfg.host);
    cfg.port = uds::util::envOrDefaultPort("UDS_SERVER_PORT", cfg.port);
    cfg.symbol = uds::util::envOrDefault("UDS_SYMBOL", cfg.symbol);
    return cfg;
}

}  // namespace uds::client

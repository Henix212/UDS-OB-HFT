#pragma once

// Shared paths, symbols, and default ports for server and client.

#include <cstdint>
#include <string>

namespace uds::config {

inline constexpr std::uint16_t kDefaultPort = 9000;
inline constexpr std::size_t kOrderBookLevels = 5;
inline constexpr std::size_t kMaxSymbolLen = 8;

inline const std::string kDefaultDataDir = "data/raw_data";

inline constexpr double kDefaultReplaySpeed = 1.0;
inline constexpr std::uint64_t kDefaultReplayStartUs = 0;

inline const char* kSymbolBtc = "BTC";
inline const char* kSymbolEth = "ETH";
inline const char* kSymbolAda = "ADA";

inline std::string csvPathForSymbol(const std::string& symbol,
                                   const std::string& data_dir = kDefaultDataDir) {
    return data_dir + "/" + symbol + "_1sec.csv";
}

}  // namespace uds::config

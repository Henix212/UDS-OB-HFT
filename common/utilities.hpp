#pragma once

// Logging, env parsing, CSV line splitting, and replay timing helpers.

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace uds::util {

inline void logInfo(const std::string& msg) {
    std::cerr << "[INFO] " << msg << '\n';
}

inline void logError(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << '\n';
}

inline std::string envOrDefault(const char* name, const std::string& fallback) {
    if (const char* value = std::getenv(name)) {
        return value;
    }
    return fallback;
}

inline double envOrDefaultDouble(const char* name, double fallback) {
    if (const char* value = std::getenv(name)) {
        try {
            return std::stod(value);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

inline std::uint16_t envOrDefaultPort(const char* name, std::uint16_t fallback) {
    if (const char* value = std::getenv(name)) {
        try {
            const int port = std::stoi(value);
            if (port > 0 && port <= 65535) {
                return static_cast<std::uint16_t>(port);
            }
        } catch (...) {
        }
    }
    return fallback;
}

inline void sleepMicros(std::uint64_t micros) {
    if (micros == 0) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
}

inline std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;

    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (ch == ',' && !in_quotes) {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    fields.push_back(current);
    return fields;
}

inline std::string trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

inline double parseDouble(const std::string& value, double fallback = 0.0) {
    try {
        return std::stod(trim(value));
    } catch (...) {
        return fallback;
    }
}

inline std::uint64_t parseUint64(const std::string& value, std::uint64_t fallback = 0) {
    try {
        return static_cast<std::uint64_t>(std::stoull(trim(value)));
    } catch (...) {
        return fallback;
    }
}

inline std::uint32_t parseUint32(const std::string& value, std::uint32_t fallback = 0) {
    try {
        return static_cast<std::uint32_t>(std::stoul(trim(value)));
    } catch (...) {
        return fallback;
    }
}

}  // namespace uds::util

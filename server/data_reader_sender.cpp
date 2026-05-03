#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "../common/protocol.hpp"

namespace uds_hft
{

    struct MappedFile
    {
        const char *data{nullptr};
        size_t size{0};
        int fd{-1};

        explicit MappedFile(const std::string &path)
        {
            fd = ::open(path.c_str(), O_RDONLY);
            if (fd < 0)
                throw std::runtime_error("open() failed: " + path);

            struct stat st{};
            ::fstat(fd, &st);
            size = static_cast<size_t>(st.st_size);

            data = static_cast<const char *>(
                ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));

            if (data == MAP_FAILED)
                throw std::runtime_error("mmap() failed");

            ::madvise(const_cast<char *>(data), size, MADV_SEQUENTIAL);
        }

        ~MappedFile()
        {
            if (data && data != MAP_FAILED)
                ::munmap(const_cast<char *>(data), size);
            if (fd >= 0)
                ::close(fd);
        }

        MappedFile(const MappedFile &) = delete;
        MappedFile &operator=(const MappedFile &) = delete;
    };

    inline std::string_view next_token(const char *&ptr, const char *end)
    {
        const char *start = ptr;
        while (ptr < end && *ptr != ',' && *ptr != '\n' && *ptr != '\r')
            ++ptr;
        std::string_view tok{start, static_cast<size_t>(ptr - start)};
        if (ptr < end)
            ++ptr;
        return tok;
    }

    inline double to_double(std::string_view sv)
    {
        char buf[64];
        size_t len = std::min(sv.size(), sizeof(buf) - 1);
        std::memcpy(buf, sv.data(), len);
        buf[len] = '\0';
        return std::strtod(buf, nullptr);
    }

    inline int64_t parse_timestamp_us(std::string_view sv)
    {
        (void)sv;
        static int64_t counter = 0;
        return counter++;
    }

    bool parse_line(const char *&ptr, const char *end,
                    MarketSnapshot &snap,
                    uint8_t symbol, uint8_t timeframe)
    {
        if (ptr >= end)
            return false;

        next_token(ptr, end);

        auto ts = next_token(ptr, end);
        snap.system_time_us = parse_timestamp_us(ts);

        snap.midpoint = to_double(next_token(ptr, end));

        snap.spread = to_double(next_token(ptr, end));

        snap.buys = to_double(next_token(ptr, end));

        snap.sells = to_double(next_token(ptr, end));

        for (int i = 0; i < DEPTH; ++i)
            snap.bids.distance[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.bids.notional[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.bids.cancel_notional[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.bids.limit_notional[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.bids.market_notional[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.asks.distance[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.asks.notional[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.asks.cancel_notional[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.asks.limit_notional[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        for (int i = 0; i < DEPTH; ++i)
            snap.asks.market_notional[i] = static_cast<float>(
                to_double(next_token(ptr, end)));

        while (ptr < end && *(ptr - 1) != '\n')
            ++ptr;

        snap.msg_type = MSG_SNAPSHOT;
        snap.symbol = symbol;
        snap.timeframe = timeframe;
        snap._pad = 0;

        return true;
    }

    class FeedClient
    {
    public:
        explicit FeedClient(const std::string &socket_path,
                            uint8_t symbol, uint8_t timeframe)
        {
            fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd_ < 0)
                throw std::runtime_error("socket() failed");

            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            std::strncpy(addr.sun_path, socket_path.c_str(),
                         sizeof(addr.sun_path) - 1);

            if (::connect(fd_, reinterpret_cast<sockaddr *>(&addr),
                          sizeof(addr)) < 0)
                throw std::runtime_error("connect() failed ");

            HandshakeMsg hsk{};
            hsk.msg_type = MSG_HANDSHAKE;
            hsk.symbol = symbol;
            hsk.timeframe = timeframe;
            hsk._pad = 0;

            send_all(&hsk, sizeof(hsk));

            uint8_t ack = 0;
            ssize_t n = ::recv(fd_, &ack, 1, MSG_WAITALL);
            if (n != 1 || ack != MSG_ACK)
                throw std::runtime_error("Handshake ACK invalid");

            std::cout << "Connected and handshake OK\n";
        }

        ~FeedClient()
        {
            if (fd_ >= 0)
                ::close(fd_);
        }

        void send_snapshot(const MarketSnapshot &snap)
        {
            send_all(&snap, sizeof(snap));
        }

    private:
        void send_all(const void *buf, size_t len)
        {
            const char *ptr = static_cast<const char *>(buf);
            size_t rem = len;
            while (rem > 0)
            {
                ssize_t n = ::write(fd_, ptr, rem);
                if (n <= 0)
                    throw std::runtime_error("write() failed");
                ptr += n;
                rem -= n;
            }
        }

        int fd_{-1};
    };

    struct FeedConfig
    {
        std::string csv_path;
        std::string socket_path{"/tmp/uds_ob_hft.sock"};
        uint8_t symbol;
        uint8_t timeframe;
        int replay_speed_ms{0};
    };

    void run_feed(const FeedConfig &cfg)
    {
        std::cout << "CSV path : " << cfg.csv_path << "\n";
        MappedFile file(cfg.csv_path);

        FeedClient client(cfg.socket_path, cfg.symbol, cfg.timeframe);

        const char *ptr = file.data;
        const char *end = file.data + file.size;

        // Skip la ligne de header
        while (ptr < end && *ptr != '\n')
            ++ptr;
        ++ptr;

        uint64_t count = 0;
        MarketSnapshot snap{};

        auto t_start = std::chrono::steady_clock::now();

        while (ptr < end)
        {
            if (!parse_line(ptr, end, snap, cfg.symbol, cfg.timeframe))
                break;

            client.send_snapshot(snap);
            count++;

            if (count % 1000 == 0)
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - t_start)
                                   .count();
                std::cout << " Snapshot send in " << elapsed << "ms\n"
                          << std::endl;
            }

            if (cfg.replay_speed_ms > 0)
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(cfg.replay_speed_ms));
        }

        std::cout << "Replay finish — "
                  << count << " snapshots\n";
    }

}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        std::cerr << "Usage: data_fetcher <csv_path> <symbol> <timeframe> "
                     "[replay_ms]\n"
                  << "  symbol    : 0=ADA 1=BTC 2=ETH\n"
                  << "  timeframe : 0=1s  1=1min 2=5min\n"
                  << "  replay_ms : 0=max speed (défaut)\n";
        return 1;
    }

    uds_hft::FeedConfig cfg;
    cfg.csv_path = argv[1];
    cfg.symbol = static_cast<uint8_t>(std::atoi(argv[2]));
    cfg.timeframe = static_cast<uint8_t>(std::atoi(argv[3]));
    cfg.replay_speed_ms = (argc >= 5) ? std::atoi(argv[4]) : 0;

    try
    {
        uds_hft::run_feed(cfg);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error : " << e.what() << "\n";
        return 1;
    }

    return 0;
}
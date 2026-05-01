#include "include/market_data_feed.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace uds_hft {

MarketDataFeed::MarketDataFeed(const std::string& socket_path,
                               int heartbeat_interval_ms)
    : socket_path_(socket_path)
    , session_mgr_(heartbeat_interval_ms)
{
    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0)
        throw std::runtime_error("socket() failed");

    ::unlink(socket_path_.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(),
                 sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed");

    if (::listen(server_fd_, 8) < 0)
        throw std::runtime_error("listen() failed");

    std::cout << "[MarketDataFeed] Socket prêt : " << socket_path_ << "\n";
}

MarketDataFeed::~MarketDataFeed() {
    stop();
    if (server_fd_ >= 0) ::close(server_fd_);
    ::unlink(socket_path_.c_str());
}

void MarketDataFeed::start() {
    running_ = true;
    accept_thread_ = std::thread(&MarketDataFeed::accept_loop, this);
    std::cout << "[MarketDataFeed] En écoute...\n";
}

void MarketDataFeed::stop() {
    running_ = false;
    if (server_fd_ >= 0) ::shutdown(server_fd_, SHUT_RDWR);
    if (accept_thread_.joinable()) accept_thread_.join();
    std::cout << "[MarketDataFeed] Arrêté.\n";
}

void MarketDataFeed::accept_loop() {
    while (running_) {
        sockaddr_un client_addr{};
        socklen_t   len = sizeof(client_addr);

        int client_fd = ::accept(server_fd_,
                                 reinterpret_cast<sockaddr*>(&client_addr),
                                 &len);
        if (client_fd < 0) {
            if (running_)
                std::cerr << "[MarketDataFeed] accept() erreur\n";
            continue;
        }

        std::cout << "[MarketDataFeed] Nouveau fd=" << client_fd
                  << " → handshake SessionManager\n";

        // MarketDataFeed ne gère plus rien :
        // le SessionManager prend en charge handshake + cycle de vie
        session_mgr_.on_connect(client_fd);
    }
}

void MarketDataFeed::push_snapshot(const MarketSnapshot& snap) {
    int delivered = session_mgr_.dispatch(snap);

    if (delivered == 0)
        std::cout << "[MarketDataFeed] Aucun client actif pour ce snapshot\n";
}

} // namespace uds_hft
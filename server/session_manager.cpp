#include "include/session_manager.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

namespace uds_hft {

struct HandshakeMsg {
    uint8_t msg_type;  
    uint8_t symbol;    
    uint8_t timeframe; 
    uint8_t _pad;
};

static constexpr uint8_t MSG_HANDSHAKE = 0xAA;
static constexpr uint8_t MSG_HEARTBEAT = 0xBB;
static constexpr uint8_t MSG_ACK       = 0xCC;

SessionManager::SessionManager(int heartbeat_interval_ms)
    : heartbeat_interval_ms_(heartbeat_interval_ms) {

    heartbeat_thread_ = std::thread(&SessionManager::heartbeat_loop, this);
}

SessionManager::~SessionManager() {
    running_ = false;
    if (heartbeat_thread_.joinable())
        heartbeat_thread_.join();
}

void SessionManager::on_connect(int fd) {
    Session sess;
    sess.fd             = fd;
    sess.symbol         = Symbol::ALL;
    sess.timeframe      = Timeframe::ALL;
    sess.last_heartbeat = std::chrono::steady_clock::now();
    sess.ready          = false;

    if (!do_handshake(sess)) {
        std::cerr << "[SessionMgr] Handshake échoué fd=" << fd << "\n";
        ::close(fd);
        return;
    }

    std::lock_guard<std::mutex> lk(sessions_mtx_);
    sessions_[fd] = sess;

    std::cout << "[SessionMgr] Session ouverte fd=" << fd
              << " symbol=" << (int)sess.symbol
              << " timeframe=" << (int)sess.timeframe << "\n";
}

bool SessionManager::do_handshake(Session& sess) {
    struct timeval tv{2, 0};
    ::setsockopt(sess.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    HandshakeMsg hsk{};
    ssize_t n = ::recv(sess.fd, &hsk, sizeof(hsk), MSG_WAITALL);

    if (n != sizeof(hsk) || hsk.msg_type != MSG_HANDSHAKE) {
        std::cerr << "[SessionMgr] Handshake invalide\n";
        return false;
    }

    sess.symbol    = static_cast<Symbol>(hsk.symbol);
    sess.timeframe = static_cast<Timeframe>(hsk.timeframe);
    sess.ready     = true;

    uint8_t ack = MSG_ACK;
    send_all(sess.fd, &ack, 1);

    tv = {0, 0};
    ::setsockopt(sess.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return true;
}

int SessionManager::dispatch(const MarketSnapshot& snap) {
    std::lock_guard<std::mutex> lk(sessions_mtx_);
    int delivered = 0;
    std::vector<int> dead;

    for (auto& [fd, sess] : sessions_) {
        if (!sess.ready) continue;

        bool sym_match = (sess.symbol    == Symbol::ALL    ||
                          (uint8_t)sess.symbol    == snap.symbol);
        bool tf_match  = (sess.timeframe == Timeframe::ALL ||
                          (uint8_t)sess.timeframe == snap.timeframe);

        if (!sym_match || !tf_match) {
            continue;
        }

        if (!send_all(fd, &snap, sizeof(snap))) {
            sess.snapshots_dropped++;
            std::cerr << "[SessionMgr] Send échoué fd=" << fd << "\n";
            dead.push_back(fd);
        } else {
            sess.snapshots_sent++;
            delivered++;
        }
    }

    for (int fd : dead) {
        ::close(fd);
        sessions_.erase(fd);
    }

    return delivered;
}

void SessionManager::on_disconnect(int fd) {
    std::lock_guard<std::mutex> lk(sessions_mtx_);
    auto it = sessions_.find(fd);
    if (it != sessions_.end()) {
        std::cout << "[SessionMgr] Session fermée fd=" << fd
                  << " snapshots_sent=" << it->second.snapshots_sent << "\n";
        ::close(fd);
        sessions_.erase(it);
    }
}

void SessionManager::heartbeat_loop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(heartbeat_interval_ms_));

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(sessions_mtx_);
        std::vector<int> zombies;

        for (auto& [fd, sess] : sessions_) {
            uint8_t ping = MSG_HEARTBEAT;
            if (!send_all(fd, &ping, 1)) {
                zombies.push_back(fd);
                continue;
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - sess.last_heartbeat).count();

            if (elapsed > 3) {
                std::cerr << "[SessionMgr] Zombie détecté fd=" << fd << "\n";
                zombies.push_back(fd);
            }
        }

        for (int fd : zombies) {
            ::close(fd);
            sessions_.erase(fd);
        }
    }
}

void SessionManager::print_stats() const {
    std::lock_guard<std::mutex> lk(sessions_mtx_);
    std::cout << "[SessionMgr] Sessions actives: " << sessions_.size() << "\n";
    for (auto& [fd, sess] : sessions_) {
        std::cout << "  fd=" << fd
                  << " sent=" << sess.snapshots_sent
                  << " dropped=" << sess.snapshots_dropped << "\n";
    }
}

bool SessionManager::send_all(int fd, const void* buf, size_t len) {
    const char* ptr = static_cast<const char*>(buf);
    size_t rem = len;
    while (rem > 0) {
        ssize_t n = ::write(fd, ptr, rem);
        if (n <= 0) return false;
        ptr += n; rem -= n;
    }
    return true;
}

} // namespace uds_hft
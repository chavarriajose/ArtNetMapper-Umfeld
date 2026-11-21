// ArtNetMapper for Umfeld :: universe buffers and the send thread. MIT License.

#include "artnetmapper/Sender.h"

#include <chrono>
#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "artnetmapper/Discovery.h"
#include "artnetmapper/Log.h"
#include "artnetmapper/Packets.h"

namespace umfeld::artnetmapper {

    Sender::Universe::Universe(const int id) : id(id) {
        packet = artDmx(1, id, data, 512); // header filled once
    }

    const std::vector<uint8_t>& Sender::Universe::nextPacket(const bool black) {
        seq       = seq % 255 + 1;
        packet[12] = static_cast<uint8_t>(seq);
        if (black) {
            std::memset(packet.data() + DMX_HEADER, 0, 512);
        } else {
            std::memcpy(packet.data() + DMX_HEADER, data, 512);
        }
        return packet;
    }

    int64_t Sender::nowNanos() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    Sender::Sender(const std::string& ip, const uint32_t address) : ip_(ip), address_(address) {
        socket_ = ::socket(AF_INET, SOCK_DGRAM, 0); // ephemeral port: never competes for 6454
        if (socket_ < 0) {
            throw std::runtime_error(std::string("ArtNetMapper: could not open a UDP socket: ") + std::strerror(errno));
        }
        int on = 1;
        setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
#ifdef SO_NOSIGPIPE
        setsockopt(socket_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        const int fd = socket_;
        sink_        = [fd](const uint8_t* p, const size_t n, const uint32_t to) {
            sockaddr_in dst{};
            dst.sin_family      = AF_INET;
            dst.sin_port        = htons(PORT);
            dst.sin_addr.s_addr = htonl(to);
            // errors (e.g. the interface went away) are ignored: the connection check notices
            ::sendto(fd, p, n, 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
        };
        try {
            checkConnection();
            thread_ = std::thread(&Sender::run, this);
        } catch (...) {
            ::close(socket_); // the destructor won't run for a half-built Sender
            throw;
        }
    }

    Sender::Sender(const std::string& ip, const uint32_t address, Sink sink) : ip_(ip), address_(address), sink_(std::move(sink)) {
        destination_ = sink_ ? address : 0;
    }

    Sender::~Sender() {
        close(false);
    }

    // ------------------------------------------------------------------ universes

    Sender::Universe& Sender::universe(const int id) {
        std::lock_guard<std::mutex> g(lock);
        auto                        it = universes_.find(id);
        if (it == universes_.end()) { it = universes_.emplace(id, Universe(id)).first; }
        return it->second;
    }

    void Sender::changed() {
        std::lock_guard<std::mutex> g(lock);
        pending_ = true;
        cv_.notify_all();
    }

    void Sender::maxRate(const float fps) {
        maxRate_ = fps;
        std::lock_guard<std::mutex> g(lock);
        cv_.notify_all();
    }

    // ------------------------------------------------------------------ send loop

    void Sender::run() {
        int64_t nextCheck = nowNanos() + CHECK_NANOS;
        while (!closed_) {
            try {
                const int64_t now = nowNanos();
                if (now - nextCheck >= 0) {
                    checkConnection();
                    nextCheck = now + CHECK_NANOS;
                }
                const int64_t wait = std::min(step(now), nextCheck - now);
                std::unique_lock<std::mutex> lk(lock);
                if (!closed_ && wait > 0 && !(pending_ && nowNanos() - lastSend_ >= minInterval())) {
                    cv_.wait_for(lk, std::chrono::nanoseconds(wait));
                }
            } catch (const std::exception& e) {
                if (!warnedError_) {
                    warnedError_ = true;
                    Log::info(std::string("send error, still trying: ") + e.what());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    int64_t Sender::minInterval() const {
        return static_cast<int64_t>(1e9 / maxRate_.load());
    }

    int64_t Sender::step(const int64_t now) {
        if (destination_.load() == 0) { return CHECK_NANOS; }
        std::lock_guard<std::mutex> g(lock);
        const int64_t               interval = pending_ ? minInterval() : KEEPALIVE_NANOS;
        int64_t                     due      = lastSend_ + interval;
        if (now - due >= 0) {
            pending_ = false;
            sendAllLocked();
            // Keep to the schedule when we woke a little late, so the average rate is
            // exactly maxRate; after a long pause, start again from now.
            lastSend_ = now - due < interval ? due : now;
            due       = lastSend_ + KEEPALIVE_NANOS;
        }
        return due - now;
    }

    void Sender::sendAllLocked() {
        const uint32_t to = destination_.load();
        if (to == 0 || !sink_) { return; }
        for (auto& [id, u]: universes_) {
            const auto& p = u.nextPacket(black_);
            sink_(p.data(), p.size(), to);
        }
    }

    // ------------------------------------------------------------------ connection

    /*
     * The IP is always sent to, except when it was on a local link (e.g. a USB node's
     * interface) and that link disappeared: then sending pauses until it returns, so
     * packets don't go out through the default route.
     */
    void Sender::checkConnection() {
        const auto link = Discovery::interfaceFor(ip_);
        const int  next = nextStatus(status_, link.has_value());
        if (next == status_) { return; }
        if (next == 1) {
            destination_          = address_;
            const std::string via = link->iface == "loopback" ? "this computer" : link->iface;
            Log::info((status_ == 3 ? "reconnected to " : "connected to ") + ip_ + " (" + via + ")");
            changed();
        } else if (next == 2) {
            destination_ = address_;
            Log::info(ip_ + " is not on any connected network. Sending anyway. If it's a USB node, check that it's plugged in.");
            changed();
        } else {
            destination_ = 0;
            Log::info("lost " + ip_ + " (its network is gone; unplugged?). Sending paused until it is back.");
        }
        status_ = next;
    }

    int Sender::nextStatus(const int status, const bool onLink) {
        if (onLink) { return 1; }
        return status == 1 || status == 3 ? 3 : 2;
    }

    // ------------------------------------------------------------------ shutdown

    void Sender::blackout(const bool on) {
        std::lock_guard<std::mutex> g(lock);
        black_   = on;
        pending_ = true;
        cv_.notify_all();
    }

    bool Sender::isBlackout() {
        std::lock_guard<std::mutex> g(lock);
        return black_;
    }

    void Sender::sendNow() {
        std::lock_guard<std::mutex> g(lock);
        sendAllLocked();
        lastSend_ = nowNanos();
        pending_  = false;
    }

    void Sender::close(const bool blackout) {
        {
            std::lock_guard<std::mutex> g(lock);
            if (closed_) { return; }
            closed_ = true;
            cv_.notify_all();
        }
        if (thread_.joinable()) { thread_.join(); }
        if (blackout && destination_.load() != 0) {
            {
                std::lock_guard<std::mutex> g(lock);
                black_ = true;
            }
            for (int i = 0; i < 3; i++) {
                sendNow();
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        }
        if (socket_ >= 0) {
            ::close(socket_);
            socket_ = -1;
        }
    }

} // namespace umfeld::artnetmapper

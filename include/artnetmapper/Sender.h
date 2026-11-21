// ArtNetMapper for Umfeld :: universe buffers and the send thread. MIT License.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace umfeld::artnetmapper {

    /**
     * Owns the universe buffers and everything about getting them onto the network:
     * the UDP socket, the send thread (rate cap, 1 s keepalive), sequence numbers,
     * the connection check and the exit blackout.
     *
     * Writers (the sketch thread) change universe data while holding `lock` and then
     * call changed(). The send thread sleeps until the next frame is due, so an idle
     * sketch costs almost nothing.
     */
    class Sender {
    public:
        /** Where packets go. Tests pass a fake; the library passes a UDP socket. */
        using Sink = std::function<void(const uint8_t* packet, size_t length, uint32_t to)>;

        /** One DMX universe: 512 values plus its own ArtDmx packet and sequence counter. */
        struct Universe {
            int                  id;
            uint8_t              data[512] = {};
            std::vector<uint8_t> packet;
            int                  seq = 0;

            explicit Universe(int id);
            /** Next ArtDmx packet: sequence 1..255, current data or all zeros. Call with the lock held. */
            const std::vector<uint8_t>& nextPacket(bool black);
        };

        static constexpr int64_t KEEPALIVE_NANOS = 1000000000LL;
        static constexpr int64_t CHECK_NANOS     = 1000000000LL;

        /** Network sender for a real sketch: opens a socket and starts the send thread. */
        Sender(const std::string& ip, uint32_t address);
        /** Sender without network or thread, for tests. An empty sink sends nothing. */
        Sender(const std::string& ip, uint32_t address, Sink sink);
        ~Sender();
        Sender(const Sender&)            = delete;
        Sender& operator=(const Sender&) = delete;

        /** Guards universe data, pending and the send itself. */
        std::mutex lock;

        /** The universe with this id, created on first use. Stable address. */
        Universe& universe(int id);
        /** All universes, by id. Call with the lock held. */
        std::map<int, Universe>& universes() { return universes_; }

        /** Marks the data as changed; the send thread wakes and sends when the rate cap allows. */
        void  changed();
        void  maxRate(float fps);
        float getMaxRate() const { return maxRate_; }
        bool  isSending() const { return destination_.load() != 0; }

        /**
         * Sends if a frame is due at time now (nanoseconds) and returns the nanoseconds
         * until the next one could be due. Public for tests.
         */
        int64_t step(int64_t now);

        void       checkConnection();
        static int nextStatus(int status, bool onLink);

        /** Blackout switch: while on, every packet carries zeros; the universe data is kept. */
        void blackout(bool on);
        bool isBlackout();
        /** Sends one frame now (zeros if the blackout is on). */
        void sendNow();
        bool isClosed() const { return closed_; }
        /** Stops the send thread, optionally sends a blackout (3 frames), closes the socket. */
        void close(bool blackout);

        static int64_t nowNanos();

    private:
        void    run();
        int64_t minInterval() const;
        void    sendAllLocked();

        std::map<int, Universe> universes_;
        std::string             ip_;
        uint32_t                address_; // host byte order
        Sink                    sink_;
        int                     socket_ = -1;
        std::thread             thread_;
        std::condition_variable cv_;

        std::atomic<uint32_t> destination_{0}; // 0 = paused
        std::atomic<float>    maxRate_{44.0f};
        bool                  pending_  = false; // guarded by lock
        int64_t               lastSend_ = INT64_MIN / 2;
        std::atomic<bool>     closed_{false};
        bool                  black_       = false; // guarded by lock
        bool                  warnedError_ = false;
        /** Connection state, for messages: 0 not checked, 1 on a local link, 2 routed, 3 link lost. */
        int status_ = 0;
    };

} // namespace umfeld::artnetmapper

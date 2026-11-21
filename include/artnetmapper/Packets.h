// ArtNetMapper for Umfeld :: Art-Net packets (ArtDmx, ArtPoll, ArtPollReply).
// Art-Net 4 layout, protocol version 14. MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace umfeld::artnetmapper {

    constexpr int PORT          = 6454;
    constexpr int OP_POLL       = 0x2000;
    constexpr int OP_POLL_REPLY = 0x2100;
    constexpr int OP_DMX        = 0x5000;
    constexpr int DMX_HEADER    = 18;

    inline const char ARTNET_ID[8] = {'A', 'r', 't', '-', 'N', 'e', 't', '\0'};

    /**
     * ArtDmx packet.
     *
     * @param sequence 1..255 (0 disables sequencing on the receiver)
     * @param universe 15-bit port address (Net:SubNet:Universe), 0..32767
     * @param data     DMX slot values, channel 1 at index 0 (may be nullptr: all 0)
     * @param length   number of slots to send, 2..512, rounded up to even
     */
    inline std::vector<uint8_t> artDmx(const int sequence, const int universe, const uint8_t* data, const int length) {
        if (universe < 0 || universe > 0x7FFF) {
            throw std::invalid_argument("Art-Net universe must be 0..32767, got " + std::to_string(universe));
        }
        int len = length < 2 ? 2 : (length > 512 ? 512 : length);
        if ((len & 1) == 1) { len++; }
        std::vector<uint8_t> p(DMX_HEADER + len, 0);
        std::memcpy(p.data(), ARTNET_ID, 8);
        p[8]  = OP_DMX & 0xFF;
        p[9]  = OP_DMX >> 8;
        p[10] = 0;
        p[11] = 14;
        p[12] = static_cast<uint8_t>(sequence);
        p[13] = 0;                                         // physical
        p[14] = static_cast<uint8_t>(universe & 0xFF);     // SubUni
        p[15] = static_cast<uint8_t>(universe >> 8 & 0x7F); // Net
        p[16] = static_cast<uint8_t>(len >> 8);
        p[17] = static_cast<uint8_t>(len & 0xFF);
        // copy only the slots given (an odd length is padded with a 0)
        if (data != nullptr && length > 0) { std::memcpy(p.data() + DMX_HEADER, data, std::min(len, length)); }
        return p;
    }

    /** ArtPoll packet (14 bytes). */
    inline std::vector<uint8_t> artPoll() {
        std::vector<uint8_t> p(14, 0);
        std::memcpy(p.data(), ARTNET_ID, 8);
        p[8]  = OP_POLL & 0xFF;
        p[9]  = OP_POLL >> 8;
        p[11] = 14;
        return p;
    }

    /** Opcode of an Art-Net packet, or -1 if it is not Art-Net. */
    inline int opcode(const uint8_t* p, const size_t length) {
        if (length < 10) { return -1; }
        if (std::memcmp(p, ARTNET_ID, 8) != 0) { return -1; }
        return p[8] | p[9] << 8;
    }

    /** Reads a null-terminated ASCII string of at most max bytes, trimmed. */
    inline std::string cString(const uint8_t* p, const size_t length, const size_t offset, const size_t max) {
        size_t end = offset;
        while (end < offset + max && end < length && p[end] != 0) { end++; }
        std::string s(reinterpret_cast<const char*>(p + offset), end - offset);
        const auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) { return ""; }
        const auto last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    struct PollReply {
        bool        valid = false;
        std::string ip;
        std::string shortName;
        std::string longName;
    };

    /** Parses an ArtPollReply; valid is false if the packet is not one. */
    inline PollReply parsePollReply(const uint8_t* p, const size_t length) {
        PollReply r;
        if (opcode(p, length) != OP_POLL_REPLY || length < 108) { return r; }
        r.valid     = true;
        r.ip        = std::to_string(p[10]) + "." + std::to_string(p[11]) + "." + std::to_string(p[12]) + "." + std::to_string(p[13]);
        r.shortName = cString(p, length, 26, 18);
        r.longName  = cString(p, length, 44, 64);
        return r;
    }

} // namespace umfeld::artnetmapper

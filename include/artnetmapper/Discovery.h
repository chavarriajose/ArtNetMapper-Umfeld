// ArtNetMapper for Umfeld :: finding Art-Net nodes. MIT License.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace umfeld::artnetmapper {

    /**
     * Finds Art-Net nodes two ways:
     *  1. ArtPoll broadcast on every IPv4 interface; nodes that reply are listed.
     *  2. Interface scan: on small point-to-point links (an interface without a default
     *     route whose subnet holds exactly one other host, typical of USB-to-DMX nodes
     *     that appear as a virtual Ethernet adapter) the single peer is pinged and
     *     listed if it answers. Large networks are never swept.
     */
    struct Discovery {
        struct Node {
            std::string ip;
            std::string name;
            std::string iface;
            bool        artPoll = false;
            /** e.g. "Electroconcept USB NODE  (answered ArtPoll, en6)". */
            std::string label() const;
        };

        /** One local IPv4 address with its interface. Addresses in host byte order. */
        struct LocalAddr {
            std::string iface;
            uint32_t    addr      = 0;
            uint32_t    broadcast = 0; // 0 = none
            int         prefix    = 0;
            bool        contains(uint32_t ip) const { return sameSubnet(addr, ip, prefix); }
        };

        static std::vector<LocalAddr> localAddresses();
        /** The local interface whose subnet contains ip ("loopback" for 127.x), or none. */
        static std::optional<LocalAddr> interfaceFor(const std::string& ip);
        static std::optional<LocalAddr> interfaceFor(const std::string& ip, const std::vector<LocalAddr>& locals);
        static bool                     sameSubnet(uint32_t a, uint32_t b, int prefix);
        /** Parses a dotted IPv4 address (host byte order); none if it isn't one. */
        static std::optional<uint32_t> parse(const std::string& ip);
        static std::string             format(uint32_t ip);

        /** Runs ArtPoll and the interface scan for about timeoutMs and returns nodes found. */
        static std::vector<Node> scan(int timeoutMs);

        /** Parses `arp -an`: complete entries per interface. */
        static std::map<std::string, std::set<std::string>> parseArp(const std::string& text);
        /** Parses `netstat -rn -f inet` (macOS): default-route interfaces and gateways. */
        static void parseMacRoutes(const std::string& text, std::set<std::string>& ifaces, std::set<std::string>& gateways);
        /** Runs a command, returns its output ("" on failure). */
        static std::string run(const std::string& command);
    };

} // namespace umfeld::artnetmapper

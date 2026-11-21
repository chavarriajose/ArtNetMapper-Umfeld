// ArtNetMapper for Umfeld :: finding Art-Net nodes. MIT License.

#include "artnetmapper/Discovery.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "artnetmapper/Log.h"
#include "artnetmapper/Packets.h"

namespace umfeld::artnetmapper {

    std::string Discovery::Node::label() const {
        std::string s;
        if (!name.empty()) { s += name + "  "; }
        s += artPoll ? "(answered ArtPoll" : "(found by interface scan, no ArtPoll reply";
        if (!iface.empty()) { s += ", " + iface; }
        return s + ")";
    }

    // ------------------------------------------------------------------ addresses

    static int prefixOf(const uint32_t mask) {
        int n = 0;
        for (uint32_t m = mask; m & 0x80000000u; m <<= 1) { n++; }
        return n;
    }

    std::vector<Discovery::LocalAddr> Discovery::localAddresses() {
        std::vector<LocalAddr> out;
        ifaddrs*               list = nullptr;
        if (getifaddrs(&list) != 0) { return out; }
        for (const ifaddrs* a = list; a != nullptr; a = a->ifa_next) {
            if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET) { continue; }
            if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) { continue; }
            LocalAddr la;
            la.iface = a->ifa_name;
            la.addr  = ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
            if (a->ifa_netmask != nullptr) {
                la.prefix = prefixOf(ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr));
            }
            if ((a->ifa_flags & IFF_BROADCAST) && a->ifa_broadaddr != nullptr && a->ifa_broadaddr->sa_family == AF_INET) {
                la.broadcast = ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_broadaddr)->sin_addr.s_addr);
            }
            out.push_back(la);
        }
        freeifaddrs(list);
        return out;
    }

    bool Discovery::sameSubnet(const uint32_t a, const uint32_t b, const int prefix) {
        if (prefix <= 0 || prefix > 32) { return false; }
        const uint32_t mask = prefix == 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefix)) - 1);
        return (a & mask) == (b & mask);
    }

    std::optional<uint32_t> Discovery::parse(const std::string& ip) {
        static const std::regex dotted(R"(\d{1,3}(\.\d{1,3}){3})");
        if (!std::regex_match(ip, dotted)) { return std::nullopt; }
        in_addr a{};
        if (inet_pton(AF_INET, ip.c_str(), &a) != 1) { return std::nullopt; }
        return ntohl(a.s_addr);
    }

    std::string Discovery::format(const uint32_t ip) {
        return std::to_string(ip >> 24 & 0xFF) + "." + std::to_string(ip >> 16 & 0xFF) + "." +
               std::to_string(ip >> 8 & 0xFF) + "." + std::to_string(ip & 0xFF);
    }

    std::optional<Discovery::LocalAddr> Discovery::interfaceFor(const std::string& ip) {
        const auto a = parse(ip);
        if (a && (*a >> 24) == 127) {
            LocalAddr la;
            la.iface  = "loopback";
            la.addr   = *a;
            la.prefix = 8;
            return la;
        }
        return interfaceFor(ip, localAddresses());
    }

    std::optional<Discovery::LocalAddr> Discovery::interfaceFor(const std::string& ip, const std::vector<LocalAddr>& locals) {
        const auto a = parse(ip);
        if (!a) { return std::nullopt; }
        for (const auto& la: locals) {
            if (la.contains(*a)) { return la; }
        }
        return std::nullopt;
    }

    // ------------------------------------------------------------------ commands

    std::string Discovery::run(const std::string& command) {
        FILE* p = popen((command + " </dev/null 2>/dev/null").c_str(), "r");
        if (p == nullptr) { return ""; }
        std::string out;
        char        buf[512];
        size_t      n;
        while ((n = fread(buf, 1, sizeof(buf), p)) > 0) { out.append(buf, n); }
        pclose(p);
        return out;
    }

    /** True for names that are safe to put on a command line (interface names). */
    static bool safeToken(const std::string& s) {
        if (s.empty()) { return false; }
        return std::all_of(s.begin(), s.end(), [](const char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'; });
    }

    std::map<std::string, std::set<std::string>> Discovery::parseArp(const std::string& text) {
        static const std::regex                       arp(R"(\((\d+\.\d+\.\d+\.\d+)\) at (\S+) (?:\[\w+\] )?on (\S+))");
        std::map<std::string, std::set<std::string>> map;
        std::istringstream                            in(text);
        std::string                                   line;
        while (std::getline(in, line)) {
            std::smatch m;
            if (!std::regex_search(line, m, arp)) { continue; }
            std::string mac = m[2];
            std::string low = mac;
            std::transform(low.begin(), low.end(), low.begin(), [](const unsigned char c) { return std::tolower(c); });
            if (low.find("incomplete") != std::string::npos || low == "ff:ff:ff:ff:ff:ff") { continue; }
            map[m[3]].insert(m[1]);
        }
        return map;
    }

    static std::vector<std::string> words(const std::string& line) {
        std::istringstream       in(line);
        std::vector<std::string> t;
        std::string              w;
        while (in >> w) { t.push_back(w); }
        return t;
    }

    void Discovery::parseMacRoutes(const std::string& text, std::set<std::string>& ifaces, std::set<std::string>& gateways) {
        std::istringstream in(text);
        std::string        line;
        while (std::getline(in, line)) {
            const auto t = words(line);
            if (t.size() >= 4 && t[0] == "default") {
                if (parse(t[1])) { gateways.insert(t[1]); }
                ifaces.insert(t[3]);
            }
        }
    }

    static void readDefaultRoutes(std::set<std::string>& ifaces, std::set<std::string>& gateways) {
#if defined(__APPLE__)
        Discovery::parseMacRoutes(Discovery::run("netstat -rn -f inet"), ifaces, gateways);
#else
        std::istringstream in(Discovery::run("ip route show default"));
        std::string        line;
        while (std::getline(in, line)) {
            const auto t = words(line);
            for (size_t i = 0; i + 1 < t.size(); i++) {
                if (t[i] == "via") { gateways.insert(t[i + 1]); }
                if (t[i] == "dev") { ifaces.insert(t[i + 1]); }
            }
        }
#endif
    }

    static bool ping(const std::string& ip) {
#if defined(__APPLE__)
        const std::string out = Discovery::run("ping -c 1 -t 1 " + ip);
#else
        const std::string out = Discovery::run("ping -c 1 -W 1 " + ip);
#endif
        return out.find(" 0.0% packet loss") != std::string::npos || out.find(" 0% packet loss") != std::string::npos;
    }

    static std::vector<Discovery::Node> scanPointToPoint(const std::vector<Discovery::LocalAddr>& locals, const std::set<std::string>& localIps) {
        std::vector<Discovery::Node> out;
        std::set<std::string>        defaultIfaces, gateways;
        readDefaultRoutes(defaultIfaces, gateways);
        const auto neighbours = Discovery::parseArp(Discovery::run("arp -an"));

        for (const auto& la: locals) {
            if (defaultIfaces.count(la.iface)) { continue; } // main network (Wi-Fi/LAN): never sweep
            if (la.prefix < 8 || !safeToken(la.iface)) { continue; }
            std::set<std::string> peers;
            const auto            n = neighbours.find(la.iface);
            if (n != neighbours.end()) { peers = n->second; }
#if defined(__APPLE__)
            std::string dhcp = Discovery::run("ipconfig getoption " + la.iface + " server_identifier");
            dhcp.erase(std::remove_if(dhcp.begin(), dhcp.end(), [](const unsigned char c) { return std::isspace(c); }), dhcp.end());
            if (!dhcp.empty()) { peers.insert(dhcp); }
#endif
            for (auto it = peers.begin(); it != peers.end();) {
                const auto a   = Discovery::parse(*it);
                const bool bad = !a || localIps.count(*it) || gateways.count(*it) || (*a >> 28) == 0xE || !la.contains(*a) ||
                                 (la.broadcast != 0 && *a == la.broadcast) || (*a & 0xFF) == 255 || (*a & 0xFF) == 0;
                it = bad ? peers.erase(it) : std::next(it);
            }
            if (peers.size() != 1) { continue; }
            const std::string peer = *peers.begin();
            if (ping(peer)) { out.push_back({peer, "", la.iface, false}); }
        }
        return out;
    }

    // ------------------------------------------------------------------ ArtPoll

    /*
     * Socket for ArtPoll. Nodes reply to UDP 6454, so try to share that port
     * (SO_REUSEADDR/SO_REUSEPORT). If another app holds it exclusively, fall back
     * to an ephemeral port: nodes that reply to the source port are still found.
     */
    static int openPollSocket() {
        for (const int port: {PORT, 0}) {
            const int s = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (s < 0) { return -1; }
            int on = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
            setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
            setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
            sockaddr_in a{};
            a.sin_family      = AF_INET;
            a.sin_port        = htons(static_cast<uint16_t>(port));
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0) { return s; }
            ::close(s);
        }
        return -1;
    }

    std::vector<Discovery::Node> Discovery::scan(const int timeoutMs) {
        const auto            locals = localAddresses();
        std::set<std::string> localIps;
        for (const auto& la: locals) { localIps.insert(format(la.addr)); }

        // The interface scan runs in parallel with ArtPoll listening. Its thread is
        // detached and shares its result through `state`, so a slow command can't
        // hold up list() for longer than the wait below.
        struct State {
            std::mutex              m;
            std::condition_variable cv;
            bool                    done = false;
            std::vector<Node>       nodes;
        };
        auto state = std::make_shared<State>();
        std::thread([state, locals, localIps] {
            auto                        found = scanPointToPoint(locals, localIps);
            std::lock_guard<std::mutex> g(state->m);
            state->nodes = std::move(found);
            state->done  = true;
            state->cv.notify_all();
        }).detach();

        std::vector<Node> polled;
        const int         s = openPollSocket();
        if (s < 0) {
            Log::warn(std::string("ArtPoll could not be sent (") + std::strerror(errno) + "). Only the interface scan was used.");
        } else {
            timeval tv{0, 100000};
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            const auto         poll = artPoll();
            std::set<uint32_t> targets;
            for (const auto& la: locals) {
                if (la.broadcast != 0) { targets.insert(la.broadcast); }
            }
            targets.insert(0xFFFFFFFFu);
            for (const uint32_t t: targets) {
                sockaddr_in dst{};
                dst.sin_family      = AF_INET;
                dst.sin_port        = htons(PORT);
                dst.sin_addr.s_addr = htonl(t);
                ::sendto(s, poll.data(), poll.size(), 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst)); // may be refused; ignore
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            uint8_t    buf[1024];
            while (std::chrono::steady_clock::now() < deadline) {
                sockaddr_in from{};
                socklen_t   fromLen = sizeof(from);
                const auto  n       = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (n <= 0) { continue; }
                const PollReply r = parsePollReply(buf, static_cast<size_t>(n));
                if (!r.valid) { continue; }
                std::string ip = r.ip == "0.0.0.0" ? format(ntohl(from.sin_addr.s_addr)) : r.ip;
                // skip ourselves / other apps on this machine, and duplicates
                if (localIps.count(ip) || std::any_of(polled.begin(), polled.end(), [&](const Node& k) { return k.ip == ip; })) { continue; }
                const auto la = interfaceFor(ip, locals);
                polled.push_back({ip, !r.longName.empty() ? r.longName : r.shortName, la ? la->iface : "", true});
            }
            ::close(s);
        }

        // ArtPoll listening is over; give the interface scan up to 2 s more to finish
        // its commands (arp, netstat, ipconfig, ping each time out on their own).
        std::vector<Node> out = polled;
        {
            std::unique_lock<std::mutex> lk(state->m);
            state->cv.wait_for(lk, std::chrono::milliseconds(2000), [&] { return state->done; });
            for (const auto& g: state->nodes) {
                if (std::none_of(polled.begin(), polled.end(), [&](const Node& k) { return k.ip == g.ip; })) { out.push_back(g); }
            }
        }
        return out;
    }

} // namespace umfeld::artnetmapper

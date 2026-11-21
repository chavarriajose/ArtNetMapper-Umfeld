// Unit tests for the ArtNetMapper core, ported from the Processing library's JUnit tests.
// No Umfeld, no network: the mapper is built offline and fed pixel arrays directly.

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ArtNetMapper.h"
#include "artnetmapper/ColorMath.h"
#include "artnetmapper/Discovery.h"
#include "artnetmapper/Log.h"
#include "artnetmapper/Packets.h"
#include "artnetmapper/Sender.h"

using namespace umfeld;
using namespace umfeld::artnetmapper;

namespace umfeld::artnetmapper {
    struct TestAccess {
        static std::unique_ptr<ArtNetMapper> mapper() { return std::unique_ptr<ArtNetMapper>(new ArtNetMapper("2.0.0.1", false)); }
        static void     sample(ArtNetMapper& m, const std::vector<uint32_t>& px, int w, int h) { m.sample(px.empty() ? nullptr : px.data(), w, h, 1); }
        static uint32_t samplePoint(const std::vector<uint32_t>& px, int w, int h, float x, float y, int r, int density = 1) { return ArtNetMapper::samplePoint(px.data(), w, h, density, x, y, r); }
        static bool     hasRect(const Fixture& f) { return f.hasRect_; }
        static void     construct(const std::string& ip) { ArtNetMapper m(ip, false); }
    };
} // namespace umfeld::artnetmapper

// ------------------------------------------------------------------ tiny test runner

static int failures = 0, checks = 0;
static std::string current;

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        checks++;                                                                                    \
        if (!(cond)) {                                                                               \
            failures++;                                                                              \
            std::cout << "  FAIL " << current << " (line " << __LINE__ << "): " #cond << std::endl; \
        }                                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                              \
    do {                                                                                                                            \
        checks++;                                                                                                                   \
        const auto va = (a);                                                                                                        \
        const auto vb = (b);                                                                                                        \
        if (!(va == vb)) {                                                                                                          \
            failures++;                                                                                                             \
            std::cout << "  FAIL " << current << " (line " << __LINE__ << "): " #a " == " #b "  got " << show(va) << " vs " << show(vb) \
                      << std::endl;                                                                                                 \
        }                                                                                                                           \
    } while (0)

#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 1e-3)

/** Runs code that must throw E; returns the message ("" if it didn't throw). */
template<typename E>
static std::string throws(const std::function<void()>& code, bool& threw) {
    threw = false;
    try {
        code();
    } catch (const E& e) {
        threw = true;
        return e.what();
    }
    return "";
}

#define CHECK_THROWS(E, code)                       \
    do {                                            \
        bool t;                                     \
        throws<E>([&] { code; }, t);                \
        checks++;                                   \
        if (!t) {                                   \
            failures++;                             \
            std::cout << "  FAIL " << current << " (line " << __LINE__ << "): expected " #E " from " #code << std::endl; \
        }                                           \
    } while (0)

#define CHECK_THROWS_WITH(E, code, text)                                                                                       \
    do {                                                                                                                       \
        bool              t;                                                                                                   \
        const std::string msg = throws<E>([&] { code; }, t);                                                                   \
        checks++;                                                                                                              \
        if (!t || msg.find(text) == std::string::npos) {                                                                       \
            failures++;                                                                                                        \
            std::cout << "  FAIL " << current << " (line " << __LINE__ << "): expected " #E " containing \"" << text << "\", got \"" << msg \
                      << "\"" << std::endl;                                                                                    \
        }                                                                                                                      \
    } while (0)

template<typename T>
static std::string show(const T& v) {
    std::ostringstream o;
    o << v;
    return o.str();
}
template<typename T>
static std::string show(const std::vector<T>& v) {
    std::ostringstream o;
    o << "{";
    for (size_t i = 0; i < v.size(); i++) { o << (i ? ", " : "") << v[i]; }
    return o.str() + "}";
}
template<typename T>
static std::string show(const std::set<T>& v) {
    std::ostringstream o;
    o << "{";
    bool first = true;
    for (const auto& x: v) {
        o << (first ? "" : ", ") << x;
        first = false;
    }
    return o.str() + "}";
}

static std::vector<std::pair<std::string, std::function<void()>>>& tests() {
    static std::vector<std::pair<std::string, std::function<void()>>> t;
    return t;
}
struct Register {
    Register(const char* name, std::function<void()> f) { tests().emplace_back(name, std::move(f)); }
};
#define TEST(name)                                   \
    static void         name();                      \
    static Register     reg_##name(#name, name);     \
    static void         name()

// ------------------------------------------------------------------ helpers

/** Umfeld pixel layout: red in the low byte. */
static uint32_t rgb(int r, int g, int b) { return 0xFF000000u | b << 16 | g << 8 | r; }

static std::unique_ptr<ArtNetMapper> mapper() {
    auto m = TestAccess::mapper();
    m->gamma(1);
    return m;
}

static std::vector<int> ch(const ArtNetMapper& m, int universe, int from, int to) {
    const auto u = m.getUniverse(universe);
    return std::vector<int>(u.begin() + (from - 1), u.begin() + to);
}

static void sample(ArtNetMapper& m, const std::vector<uint32_t>& px, int w, int h) { TestAccess::sample(m, px, w, h); }

/** Captures what the core prints, while code runs. */
static std::string captured(const std::function<void()>& code) {
    std::string out;
    auto        oldInfo = Log::infoOut(), oldWarn = Log::warnOut();
    Log::infoOut()      = [&](const std::string& s) { out += s + "\n"; };
    Log::warnOut()      = [&](const std::string& s) { out += s + "\n"; };
    code();
    Log::infoOut() = oldInfo;
    Log::warnOut() = oldWarn;
    return out;
}

static int count(const std::string& text, const std::string& word) {
    int n = 0;
    for (size_t p = text.find(word); p != std::string::npos; p = text.find(word, p + 1)) { n++; }
    return n;
}

// ------------------------------------------------------------------ packets

TEST(artDmxHeaderAndData) {
    uint8_t data[512] = {};
    data[0]           = 255;
    data[511]         = 7;
    const auto p      = artDmx(3, 0, data, 512);
    CHECK_EQ(p.size(), size_t(18 + 512));
    CHECK(std::memcmp(p.data(), "Art-Net\0", 8) == 0);
    CHECK_EQ(int(p[8]), 0x00);
    CHECK_EQ(int(p[9]), 0x50);
    CHECK_EQ(int(p[11]), 14);
    CHECK_EQ(int(p[12]), 3);
    CHECK_EQ(int(p[16]), 0x02);
    CHECK_EQ(int(p[17]), 0x00);
    CHECK_EQ(int(p[18]), 255);
    CHECK_EQ(int(p[18 + 511]), 7);
}

TEST(artDmxUniverseAndLength) {
    uint8_t    data[512] = {};
    const auto p         = artDmx(1, 0x1234, data, 512);
    CHECK_EQ(int(p[14]), 0x34);
    CHECK_EQ(int(p[15]), 0x12);
    const auto max = artDmx(1, 0x7FFF, data, 512);
    CHECK_EQ(int(max[14]), 0xFF);
    CHECK_EQ(int(max[15]), 0x7F);
    CHECK_THROWS(std::invalid_argument, artDmx(1, 32768, data, 512));
    CHECK_THROWS(std::invalid_argument, artDmx(1, -1, data, 512));
    uint8_t    seven[7] = {1, 2, 3, 4, 5, 6, 7};
    const auto odd      = artDmx(1, 0, seven, 7); // padded to 8, never reads past the 7 given
    CHECK_EQ(int(odd[16] << 8 | odd[17]), 8);
    CHECK_EQ(odd.size(), size_t(18 + 8));
    CHECK_EQ(int(odd[18 + 6]), 7);
    CHECK_EQ(int(odd[18 + 7]), 0);
}

TEST(artPollPacket) {
    const auto p = artPoll();
    CHECK_EQ(p.size(), size_t(14));
    CHECK_EQ(opcode(p.data(), p.size()), OP_POLL);
    CHECK_EQ(int(p[11]), 14);
}

TEST(parsesPollReplyWithNullTerminatedNames) {
    std::vector<uint8_t> p(239, 0);
    std::memcpy(p.data(), "Art-Net\0", 8);
    p[9]  = 0x21;
    p[10] = 2;
    p[13] = 1;
    std::memcpy(p.data() + 26, "Node\0garbage", 12);
    std::memcpy(p.data() + 44, "My DMX Node", 11);
    const auto r = parsePollReply(p.data(), p.size());
    CHECK(r.valid);
    CHECK_EQ(r.ip, std::string("2.0.0.1"));
    CHECK_EQ(r.shortName, std::string("Node"));
    CHECK_EQ(r.longName, std::string("My DMX Node"));
    const uint8_t junk[20] = {1, 2, 3};
    CHECK_EQ(opcode(junk, 20), -1);
    CHECK(!parsePollReply(junk, 20).valid);
}

// ------------------------------------------------------------------ color

TEST(gammaTables) {
    const auto lin = gammaTable(1.0f);
    for (int i = 0; i < 256; i++) { CHECK_EQ(lin[i], i); }
    const auto t = gammaTable(2.5f);
    CHECK_EQ(t[0], 0);
    CHECK_EQ(t[255], 255);
    CHECK_EQ(t[128], int(std::lround(255 * std::pow(128 / 255.0, 2.5))));
    for (int i = 1; i < 256; i++) { CHECK(t[i] >= t[i - 1]); }
}

TEST(whiteExtractionAndBrightness) {
    const auto lin = gammaTable(1.0f);
    int        out[4];
    processColor(255, 255, 255, true, lin, 1, out);
    CHECK_EQ(show(std::vector<int>(out, out + 4)), std::string("{0, 0, 0, 255}"));
    processColor(200, 100, 50, true, lin, 1, out);
    CHECK_EQ(show(std::vector<int>(out, out + 4)), std::string("{150, 50, 0, 50}"));
    processColor(200, 100, 50, false, lin, 1, out);
    CHECK_EQ(show(std::vector<int>(out, out + 4)), std::string("{200, 100, 50, 0}"));
    const auto g = gammaTable(2.5f);
    processColor(200, 100, 50, true, g, 0.5f, out);
    CHECK_EQ(out[0], int(std::lround(g[150] * 0.5f)));
    CHECK_EQ(out[2], 0);
    processColor(200, 100, 50, true, g, 0, out);
    CHECK_EQ(show(std::vector<int>(out, out + 4)), std::string("{0, 0, 0, 0}"));
}

// ------------------------------------------------------------------ discovery

TEST(parsesMacArp) {
    const auto m = Discovery::parseArp("? (2.0.0.1) at 2:b6:c0:be:4d:0 on en6 ifscope [ethernet]\n"
                                       "? (2.0.0.255) at ff:ff:ff:ff:ff:ff on en6 ifscope [ethernet]\n"
                                       "? (192.168.1.1) at a4:2b:b0:11:22:33 on en0 ifscope [ethernet]\n"
                                       "? (192.168.1.40) at (incomplete) on en0 ifscope [ethernet]");
    CHECK_EQ(m.at("en6"), std::set<std::string>{"2.0.0.1"});
    CHECK_EQ(m.at("en0"), std::set<std::string>{"192.168.1.1"});
}

TEST(parsesLinuxArp) {
    const auto m = Discovery::parseArp("? (2.0.0.1) at 02:b6:c0:be:4d:00 [ether] on usb0");
    CHECK_EQ(m.at("usb0"), std::set<std::string>{"2.0.0.1"});
}

TEST(parsesMacDefaultRoutes) {
    std::set<std::string> ifaces, gws;
    Discovery::parseMacRoutes("Routing tables\n"
                              "Destination        Gateway            Flags               Netif Expire\n"
                              "default            192.168.1.1        UGScg                 en0\n"
                              "2/24               link#22            UCS                   en6      !",
                              ifaces, gws);
    CHECK_EQ(ifaces, std::set<std::string>{"en0"});
    CHECK_EQ(gws, std::set<std::string>{"192.168.1.1"});
}

TEST(subnetsAndParsing) {
    const uint32_t a = *Discovery::parse("2.0.0.2");
    CHECK(Discovery::sameSubnet(a, *Discovery::parse("2.0.0.1"), 24));
    CHECK(!Discovery::sameSubnet(a, *Discovery::parse("2.0.1.1"), 24));
    CHECK(Discovery::sameSubnet(a, *Discovery::parse("2.200.3.4"), 8));
    CHECK(!Discovery::parse("not-an-ip"));
    CHECK(!Discovery::parse("example.com"));
    CHECK(!Discovery::parse("300.1.1.1"));
    CHECK_EQ(Discovery::format(a), std::string("2.0.0.2"));
    CHECK_EQ(Discovery::interfaceFor("127.0.0.1")->iface, std::string("loopback"));
    CHECK_THROWS_WITH(std::invalid_argument, TestAccess::construct("localhost"), "is not an IP address");
}

// ------------------------------------------------------------------ fixtures

TEST(newFixtureIsAllZeroAndManual) {
    auto m = mapper();
    m->fixture(4, 1)->setPos(0, 0);
    sample(*m, {rgb(255, 255, 255)}, 1, 1);
    CHECK_EQ(ch(*m, 0, 1, 4), (std::vector<int>{0, 0, 0, 0}));
}

TEST(setValIsRawAndClamped) {
    auto m = mapper();
    m->gamma(2.5f)->brightness(0.5f);
    Fixture* f = m->fixture(3, 1);
    f->setVal(0, 200)->setVal(1, 300)->setVal(2, -5);
    sample(*m, {}, 0, 0);
    CHECK_EQ(ch(*m, 0, 1, 3), (std::vector<int>{200, 255, 0})); // no gamma, no brightness
    f->setVal(0, 127.6f);
    sample(*m, {}, 0, 0);
    CHECK_EQ(m->getUniverse(0)[0], 128);
    CHECK_EQ(f->getVal(0), 128);
}

TEST(setValsSetsFromChannelZero) {
    auto     m = mapper();
    Fixture* f = m->fixture(6, 10);
    f->setVals(255, 0, 7);
    sample(*m, {}, 0, 0);
    CHECK_EQ(ch(*m, 0, 10, 15), (std::vector<int>{255, 0, 7, 0, 0, 0}));
    CHECK_THROWS(std::invalid_argument, f->setVals(1, 2, 3, 4, 5, 6, 7));
}

TEST(channelRangeChecked) {
    auto     m = mapper();
    Fixture* f = m->fixture(4, 1);
    CHECK_THROWS_WITH(std::invalid_argument, f->setVal(4, 1), "numbered 0-3");
    CHECK_THROWS(std::invalid_argument, f->setVal(-1, 1));
    CHECK_THROWS(std::invalid_argument, f->colorChannels(0, 1, 4));
    CHECK_THROWS(std::invalid_argument, f->colorChannels(0, 0, 1));
    CHECK_THROWS(std::invalid_argument, m->fixture(0, 1));
}

TEST(eightChannelModeWithColorChannels) {
    auto     m = mapper(); // dimmer r g b w strobe mode speed
    Fixture* f = m->fixture(8, 1)->setPos(0, 0)->colorChannels(1, 2, 3, 4);
    f->setVal(0, 255)->setVal(6, 200);
    sample(*m, {rgb(200, 100, 50)}, 1, 1); // w = 50
    CHECK_EQ(ch(*m, 0, 1, 8), (std::vector<int>{255, 150, 50, 0, 50, 0, 200, 0}));
}

TEST(rgbOnlyHasNoWhite) {
    auto m = mapper();
    m->fixture(3, 1)->setPos(0, 0)->colorChannels(0, 1, 2);
    sample(*m, {rgb(255, 255, 255)}, 1, 1);
    CHECK_EQ(ch(*m, 0, 1, 3), (std::vector<int>{255, 255, 255}));
}

TEST(whiteModeOffLeavesWAtZero) {
    auto m = mapper();
    m->fixture(4, 1)->setPos(0, 0)->colorChannels(0, 1, 2, 3)->whiteMode(false);
    sample(*m, {rgb(200, 100, 50)}, 1, 1);
    CHECK_EQ(ch(*m, 0, 1, 4), (std::vector<int>{200, 100, 50, 0}));
}

TEST(manualValueWinsUntilRelease) {
    auto                        m     = mapper();
    Fixture*                    f     = m->fixture(4, 1)->setPos(0, 0)->colorChannels(0, 1, 2, 3);
    const std::vector<uint32_t> green = {rgb(0, 255, 0)};
    f->setVals(255, 0, 0, 0);
    sample(*m, green, 1, 1);
    CHECK_EQ(ch(*m, 0, 1, 4), (std::vector<int>{255, 0, 0, 0}));
    sample(*m, green, 1, 1); // still holds next frame
    CHECK_EQ(ch(*m, 0, 1, 4), (std::vector<int>{255, 0, 0, 0}));
    f->release(0);
    sample(*m, green, 1, 1);
    CHECK_EQ(ch(*m, 0, 1, 4), (std::vector<int>{0, 0, 0, 0})); // ch0 follows the canvas (red 0), ch1 still manual 0
    f->release();
    sample(*m, green, 1, 1);
    CHECK_EQ(ch(*m, 0, 1, 4), (std::vector<int>{0, 255, 0, 0}));
}

TEST(colorChannelsCorrectedManualNot) {
    auto m = mapper();
    m->gamma(2.5f)->brightness(0.5f);
    Fixture* f = m->fixture(4, 1)->setPos(0, 0)->colorChannels(0, 1, 2);
    f->setVal(3, 128);
    sample(*m, {rgb(128, 128, 128)}, 1, 1);
    const int e = int(std::lround(gammaTable(2.5f)[128] * 0.5f));
    CHECK_EQ(ch(*m, 0, 1, 4), (std::vector<int>{e, e, e, 128}));
}

TEST(stripSetValAllAndPixel) {
    auto                        m    = mapper();
    Fixture*                    s    = m->strip(3, 1, 4)->line(0, 0, 3, 0)->colorChannels(0, 1, 2);
    const std::vector<uint32_t> blue = {rgb(0, 0, 255), rgb(0, 0, 255), rgb(0, 0, 255), rgb(0, 0, 255)};
    s->pixel(2)->setVals(255, 0, 0);
    sample(*m, blue, 4, 1);
    CHECK_EQ(ch(*m, 0, 1, 12), (std::vector<int>{0, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0, 255}));
    CHECK_EQ(s->pixel(2)->getVal(0), 255);
    s->pixel(2)->release();
    s->setVal(1, 9);
    sample(*m, blue, 4, 1);
    CHECK_EQ(ch(*m, 0, 1, 12), (std::vector<int>{0, 9, 255, 0, 9, 255, 0, 9, 255, 0, 9, 255}));
    CHECK_THROWS_WITH(std::invalid_argument, s->pixel(4), "numbered 0-3");
}

TEST(pastAddress512Throws) {
    auto m = mapper();
    CHECK_THROWS_WITH(std::invalid_argument, m->matrix(3, 0, 400, 8, 8),
                      "matrix at DMX 400 needs 192 channels, but a universe ends at 512. Use DMX address 321 or lower");
    CHECK_THROWS_WITH(std::invalid_argument, m->matrix(3, 0, 1, 16, 16), "needs 768 channels, but one universe holds 512");
    CHECK_THROWS(std::invalid_argument, m->strip(3, 0, 511, 1));
}

TEST(exactFitAtEndOfUniverse) {
    auto m = mapper();
    CHECK_EQ(m->strip(3, 0, 1, 170)->lastAddress(), 510);
    CHECK_EQ(m->fixture(2, 511)->lastAddress(), 512);
    CHECK_THROWS(std::invalid_argument, m->fixture(3, 511));
    CHECK_THROWS_WITH(std::invalid_argument, m->fixture(3, 0), "DMX addresses start at 1");
    CHECK_THROWS(std::invalid_argument, m->fixture(3, -1, 1));
}

TEST(overlapOnlyWarns) {
    auto              m   = mapper();
    const std::string out = captured([&] {
        m->fixture(4, 1);
        m->fixture(4, 3); // overlaps 3-4
        m->fixture(4, 1, 1); // other universe: fine
    });
    CHECK_EQ(count(out, "warning"), 1);
    CHECK_EQ(m->fixtures().size(), size_t(3));
}

TEST(unplacedLinkedFixtureWarnsOnceManualDoesNot) {
    auto              m   = mapper();
    const std::string out = captured([&] {
        m->strip(3, 1, 2)->colorChannels(0, 1, 2);
        m->fixture(3, 20)->setVal(0, 5);
        sample(*m, {rgb(1, 2, 3)}, 1, 1);
        sample(*m, {rgb(1, 2, 3)}, 1, 1);
    });
    CHECK_EQ(count(out, "has no position"), 1);
    CHECK(out.find("line(x1, y1, x2, y2)") != std::string::npos);
}

TEST(lineSpreadsFromFirstToLast) {
    auto     m = mapper();
    Fixture* s = m->strip(3, 1, 5)->line(0, 0, 100, 0);
    CHECK_NEAR(s->pixel(0)->x(), 0);
    CHECK_NEAR(s->pixel(1)->x(), 25);
    CHECK_NEAR(s->pixel(4)->x(), 100);
}

TEST(lineToWidthReadsEdgePixel) {
    auto m = mapper();
    m->strip(3, 1, 3)->line(0, 5, 100, 5)->colorChannels(0, 1, 2);
    sample(*m, std::vector<uint32_t>(100 * 10, rgb(255, 255, 255)), 100, 10);
    CHECK_EQ(ch(*m, 0, 1, 9), (std::vector<int>(9, 255)));
}

TEST(rectOnlyForMatrix) {
    auto     m = mapper();
    Fixture* p = m->fixture(3, 1);
    CHECK_THROWS_WITH(std::logic_error, p->rect(0, 0, 10, 10), "Place this fixture with setPos(x, y)");
    CHECK_THROWS(std::logic_error, p->rotate(1));
}

TEST(placingAnotherWayLeavesTheGrid) {
    auto     m  = mapper();
    Fixture* mx = m->matrix(3, 1, 2, 2)->rect(0, 0, 10, 10);
    mx->setPos(7, 7);
    mx->rotate(1);
    CHECK_NEAR(mx->pixel(0)->x(), 7);
    CHECK(!TestAccess::hasRect(*mx));
}

TEST(infoGetters) {
    auto     m = mapper();
    Fixture* s = m->strip(3, 1, 9, 30);
    CHECK_EQ(s->address(), 9);
    CHECK_EQ(s->lastAddress(), 98);
    CHECK_EQ(s->universe(), 1);
    CHECK_EQ(s->channels(), 3);
    CHECK_EQ(s->pixels(), 30);
    CHECK_EQ(s->totalChannels(), 90);
    Fixture* p = m->fixture(3, 200)->setPos(12, 34)->sampleRadius(2)->whiteMode(false);
    CHECK_NEAR(p->x(), 12);
    CHECK_NEAR(p->y(), 34);
    CHECK_EQ(p->getSampleRadius(), 2);
    CHECK(!p->getWhiteMode());
    CHECK_NEAR(p->getGamma(), 1.0f); // follows the global gamma
    m->gamma(2.2f);
    CHECK_NEAR(p->getGamma(), 2.2f);
    p->gamma(1.8f);
    CHECK_NEAR(p->getGamma(), 1.8f);
    m->brightness(0.4f)->maxRate(30);
    CHECK_NEAR(m->getBrightness(), 0.4f);
    CHECK_NEAR(m->getMaxRate(), 30.0f);
    CHECK_THROWS(std::invalid_argument, m->maxRate(0));
    CHECK_THROWS(std::invalid_argument, m->gamma(0));
}

TEST(toStringSaysWhatAndWhere) {
    auto m = mapper();
    CHECK_EQ(m->strip(3, 1, 9, 30)->toString(), std::string("strip, 30 pixels x 3 channels, universe 1, DMX 9-98"));
    CHECK_EQ(m->fixture(1, 1)->toString(), std::string("fixture, 1 channel, universe 0, DMX 1-1"));
    CHECK_EQ(m->matrix(3, 100, 2, 3)->toString(), std::string("matrix, 2x3 pixels x 3 channels, universe 0, DMX 100-117"));
    std::ostringstream o;
    o << *m->fixture(4, 300);
    CHECK_EQ(o.str(), std::string("fixture, 4 channels, universe 0, DMX 300-303"));
}

// ------------------------------------------------------------------ mapping

static std::pair<int, int> cell(int i, int cols, int rows, int wiring, int corner, int order) {
    int c, r;
    Fixture::gridPosition(i, cols, rows, wiring, corner, order, c, r);
    return {c, r};
}

TEST(gridWiring) {
    using A = ArtNetMapper;
    CHECK(cell(0, 4, 3, A::PROGRESSIVE, A::TOP_LEFT, A::ROWS) == std::make_pair(0, 0));
    CHECK(cell(3, 4, 3, A::PROGRESSIVE, A::TOP_LEFT, A::ROWS) == std::make_pair(3, 0));
    CHECK(cell(4, 4, 3, A::PROGRESSIVE, A::TOP_LEFT, A::ROWS) == std::make_pair(0, 1));
    CHECK(cell(4, 4, 3, A::SERPENTINE, A::TOP_LEFT, A::ROWS) == std::make_pair(3, 1));
    CHECK(cell(7, 4, 3, A::SERPENTINE, A::TOP_LEFT, A::ROWS) == std::make_pair(0, 1));
    CHECK(cell(8, 4, 3, A::SERPENTINE, A::TOP_LEFT, A::ROWS) == std::make_pair(0, 2));
    CHECK(cell(1, 4, 3, A::PROGRESSIVE, A::TOP_LEFT, A::COLUMNS) == std::make_pair(0, 1));
    CHECK(cell(3, 4, 3, A::SERPENTINE, A::TOP_LEFT, A::COLUMNS) == std::make_pair(1, 2));
    CHECK(cell(0, 4, 3, A::PROGRESSIVE, A::TOP_RIGHT, A::ROWS) == std::make_pair(3, 0));
    CHECK(cell(0, 4, 3, A::PROGRESSIVE, A::BOTTOM_LEFT, A::ROWS) == std::make_pair(0, 2));
    CHECK(cell(0, 4, 3, A::PROGRESSIVE, A::BOTTOM_RIGHT, A::ROWS) == std::make_pair(3, 2));
}

TEST(every16WiringIsAPermutation) {
    using A        = ArtNetMapper;
    const int cols = 5, rows = 4;
    for (const int w: {A::PROGRESSIVE, A::SERPENTINE}) {
        for (const int c: {A::TOP_LEFT, A::TOP_RIGHT, A::BOTTOM_LEFT, A::BOTTOM_RIGHT}) {
            for (const int o: {A::ROWS, A::COLUMNS}) {
                std::vector<bool> seen(cols * rows, false);
                for (int i = 0; i < cols * rows; i++) {
                    const auto p = cell(i, cols, rows, w, c, o);
                    const int  k = p.second * cols + p.first;
                    CHECK(!seen[k]);
                    seen[k] = true;
                }
            }
        }
    }
}

TEST(rectPlacesCentersAndRotateAboutCenter) {
    auto     m  = mapper();
    Fixture* mx = m->matrix(3, 1, 2, 2)->rect(0, 0, 100, 100);
    CHECK_NEAR(mx->pixel(0)->x(), 25);
    CHECK_NEAR(mx->pixel(0)->y(), 25);
    CHECK_NEAR(mx->pixel(1)->x(), 75);
    mx->rotate(static_cast<float>(M_PI)); // 180 degrees: pixel 0 moves to bottom-right
    CHECK_NEAR(mx->pixel(0)->x(), 75);
    CHECK_NEAR(mx->pixel(0)->y(), 75);
}

TEST(serpentineMatrixSamplesInDmxOrder) {
    auto m = mapper();
    m->matrix(3, 1, 2, 2)->rect(0, 0, 2, 2)->colorChannels(0, 1, 2)->wiring(ArtNetMapper::SERPENTINE, ArtNetMapper::TOP_LEFT, ArtNetMapper::ROWS);
    sample(*m, {rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255), rgb(255, 255, 255)}, 2, 2); // TL red, TR green, BL blue, BR white
    CHECK_EQ(ch(*m, 0, 1, 12), (std::vector<int>{255, 0, 0, 0, 255, 0, 255, 255, 255, 0, 0, 255})); // TL, TR, BR, BL
}

TEST(swappedWiringArgumentsAreRejected) {
    auto     m  = mapper();
    Fixture* mx = m->matrix(3, 1, 2, 2)->rect(0, 0, 10, 10);
    CHECK_THROWS_WITH(std::invalid_argument, mx->wiring(ArtNetMapper::TOP_LEFT, ArtNetMapper::SERPENTINE, ArtNetMapper::ROWS), "got 10");
}

TEST(perFixtureGammaOverridesGlobal) {
    auto m = TestAccess::mapper(); // global 2.5
    m->fixture(3, 1)->setPos(0, 0)->colorChannels(0, 1, 2)->gamma(1);
    m->fixture(3, 4)->setPos(0, 0)->colorChannels(0, 1, 2);
    sample(*m, {rgb(128, 128, 128)}, 1, 1);
    const auto u = m->getUniverse(0);
    CHECK_EQ(u[0], 128);
    CHECK_EQ(u[3], gammaTable(2.5f)[128]);
}

TEST(sampleRadiusAveragesAndOutsideIsBlack) {
    std::vector<uint32_t> px(9, rgb(0, 0, 0));
    px[4] = rgb(255, 255, 255); // white center of 3x3
    CHECK_EQ(TestAccess::samplePoint(px, 3, 3, 1, 1, 0) & 0xFFFFFF, 0xFFFFFFu);
    CHECK_EQ(TestAccess::samplePoint(px, 3, 3, 1, 1, 1), (255u / 9) * 0x010101u);
    CHECK_EQ(TestAccess::samplePoint(px, 3, 3, 10, 10, 0), 0u);
}

TEST(pixelDensityScalesCoordinates) {
    std::vector<uint32_t> px(16, rgb(0, 0, 0)); // 4x4 physical pixels, sketch is 2x2 at density 2
    px[2 * 4 + 2] = rgb(0, 255, 0);
    CHECK_EQ(TestAccess::samplePoint(px, 4, 4, 1, 1, 0, 2) & 0xFFFFFF, 0x00FF00u);
}

TEST(redIsTheLowByte) {
    auto m = mapper();
    m->fixture(3, 1)->setPos(0, 0)->colorChannels(0, 1, 2);
    sample(*m, {0xFF030201u}, 1, 1); // Umfeld: r = 0x01, g = 0x02, b = 0x03
    CHECK_EQ(ch(*m, 0, 1, 3), (std::vector<int>{1, 2, 3}));
}

// ------------------------------------------------------------------ sender

static constexpr int64_t MS = 1000000LL;

struct FakeNet {
    std::vector<std::vector<uint8_t>> sent;
    Sender::Sink                      sink() {
        return [this](const uint8_t* p, size_t n, uint32_t) { sent.emplace_back(p, p + n); };
    }
};

TEST(sendsChangedFrameThenWaitsForRateCap) {
    FakeNet net;
    Sender  s("127.0.0.1", 0x7F000001, net.sink());
    s.universe(0);
    s.changed();
    const int64_t now = 1000 * MS;
    s.step(now);
    CHECK_EQ(net.sent.size(), size_t(1));
    s.changed();
    const int64_t wait = s.step(now + 5 * MS); // 44 fps: 22.7 ms between frames
    CHECK_EQ(net.sent.size(), size_t(1));
    CHECK(wait > 15 * MS && wait < 20 * MS);
    s.step(now + 23 * MS);
    CHECK_EQ(net.sent.size(), size_t(2));
}

TEST(keepaliveEverySecondWhenIdle) {
    FakeNet net;
    Sender  s("127.0.0.1", 0x7F000001, net.sink());
    s.universe(0);
    const int64_t now = 1000 * MS;
    s.step(now); // first frame goes out (never sent before)
    CHECK_EQ(net.sent.size(), size_t(1));
    CHECK_EQ(s.step(now), 1000 * MS);
    s.step(now + 999 * MS);
    CHECK_EQ(net.sent.size(), size_t(1));
    s.step(now + 1000 * MS);
    CHECK_EQ(net.sent.size(), size_t(2));
}

TEST(sequencePerUniverseRuns1To255) {
    FakeNet net;
    Sender  s("127.0.0.1", 0x7F000001, net.sink());
    s.universe(0);
    s.universe(3);
    const int64_t now = 1000 * MS;
    for (int i = 0; i < 300; i++) {
        s.changed();
        s.step(now + i * 30 * MS);
    }
    CHECK_EQ(net.sent.size(), size_t(600));
    bool ok = true;
    for (size_t i = 0; i < net.sent.size(); i += 2) {
        const int expected = int(i / 2) % 255 + 1;
        ok &= net.sent[i][12] == expected && net.sent[i + 1][12] == expected && net.sent[i + 1][14] == 3;
    }
    CHECK(ok);
}

TEST(packetCarriesCurrentData) {
    FakeNet           net;
    Sender            s("127.0.0.1", 0x7F000001, net.sink());
    Sender::Universe& u = s.universe(2);
    {
        std::lock_guard<std::mutex> g(s.lock);
        u.data[0]   = 7;
        u.data[511] = 200;
    }
    s.changed();
    s.step(1000 * MS);
    CHECK_EQ(int(net.sent[0][18]), 7);
    CHECK_EQ(int(net.sent[0][18 + 511]), 200);
    CHECK_EQ(int(net.sent[0][14]), 2);
}

TEST(blackoutSwitchSendsZerosButKeepsData) {
    FakeNet           net;
    Sender            s("127.0.0.1", 0x7F000001, net.sink());
    Sender::Universe& u = s.universe(0);
    u.data[5]           = 9;
    s.blackout(true);
    CHECK(s.isBlackout());
    const int64_t now = 1000 * MS;
    s.step(now);
    CHECK_EQ(int(net.sent[0][18 + 5]), 0); // sent as 0
    CHECK_EQ(int(u.data[5]), 9);           // value kept
    s.changed();
    s.step(now + 30 * MS);
    CHECK_EQ(int(net.sent[1][18 + 5]), 0); // stays black while data changes
    s.blackout(false);
    s.step(now + 60 * MS);
    CHECK_EQ(int(net.sent[2][18 + 5]), 9); // back to the current value at once
}

TEST(closeSendsThreeBlackFrames) {
    FakeNet           net;
    Sender            s("127.0.0.1", 0x7F000001, net.sink());
    Sender::Universe& u = s.universe(0);
    u.data[0]           = 50;
    s.close(true);
    CHECK_EQ(net.sent.size(), size_t(3));
    CHECK_EQ(int(net.sent[2][18]), 0);
    CHECK(s.isClosed());
}

TEST(connectionStates) {
    CHECK_EQ(Sender::nextStatus(0, true), 1);  // first check, on a link
    CHECK_EQ(Sender::nextStatus(0, false), 2); // first check, routed
    CHECK_EQ(Sender::nextStatus(1, false), 3); // link lost
    CHECK_EQ(Sender::nextStatus(3, false), 3); // still lost
    CHECK_EQ(Sender::nextStatus(3, true), 1);  // back
    CHECK_EQ(Sender::nextStatus(2, false), 2); // routed stays routed
}

int main() {
    for (auto& [name, fn]: tests()) {
        current        = name;
        const int fail = failures;
        try {
            fn();
        } catch (const std::exception& e) {
            failures++;
            std::cout << "  FAIL " << name << ": unexpected exception: " << e.what() << std::endl;
        }
        if (failures == fail) { std::cout << "  ok   " << name << std::endl; }
    }
    std::cout << tests().size() << " tests, " << checks << " checks, " << failures << " failures" << std::endl;
    return failures == 0 ? 0 : 1;
}

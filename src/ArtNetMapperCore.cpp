// ArtNetMapper for Umfeld :: fixtures, settings and sampling. No Umfeld calls here,
// so the unit tests build this file without Umfeld. MIT License.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "ArtNetMapper.h"
#include "artnetmapper/Discovery.h"
#include "artnetmapper/Log.h"
#include "artnetmapper/Sender.h"

namespace umfeld {

    namespace artnetmapper {
        std::function<void(const std::string&)>& Log::infoOut() {
            static std::function<void(const std::string&)> f = [](const std::string& s) { std::cout << s << std::endl; };
            return f;
        }
        std::function<void(const std::string&)>& Log::warnOut() {
            static std::function<void(const std::string&)> f = [](const std::string& s) { std::cout << s << std::endl; };
            return f;
        }
    } // namespace artnetmapper

    using artnetmapper::Log;
    using artnetmapper::Sender;

    void (*ArtNetMapper::onDestroy)(ArtNetMapper*) = nullptr;

    static void fail(const std::string& message) {
        throw std::invalid_argument("ArtNetMapper: " + message);
    }

    ArtNetMapper::ArtNetMapper(const std::string& ip, const bool network) : ip_(ip) {
        init(network);
    }

    void ArtNetMapper::init(const bool network) {
        verbose_                  = network;
        const auto first          = ip_.find_first_not_of(" \t");
        const auto last           = ip_.find_last_not_of(" \t");
        ip_                       = first == std::string::npos ? "" : ip_.substr(first, last - first + 1);
        const auto address        = artnetmapper::Discovery::parse(ip_);
        if (!address || *address == 0) {
            fail("\"" + ip_ + "\" is not an IP address. Pass an IP from ArtNetMapper::list(), or type one like \"2.0.0.1\".");
        }
        if (network) {
            Log::info(std::string("version ") + VERSION);
            sender_ = std::make_unique<Sender>(ip_, *address);
        } else {
            sender_ = std::make_unique<Sender>(ip_, *address, Sender::Sink{});
        }
    }

    ArtNetMapper::~ArtNetMapper() {
        if (onDestroy != nullptr) { onDestroy(this); }
        close();
    }

    void ArtNetMapper::close() {
        if (sender_ == nullptr || sender_->isClosed()) { return; }
        sender_->close(blackoutOnExit_);
    }

    bool ArtNetMapper::isConnected() const {
        return sender_->isSending();
    }

    // ------------------------------------------------------------------ fixtures

    Fixture* ArtNetMapper::fixture(const int channels, const int address) {
        return fixture(channels, 0, address);
    }

    Fixture* ArtNetMapper::fixture(const int channels, const int universe, const int address) {
        precheck("fixture", channels, universe, address, 1);
        return add(new Fixture(channels, universe, address, Fixture::PAR, 1, 1));
    }

    Fixture* ArtNetMapper::strip(const int channelsPerPixel, const int address, const int pixels) {
        return strip(channelsPerPixel, 0, address, pixels);
    }

    Fixture* ArtNetMapper::strip(const int channelsPerPixel, const int universe, const int address, const int pixels) {
        if (pixels < 1) { fail("a strip needs at least 1 pixel, got " + std::to_string(pixels)); }
        precheck("strip", channelsPerPixel, universe, address, pixels);
        Fixture* f = add(new Fixture(channelsPerPixel, universe, address, Fixture::STRIP, pixels, 1));
        if (channelsPerPixel > pixels) {
            Log::warn(f->toString() + " has more channels per pixel than pixels. Did you mean strip(" + std::to_string(pixels) + ", " +
                      std::to_string(universe) + ", " + std::to_string(address) + ", " + std::to_string(channelsPerPixel) +
                      ")? Order: channelsPerPixel, universe, address, pixels.");
        }
        return f;
    }

    Fixture* ArtNetMapper::matrix(const int channelsPerPixel, const int address, const int cols, const int rows) {
        return matrix(channelsPerPixel, 0, address, cols, rows);
    }

    Fixture* ArtNetMapper::matrix(const int channelsPerPixel, const int universe, const int address, const int cols, const int rows) {
        if (cols < 1 || rows < 1) {
            fail("a matrix needs at least 1x1 pixels, got " + std::to_string(cols) + "x" + std::to_string(rows));
        }
        precheck("matrix", channelsPerPixel, universe, address, static_cast<long long>(cols) * rows);
        Fixture* f = add(new Fixture(channelsPerPixel, universe, address, Fixture::MATRIX, cols, rows));
        if (channelsPerPixel > static_cast<long long>(cols) * rows) {
            Log::warn(f->toString() + " has more channels per pixel than pixels. Order: channelsPerPixel, universe, address, cols, rows.");
        }
        return f;
    }

    void ArtNetMapper::precheck(const std::string& what, const int channels, const int universe, const int address, const long long pixels) {
        if (channels < 1) {
            fail("a " + what + " needs at least 1 channel" + (what == "fixture" ? "" : " per pixel") + ", got " + std::to_string(channels));
        }
        if (universe < 0 || universe > 0x7FFF) {
            fail("universe must be 0-32767 (counted from 0), got " + std::to_string(universe));
        }
        if (address < 1 || address > 512) {
            fail("DMX address must be 1-512, as set on the light, got " + std::to_string(address) +
                 (address == 0 ? ". DMX addresses start at 1." : "."));
        }
        const long long needed = pixels * channels;
        if (needed > 512) {
            fail("this " + what + " needs " + std::to_string(needed) +
                 " channels, but one universe holds 512. Split it into smaller fixtures on separate universes.");
        }
        if (address - 1 + needed > 512) {
            fail(what + " at DMX " + std::to_string(address) + " needs " + std::to_string(needed) +
                 " channels, but a universe ends at 512. Use DMX address " + std::to_string(513 - needed) +
                 " or lower, or put it on another universe.");
        }
    }

    Fixture* ArtNetMapper::add(Fixture* raw) {
        std::unique_ptr<Fixture> f(raw);
        f->universeData_ = sender_->universe(f->universe_).data;
        if (verbose_) { Log::info("added " + f->toString()); }
        for (const auto& o: fixtures_) {
            if (o->universe_ == f->universe_ && f->address_ <= o->lastAddress() && o->address_ <= f->lastAddress()) {
                Log::warn(f->toString() + " uses the same DMX channels as " + o->toString() + ". Both write to them; the one added last wins.");
            }
        }
        f->mapper_ = this;
        fixtures_.push_back(std::move(f));
        return raw;
    }

    void ArtNetMapper::linkChanged() {
        anyLinked_ = std::any_of(fixtures_.begin(), fixtures_.end(), [](const auto& f) { return f->colorCount_ > 0; });
    }

    std::vector<Fixture*> ArtNetMapper::fixtures() const {
        std::vector<Fixture*> out;
        for (const auto& f: fixtures_) { out.push_back(f.get()); }
        return out;
    }

    // ------------------------------------------------------------------ settings

    ArtNetMapper* ArtNetMapper::source(PGraphics* pg) {
        source_ = pg;
        return this;
    }

    ArtNetMapper* ArtNetMapper::gamma(const float gamma) {
        gamma_    = checkGamma(gamma);
        gammaLut_ = artnetmapper::gammaTable(gamma);
        return this;
    }

    float ArtNetMapper::checkGamma(const float gamma) {
        if (!(gamma > 0)) {
            fail("gamma must be more than 0 (1.0 = no correction, 2.5 = default), got " + std::to_string(gamma));
        }
        return gamma;
    }

    ArtNetMapper* ArtNetMapper::brightness(const float level) {
        brightness_ = std::max(0.0f, std::min(1.0f, level));
        return this;
    }

    float ArtNetMapper::getMaxRate() const {
        return sender_->getMaxRate();
    }

    ArtNetMapper* ArtNetMapper::maxRate(const float fps) {
        if (!(fps > 0)) { fail("maxRate must be more than 0 frames per second (default 44), got " + std::to_string(fps)); }
        sender_->maxRate(fps);
        return this;
    }

    ArtNetMapper* ArtNetMapper::autoSend(const bool on) {
        autoSend_ = on;
        return this;
    }

    ArtNetMapper* ArtNetMapper::showLocations(const bool on) {
        showLocations_ = on;
        return this;
    }

    ArtNetMapper* ArtNetMapper::blackoutOnExit(const bool on) {
        blackoutOnExit_ = on;
        return this;
    }

    std::vector<int> ArtNetMapper::getUniverse(const int universe) const {
        std::vector<int>            out(512, 0);
        std::lock_guard<std::mutex> g(sender_->lock);
        const auto                  it = sender_->universes().find(universe);
        if (it != sender_->universes().end()) {
            for (int i = 0; i < 512; i++) { out[i] = it->second.data[i]; }
        }
        return out;
    }

    ArtNetMapper* ArtNetMapper::blackout(const bool on) {
        sender_->blackout(on);
        return this;
    }

    bool ArtNetMapper::isBlackout() const {
        return sender_->isBlackout();
    }

    // ------------------------------------------------------------------ sampling

    void ArtNetMapper::sample(const uint32_t* pixels, const int pw, const int ph, const int density) {
        if (sender_->isClosed()) { return; }
        bool changed = false;
        {
            std::lock_guard<std::mutex> g(sender_->lock);
            for (const auto& fp: fixtures_) {
                Fixture&      f      = *fp;
                uint8_t*      buf    = f.universeData_;
                const int     size   = f.channels_;
                const bool    linked = f.colorCount_ > 0 && pixels != nullptr;
                const auto&   lut    = f.gammaLut_ ? *f.gammaLut_ : gammaLut_;
                const bool    white  = f.colorCount_ == 4 && f.whiteMode_;
                if (linked && !f.placed_ && !f.warnedUnplaced_) {
                    f.warnedUnplaced_ = true;
                    Log::warn(f.toString() + " follows the canvas but has no position. Call " + f.placeHint() +
                              ". Until then it reads the pixel at (0, 0).");
                }
                for (int e = 0; e < f.count_; e++) {
                    if (linked) {
                        const uint32_t c = samplePoint(pixels, pw, ph, density, f.px_[e], f.py_[e], f.sampleRadius_);
                        f.sampled_[e]    = c;
                        artnetmapper::processColor(c & 0xFF, c >> 8 & 0xFF, c >> 16 & 0xFF, white, lut, brightness_, rgbw_);
                    }
                    const int base = e * size;
                    const int dmx  = f.address_ - 1 + base;
                    for (int k = 0; k < size; k++) {
                        const int i = base + k;
                        const int v = linked && f.role_[k] >= 0 && !f.manual_[i] ? rgbw_[f.role_[k]] : f.values_[i];
                        f.output_[i] = v;
                        if (buf[dmx + k] != static_cast<uint8_t>(v)) {
                            buf[dmx + k] = static_cast<uint8_t>(v);
                            changed      = true;
                        }
                    }
                }
            }
        }
        if (changed) { sender_->changed(); }
    }

    uint32_t ArtNetMapper::samplePoint(const uint32_t* pixels, const int w, const int h, const int density, const float x, const float y, const int r) {
        const int cx = static_cast<int>(std::floor(x * density)), cy = static_cast<int>(std::floor(y * density)); // pixel containing the point
        const int radius = r * density;
        // Points up to one pixel past the edge (e.g. line(0, y, width, y)) read the edge
        // pixel; points further outside read black.
        if (w <= 0 || h <= 0 || cx < -1 || cy < -1 || cx > w || cy > h) { return 0; }
        uint64_t sr = 0, sg = 0, sb = 0, n = 0;
        for (int yy = cy - radius; yy <= cy + radius; yy++) {
            const int sy = std::max(0, std::min(h - 1, yy));
            for (int xx = cx - radius; xx <= cx + radius; xx++) {
                const int      sx = std::max(0, std::min(w - 1, xx));
                const uint32_t c  = pixels[sy * w + sx];
                sr += c & 0xFF;
                sg += c >> 8 & 0xFF;
                sb += c >> 16 & 0xFF;
                n++;
            }
        }
        return static_cast<uint32_t>(sr / n | (sg / n) << 8 | (sb / n) << 16);
    }

} // namespace umfeld

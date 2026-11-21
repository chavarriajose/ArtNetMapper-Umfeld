// ArtNetMapper for Umfeld :: a DMX light (par, strip or matrix). MIT License.

#include "artnetmapper/Fixture.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "ArtNetMapper.h"

namespace umfeld {

    static std::string plural(const int n, const std::string& word) {
        return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
    }

    static void fail(const std::string& message) {
        throw std::invalid_argument("ArtNetMapper: " + message);
    }

    Fixture::Fixture(const int channels, const int universe, const int address, const int kind, const int cols, const int rows)
        : channels_(channels), universe_(universe), address_(address), kind_(kind), cols_(cols), rows_(rows), count_(cols * rows),
          values_(count_ * channels, 0), manual_(count_ * channels, false), output_(count_ * channels, 0), role_(channels, -1),
          px_(count_, 0.0f), py_(count_, 0.0f), sampled_(count_, 0), pixelObjects_(count_),
          wiring_(ArtNetMapper::PROGRESSIVE), corner_(ArtNetMapper::TOP_LEFT), order_(ArtNetMapper::ROWS) {}

    // ------------------------------------------------------------------ info

    std::string Fixture::toString() const {
        std::string what;
        if (kind_ == PAR) {
            what = "fixture, " + plural(channels_, "channel");
        } else if (kind_ == STRIP) {
            what = "strip, " + plural(count_, "pixel") + " x " + plural(channels_, "channel");
        } else {
            what = "matrix, " + std::to_string(cols_) + "x" + std::to_string(rows_) + " pixels x " + plural(channels_, "channel");
        }
        return what + ", universe " + std::to_string(universe_) + ", DMX " + std::to_string(address_) + "-" + std::to_string(lastAddress());
    }

    std::ostream& operator<<(std::ostream& out, const Fixture& f) {
        return out << f.toString();
    }

    std::string Fixture::kindName() const {
        return kind_ == PAR ? "fixture" : kind_ == STRIP ? "strip" : "matrix";
    }

    std::string Fixture::placeHint() const {
        return kind_ == PAR ? "setPos(x, y)" : kind_ == STRIP ? "line(x1, y1, x2, y2)" : "rect(x, y, w, h)";
    }

    std::string Fixture::perPixel() const {
        return kind_ == PAR ? "" : " per pixel";
    }

    // ------------------------------------------------------------------ placement

    Fixture* Fixture::setPos(const float x, const float y) {
        placed_  = true;
        hasRect_ = false;
        std::fill(px_.begin(), px_.end(), x);
        std::fill(py_.begin(), py_.end(), y);
        return this;
    }

    Fixture* Fixture::line(const float x1, const float y1, const float x2, const float y2) {
        placed_  = true;
        hasRect_ = false;
        for (int i = 0; i < count_; i++) {
            const float t = count_ == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(count_ - 1);
            px_[i]        = x1 + (x2 - x1) * t;
            py_[i]        = y1 + (y2 - y1) * t;
        }
        return this;
    }

    Fixture* Fixture::rect(const float x, const float y, const float w, const float h) {
        requireMatrix("rect()");
        hasRect_ = true;
        placed_  = true;
        rx_      = x;
        ry_      = y;
        rw_      = w;
        rh_      = h;
        layoutGrid();
        return this;
    }

    Fixture* Fixture::rotate(const float radians) {
        requireMatrix("rotate()");
        angle_ = radians;
        layoutGrid();
        return this;
    }

    Fixture* Fixture::wiring(const int wiring, const int corner, const int order) {
        requireMatrix("wiring()");
        const std::string example = " Example: wiring(ArtNetMapper::SERPENTINE, ArtNetMapper::TOP_LEFT, ArtNetMapper::ROWS)";
        if (wiring != ArtNetMapper::PROGRESSIVE && wiring != ArtNetMapper::SERPENTINE) {
            fail("wiring(wiring, corner, order): wiring must be ArtNetMapper::PROGRESSIVE or ArtNetMapper::SERPENTINE, got " +
                 std::to_string(wiring) + "." + example);
        }
        if (corner < ArtNetMapper::TOP_LEFT || corner > ArtNetMapper::BOTTOM_RIGHT) {
            fail("wiring(wiring, corner, order): corner must be ArtNetMapper::TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT or BOTTOM_RIGHT, got " +
                 std::to_string(corner) + "." + example);
        }
        if (order != ArtNetMapper::ROWS && order != ArtNetMapper::COLUMNS) {
            fail("wiring(wiring, corner, order): order must be ArtNetMapper::ROWS or ArtNetMapper::COLUMNS, got " +
                 std::to_string(order) + "." + example);
        }
        wiring_ = wiring;
        corner_ = corner;
        order_  = order;
        layoutGrid();
        return this;
    }

    void Fixture::requireMatrix(const std::string& what) const {
        if (kind_ != MATRIX) {
            throw std::logic_error("ArtNetMapper: " + what + " is only for matrices. Place this " + kindName() + " with " + placeHint() + ".");
        }
    }

    // ------------------------------------------------------------------ canvas link

    Fixture* Fixture::colorChannels() { return link({}); }
    Fixture* Fixture::colorChannels(const int r, const int g, const int b) { return link({r, g, b}); }
    Fixture* Fixture::colorChannels(const int r, const int g, const int b, const int w) { return link({r, g, b, w}); }

    Fixture* Fixture::link(const std::initializer_list<int> rgbw) {
        std::vector<int> r(channels_, -1);
        int              i = 0;
        for (const int ch: rgbw) {
            checkChannel(ch);
            if (r[ch] >= 0) { fail("colorChannels(): channel " + std::to_string(ch) + " is given twice."); }
            r[ch] = i++;
        }
        role_       = r;
        colorCount_ = static_cast<int>(rgbw.size());
        for (int e = 0; e < count_; e++) {
            for (const int ch: rgbw) { manual_[e * channels_ + ch] = false; }
        }
        if (mapper_ != nullptr) { mapper_->linkChanged(); }
        return this;
    }

    Fixture* Fixture::sampleRadius(const int radius) {
        sampleRadius_ = std::max(0, radius);
        return this;
    }

    Fixture* Fixture::whiteMode(const bool on) {
        whiteMode_ = on;
        return this;
    }

    Fixture* Fixture::gamma(const float gamma) {
        gammaLut_ = std::make_unique<artnetmapper::GammaTable>(artnetmapper::gammaTable(ArtNetMapper::checkGamma(gamma)));
        gamma_    = gamma;
        return this;
    }

    float Fixture::getGamma() const {
        return std::isnan(gamma_) && mapper_ != nullptr ? mapper_->getGamma() : gamma_;
    }

    // ------------------------------------------------------------------ manual values

    Fixture* Fixture::setVal(const int ch, const float value) {
        checkChannel(ch);
        for (int e = 0; e < count_; e++) { put(e, ch, value); }
        return this;
    }

    Fixture* Fixture::setValsList(const std::initializer_list<float> values) {
        checkVals(values.size());
        for (int e = 0; e < count_; e++) {
            int k = 0;
            for (const float v: values) { put(e, k++, v); }
        }
        return this;
    }

    Fixture* Fixture::release() {
        std::fill(manual_.begin(), manual_.end(), false);
        return this;
    }

    Fixture* Fixture::release(const int ch) {
        checkChannel(ch);
        for (int e = 0; e < count_; e++) { manual_[e * channels_ + ch] = false; }
        return this;
    }

    int Fixture::getVal(const int ch) const {
        checkChannel(ch);
        return current(0, ch);
    }

    int Fixture::current(const int e, const int ch) const {
        const int i = e * channels_ + ch;
        return role_[ch] >= 0 && !manual_[i] ? output_[i] : values_[i];
    }

    Fixture::Pixel* Fixture::pixel(const int index) {
        checkPixel(index);
        auto& p = pixelObjects_[index];
        if (!p) { p.reset(new Pixel(this, index)); }
        return p.get();
    }

    void Fixture::put(const int e, const int ch, const float value) {
        const int i = e * channels_ + ch;
        values_[i]  = static_cast<int>(std::lround(std::max(0.0f, std::min(255.0f, value))));
        manual_[i]  = true;
    }

    void Fixture::checkChannel(const int ch) const {
        if (ch < 0 || ch >= channels_) {
            fail("channel " + std::to_string(ch) + " doesn't exist. This " + kindName() + " has " + plural(channels_, "channel") + perPixel() +
                 ", numbered 0-" + std::to_string(channels_ - 1) + ". Channels inside a fixture count from 0; the DMX address (" +
                 std::to_string(address_) + ") is set when it's created.");
        }
    }

    void Fixture::checkVals(const size_t n) const {
        if (n > static_cast<size_t>(channels_)) {
            fail("setVals() got " + std::to_string(n) + " values, but this " + kindName() + " has " + plural(channels_, "channel") + perPixel() +
                 " (0-" + std::to_string(channels_ - 1) + "). Pass at most " + std::to_string(channels_) + ".");
        }
    }

    void Fixture::checkPixel(const int index) const {
        if (index < 0 || index >= count_) {
            fail("pixel " + std::to_string(index) + " doesn't exist. This " + kindName() + " has " + plural(count_, "pixel") +
                 ", numbered 0-" + std::to_string(count_ - 1) + ".");
        }
    }

    // ------------------------------------------------------------------ Pixel

    Fixture::Pixel* Fixture::Pixel::setVal(const int ch, const float value) {
        f->checkChannel(ch);
        f->put(e, ch, value);
        return this;
    }

    Fixture::Pixel* Fixture::Pixel::setValsList(const std::initializer_list<float> values) {
        f->checkVals(values.size());
        int k = 0;
        for (const float v: values) { f->put(e, k++, v); }
        return this;
    }

    Fixture::Pixel* Fixture::Pixel::release() {
        for (int k = 0; k < f->channels_; k++) { f->manual_[e * f->channels_ + k] = false; }
        return this;
    }

    Fixture::Pixel* Fixture::Pixel::release(const int ch) {
        f->checkChannel(ch);
        f->manual_[e * f->channels_ + ch] = false;
        return this;
    }

    int Fixture::Pixel::getVal(const int ch) const {
        f->checkChannel(ch);
        return f->current(e, ch);
    }

    float Fixture::Pixel::x() const { return f->px_[e]; }
    float Fixture::Pixel::y() const { return f->py_[e]; }

    // ------------------------------------------------------------------ geometry

    float Fixture::minSpacing() const {
        float m = std::numeric_limits<float>::infinity();
        for (int i = 1; i < count_; i++) {
            const float dx = px_[i] - px_[i - 1], dy = py_[i] - py_[i - 1];
            m              = std::min(m, std::sqrt(dx * dx + dy * dy));
        }
        return m;
    }

    void Fixture::outline(const float margin, float out[8]) const {
        const float cx = rx_ + rw_ / 2.0f, cy = ry_ + rh_ / 2.0f;
        const float hw = rw_ / 2.0f + margin, hh = rh_ / 2.0f + margin;
        const float c = std::cos(angle_), s = std::sin(angle_);
        const float sx[4] = {-hw, hw, hw, -hw}, sy[4] = {-hh, -hh, hh, hh};
        for (int k = 0; k < 4; k++) {
            out[2 * k]     = cx + sx[k] * c - sy[k] * s;
            out[2 * k + 1] = cy + sx[k] * s + sy[k] * c;
        }
    }

    void Fixture::layoutGrid() {
        if (!hasRect_) { return; }
        const float cx = rx_ + rw_ / 2.0f, cy = ry_ + rh_ / 2.0f;
        const float c = std::cos(angle_), s = std::sin(angle_);
        for (int i = 0; i < count_; i++) {
            int col, row;
            gridPosition(i, cols_, rows_, wiring_, corner_, order_, col, row);
            const float x  = rx_ + (col + 0.5f) * rw_ / cols_;
            const float y  = ry_ + (row + 0.5f) * rh_ / rows_;
            const float dx = x - cx, dy = y - cy;
            px_[i]         = cx + dx * c - dy * s;
            py_[i]         = cy + dx * s + dy * c;
        }
    }

    void Fixture::gridPosition(const int i, const int cols, const int rows, const int wiring, const int corner, const int order, int& col, int& row) {
        if (order == ArtNetMapper::ROWS) {
            row = i / cols;
            col = i % cols;
            if (wiring == ArtNetMapper::SERPENTINE && (row & 1) == 1) { col = cols - 1 - col; }
        } else {
            col = i / rows;
            row = i % rows;
            if (wiring == ArtNetMapper::SERPENTINE && (col & 1) == 1) { row = rows - 1 - row; }
        }
        if (corner == ArtNetMapper::TOP_RIGHT || corner == ArtNetMapper::BOTTOM_RIGHT) { col = cols - 1 - col; }
        if (corner == ArtNetMapper::BOTTOM_LEFT || corner == ArtNetMapper::BOTTOM_RIGHT) { row = rows - 1 - row; }
    }

} // namespace umfeld

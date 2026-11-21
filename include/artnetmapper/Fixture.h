// ArtNetMapper for Umfeld :: a DMX light (par, strip or matrix). MIT License.

#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "artnetmapper/ColorMath.h"

namespace umfeld {

    class ArtNetMapper;
    namespace artnetmapper {
        struct TestAccess;
        struct Hooks;
    }

    /**
     * A DMX light: a number of channels per pixel, at a DMX address. A par is a fixture
     * with one pixel; a strip is a line of pixels; a matrix is a grid of pixels.
     * Create one with ArtNetMapper::fixture(), strip() or matrix(). The ArtNetMapper
     * owns it: don't delete it.
     *
     * Channels inside a fixture count from 0, in the order the light's manual lists
     * them. Every channel is 0 until you set it with setVal() or link it to the canvas
     * with colorChannels().
     *
     *     Fixture* par = dmx->fixture(8, 1);   // 8 channels at DMX address 1
     *     par->setPos(150, 150);
     *     par->colorChannels(1, 2, 3, 4);      // r, g, b, w follow the canvas
     *     par->setVal(0, 255);                 // dimmer, sent exactly as written
     *
     * Call its methods from setup(), draw() or the event callbacks (mousePressed(),
     * keyPressed()...). Calls from your own threads may show up half-applied for one frame.
     */
    class Fixture {
    public:
        /** One pixel of a strip or matrix. Get it with Fixture::pixel(). */
        class Pixel {
        public:
            /** Sets one channel of this pixel (counted from 0) to 0-255, sent exactly as written. */
            Pixel* setVal(int ch, float value);
            /** Sets channels 0, 1, 2... of this pixel, e.g. setVals(255, 0, 0). */
            template<typename... V>
            Pixel* setVals(V... values) { return setValsList({static_cast<float>(values)...}); }
            /** Stops this pixel's hand-set values overriding the canvas. */
            Pixel* release();
            /** Stops the hand-set value of one channel of this pixel overriding the canvas. */
            Pixel* release(int ch);
            /** Value of a channel of this pixel (hand-set, or computed in the last draw()). */
            int getVal(int ch) const;
            /** x of this pixel's position. */
            float x() const;
            /** y of this pixel's position. */
            float y() const;

            Pixel* setValsList(std::initializer_list<float> values);

        private:
            friend class Fixture;
            Pixel(Fixture* f, int e) : f(f), e(e) {}
            Fixture* f;
            int      e;
        };

        // ------------------------------------------------------------------ info

        /** Channels per pixel (the number given when it was created). */
        int channels() const { return channels_; }
        /** Number of pixels: 1 for a par. */
        int pixels() const { return count_; }
        /** Total DMX channels used: pixels x channels per pixel. */
        int totalChannels() const { return count_ * channels_; }
        /** First DMX address, 1-512, as given when it was created. */
        int address() const { return address_; }
        /** Last DMX address used, 1-512. */
        int lastAddress() const { return address_ + totalChannels() - 1; }
        /** Art-Net universe, counted from 0. */
        int universe() const { return universe_; }
        /** e.g. "strip, 30 pixels x 3 channels, universe 1, DMX 9-98". */
        std::string toString() const;

        // ------------------------------------------------------------------ placement

        /**
         * Places the fixture (all its pixels) at one point, in sketch pixels (or in the
         * ArtNetMapper::source() buffer's pixels). Typical for a par.
         */
        Fixture* setPos(float x, float y);
        /**
         * Spreads the pixels evenly from (x1, y1), where pixel 0 sits, to (x2, y2), where
         * the last pixel sits. Swap the points to reverse a strip.
         */
        Fixture* line(float x1, float y1, float x2, float y2);
        /**
         * Lays a matrix out as a grid filling this rectangle, one pixel in the center of
         * each cell. Which cell is pixel 0 depends on wiring().
         */
        Fixture* rect(float x, float y, float w, float h);
        /** Rotates a matrix about the center of its rect. Use radians(degrees) to give degrees. */
        Fixture* rotate(float radians);
        /**
         * The order the matrix's pixels are wired in. Default: PROGRESSIVE, TOP_LEFT, ROWS.
         *
         * @param wiring ArtNetMapper::PROGRESSIVE or ArtNetMapper::SERPENTINE
         * @param corner where pixel 0 is: ArtNetMapper::TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT or BOTTOM_RIGHT
         * @param order  ArtNetMapper::ROWS or ArtNetMapper::COLUMNS
         */
        Fixture* wiring(int wiring, int corner, int order);
        /** x of the position set with setPos() (pixel 0). For one pixel of a strip: pixel(i)->x(). */
        float x() const { return px_[0]; }
        /** y of the position set with setPos() (pixel 0). For one pixel of a strip: pixel(i)->y(). */
        float y() const { return py_[0]; }

        // ------------------------------------------------------------------ canvas link

        /**
         * Links channels to the canvas color at the fixture's position: 3 channels (red,
         * green, blue) or 4 (red, green, blue, white), in that order, counted from 0.
         * These channels then follow the canvas, even if they were set by hand before.
         * colorChannels() with nothing in the brackets removes the link.
         *
         *     par->colorChannels(0, 1, 2, 3);   // 4-channel mode: r g b w
         *     par->colorChannels(1, 2, 3, 4);   // 8-channel mode: dimmer r g b w strobe mode speed
         */
        Fixture* colorChannels();
        Fixture* colorChannels(int r, int g, int b);
        Fixture* colorChannels(int r, int g, int b, int w);
        /** Averages a square around each position instead of reading one pixel (0 = one pixel, default). */
        Fixture* sampleRadius(int radius);
        /**
         * White extraction, for 4 color channels (default on): white = the smallest of red,
         * green and blue, which is then taken away from all three. Off: white stays 0.
         */
        Fixture* whiteMode(bool on);
        /** Gamma for this fixture's canvas colors, instead of the global one. More than 0; 1.0 = no correction. */
        Fixture* gamma(float gamma);
        /** This fixture's gamma, or the global one if it has none of its own. */
        float getGamma() const;
        /** The sample radius set with sampleRadius() (0 = one pixel). */
        int getSampleRadius() const { return sampleRadius_; }
        /** Whether white extraction is on (see whiteMode()). */
        bool getWhiteMode() const { return whiteMode_; }

        // ------------------------------------------------------------------ manual values

        /**
         * Sets one channel on every pixel. The value is sent exactly as written: no gamma,
         * no brightness. On a canvas-linked channel it wins until release().
         *
         * @param ch    channel inside the fixture, counted from 0
         * @param value 0-255; clamped; decimals are rounded, so red(c) works directly
         */
        Fixture* setVal(int ch, float value);
        /**
         * Sets channels 0, 1, 2... on every pixel, e.g. setVals(255, 0, 0, 0). Covers
         * canvas-linked channels too: this is how a color is hard-coded. To set only one
         * channel, use setVal().
         */
        template<typename... V>
        Fixture* setVals(V... values) { return setValsList({static_cast<float>(values)...}); }
        /** Stops hand-set values overriding the canvas, on every pixel. Unlinked channels keep their value. */
        Fixture* release();
        /** Stops the hand-set value of one channel overriding the canvas, on every pixel. */
        Fixture* release(int ch);
        /**
         * Value of a channel on pixel 0: the hand-set value, or for a canvas-linked channel
         * the value computed at the end of the last draw().
         */
        int getVal(int ch) const;
        /** One pixel of a strip or matrix, counted from 0 in DMX order, to set it on its own. */
        Pixel* pixel(int index);

        Fixture* setValsList(std::initializer_list<float> values);

        Fixture(const Fixture&)            = delete;
        Fixture& operator=(const Fixture&) = delete;

    private:
        friend class ArtNetMapper;
        friend struct artnetmapper::TestAccess;
        friend struct artnetmapper::Hooks;

        static constexpr int PAR = 0, STRIP = 1, MATRIX = 2;

        Fixture(int channels, int universe, int address, int kind, int cols, int rows);

        std::string kindName() const;
        /** The placement call that fits this kind of fixture, for messages. */
        std::string placeHint() const;
        std::string perPixel() const;
        void        requireMatrix(const std::string& what) const;
        void        checkChannel(int ch) const;
        void        checkVals(size_t n) const;
        void        checkPixel(int index) const;
        void        put(int e, int ch, float value);
        int         current(int e, int ch) const;
        Fixture*    link(std::initializer_list<int> rgbw);
        void        layoutGrid();
        /** Smallest distance between consecutive pixels (DMX order); infinite for one pixel. */
        float minSpacing() const;
        /** Corners of the matrix rect grown by margin, rotated: x0,y0 .. x3,y3. */
        void outline(float margin, float out[8]) const;

    public:
        /**
         * Maps DMX pixel index i to a grid cell: col (0 = left), row (0 = top).
         * Used by rect(); public for tests.
         */
        static void gridPosition(int i, int cols, int rows, int wiring, int corner, int order, int& col, int& row);

    private:
        const int channels_, universe_, address_, kind_, cols_, rows_, count_;

        /** Hand-set values, per pixel and channel: [pixel * channels + ch]. */
        std::vector<int> values_;
        /** True where a hand-set value overrides a canvas-linked channel. */
        std::vector<bool> manual_;
        /** Last output values, same layout as values_. */
        std::vector<int> output_;
        /** Role of each channel: -1 none, 0 r, 1 g, 2 b, 3 w. */
        std::vector<int> role_;
        int              colorCount_ = 0;

        /** Sample points per pixel, in DMX order. */
        std::vector<float> px_, py_;
        /** Last color read from the canvas per pixel (Umfeld layout, red in the low byte), for the overlay. */
        std::vector<uint32_t> sampled_;

        bool                                     whiteMode_ = true;
        float                                    gamma_     = NAN; // NaN = use the global one
        std::unique_ptr<artnetmapper::GammaTable> gammaLut_;
        int                                      sampleRadius_   = 0;
        bool                                     placed_         = false;
        bool                                     warnedUnplaced_ = false;

        ArtNetMapper* mapper_       = nullptr;
        uint8_t*      universeData_ = nullptr;

        std::vector<std::unique_ptr<Pixel>> pixelObjects_;

        // matrix grid state; hasRect_ is false once the fixture is placed another way
        bool  hasRect_ = false;
        float rx_ = 0, ry_ = 0, rw_ = 0, rh_ = 0, angle_ = 0;
        int   wiring_, corner_, order_;
    };

    /** Prints toString(), so console(*par) works. */
    std::ostream& operator<<(std::ostream& out, const Fixture& f);

} // namespace umfeld

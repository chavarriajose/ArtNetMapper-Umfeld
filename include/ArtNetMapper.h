// ArtNetMapper for Umfeld :: maps points of a sketch to DMX fixtures and sends them
// over Art-Net. MIT License.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "artnetmapper/ColorMath.h"
#include "artnetmapper/Fixture.h"

namespace umfeld {

    class PGraphics;
    namespace artnetmapper {
        class Sender;
        struct Hooks;
        struct TestAccess;
    } // namespace artnetmapper

    /**
     * Maps points of an Umfeld sketch to DMX fixtures and sends them over Art-Net.
     * One ArtNetMapper sends to one Art-Net node; create several for several nodes.
     *
     *     ArtNetMapper* dmx;
     *
     *     void setup() {
     *         std::vector<std::string> nodes = ArtNetMapper::list(); // prints the nodes found
     *         dmx = new ArtNetMapper(nodes[0]);                        // no node? use "127.0.0.1"
     *         Fixture* par = dmx->fixture(4, 1);                       // 4 channels at DMX address 1
     *         par->setPos(300, 200);
     *         par->colorChannels(0, 1, 2, 3);                          // r, g, b, w follow the canvas
     *     }
     *
     *     void draw() {
     *         background(frameCount % 255, 0, 120);                    // the light follows the canvas
     *     }
     *
     * Create it in setup() (not in settings(), and not as a global object): it hooks
     * itself into Umfeld's frame loop, reads the canvas after every draw() and sends
     * a blackout when the sketch ends.
     */
    class ArtNetMapper {
    public:
        /** Library version, shown in the console on startup. */
        static constexpr const char* VERSION = "0.2.0";

        /** wiring(): every row (or column) runs in the same direction. */
        static constexpr int PROGRESSIVE = 0;
        /** wiring(): every other row (or column) runs back the other way. */
        static constexpr int SERPENTINE = 1;
        /** wiring(): the corner where pixel 0 sits. */
        static constexpr int TOP_LEFT = 10, TOP_RIGHT = 11, BOTTOM_LEFT = 12, BOTTOM_RIGHT = 13;
        /** wiring(): pixel 1 is next to pixel 0 along a row, or along a column. */
        static constexpr int ROWS = 20, COLUMNS = 21;

        // ------------------------------------------------------------------ create and connect

        /**
         * Connects to an Art-Net node. Call it in setup().
         *
         * @param ip the node's IP, e.g. nodes[0] from list(), or typed like "2.0.0.1".
         *           "127.0.0.1" sends to this computer, to work without a node.
         */
        explicit ArtNetMapper(const std::string& ip);
        /** Sends the blackout (if blackoutOnExit is on) and stops sending. */
        ~ArtNetMapper();
        ArtNetMapper(const ArtNetMapper&)            = delete;
        ArtNetMapper& operator=(const ArtNetMapper&) = delete;

        /** Scans for Art-Net nodes (about 1.5 s), prints a numbered list and returns their IPs. */
        static std::vector<std::string> list(int timeoutMs = 1500);

        /** Adds a single light (a par: a fixture with one pixel) on universe 0. Place it with setPos(). */
        Fixture* fixture(int channels, int address);
        /** Adds a single light on a universe (counted from 0). */
        Fixture* fixture(int channels, int universe, int address);
        /** Adds a strip (light bar) of pixels on universe 0. Place it with line(). */
        Fixture* strip(int channelsPerPixel, int address, int pixels);
        /** Adds a strip (light bar) of pixels on a universe. */
        Fixture* strip(int channelsPerPixel, int universe, int address, int pixels);
        /** Adds a matrix (grid) of pixels on universe 0. Place it with rect(). */
        Fixture* matrix(int channelsPerPixel, int address, int cols, int rows);
        /** Adds a matrix (grid) of pixels on a universe. Set the pixel order with wiring(). */
        Fixture* matrix(int channelsPerPixel, int universe, int address, int cols, int rows);

        // ------------------------------------------------------------------ follow the canvas

        /**
         * Reads canvas colors from this buffer (from createGraphics()) instead of the
         * sketch window. Fixture positions are then in the buffer's pixels. nullptr = the window.
         */
        ArtNetMapper* source(PGraphics* pg);
        /** Gamma for canvas colors, for every fixture without its own. More than 0; 2.5 (default) matches Resolume, 1.0 = none. */
        ArtNetMapper* gamma(float gamma);
        /** The global gamma set with gamma() (default 2.5). */
        float getGamma() const { return gamma_; }
        /** Brightness of canvas colors, applied after gamma: 0.0 to 1.0 (default), clamped. Values set with setVal() are not scaled. */
        ArtNetMapper* brightness(float level);
        /** The brightness set with brightness() (default 1.0). */
        float getBrightness() const { return brightness_; }
        /** Draws a marker at each fixture's position, in the color it reads (default on). */
        ArtNetMapper* showLocations(bool on);

        // ------------------------------------------------------------------ sending

        /** true (default): read the canvas and send after every draw(). false: call send() yourself. */
        ArtNetMapper* autoSend(bool on);
        /** Reads the canvas now and queues the frame for sending. Only needed with autoSend(false). */
        void send();
        /** Caps how many frames per second are sent. More than 0; default 44 (DMX tops out around 44). */
        ArtNetMapper* maxRate(float fps);
        /** The cap set with maxRate() (default 44). */
        float getMaxRate() const;
        /**
         * Blackout switch. true: every light gets 0, whatever the sketch does, until
         * blackout(false). Fixture values keep updating in the meantime.
         */
        ArtNetMapper* blackout(bool on);
        /** Whether the blackout switch is on. */
        bool isBlackout() const;
        /** Sends all channels at 0 when the sketch stops (default on). */
        ArtNetMapper* blackoutOnExit(bool on);

        // ------------------------------------------------------------------ info

        /** True while packets are being sent: the node's network is connected, or the IP is routed. */
        bool isConnected() const;
        /** The IP this ArtNetMapper sends to. */
        std::string ip() const { return ip_; }
        /** All fixtures added so far, in the order they were added. */
        std::vector<Fixture*> fixtures() const;
        /**
         * The current 512 values of a universe, as computed from the fixtures (also during
         * a blackout). Index 0 is DMX address 1, unlike the channels inside a fixture,
         * which count from 0 at the fixture's address.
         */
        std::vector<int> getUniverse(int universe) const;

        static float checkGamma(float gamma);

    private:
        friend class Fixture;
        friend struct artnetmapper::Hooks;
        friend struct artnetmapper::TestAccess;

        /** Offline mapper for tests: no network, no Umfeld hook. */
        ArtNetMapper(const std::string& ip, bool network);
        void init(bool network);

        static void precheck(const std::string& what, int channels, int universe, int address, long long pixels);
        Fixture*    add(Fixture* f);
        void        linkChanged();
        /** Called by the frame hook after the sketch's draw(). */
        void frame();
        void update();
        /** Stops sending; sends the blackout first if blackoutOnExit is on. */
        void close();
        /**
         * Computes every fixture's output and writes it into the universe buffers.
         * pixels: pw * ph, Umfeld layout (red in the low byte); nullptr = no canvas.
         * density: pixels per sketch pixel (positions and radii are scaled by it).
         */
        void sample(const uint32_t* pixels, int pw, int ph, int density);
        /** Average color of a square around (x, y) in sketch pixels. Outside = black. Same layout as the pixels. */
        static uint32_t samplePoint(const uint32_t* pixels, int pw, int ph, int density, float x, float y, int radius);

        std::string                            ip_;
        std::unique_ptr<artnetmapper::Sender>  sender_;
        std::vector<std::unique_ptr<Fixture>>  fixtures_;
        PGraphics*                             source_   = nullptr;
        float                                  gamma_    = 2.5f;
        artnetmapper::GammaTable               gammaLut_ = artnetmapper::gammaTable(2.5f);
        float                                  brightness_     = 1.0f;
        bool                                   autoSend_       = true;
        bool                                   showLocations_  = true;
        std::atomic<bool>                      blackoutOnExit_{true};
        /** True when at least one fixture follows the canvas, so the canvas must be read. */
        bool anyLinked_ = false;
        /** Prints a line per added fixture (off in unit tests). */
        bool verbose_ = false;
        int  rgbw_[4] = {};

        /** Set by the Umfeld layer: unhooks a mapper that is deleted. */
        static void (*onDestroy)(ArtNetMapper*);
    };

} // namespace umfeld

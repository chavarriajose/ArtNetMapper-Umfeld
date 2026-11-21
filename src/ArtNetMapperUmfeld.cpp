// ArtNetMapper for Umfeld :: the parts that talk to Umfeld: the frame hook, reading
// the canvas, the location markers and console output. MIT License.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <stdexcept>

#include "Umfeld.h"
#include "PGraphics.h"
#include "Subsystems.h"
#include "UmfeldMain.h"

#include "ArtNetMapper.h"
#include "artnetmapper/Discovery.h"
#include "artnetmapper/Log.h"
#include "artnetmapper/Sender.h"

namespace umfeld {

    using artnetmapper::Log;

    namespace artnetmapper {

        /**
         * Hooks every ArtNetMapper into Umfeld's frame loop through one Subsystem.
         *
         * It is placed just before the graphics subsystem, so its draw_post() runs after
         * the sketch's draw() but before the frame is finished and shown (a library
         * registered with register_library() runs after the frame is shown, too late to
         * read it or draw on it). Its shutdown() runs when the sketch ends.
         */
        struct Hooks {
            static std::vector<ArtNetMapper*>& registry() {
                static std::vector<ArtNetMapper*> r;
                return r;
            }

            /** Once, before any send thread exists (the send thread prints through these too). */
            static void useUmfeldConsole() {
                static std::once_flag once;
                std::call_once(once, [] {
                    Log::infoOut() = [](const std::string& s) { console(s); };
                    Log::warnOut() = [](const std::string& s) { warning(s); };
                });
            }

            static bool& installed() {
                static bool i = false;
                return i;
            }

            /*
             * The hook goes into Umfeld's subsystem list. Umfeld walks that list while it
             * calls mousePressed(), keyPressed()..., so changing it there is unsafe: the
             * first ArtNetMapper must be created in setup() (or settings()).
             */
            static void checkCanAttach() {
                if (!installed() && umfeld_setup_completed()) {
                    throw std::logic_error("ArtNetMapper: create the ArtNetMapper in setup(), not in draw() or an event"
                                           " function. Keep the pointer in a global variable and use it from draw().");
                }
            }

            static void attach(ArtNetMapper* m) {
                static Subsystem hook;
                if (!installed()) {
                    installed() = true;
                    registry(); // built before atexit() below, so it still exists when closeAll runs at exit
                    hook.name        = [] { return "ArtNetMapper"; };
                    hook.draw_post   = drawPost;
                    hook.shutdown    = closeAll;
                    hook.id          = -1;
                    auto&      list  = subsystems;
                    const auto where = std::find(list.begin(), list.end(), static_cast<Subsystem*>(subsystem_graphics));
                    list.insert(where, &hook);
                    std::atexit(closeAll); // also blackout when the program ends some other way
                }
                registry().push_back(m);
                ArtNetMapper::onDestroy = detach;
            }

            static void detach(ArtNetMapper* m) {
                auto& r = registry();
                r.erase(std::remove(r.begin(), r.end(), m), r.end());
            }

            static void drawPost() {
                for (ArtNetMapper* m: registry()) { m->frame(); }
            }

            static void closeAll() {
                for (ArtNetMapper* m: registry()) { m->close(); }
            }

            static void overlay(const ArtNetMapper& m) {
                pushStyle();
                pushMatrix();
                g->resetMatrix();
                noFill();
                strokeWeight(1);
                for (const auto& f: m.fixtures_) {
                    if (f->placed_) { markers(*f); }
                }
                popMatrix();
                popStyle();
            }

            /*
             * Markers never touch a fixture's own sample squares, so a canvas that is
             * never cleared doesn't feed them back into the colors read. Rings shrink to
             * fit between neighbouring pixels; when even the smallest ring would touch a
             * neighbour, a strip gets small ticks beside its line and a matrix an outline
             * around its grid.
             */
            static void markers(const Fixture& f) {
                const float hd      = f.sampleRadius_ * 1.415f; // half-diagonal of the sample square
                const float spacing = f.minSpacing();
                const float ring    = std::min(hd + 4.5f, spacing - hd - 3.0f);
                if (ring >= hd + 1.5f) {
                    for (int e = 0; e < f.count_; e++) {
                        const float x = f.px_[e], y = f.py_[e];
                        stroke_8(0, 0, 0);
                        circleOutline(x, y, ring + 1.5f);
                        strokeColor(f, e);
                        circleOutline(x, y, ring);
                    }
                } else if (f.hasRect_) {
                    float c[8];
                    f.outline(hd + 3.0f, c);
                    stroke_8(128, 128, 128);
                    for (int k = 0; k < 4; k++) { line(c[2 * k], c[2 * k + 1], c[(2 * k + 2) % 8], c[(2 * k + 3) % 8]); }
                } else {
                    const float dx = f.px_[f.count_ - 1] - f.px_[0], dy = f.py_[f.count_ - 1] - f.py_[0];
                    const float len = std::sqrt(dx * dx + dy * dy);
                    const float nx = len > 0 ? -dy / len : 0, ny = len > 0 ? dx / len : 1;
                    const float a = hd + 3.0f, b = hd + 7.0f;
                    for (int e = 0; e < f.count_; e++) {
                        strokeColor(f, e);
                        line(f.px_[e] + nx * a, f.py_[e] + ny * a, f.px_[e] + nx * b, f.py_[e] + ny * b);
                    }
                }
            }

            /** A ring drawn with lines, so the sketch's ellipseMode() is left alone. */
            static void circleOutline(const float x, const float y, const float r) {
                constexpr int n = 24;
                for (int k = 0; k < n; k++) {
                    const float a0 = TWO_PI * k / n, a1 = TWO_PI * (k + 1) / n;
                    line(x + r * std::cos(a0), y + r * std::sin(a0), x + r * std::cos(a1), y + r * std::sin(a1));
                }
            }

            /** The color read from the canvas, or grey for a fully manual fixture. */
            static void strokeColor(const Fixture& f, const int e) {
                if (f.colorCount_ > 0) {
                    const uint32_t c = f.sampled_[e];
                    stroke_8(c & 0xFF, c >> 8 & 0xFF, c >> 16 & 0xFF);
                } else {
                    stroke_8(128, 128, 128);
                }
            }
        };

    } // namespace artnetmapper

    using artnetmapper::Hooks;

    ArtNetMapper::ArtNetMapper(const std::string& ip) : ip_(ip) {
        Hooks::checkCanAttach();
        Hooks::useUmfeldConsole();
        init(true);
        Hooks::attach(this); // last, so Umfeld never calls into a half-built object
    }

    std::vector<std::string> ArtNetMapper::list(const int timeoutMs) {
        Hooks::useUmfeldConsole();
        const auto               nodes = artnetmapper::Discovery::scan(timeoutMs);
        std::vector<std::string> ips;
        if (nodes.empty()) {
            Log::info("no Art-Net nodes found. Check the cable and that the node is powered. nodes[0] is then invalid and crashes the sketch;"
                      " to run without a node, use new ArtNetMapper(\"127.0.0.1\").");
        } else {
            Log::info("found " + std::to_string(nodes.size()) + " Art-Net node" + (nodes.size() == 1 ? "" : "s") + ":");
            for (size_t i = 0; i < nodes.size(); i++) {
                ips.push_back(nodes[i].ip);
                console("  [", i, "] \"", nodes[i].ip, "\"  ", nodes[i].label());
            }
        }
        return ips;
    }

    void ArtNetMapper::frame() {
        if (g == nullptr) { return; } // window closed
        if (autoSend_) { update(); }
        if (showLocations_) { Hooks::overlay(*this); }
    }

    void ArtNetMapper::send() {
        update();
    }

    void ArtNetMapper::update() {
        if (!anyLinked_) { // all fixtures manual: no need to read the canvas
            sample(nullptr, 0, 0, 1);
            return;
        }
        if (source_ != nullptr) {
            // an offscreen buffer gets its CPU copy on first use; loadPixels() then reads its texture
            if (!source_->ensure_pixel_buffer()) { return; }
            source_->loadPixels();
            if (source_->pixels != nullptr) {
                const int d = std::max(1, source_->getPixelDensity()); // the CPU copy is width x height x density^2
                sample(source_->pixels, static_cast<int>(source_->width) * d, static_cast<int>(source_->height) * d, d);
            }
            return;
        }
        if (g == nullptr) { return; }
        loadPixels(true, true); // fills `pixels` at the sketch's logical size, whatever the display density
        if (pixels != nullptr) { sample(pixels, static_cast<int>(width), static_cast<int>(height), 1); }
    }

} // namespace umfeld

// ArtNetMapper for Umfeld :: color pipeline (white extraction, gamma, brightness). MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace umfeld::artnetmapper {

    using GammaTable = std::array<int, 256>;

    /** 256-entry gamma lookup table. gamma 1.0 is identity. */
    inline GammaTable gammaTable(const float gamma) {
        GammaTable t{};
        for (int i = 0; i < 256; i++) {
            t[i] = gamma == 1.0f ? i : static_cast<int>(std::lround(255.0 * std::pow(i / 255.0, static_cast<double>(gamma))));
        }
        return t;
    }

    inline int scaleBrightness(const int v, const float brightness) {
        if (brightness >= 1.0f) { return v; }
        if (brightness <= 0.0f) { return 0; }
        return static_cast<int>(std::lround(v * brightness));
    }

    /**
     * Runs the pipeline for one pixel: white extraction, then gamma, then brightness.
     * out receives {r, g, b, w}; w is 0 when no white extraction happens.
     */
    inline void processColor(int r, int g, int b, const bool extractWhite, const GammaTable& lut,
                             const float brightness, int out[4]) {
        int w = 0;
        if (extractWhite) {
            w = std::min(r, std::min(g, b));
            r -= w;
            g -= w;
            b -= w;
        }
        out[0] = scaleBrightness(lut[r], brightness);
        out[1] = scaleBrightness(lut[g], brightness);
        out[2] = scaleBrightness(lut[b], brightness);
        out[3] = scaleBrightness(lut[w], brightness);
    }

} // namespace umfeld::artnetmapper

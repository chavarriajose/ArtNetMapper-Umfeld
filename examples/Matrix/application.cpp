// Matrix: an 8x8 RGB matrix (3 channels per pixel) wired serpentine from the
// top-left corner, rotated 15 degrees, over a plasma pattern.
//
// Set your matrix to DMX address 1 (universe 0), or change the numbers below.
// Argument order: matrix(channelsPerPixel, address, cols, rows)
//             or  matrix(channelsPerPixel, universe, address, cols, rows)

#include "Umfeld.h"
#include "UmfeldArtNetMapper.h"

using namespace umfeld;

ArtNetMapper* dmx;

void settings() {
    size(500, 500);
}

void setup() {
    std::vector<std::string> nodes = ArtNetMapper::list(); // prints the nodes found
    dmx = new ArtNetMapper(nodes[0]);                      // no node? use "127.0.0.1"

    Fixture* m = dmx->matrix(3, 1, 8, 8);                  // 3 channels per pixel, address 1, 8x8
    m->rect(130, 130, 240, 240);
    m->rotate(radians(15));
    m->wiring(ArtNetMapper::SERPENTINE, ArtNetMapper::TOP_LEFT, ArtNetMapper::ROWS);
    m->colorChannels(0, 1, 2);
    m->sampleRadius(3);                                    // average a small area per pixel

    colorMode(HSB, 1, 1, 1);
}

void draw() {
    const float t = millis() / 1000.0f;
    const int   w = static_cast<int>(width), h = static_cast<int>(height);
    loadPixels();                                          // pixels[] is width x height, whatever the display density
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            const float   v = sin(x * 0.02f + t) + sin(y * 0.03f - t) + sin((x + y) * 0.015f + t * 0.5f);
            const color_t c = color(fmod(v * 0.15f + t * 0.05f + 10.0f, 1.0f), 0.9f, 1.0f);
            pixels[y * w + x]           = c;
            pixels[y * w + x + 1]       = c;
            pixels[(y + 1) * w + x]     = c;
            pixels[(y + 1) * w + x + 1] = c;
        }
    }
    updatePixels();
}

// Strip: a 30-pixel RGB strip (3 channels per pixel) laid along a line.
//
// Set your strip to DMX address 1 (universe 0), or change the numbers below.
// Argument order: strip(channelsPerPixel, address, pixels)
//             or  strip(channelsPerPixel, universe, address, pixels)
//
// Drag the mouse to move the strip's end point. Pixel 0 is always green (set by hand).

#include "Umfeld.h"
#include "UmfeldArtNetMapper.h"

using namespace umfeld;

ArtNetMapper* dmx;
Fixture*      strip;

void settings() {
    size(600, 400);
}

void setup() {
    std::vector<std::string> nodes = ArtNetMapper::list(); // prints the nodes found
    dmx   = new ArtNetMapper(nodes[0]);                    // no node? use "127.0.0.1"
    strip = dmx->strip(3, 1, 30);                          // 3 channels per pixel, address 1, 30 pixels
    strip->line(50, 200, 550, 200);
    strip->colorChannels(0, 1, 2);                         // r, g, b of every pixel follow the canvas
    strip->pixel(0)->setVals(0, 255, 0);                   // one pixel set by hand
}

void draw() {
    background(0);
    noStroke();
    const float x = width / 2 + sin(frameCount * 0.03f) * 250; // a blob bouncing along the strip
    for (int r = 120; r > 0; r -= 4) {
        fill(255 - r * 2, 80 + r, 255, 40);
        ellipse(x, 200, r * 2, r);
    }
}

void mouseDragged() {
    strip->line(50, 200, mouseX, mouseY);
}

// TwoPars: two RGBW pars in their 4-channel mode (r, g, b, w),
// par 1 at DMX address 1, par 2 at DMX address 5 (universe 0).
// Each par takes its color from the point marked on the canvas.
// For the 8-channel mode of the same par, see the ParModes example.
//
// Mouse X: gradient speed.  W: toggle white extraction.
// Hold the mouse button: par 2 is hard-coded red with setVals(); release it to follow the canvas.

#include "Umfeld.h"
#include "UmfeldArtNetMapper.h"

using namespace umfeld;

ArtNetMapper* dmx;
Fixture*      par1;
Fixture*      par2;
bool          white = true;

void settings() {
    size(600, 300);
}

void setup() {
    std::vector<std::string> nodes = ArtNetMapper::list(); // prints the nodes found
    dmx = new ArtNetMapper(nodes[0]);                      // no node? use "127.0.0.1"

    par1 = dmx->fixture(4, 1);                             // 4 channels at DMX address 1
    par1->setPos(150, 150);
    par1->colorChannels(0, 1, 2, 3);                       // r, g, b, w follow the canvas

    par2 = dmx->fixture(4, 5);
    par2->setPos(450, 150);
    par2->colorChannels(0, 1, 2, 3);

    colorMode(HSB, 360, 100, 100);
}

void draw() {
    const float speed = map(mouseX, 0, width, 0.2, 3);
    const float t     = frameCount * speed;
    for (int x = 0; x < width; x++) {                       // scrolling hue gradient
        stroke(fmod(t + x * 0.6f, 360.0f), 100, 100);
        line(x, 0, x, height);
    }
    debug_text(std::string("white extraction: ") + (white ? "on" : "off") + " (W)   hold mouse: par 2 red   node: " +
                   (dmx->isConnected() ? dmx->ip() : "none"),
               10, height - 20);
}

void mousePressed() {
    par2->setVals(255, 0, 0, 0);                           // hard-coded, sent exactly as written
}

void mouseReleased() {
    par2->release();                                       // follow the canvas again
}

void keyPressed() {
    if (key == 'w' || key == 'W') {
        white = !white;
        par1->whiteMode(white);
        par2->whiteMode(white);
    }
}

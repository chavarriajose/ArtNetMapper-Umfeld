// ParModes: the same RGBW par in its 8-channel mode, at DMX address 1 (universe 0).
//
// From the light's manual, 8-channel mode is:
//   ch0 dimmer, ch1 red, ch2 green, ch3 blue, ch4 white, ch5 strobe, ch6 mode, ch7 speed
// Set the light to that mode and address, or change the numbers below to match yours.
//
// Colors follow the canvas through colorChannels(1, 2, 3, 4); the other channels
// are set by hand. Mouse X: dimmer.  S: strobe on/off.

#include "Umfeld.h"
#include "UmfeldArtNetMapper.h"

using namespace umfeld;

ArtNetMapper* dmx;
Fixture*      par;
bool          strobe = false;

void settings() {
    size(400, 300);
}

void setup() {
    std::vector<std::string> nodes = ArtNetMapper::list(); // prints the nodes found
    dmx = new ArtNetMapper(nodes[0]);                      // no node? use "127.0.0.1"

    par = dmx->fixture(8, 1);                              // 8 channels at DMX address 1
    par->setPos(200, 150);
    par->colorChannels(1, 2, 3, 4);                        // r, g, b, w follow the canvas
    par->setVal(0, 255);                                   // dimmer full
    par->setVal(5, 0);                                     // strobe off

    colorMode(HSB, 360, 100, 100);
}

void draw() {
    background(frameCount % 360, 80, 100);                 // slowly cycling color
    par->setVal(0, map(mouseX, 0, width, 0, 255));         // dimmer, sent exactly as written
    debug_text("dimmer: " + std::to_string(par->getVal(0)) + "   strobe (S): " + (strobe ? "on" : "off"), 10, height - 20);
}

void keyPressed() {
    if (key == 's' || key == 'S') {
        strobe = !strobe;
        par->setVal(5, strobe ? 200 : 0);                  // check your manual for strobe speeds
    }
}

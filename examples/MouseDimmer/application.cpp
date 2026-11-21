// MouseDimmer: a light set completely by hand, without the canvas.
//
// No setPos() and no colorChannels(): every channel is set with setVal().
// The light here is a 4-channel RGBW par at DMX address 1 (universe 0).
// Mouse X: brightness of the white channel.  Mouse Y: red.
//
// fixture(channels, address)       channels count from 0 inside the fixture

#include "Umfeld.h"
#include "UmfeldArtNetMapper.h"

using namespace umfeld;

ArtNetMapper* dmx;
Fixture*      par;

void settings() {
    size(400, 300);
}

void setup() {
    std::vector<std::string> nodes = ArtNetMapper::list(); // prints the nodes found
    dmx = new ArtNetMapper(nodes[0]);                      // no node? use "127.0.0.1"
    par = dmx->fixture(4, 1);                              // 4 channels at DMX address 1
}

void draw() {
    const float white = map(mouseX, 0, width, 0, 255);
    const float red   = map(mouseY, height, 0, 0, 255);
    par->setVal(3, white);                                 // channel 3 = white
    par->setVal(0, red);                                   // channel 0 = red
    background(red, white * 0.3f, white * 0.3f);
    debug_text("white (ch3): " + std::to_string(par->getVal(3)), 20, 20);
    debug_text("red (ch0): " + std::to_string(par->getVal(0)), 20, 40);
}

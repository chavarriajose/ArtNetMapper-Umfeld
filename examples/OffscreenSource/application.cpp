// OffscreenSource: the lights read an offscreen buffer instead of the window.
//
// The lighting content is drawn into a PGraphics; the window shows a preview and
// a control. Useful with shaders, or when the window is a UI.
// Fixture positions are in the buffer's pixels. Two RGBW pars at DMX 1 and 5.
// Mouse Y: global brightness of the canvas colors.

#include "Umfeld.h"
#include "PGraphics.h"
#include "UmfeldArtNetMapper.h"

using namespace umfeld;

ArtNetMapper* dmx;
PGraphics*    buffer;

void settings() {
    size(600, 400);
}

void setup() {
    buffer = createGraphics(300, 200);
    std::vector<std::string> nodes = ArtNetMapper::list(); // prints the nodes found
    dmx = new ArtNetMapper(nodes[0]);                      // no node? use "127.0.0.1"
    dmx->source(buffer);                                   // read the buffer, not the window
    dmx->fixture(4, 1)->setPos(75, 100)->colorChannels(0, 1, 2, 3);
    dmx->fixture(4, 5)->setPos(225, 100)->colorChannels(0, 1, 2, 3);
    dmx->showLocations(false);                             // markers would land on the window, not the buffer
}

void draw() {
    const float level = map(mouseY, 0, height, 1, 0);
    dmx->brightness(level);

    buffer->beginDraw();
    buffer->background(0);
    buffer->noStroke();
    buffer->fill(255, 60, 0);
    buffer->ellipse(150 + sin(frameCount * 0.05f) * 120, 100, 160, 160);
    buffer->endDraw();

    background(40);
    image(buffer, 20, 20);                                 // preview
    debug_text("brightness: " + std::to_string(level).substr(0, 4) + " (move the mouse up and down)", 20, 250);
}

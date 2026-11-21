// Loopback test sketch: draws four known color quadrants, maps fixtures onto them,
// sends to 127.0.0.1 for ~2 s, then exits (which triggers the blackout).
// Checked by tests/loopback/check.py. Same fixtures and values as the Processing
// library's loopback test. Do not change values without updating the checker.

#include "Umfeld.h"
#include "UmfeldArtNetMapper.h"

using namespace umfeld;

ArtNetMapper* dmx;

void settings() {
    size(200, 100); // Umfeld runs at 60 fps by default
}

void setup() {
    dmx = new ArtNetMapper("127.0.0.1");
    dmx->gamma(1);                                                   // exact values for checking

    dmx->fixture(4, 1)->setPos(50, 25)->colorChannels(0, 1, 2, 3);  // TL white -> 0,0,0,255
    Fixture* p8 = dmx->fixture(6, 5)->setPos(150, 25)->colorChannels(1, 2, 3, 4); // TR
    p8->setVal(0, 255)->setVal(5, 77);                               // dimmer, strobe -> 255,150,50,0,50,77
    Fixture* s = dmx->strip(3, 1, 1, 3)->line(10, 25, 190, 25)->colorChannels(0, 1, 2); // u1
    s->pixel(1)->setVals(1, 2, 3);                                   // hard-coded middle pixel
    dmx->matrix(3, 2, 1, 2, 2)->rect(0, 0, 200, 100)->colorChannels(0, 1, 2)->wiring(ArtNetMapper::SERPENTINE, ArtNetMapper::TOP_LEFT, ArtNetMapper::ROWS); // u2
    dmx->fixture(3, 3, 1)->setPos(195, 95)->colorChannels(0, 1, 2); // u3: animated, drives the send rate
}

void draw() {
    noStroke();
    fill(255, 255, 255);
    rect(0, 0, 100, 50);      // TL white
    fill(200, 100, 50);
    rect(100, 0, 100, 50);    // TR
    fill(0, 255, 0);
    rect(0, 50, 100, 50);     // BL green
    fill(0, 0, 255);
    rect(100, 50, 100, 50);   // BR blue
    fill(1 + frameCount % 200, 0, 0);
    rect(190, 90, 10, 10);    // changes every frame
    if (frameCount == 120) { exit(); }
}

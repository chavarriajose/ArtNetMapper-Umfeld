// Discover: lists the Art-Net nodes connected to this computer.
//
// The console shows a numbered list, like Serial::list():
//   [0] "2.0.0.1"  Electroconcept USB NODE  (answered ArtPoll, en6)
// Nodes that don't answer ArtPoll are found by the interface scan instead:
//   [0] "2.0.0.1"  (found by interface scan, no ArtPoll reply, en6)
// In your sketch, use nodes[0], or copy another IP from the list.
//
// Click the window to scan again.

#include "Umfeld.h"
#include "UmfeldArtNetMapper.h"

using namespace umfeld;

std::vector<std::string> nodes;

void settings() {
    size(480, 240);
}

void setup() {
    nodes = ArtNetMapper::list(); // waits ~1.5 s; ArtNetMapper::list(3000) waits longer
}

void draw() {
    background(20);
    if (nodes.empty()) {
        debug_text("No Art-Net nodes found.", 20, 30);
        debug_text("Click to scan again.", 20, 50);
    } else {
        debug_text("Art-Net nodes (click to scan again):", 20, 30);
        for (size_t i = 0; i < nodes.size(); i++) {
            debug_text("[" + std::to_string(i) + "] " + nodes[i], 20, 60 + i * 20);
        }
    }
}

void mousePressed() {
    nodes = ArtNetMapper::list();
}

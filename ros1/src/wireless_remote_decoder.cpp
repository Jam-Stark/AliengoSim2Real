#include "aliengo_deploy/wireless_remote_decoder.h"

namespace aliengo {

WirelessRemoteState WirelessRemoteDecoder::decode(const uint8_t data[40]) const {
    WirelessRemoteState state;

    // head: bytes 0-1 (little-endian)
    std::memcpy(&state.head, &data[0], sizeof(uint16_t));

    // keys: bytes 2-3 (little-endian)
    std::memcpy(&state.keys, &data[2], sizeof(uint16_t));

    // lx: bytes 4-7
    std::memcpy(&state.lx, &data[4], sizeof(float));

    // rx: bytes 8-11
    std::memcpy(&state.rx, &data[8], sizeof(float));

    // ry: bytes 12-15
    std::memcpy(&state.ry, &data[12], sizeof(float));

    // L2: bytes 16-19
    std::memcpy(&state.l2, &data[16], sizeof(float));

    // ly: bytes 20-23
    std::memcpy(&state.ly, &data[20], sizeof(float));

    return state;
}

WirelessRemoteDecoder::ButtonEdges
WirelessRemoteDecoder::computeEdges(const WirelessRemoteState &prev,
                                    const WirelessRemoteState &curr) const {
    ButtonEdges edges;

    auto rising = [](bool was, bool now) -> bool { return !was && now; };

    edges.a      = rising(prev.key(kKeyA),      curr.key(kKeyA));
    edges.b      = rising(prev.key(kKeyB),      curr.key(kKeyB));
    edges.x      = rising(prev.key(kKeyX),      curr.key(kKeyX));
    edges.y      = rising(prev.key(kKeyY),      curr.key(kKeyY));
    edges.l1     = rising(prev.key(kKeyL1),     curr.key(kKeyL1));
    edges.r1     = rising(prev.key(kKeyR1),     curr.key(kKeyR1));
    edges.l2     = rising(prev.key(kKeyL2),     curr.key(kKeyL2));
    edges.start  = rising(prev.key(kKeyStart),  curr.key(kKeyStart));
    edges.select = rising(prev.key(kKeySelect), curr.key(kKeySelect));
    edges.up     = rising(prev.key(kKeyUp),     curr.key(kKeyUp));
    edges.down   = rising(prev.key(kKeyDown),   curr.key(kKeyDown));
    edges.left   = rising(prev.key(kKeyLeft),   curr.key(kKeyLeft));
    edges.right  = rising(prev.key(kKeyRight),  curr.key(kKeyRight));

    return edges;
}

} // namespace aliengo

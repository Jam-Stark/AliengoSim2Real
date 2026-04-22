#pragma once

#include <cstdint>
#include <cstring>

namespace aliengo {

// ============================================================
// Unitree wireless remote controller data decoded from
// LowState.wirelessRemote[40] byte array.
//
// Memory layout (from Unitree SDK comm.h):
//   bytes 0-1:   head (uint16)
//   bytes 2-3:   key bitfield (uint16)
//   bytes 4-7:   lx (float)  left stick X
//   bytes 8-11:  rx (float)  right stick X
//   bytes 12-15: ry (float)  right stick Y
//   bytes 16-19: L2 (float)
//   bytes 20-23: ly (float)  left stick Y
//   bytes 24-39: reserved
// ============================================================

// Key bitfield masks
constexpr uint16_t kKeyR1     = 1u << 0;
constexpr uint16_t kKeyL1     = 1u << 1;
constexpr uint16_t kKeyStart  = 1u << 2;
constexpr uint16_t kKeySelect = 1u << 3;
constexpr uint16_t kKeyR2     = 1u << 4;
constexpr uint16_t kKeyL2     = 1u << 5;
constexpr uint16_t kKeyF1     = 1u << 6;
constexpr uint16_t kKeyF2     = 1u << 7;
constexpr uint16_t kKeyA      = 1u << 8;
constexpr uint16_t kKeyB      = 1u << 9;
constexpr uint16_t kKeyX      = 1u << 10;
constexpr uint16_t kKeyY      = 1u << 11;
constexpr uint16_t kKeyUp     = 1u << 12;
constexpr uint16_t kKeyRight  = 1u << 13;
constexpr uint16_t kKeyDown   = 1u << 14;
constexpr uint16_t kKeyLeft   = 1u << 15;

struct WirelessRemoteState {
    uint16_t head = 0;
    uint16_t keys = 0;
    float lx = 0.0f;   // left stick X  [-1, 1]
    float ly = 0.0f;   // left stick Y  [-1, 1]
    float rx = 0.0f;   // right stick X [-1, 1]
    float ry = 0.0f;   // right stick Y [-1, 1]
    float l2 = 0.0f;   // L2 trigger    [0, 1]

    bool key(uint16_t mask) const { return (keys & mask) != 0; }
};

class WirelessRemoteDecoder {
public:
    WirelessRemoteDecoder() = default;

    /// Decode 40 bytes from LowState.wirelessRemote into state
    WirelessRemoteState decode(const uint8_t data[40]) const;

    /// Edge detection: returns true on rising edge (was not pressed, now pressed)
    struct ButtonEdges {
        bool a = false;
        bool b = false;
        bool x = false;
        bool y = false;
        bool l1 = false;
        bool r1 = false;
        bool l2 = false;
        bool start = false;
        bool select = false;
        bool up = false;
        bool down = false;
        bool left = false;
        bool right = false;
    };

    /// Compute rising edges from previous to current state
    ButtonEdges computeEdges(const WirelessRemoteState &prev,
                             const WirelessRemoteState &curr) const;
};

} // namespace aliengo

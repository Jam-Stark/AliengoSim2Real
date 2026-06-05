#ifndef A2_LOWLEVEL_A2_REMOTE_H_
#define A2_LOWLEVEL_A2_REMOTE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace a2_lowlevel {

inline constexpr std::size_t kA2RemotePacketSize = 40;

struct A2RemoteButtons {
  bool r1 = false;
  bool l1 = false;
  bool start = false;
  bool select = false;
  bool r2 = false;
  bool l2 = false;
  bool f1 = false;
  bool f3 = false;
  bool a = false;
  bool b = false;
  bool x = false;
  bool y = false;
  bool up = false;
  bool right = false;
  bool down = false;
  bool left = false;
};

struct A2RemoteState {
  bool valid = false;
  float lx = 0.0f;
  float rx = 0.0f;
  float ry = 0.0f;
  float ly = 0.0f;
  float raw_lx = 0.0f;
  float raw_rx = 0.0f;
  float raw_ry = 0.0f;
  float raw_ly = 0.0f;
  A2RemoteButtons buttons;
};

A2RemoteState decode_a2_remote(
    const std::array<std::uint8_t, kA2RemotePacketSize> &wireless_remote,
    float deadzone = 0.08f);

std::vector<const char *> pressed_a2_remote_button_names(
    const A2RemoteState &state);

}  // namespace a2_lowlevel

#endif  // A2_LOWLEVEL_A2_REMOTE_H_

#include "a2_lowlevel/a2_remote.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace a2_lowlevel {
namespace {

constexpr std::size_t kR1L1ButtonByte = 2;
constexpr std::size_t kAbxyButtonByte = 3;
constexpr std::size_t kLxOffset = 4;
constexpr std::size_t kRxOffset = 8;
constexpr std::size_t kRyOffset = 12;
constexpr std::size_t kLyOffset = 20;

bool bit_is_set(std::uint8_t value, unsigned bit) {
  return (value & static_cast<std::uint8_t>(1U << bit)) != 0U;
}

float read_little_endian_float(
    const std::array<std::uint8_t, kA2RemotePacketSize> &bytes,
    std::size_t offset) {
  std::uint32_t raw = static_cast<std::uint32_t>(bytes[offset]) |
                      (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
                      (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
                      (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
  float value = 0.0f;
  static_assert(sizeof(value) == sizeof(raw), "float must be 32-bit");
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

float sanitize_deadzone(float deadzone) {
  if (!std::isfinite(deadzone)) {
    return 0.0f;
  }
  return std::clamp(std::abs(deadzone), 0.0f, 1.0f);
}

float apply_deadzone_and_clamp(float value, float deadzone) {
  const float clamped = std::clamp(value, -1.0f, 1.0f);
  if (std::abs(clamped) <= deadzone) {
    return 0.0f;
  }
  return clamped;
}

}  // namespace

A2RemoteState decode_a2_remote(
    const std::array<std::uint8_t, kA2RemotePacketSize> &wireless_remote,
    float deadzone) {
  A2RemoteState state;

  const std::uint8_t byte2 = wireless_remote[kR1L1ButtonByte];
  state.buttons.r1 = bit_is_set(byte2, 0);
  state.buttons.l1 = bit_is_set(byte2, 1);
  state.buttons.start = bit_is_set(byte2, 2);
  state.buttons.select = bit_is_set(byte2, 3);
  state.buttons.r2 = bit_is_set(byte2, 4);
  state.buttons.l2 = bit_is_set(byte2, 5);
  state.buttons.f1 = bit_is_set(byte2, 6);
  state.buttons.f3 = bit_is_set(byte2, 7);

  const std::uint8_t byte3 = wireless_remote[kAbxyButtonByte];
  state.buttons.a = bit_is_set(byte3, 0);
  state.buttons.b = bit_is_set(byte3, 1);
  state.buttons.x = bit_is_set(byte3, 2);
  state.buttons.y = bit_is_set(byte3, 3);
  state.buttons.up = bit_is_set(byte3, 4);
  state.buttons.right = bit_is_set(byte3, 5);
  state.buttons.down = bit_is_set(byte3, 6);
  state.buttons.left = bit_is_set(byte3, 7);

  state.raw_lx = read_little_endian_float(wireless_remote, kLxOffset);
  state.raw_rx = read_little_endian_float(wireless_remote, kRxOffset);
  state.raw_ry = read_little_endian_float(wireless_remote, kRyOffset);
  state.raw_ly = read_little_endian_float(wireless_remote, kLyOffset);

  if (!std::isfinite(state.raw_lx) || !std::isfinite(state.raw_rx) ||
      !std::isfinite(state.raw_ry) || !std::isfinite(state.raw_ly)) {
    state.lx = 0.0f;
    state.rx = 0.0f;
    state.ry = 0.0f;
    state.ly = 0.0f;
    state.valid = false;
    return state;
  }

  const float safe_deadzone = sanitize_deadzone(deadzone);
  state.lx = apply_deadzone_and_clamp(state.raw_lx, safe_deadzone);
  state.rx = apply_deadzone_and_clamp(state.raw_rx, safe_deadzone);
  state.ry = apply_deadzone_and_clamp(state.raw_ry, safe_deadzone);
  state.ly = apply_deadzone_and_clamp(state.raw_ly, safe_deadzone);
  state.valid = true;
  return state;
}

std::vector<const char *> pressed_a2_remote_button_names(
    const A2RemoteState &state) {
  std::vector<const char *> names;
  const auto &buttons = state.buttons;
  if (buttons.r1) names.push_back("R1");
  if (buttons.l1) names.push_back("L1");
  if (buttons.start) names.push_back("Start");
  if (buttons.select) names.push_back("Select");
  if (buttons.r2) names.push_back("R2");
  if (buttons.l2) names.push_back("L2");
  if (buttons.f1) names.push_back("F1");
  if (buttons.f3) names.push_back("F3");
  if (buttons.a) names.push_back("A");
  if (buttons.b) names.push_back("B");
  if (buttons.x) names.push_back("X");
  if (buttons.y) names.push_back("Y");
  if (buttons.up) names.push_back("Up");
  if (buttons.right) names.push_back("Right");
  if (buttons.down) names.push_back("Down");
  if (buttons.left) names.push_back("Left");
  return names;
}

}  // namespace a2_lowlevel

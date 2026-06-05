#include "a2_lowlevel/a2_remote.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {

void write_little_endian_float(
    std::array<std::uint8_t, a2_lowlevel::kA2RemotePacketSize> &bytes,
    std::size_t offset, float value) {
  std::uint32_t raw = 0;
  static_assert(sizeof(raw) == sizeof(value), "float must be 32-bit");
  std::memcpy(&raw, &value, sizeof(raw));
  bytes[offset] = static_cast<std::uint8_t>(raw & 0xFFU);
  bytes[offset + 1] = static_cast<std::uint8_t>((raw >> 8U) & 0xFFU);
  bytes[offset + 2] = static_cast<std::uint8_t>((raw >> 16U) & 0xFFU);
  bytes[offset + 3] = static_cast<std::uint8_t>((raw >> 24U) & 0xFFU);
}

bool close(float actual, float expected) {
  return std::abs(actual - expected) < 1e-6f;
}

}  // namespace

int main() {
  std::array<std::uint8_t, a2_lowlevel::kA2RemotePacketSize> bytes{};
  bytes[2] = static_cast<std::uint8_t>((1U << 3U) | (1U << 5U));
  bytes[3] = static_cast<std::uint8_t>((1U << 1U) | (1U << 5U));
  write_little_endian_float(bytes, 4, 0.05f);
  write_little_endian_float(bytes, 8, -0.50f);
  write_little_endian_float(bytes, 12, 1.40f);
  write_little_endian_float(bytes, 20, 0.90f);

  const auto decoded = a2_lowlevel::decode_a2_remote(bytes, 0.08f);
  assert(decoded.valid);
  assert(close(decoded.raw_lx, 0.05f));
  assert(close(decoded.raw_rx, -0.50f));
  assert(close(decoded.raw_ry, 1.40f));
  assert(close(decoded.raw_ly, 0.90f));
  assert(close(decoded.lx, 0.0f));
  assert(close(decoded.rx, -0.50f));
  assert(close(decoded.ry, 1.0f));
  assert(close(decoded.ly, 0.90f));
  assert(decoded.buttons.select);
  assert(decoded.buttons.l2);
  assert(decoded.buttons.b);
  assert(decoded.buttons.right);
  assert(!decoded.buttons.a);

  const auto names = a2_lowlevel::pressed_a2_remote_button_names(decoded);
  assert(names.size() == 4);

  write_little_endian_float(bytes, 4, std::nanf(""));
  const auto invalid = a2_lowlevel::decode_a2_remote(bytes, 0.08f);
  assert(!invalid.valid);
  assert(close(invalid.lx, 0.0f));
  assert(close(invalid.rx, 0.0f));
  assert(close(invalid.ry, 0.0f));
  assert(close(invalid.ly, 0.0f));

  std::cout << "a2_remote_decode_test passed\n";
  return 0;
}

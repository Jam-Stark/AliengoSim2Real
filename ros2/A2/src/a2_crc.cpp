#include "a2_lowlevel/a2_crc.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace a2_lowlevel {
namespace a2_crc {
namespace {

struct RawMotorCommand {
  std::uint8_t mode = 0;
  std::array<std::uint8_t, 3> padding{};
  float q = 0.0f;
  float dq = 0.0f;
  float tau = 0.0f;
  float kp = 0.0f;
  float kd = 0.0f;
  std::uint32_t reserve = 0;
};

struct RawLowCommand {
  std::uint8_t mode_pr = 0;
  std::uint8_t mode_machine = 0;
  std::array<std::uint8_t, 2> padding{};
  std::array<RawMotorCommand, 35> motor_cmd{};
  std::array<std::uint32_t, 4> reserve{};
  std::uint32_t crc = 0;
};

static_assert(sizeof(RawMotorCommand) == 28,
              "A2 raw MotorCmd layout must remain 28 bytes.");
static_assert(offsetof(RawMotorCommand, q) == 4,
              "A2 raw MotorCmd q offset must match manual layout.");
static_assert(offsetof(RawMotorCommand, dq) == 8,
              "A2 raw MotorCmd dq offset must match manual layout.");
static_assert(offsetof(RawMotorCommand, tau) == 12,
              "A2 raw MotorCmd tau offset must match manual layout.");
static_assert(offsetof(RawMotorCommand, kp) == 16,
              "A2 raw MotorCmd kp offset must match manual layout.");
static_assert(offsetof(RawMotorCommand, kd) == 20,
              "A2 raw MotorCmd kd offset must match manual layout.");
static_assert(offsetof(RawMotorCommand, reserve) == 24,
              "A2 raw MotorCmd reserve offset must match manual layout.");
static_assert(offsetof(RawLowCommand, motor_cmd) == 4,
              "A2 raw LowCmd motor_cmd offset must match manual layout.");
static_assert(offsetof(RawLowCommand, reserve) == 984,
              "A2 raw LowCmd reserve offset must match manual layout.");
static_assert(offsetof(RawLowCommand, crc) == 1000,
              "A2 raw LowCmd crc offset must match manual layout.");
static_assert(sizeof(RawLowCommand) == 1004,
              "A2 raw LowCmd layout must remain 1004 bytes.");

constexpr std::size_t kLowCmdCrcBytes = offsetof(RawLowCommand, crc);
constexpr std::size_t kLowCmdCrcWords = kLowCmdCrcBytes / sizeof(std::uint32_t);
static_assert(kLowCmdCrcBytes % sizeof(std::uint32_t) == 0,
              "A2 raw LowCmd CRC region must be 32-bit aligned.");

template <typename ReserveT>
std::uint32_t first_reserve_word(const ReserveT &reserve) {
  if constexpr (std::is_arithmetic_v<ReserveT>) {
    return static_cast<std::uint32_t>(reserve);
  } else {
    return reserve.empty() ? 0U : static_cast<std::uint32_t>(reserve[0]);
  }
}

template <typename ReserveT>
void copy_low_cmd_reserve(const ReserveT &source,
                          std::array<std::uint32_t, 4> &target) {
  target.fill(0);
  if constexpr (std::is_arithmetic_v<ReserveT>) {
    target[0] = static_cast<std::uint32_t>(source);
  } else {
    const std::size_t count = std::min(target.size(), source.size());
    for (std::size_t i = 0; i < count; ++i) {
      target[i] = static_cast<std::uint32_t>(source[i]);
    }
  }
}

RawLowCommand to_raw_low_command(const unitree_hg::msg::LowCmd &command) {
  RawLowCommand raw{};
  raw.mode_pr = static_cast<std::uint8_t>(command.mode_pr);
  raw.mode_machine = static_cast<std::uint8_t>(command.mode_machine);

  const std::size_t motor_count =
      std::min(raw.motor_cmd.size(), command.motor_cmd.size());
  for (std::size_t i = 0; i < motor_count; ++i) {
    raw.motor_cmd[i].mode =
        static_cast<std::uint8_t>(command.motor_cmd[i].mode);
    raw.motor_cmd[i].q = command.motor_cmd[i].q;
    raw.motor_cmd[i].dq = command.motor_cmd[i].dq;
    raw.motor_cmd[i].tau = command.motor_cmd[i].tau;
    raw.motor_cmd[i].kp = command.motor_cmd[i].kp;
    raw.motor_cmd[i].kd = command.motor_cmd[i].kd;
    raw.motor_cmd[i].reserve = first_reserve_word(command.motor_cmd[i].reserve);
  }

  copy_low_cmd_reserve(command.reserve, raw.reserve);
  return raw;
}

}  // namespace

std::uint32_t crc32_core(const std::uint32_t *words,
                         std::size_t word_count) {
  std::uint32_t crc = 0xFFFFFFFFU;
  constexpr std::uint32_t polynomial = 0x04C11DB7U;

  for (std::size_t i = 0; i < word_count; ++i) {
    std::uint32_t xbit = 1U << 31;
    const std::uint32_t data = words[i];
    for (std::uint32_t bit = 0; bit < 32; ++bit) {
      if ((crc & 0x80000000U) != 0U) {
        crc <<= 1U;
        crc ^= polynomial;
      } else {
        crc <<= 1U;
      }

      if ((data & xbit) != 0U) {
        crc ^= polynomial;
      }
      xbit >>= 1U;
    }
  }

  return crc;
}

std::uint32_t compute_low_cmd_crc(const unitree_hg::msg::LowCmd &command) {
  const RawLowCommand raw = to_raw_low_command(command);
  std::array<std::uint32_t, kLowCmdCrcWords> words{};
  std::memcpy(words.data(), &raw, kLowCmdCrcBytes);
  return crc32_core(words.data(), words.size());
}

void update_low_cmd_crc(unitree_hg::msg::LowCmd &command) {
  command.crc = compute_low_cmd_crc(command);
}

}  // namespace a2_crc
}  // namespace a2_lowlevel

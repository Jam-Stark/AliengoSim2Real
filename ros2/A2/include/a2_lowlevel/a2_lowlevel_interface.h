#ifndef A2_LOWLEVEL_A2_LOWLEVEL_INTERFACE_H_
#define A2_LOWLEVEL_A2_LOWLEVEL_INTERFACE_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "rclcpp/rclcpp.hpp"
#include "unitree_hg/msg/low_cmd.hpp"
#include "unitree_hg/msg/low_state.hpp"

namespace a2_lowlevel {

inline constexpr std::size_t kA2JointCount = 12;
inline constexpr std::size_t kA2LowCmdMotorCount = 35;
inline constexpr std::size_t kA2WirelessRemoteSize = 40;
inline constexpr std::uint8_t kA2StopMode = 0x00;
inline constexpr std::uint8_t kA2FocMode = 0x01;

enum class A2MotorIndex : std::size_t {
  FR_BODY = 0,
  FR_THIGH = 1,
  FR_CALF = 2,
  FL_BODY = 3,
  FL_THIGH = 4,
  FL_CALF = 5,
  RR_BODY = 6,
  RR_THIGH = 7,
  RR_CALF = 8,
  RL_BODY = 9,
  RL_THIGH = 10,
  RL_CALF = 11,
};

inline constexpr std::array<A2MotorIndex, kA2JointCount> kA2MotorOrder = {
    A2MotorIndex::FR_BODY, A2MotorIndex::FR_THIGH, A2MotorIndex::FR_CALF,
    A2MotorIndex::FL_BODY, A2MotorIndex::FL_THIGH, A2MotorIndex::FL_CALF,
    A2MotorIndex::RR_BODY, A2MotorIndex::RR_THIGH, A2MotorIndex::RR_CALF,
    A2MotorIndex::RL_BODY, A2MotorIndex::RL_THIGH, A2MotorIndex::RL_CALF};

inline constexpr std::array<const char *, kA2JointCount> kA2MotorNames = {
    "FR_BODY", "FR_THIGH", "FR_CALF", "FL_BODY", "FL_THIGH", "FL_CALF",
    "RR_BODY", "RR_THIGH", "RR_CALF", "RL_BODY", "RL_THIGH", "RL_CALF"};

struct A2LowStateSnapshot {
  bool has_state = false;
  std::uint8_t mode_pr = 0;
  std::uint8_t mode_machine = 0;
  std::uint32_t tick = 0;
  std::array<float, 4> quaternion{};
  std::array<float, 3> gyroscope{};
  std::array<float, kA2JointCount> joint_q{};
  std::array<float, kA2JointCount> joint_dq{};
  std::array<std::uint8_t, kA2WirelessRemoteSize> wireless_remote{};
};

struct A2JointCommand {
  std::uint8_t mode = kA2FocMode;
  float q = 0.0f;
  float dq = 0.0f;
  float tau = 0.0f;
  float kp = 0.0f;
  float kd = 0.0f;
};

class A2LowLevelInterface : public rclcpp::Node {
 public:
  explicit A2LowLevelInterface(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  A2LowStateSnapshot latest_state() const;
  bool has_fresh_state(std::chrono::milliseconds timeout) const;
  bool publish_zero();
  bool publish_joint_commands(
      const std::array<A2JointCommand, kA2JointCount> &commands);

 private:
  void low_state_callback(const unitree_hg::msg::LowState::SharedPtr msg);

  rclcpp::Publisher<unitree_hg::msg::LowCmd>::SharedPtr low_cmd_pub_;
  rclcpp::Subscription<unitree_hg::msg::LowState>::SharedPtr low_state_sub_;

  mutable std::mutex state_mutex_;
  A2LowStateSnapshot latest_state_;
  std::chrono::steady_clock::time_point latest_state_time_{};
  std::chrono::milliseconds fresh_state_timeout_{200};
};

}  // namespace a2_lowlevel

#endif  // A2_LOWLEVEL_A2_LOWLEVEL_INTERFACE_H_

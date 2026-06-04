#include "a2_lowlevel/a2_lowlevel_interface.h"

#include <algorithm>
#include <iterator>
#include <type_traits>

#include "a2_lowlevel/a2_crc.h"

namespace a2_lowlevel {
namespace {

template <typename SourceArray, typename TargetArray>
void copy_array_prefix(const SourceArray &source, TargetArray &target) {
  const std::size_t count = std::min(target.size(), source.size());
  std::copy_n(source.begin(), count, target.begin());
  if (count < target.size()) {
    std::fill(target.begin() + static_cast<std::ptrdiff_t>(count),
              target.end(), typename TargetArray::value_type{});
  }
}

template <typename ReserveT>
void clear_reserve(ReserveT &reserve) {
  if constexpr (std::is_arithmetic_v<ReserveT>) {
    reserve = 0;
  } else {
    std::fill(reserve.begin(), reserve.end(), typename ReserveT::value_type{});
  }
}

template <typename MotorCommandT>
void clear_motor_command(MotorCommandT &motor_command) {
  motor_command.mode = kA2StopMode;
  motor_command.q = 0.0f;
  motor_command.dq = 0.0f;
  motor_command.tau = 0.0f;
  motor_command.kp = 0.0f;
  motor_command.kd = 0.0f;
  clear_reserve(motor_command.reserve);
}

void clear_low_command(unitree_hg::msg::LowCmd &command) {
  command.mode_pr = 0;
  command.mode_machine = 0;
  for (auto &motor_command : command.motor_cmd) {
    clear_motor_command(motor_command);
  }
  std::fill(command.reserve.begin(), command.reserve.end(), 0U);
  command.crc = 0;
}

bool is_nonzero_joint_command(const A2JointCommand &command) {
  return command.q != 0.0f || command.dq != 0.0f || command.tau != 0.0f ||
         command.kp != 0.0f || command.kd != 0.0f;
}

}  // namespace

A2LowLevelInterface::A2LowLevelInterface(const rclcpp::NodeOptions &options)
    : rclcpp::Node("a2_lowlevel_interface", options) {
  const int state_timeout_ms =
      this->declare_parameter<int>("state_timeout_ms", 200);
  fresh_state_timeout_ =
      std::chrono::milliseconds(std::max(1, state_timeout_ms));

  low_cmd_pub_ =
      this->create_publisher<unitree_hg::msg::LowCmd>("rt/lowcmd", 10);
  low_state_sub_ = this->create_subscription<unitree_hg::msg::LowState>(
      "rt/lowstate", 10,
      [this](const unitree_hg::msg::LowState::SharedPtr msg) {
        low_state_callback(msg);
      });

  RCLCPP_INFO(this->get_logger(),
              "A2 low-level interface ready: pub rt/lowcmd, sub rt/lowstate, "
              "fresh_state_timeout=%ld ms",
              static_cast<long>(fresh_state_timeout_.count()));
}

A2LowStateSnapshot A2LowLevelInterface::latest_state() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return latest_state_;
}

bool A2LowLevelInterface::has_fresh_state(
    std::chrono::milliseconds timeout) const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!latest_state_.has_state) {
    return false;
  }
  const auto age = std::chrono::steady_clock::now() - latest_state_time_;
  return age <= timeout;
}

bool A2LowLevelInterface::publish_zero() {
  A2LowStateSnapshot state;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state = latest_state_;
  }

  unitree_hg::msg::LowCmd command{};
  clear_low_command(command);
  if (state.has_state) {
    command.mode_pr = state.mode_pr;
    command.mode_machine = state.mode_machine;
  }

  a2_crc::update_low_cmd_crc(command);
  low_cmd_pub_->publish(command);
  return true;
}

bool A2LowLevelInterface::publish_joint_commands(
    const std::array<A2JointCommand, kA2JointCount> &commands) {
  A2LowStateSnapshot state;
  bool fresh = false;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state = latest_state_;
    if (latest_state_.has_state) {
      const auto age = std::chrono::steady_clock::now() - latest_state_time_;
      fresh = age <= fresh_state_timeout_;
    }
  }

  if (!fresh) {
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "Refusing A2 joint command because rt/lowstate is missing or stale.");
    return false;
  }

  unitree_hg::msg::LowCmd command{};
  clear_low_command(command);
  command.mode_pr = state.mode_pr;
  command.mode_machine = state.mode_machine;

  for (std::size_t i = 0; i < commands.size(); ++i) {
    command.motor_cmd[i].mode =
        is_nonzero_joint_command(commands[i]) ? kA2FocMode : commands[i].mode;
    command.motor_cmd[i].q = commands[i].q;
    command.motor_cmd[i].dq = commands[i].dq;
    command.motor_cmd[i].tau = commands[i].tau;
    command.motor_cmd[i].kp = commands[i].kp;
    command.motor_cmd[i].kd = commands[i].kd;
    clear_reserve(command.motor_cmd[i].reserve);
  }

  a2_crc::update_low_cmd_crc(command);
  low_cmd_pub_->publish(command);
  return true;
}

void A2LowLevelInterface::low_state_callback(
    const unitree_hg::msg::LowState::SharedPtr msg) {
  A2LowStateSnapshot snapshot;
  snapshot.has_state = true;
  snapshot.mode_pr = static_cast<std::uint8_t>(msg->mode_pr);
  snapshot.mode_machine = static_cast<std::uint8_t>(msg->mode_machine);
  snapshot.tick = static_cast<std::uint32_t>(msg->tick);
  copy_array_prefix(msg->imu_state.quaternion, snapshot.quaternion);
  copy_array_prefix(msg->imu_state.gyroscope, snapshot.gyroscope);
  copy_array_prefix(msg->wireless_remote, snapshot.wireless_remote);

  const std::size_t motor_count =
      std::min(snapshot.joint_q.size(), msg->motor_state.size());
  for (std::size_t i = 0; i < motor_count; ++i) {
    snapshot.joint_q[i] = msg->motor_state[i].q;
    snapshot.joint_dq[i] = msg->motor_state[i].dq;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    latest_state_ = snapshot;
    latest_state_time_ = std::chrono::steady_clock::now();
  }
}

}  // namespace a2_lowlevel

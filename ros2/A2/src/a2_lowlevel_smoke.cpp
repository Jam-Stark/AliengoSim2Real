#include "a2_lowlevel/a2_lowlevel_interface.h"
#include "a2_lowlevel/a2_remote.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "rclcpp/rclcpp.hpp"

namespace {

std::string format_joint_vector(
    const std::array<float, a2_lowlevel::kA2JointCount> &values) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3) << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << values[i];
  }
  out << "]";
  return out.str();
}

std::string format_remote_buttons(
    const a2_lowlevel::A2RemoteState &remote) {
  const auto names = a2_lowlevel::pressed_a2_remote_button_names(remote);
  if (names.empty()) {
    return "none";
  }

  std::ostringstream out;
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (i != 0) {
      out << ",";
    }
    out << names[i];
  }
  return out.str();
}

std::string format_remote_state(const a2_lowlevel::A2RemoteState &remote) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(3);
  if (!remote.valid) {
    out << "remote=invalid buttons=" << format_remote_buttons(remote);
    return out.str();
  }
  out << "remote_sticks=[lx=" << remote.lx << ", rx=" << remote.rx
      << ", ry=" << remote.ry << ", ly=" << remote.ly << "] buttons="
      << format_remote_buttons(remote);
  return out.str();
}

std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
make_stand_test_command() {
  constexpr std::array<float, a2_lowlevel::kA2JointCount> kStandTarget = {
      0.0f, 0.67f, -1.30f, 0.0f, 0.67f, -1.30f,
      0.0f, 0.67f, -1.30f, 0.0f, 0.67f, -1.30f};

  std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount> commands;
  for (std::size_t i = 0; i < commands.size(); ++i) {
    commands[i].mode = a2_lowlevel::kA2FocMode;
    commands[i].q = kStandTarget[i];
    commands[i].dq = 0.0f;
    commands[i].tau = 0.0f;
    commands[i].kp = 20.0f;
    commands[i].kd = 2.0f;
  }
  return commands;
}

}  // namespace

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<a2_lowlevel::A2LowLevelInterface>();

  const bool publish_zero =
      node->declare_parameter<bool>("publish_zero", false);
  const bool stand_test = node->declare_parameter<bool>("stand_test", false);
  const int state_timeout_ms = std::max(
      1, static_cast<int>(node->get_parameter("state_timeout_ms").as_int()));
  const double command_hz =
      std::max(0.1, node->declare_parameter<double>("command_hz", 20.0));
  const bool log_remote =
      node->declare_parameter<bool>("log_remote", false);
  const double remote_deadzone =
      node->declare_parameter<double>("remote_deadzone", 0.08);
  const auto state_timeout = std::chrono::milliseconds(state_timeout_ms);

  RCLCPP_INFO(node->get_logger(),
              "a2_lowlevel_smoke params: publish_zero=%s, stand_test=%s, "
              "state_timeout_ms=%d, command_hz=%.2f, log_remote=%s, "
              "remote_deadzone=%.3f",
              publish_zero ? "true" : "false", stand_test ? "true" : "false",
              state_timeout_ms, command_hz, log_remote ? "true" : "false",
              remote_deadzone);

  if (stand_test) {
    RCLCPP_WARN(node->get_logger(),
                "stand_test will publish low-stiffness FOC targets. Confirm "
                "Unitree ai_sports/ai_sport motion service is closed before "
                "running on hardware.");
  } else if (publish_zero) {
    RCLCPP_WARN(node->get_logger(),
                "publish_zero will publish zero/stop LowCmd frames. Confirm "
                "this is expected before running on hardware.");
  } else {
    RCLCPP_INFO(node->get_logger(),
                "listen-only mode: no LowCmd will be published.");
  }

  if (stand_test && publish_zero) {
    RCLCPP_WARN(node->get_logger(),
                "Both stand_test and publish_zero are true; stand_test takes "
                "precedence.");
  }

  auto print_timer = node->create_wall_timer(std::chrono::seconds(1), [node,
                                                                       log_remote,
                                                                       remote_deadzone] {
    const auto state = node->latest_state();
    if (!state.has_state) {
      RCLCPP_INFO(node->get_logger(), "Waiting for rt/lowstate...");
      return;
    }

    if (log_remote) {
      const auto remote = a2_lowlevel::decode_a2_remote(
          state.wireless_remote, static_cast<float>(remote_deadzone));
      RCLCPP_INFO(
          node->get_logger(),
          "tick=%u mode_pr=%u mode_machine=%u joint_q=%s joint_dq=%s %s",
          state.tick, static_cast<unsigned>(state.mode_pr),
          static_cast<unsigned>(state.mode_machine),
          format_joint_vector(state.joint_q).c_str(),
          format_joint_vector(state.joint_dq).c_str(),
          format_remote_state(remote).c_str());
      return;
    }

    RCLCPP_INFO(
        node->get_logger(),
        "tick=%u mode_pr=%u mode_machine=%u joint_q=%s joint_dq=%s",
        state.tick, static_cast<unsigned>(state.mode_pr),
        static_cast<unsigned>(state.mode_machine),
        format_joint_vector(state.joint_q).c_str(),
        format_joint_vector(state.joint_dq).c_str());
  });

  rclcpp::TimerBase::SharedPtr command_timer;
  if (stand_test || publish_zero) {
    const auto command_period =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(1.0 / command_hz));
    const auto stand_command = make_stand_test_command();

    command_timer = node->create_wall_timer(command_period, [node,
                                                             state_timeout,
                                                             stand_test,
                                                             stand_command] {
      if (stand_test) {
        if (!node->has_fresh_state(state_timeout)) {
          RCLCPP_WARN_THROTTLE(
              node->get_logger(), *node->get_clock(), 2000,
              "stand_test waiting for fresh rt/lowstate before publishing.");
          return;
        }
        node->publish_joint_commands(stand_command);
        return;
      }

      node->publish_zero();
    });
  }

  rclcpp::spin(node);
  command_timer.reset();
  print_timer.reset();
  rclcpp::shutdown();
  return 0;
}

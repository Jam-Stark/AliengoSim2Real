#include "a2_lowlevel/a2_lowlevel_interface.h"
#include "a2_lowlevel/a2_remote.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

std::string format_joint_order() {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < a2_lowlevel::kA2MotorNames.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << a2_lowlevel::kA2MotorNames[i];
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

double smoothstep(double x) {
  const double clamped = std::clamp(x, 0.0, 1.0);
  return clamped * clamped * (3.0 - 2.0 * clamped);
}

std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
make_pose_command(const std::array<float, a2_lowlevel::kA2JointCount> &target,
                  const float kp, const float kd) {
  std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount> commands;
  for (std::size_t i = 0; i < commands.size(); ++i) {
    commands[i].mode = a2_lowlevel::kA2FocMode;
    commands[i].q = target[i];
    commands[i].dq = 0.0f;
    commands[i].tau = 0.0f;
    commands[i].kp = kp;
    commands[i].kd = kd;
  }
  return commands;
}

std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
make_stand_test_command() {
  constexpr std::array<float, a2_lowlevel::kA2JointCount> kStandTarget = {
      0.0f, 0.67f, -1.30f, 0.0f, 0.67f, -1.30f,
      0.0f, 0.67f, -1.30f, 0.0f, 0.67f, -1.30f};

  return make_pose_command(kStandTarget, 20.0f, 2.0f);
}

}  // namespace

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<a2_lowlevel::A2LowLevelInterface>();

  const bool publish_zero =
      node->declare_parameter<bool>("publish_zero", false);
  const bool stand_test = node->declare_parameter<bool>("stand_test", false);
  const bool custom_pose = node->declare_parameter<bool>("custom_pose", false);
  const auto custom_pose_q_values =
      node->declare_parameter<std::vector<double>>("custom_pose_q",
                                                  std::vector<double>{});
  const double custom_pose_kp =
      node->declare_parameter<double>("custom_pose_kp", 20.0);
  const double custom_pose_kd =
      node->declare_parameter<double>("custom_pose_kd", 2.0);
  const double custom_pose_interpolate_sec =
      node->declare_parameter<double>("custom_pose_interpolate_sec", 2.0);
  const int state_timeout_ms = std::max(
      1, static_cast<int>(node->get_parameter("state_timeout_ms").as_int()));
  const double command_hz =
      std::max(0.1, node->declare_parameter<double>("command_hz", 20.0));
  const bool log_remote =
      node->declare_parameter<bool>("log_remote", false);
  const double remote_deadzone =
      node->declare_parameter<double>("remote_deadzone", 0.08);
  const auto state_timeout = std::chrono::milliseconds(state_timeout_ms);

  std::array<float, a2_lowlevel::kA2JointCount> custom_pose_target{};
  if (custom_pose) {
    if (custom_pose_q_values.size() != a2_lowlevel::kA2JointCount) {
      RCLCPP_ERROR(node->get_logger(),
                   "custom_pose_q length must be %zu in A2 low-level order "
                   "%s; got %zu.",
                   a2_lowlevel::kA2JointCount, format_joint_order().c_str(),
                   custom_pose_q_values.size());
      rclcpp::shutdown();
      return 2;
    }
    if (!std::isfinite(custom_pose_kp) || custom_pose_kp < 0.0 ||
        !std::isfinite(custom_pose_kd) || custom_pose_kd < 0.0 ||
        !std::isfinite(custom_pose_interpolate_sec) ||
        custom_pose_interpolate_sec < 0.0) {
      RCLCPP_ERROR(node->get_logger(),
                   "custom_pose gains/timing must be finite and non-negative: "
                   "kp=%.6f kd=%.6f interpolate_sec=%.6f",
                   custom_pose_kp, custom_pose_kd,
                   custom_pose_interpolate_sec);
      rclcpp::shutdown();
      return 2;
    }
    for (std::size_t i = 0; i < custom_pose_q_values.size(); ++i) {
      if (!std::isfinite(custom_pose_q_values[i])) {
        RCLCPP_ERROR(node->get_logger(),
                     "custom_pose_q[%zu] for %s is not finite: %.6f", i,
                     a2_lowlevel::kA2MotorNames[i], custom_pose_q_values[i]);
        rclcpp::shutdown();
        return 2;
      }
      custom_pose_target[i] = static_cast<float>(custom_pose_q_values[i]);
    }
  }

  RCLCPP_INFO(node->get_logger(),
              "a2_lowlevel_smoke params: publish_zero=%s, stand_test=%s, "
              "custom_pose=%s, "
              "state_timeout_ms=%d, command_hz=%.2f, log_remote=%s, "
              "remote_deadzone=%.3f",
              publish_zero ? "true" : "false", stand_test ? "true" : "false",
              custom_pose ? "true" : "false", state_timeout_ms, command_hz,
              log_remote ? "true" : "false", remote_deadzone);

  if (custom_pose) {
    RCLCPP_WARN(node->get_logger(),
                "custom_pose will publish LowCmd joint targets through "
                "publish_joint_commands(): target_q=%s kp=%.3f kd=%.3f "
                "interpolate_sec=%.3f order=%s. Confirm Unitree built-in "
                "motion service is closed and no other LowCmd publisher is "
                "active.",
                format_joint_vector(custom_pose_target).c_str(),
                custom_pose_kp, custom_pose_kd,
                custom_pose_interpolate_sec, format_joint_order().c_str());
  } else if (stand_test) {
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

  if (custom_pose && (stand_test || publish_zero)) {
    RCLCPP_WARN(node->get_logger(),
                "custom_pose=true takes precedence over stand_test and "
                "publish_zero; lower-priority publish modes are ignored.");
  } else if (stand_test && publish_zero) {
    RCLCPP_WARN(node->get_logger(),
                "Both stand_test and publish_zero are true; stand_test takes "
                "precedence.");
  }

  auto print_timer = node->create_wall_timer(std::chrono::seconds(1), [node,
                                                                       log_remote,
                                                                       remote_deadzone] {
    const auto state = node->latest_state();
    if (!state.has_state) {
      RCLCPP_INFO(node->get_logger(), "Waiting for %s...",
                  node->lowstate_topic().c_str());
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
  if (custom_pose || stand_test || publish_zero) {
    const auto command_period =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(1.0 / command_hz));
    const auto stand_command = make_stand_test_command();
    const float custom_kp = static_cast<float>(custom_pose_kp);
    const float custom_kd = static_cast<float>(custom_pose_kd);
    std::array<float, a2_lowlevel::kA2JointCount> custom_pose_start{};
    std::chrono::steady_clock::time_point custom_pose_start_time{};
    bool custom_pose_started = false;

    command_timer = node->create_wall_timer(command_period, [node,
                                                             state_timeout,
                                                             custom_pose,
                                                             stand_test,
                                                             stand_command,
                                                             custom_pose_target,
                                                             custom_kp,
                                                             custom_kd,
                                                             custom_pose_interpolate_sec,
                                                             custom_pose_start,
                                                             custom_pose_start_time,
                                                             custom_pose_started] mutable {
      if (custom_pose) {
        if (!node->has_fresh_state(state_timeout)) {
          RCLCPP_WARN_THROTTLE(
              node->get_logger(), *node->get_clock(), 2000,
              "custom_pose waiting for fresh %s before publishing.",
              node->lowstate_topic().c_str());
          return;
        }

        const auto state = node->latest_state();
        if (!state.has_state) {
          RCLCPP_WARN_THROTTLE(
              node->get_logger(), *node->get_clock(), 2000,
              "custom_pose has no LowState snapshot after fresh-state check.");
          return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!custom_pose_started) {
          custom_pose_start = state.joint_q;
          custom_pose_start_time = now;
          custom_pose_started = true;
          RCLCPP_INFO(node->get_logger(),
                      "custom_pose captured start_q=%s; interpolating to "
                      "target_q=%s over %.3f sec.",
                      format_joint_vector(custom_pose_start).c_str(),
                      format_joint_vector(custom_pose_target).c_str(),
                      custom_pose_interpolate_sec);
        }

        double alpha = 1.0;
        if (custom_pose_interpolate_sec > 0.0) {
          const double elapsed =
              std::chrono::duration<double>(now - custom_pose_start_time)
                  .count();
          alpha = smoothstep(elapsed / custom_pose_interpolate_sec);
        }

        std::array<float, a2_lowlevel::kA2JointCount> interpolated_target{};
        for (std::size_t i = 0; i < interpolated_target.size(); ++i) {
          interpolated_target[i] = static_cast<float>(
              static_cast<double>(custom_pose_start[i]) +
              (static_cast<double>(custom_pose_target[i]) -
               static_cast<double>(custom_pose_start[i])) *
                  alpha);
        }
        node->publish_joint_commands(
            make_pose_command(interpolated_target, custom_kp, custom_kd));
        return;
      }

      if (stand_test) {
        if (!node->has_fresh_state(state_timeout)) {
          RCLCPP_WARN_THROTTLE(
              node->get_logger(), *node->get_clock(), 2000,
              "stand_test waiting for fresh %s before publishing.",
              node->lowstate_topic().c_str());
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

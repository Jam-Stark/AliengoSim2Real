#include "a2_piper_stage2_direct/stage2_direct_node.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "trajectory_msgs/msg/joint_trajectory_point.hpp"

#ifndef A2_PIPER_STAGE2_DEFAULT_BUNDLE_PATH
#define A2_PIPER_STAGE2_DEFAULT_BUNDLE_PATH \
  "deploy/a2_piper_stage2/policy_bundle"
#endif

namespace a2_piper_stage2_direct {
namespace {

constexpr std::array<std::size_t, 12> kTrainingToA2Index = {
    3, 0, 9, 6, 4, 1, 10, 7, 5, 2, 11, 8};
constexpr std::array<const char *, 6> kPiperJointNames = {
    "arm_j1", "arm_j2", "arm_j3", "arm_j4", "arm_j5", "arm_j6"};
constexpr double kRemoteZeroEpsilon = 1.0e-5;

double smoothstep01(double value) {
  const double clamped = std::clamp(value, 0.0, 1.0);
  return clamped * clamped * (3.0 - 2.0 * clamped);
}

bool is_rear_training_joint(std::size_t index) {
  return index == 2 || index == 3 || index == 6 || index == 7 || index == 10 ||
         index == 11;
}

template <typename Container>
bool all_finite(const Container &values) {
  return std::all_of(values.begin(), values.end(),
                     [](const auto value) { return std::isfinite(value); });
}

std::string clean_status_reason(std::string reason) {
  std::replace(reason.begin(), reason.end(), ';', ',');
  std::replace(reason.begin(), reason.end(), '\n', ' ');
  return reason;
}

}  // namespace

Stage2DirectNode::Stage2DirectNode(const rclcpp::NodeOptions &options)
    : A2LowLevelInterface(options) {
  bundle_dir_ = this->declare_parameter<std::string>(
      "bundle_dir", A2_PIPER_STAGE2_DEFAULT_BUNDLE_PATH);
  site_config_ =
      this->declare_parameter<std::string>("site_config", "/site.yaml");
  validate_live_site_only_ =
      this->declare_parameter<bool>("validate_live_site_only", false);
  piper_state_topic_ = this->declare_parameter<std::string>(
      "piper_state_topic", "/piper/joint_states");
  piper_command_topic_ = this->declare_parameter<std::string>(
      "piper_command_topic", "/piper/joint_command");
  piper_stop_service_ = this->declare_parameter<std::string>(
      "piper_stop_service", "/piper/stop");
  status_topic_ = this->declare_parameter<std::string>(
      "status_topic", "/a2_piper_stage2/status");
  enable_motion_ = this->declare_parameter<bool>("enable_motion", false);
  live_acknowledged_ =
      this->declare_parameter<bool>("live_acknowledged", false);
  component_mode_param_ =
      this->declare_parameter<std::string>("component_mode", "both");
  a2_state_max_age_ms_ =
      this->declare_parameter<int>("a2_state_max_age_ms", 200);
  piper_state_max_age_ms_ =
      this->declare_parameter<int>("piper_state_max_age_ms", 200);
  maximum_state_skew_ms_ =
      this->declare_parameter<int>("maximum_state_skew_ms", 50);
  max_remote_vx_ = this->declare_parameter<double>("max_remote_vx", 0.8);
  max_remote_vy_ = this->declare_parameter<double>("max_remote_vy", 0.5);
  max_remote_yaw_ = this->declare_parameter<double>("max_remote_yaw", 0.6);
  remote_deadzone_ = this->declare_parameter<double>("remote_deadzone", 0.08);
  arm_goal_radius_m_ =
      this->declare_parameter<double>("arm_goal_radius_m", 0.6);
  arm_goal_pitch_rad_ =
      this->declare_parameter<double>("arm_goal_pitch_rad", 0.0);
  arm_goal_yaw_rad_ =
      this->declare_parameter<double>("arm_goal_yaw_rad", 0.0);
  standup_stage1_steps_ =
      this->declare_parameter<int>("standup_stage1_steps", 150);
  standup_stage2_steps_ =
      this->declare_parameter<int>("standup_stage2_steps", 150);
  standup_rear_alpha_lead_ =
      this->declare_parameter<double>("standup_rear_alpha_lead", 0.10);
  standup_front_alpha_lag_ =
      this->declare_parameter<double>("standup_front_alpha_lag", 0.04);
  standup_kp_start_ =
      this->declare_parameter<double>("standup_kp_start", 3.0);
  standup_kd_start_ =
      this->declare_parameter<double>("standup_kd_start", 0.5);
  controlled_down_steps_ =
      this->declare_parameter<int>("controlled_down_steps", 250);
  controlled_down_hip_q_ =
      this->declare_parameter<double>("controlled_down_hip_q", 0.0);
  controlled_down_thigh_q_ =
      this->declare_parameter<double>("controlled_down_thigh_q", 1.5);
  controlled_down_calf_q_ =
      this->declare_parameter<double>("controlled_down_calf_q", -2.77);
  controlled_down_gain_scale_ =
      this->declare_parameter<double>("controlled_down_gain_scale", 1.0);
  status_period_ms_ =
      this->declare_parameter<int>("status_period_ms", 200);

  Stage2Contract policy_contract = Stage2Contract::load(bundle_dir_);
  if ((enable_motion_ && live_acknowledged_) || validate_live_site_only_) {
    const LiveSiteContract site_contract =
        LiveSiteContract::load_and_apply(site_config_, policy_contract);
    inference_deadline_s_ = site_contract.inference_deadline_s;
    consecutive_deadline_miss_limit_ =
        site_contract.consecutive_deadline_miss_limit;
    live_site_contract_loaded_ = true;
  }
  runtime_ =
      std::make_unique<DualPolicyRuntime>(std::move(policy_contract));

  const auto command_qos =
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile();
  piper_command_pub_ =
      this->create_publisher<trajectory_msgs::msg::JointTrajectory>(
          piper_command_topic_, command_qos);
  piper_state_sub_ = this->create_subscription<sensor_msgs::msg::JointState>(
      piper_state_topic_, rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::JointState::SharedPtr message) {
        piper_state_callback(message);
      });
  piper_stop_client_ =
      this->create_client<std_srvs::srv::Trigger>(piper_stop_service_);
  status_pub_ =
      this->create_publisher<std_msgs::msg::String>(status_topic_, 10);

  if (!refresh_parameters()) {
    throw std::runtime_error("Invalid Stage2 direct runtime parameters");
  }
  const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(runtime_->contract().policy_period_s));
  control_timer_ =
      this->create_wall_timer(period, [this]() { control_tick(); });

  RCLCPP_INFO(
      this->get_logger(),
      "Stage2 direct node ready: bundle=%s dog=[1620->12] arm=[600->8] "
      "rate=50Hz piper_state=%s piper_command=%s output requires "
      "enable_motion=true AND live_acknowledged=true",
      runtime_->contract().bundle_dir.string().c_str(), piper_state_topic_.c_str(),
      piper_command_topic_.c_str());
  if (validate_live_site_only_) {
    publish_status("ready", "live site contract validation passed", true);
  } else {
    publish_status("waiting", "awaiting synchronized A2 and PiPER state", true);
  }
}

void Stage2DirectNode::piper_state_callback(
    const sensor_msgs::msg::JointState::SharedPtr message) {
  if (message->name.size() != kPiperJointNames.size() ||
      message->position.size() != kPiperJointNames.size()) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "Rejecting PiPER JointState: expected exactly 6 names and positions");
    return;
  }

  PiperSnapshot snapshot;
  snapshot.has_state = true;
  snapshot.received_steady_time = SteadyClock::now();
  for (std::size_t expected = 0; expected < kPiperJointNames.size(); ++expected) {
    std::size_t found = message->name.size();
    for (std::size_t index = 0; index < message->name.size(); ++index) {
      if (message->name[index] == kPiperJointNames[expected]) {
        if (found != message->name.size()) {
          RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                                "Rejecting PiPER JointState: duplicate joint %s",
                                kPiperJointNames[expected]);
          return;
        }
        found = index;
      }
    }
    if (found == message->name.size() ||
        !std::isfinite(message->position[found])) {
      RCLCPP_ERROR_THROTTLE(
          this->get_logger(), *this->get_clock(), 2000,
          "Rejecting PiPER JointState: missing/non-finite joint %s",
          kPiperJointNames[expected]);
      return;
    }
    snapshot.joint_position_rad[expected] =
        static_cast<float>(message->position[found]);
  }

  std::lock_guard<std::mutex> lock(piper_state_mutex_);
  latest_piper_state_ = snapshot;
}

bool Stage2DirectNode::refresh_parameters() {
  this->get_parameter("enable_motion", enable_motion_);
  this->get_parameter("live_acknowledged", live_acknowledged_);
  this->get_parameter("component_mode", component_mode_param_);
  this->get_parameter("a2_state_max_age_ms", a2_state_max_age_ms_);
  this->get_parameter("piper_state_max_age_ms", piper_state_max_age_ms_);
  this->get_parameter("maximum_state_skew_ms", maximum_state_skew_ms_);
  this->get_parameter("max_remote_vx", max_remote_vx_);
  this->get_parameter("max_remote_vy", max_remote_vy_);
  this->get_parameter("max_remote_yaw", max_remote_yaw_);
  this->get_parameter("remote_deadzone", remote_deadzone_);
  this->get_parameter("arm_goal_radius_m", arm_goal_radius_m_);
  this->get_parameter("arm_goal_pitch_rad", arm_goal_pitch_rad_);
  this->get_parameter("arm_goal_yaw_rad", arm_goal_yaw_rad_);
  this->get_parameter("standup_stage1_steps", standup_stage1_steps_);
  this->get_parameter("standup_stage2_steps", standup_stage2_steps_);
  this->get_parameter("standup_rear_alpha_lead", standup_rear_alpha_lead_);
  this->get_parameter("standup_front_alpha_lag", standup_front_alpha_lag_);
  this->get_parameter("standup_kp_start", standup_kp_start_);
  this->get_parameter("standup_kd_start", standup_kd_start_);
  this->get_parameter("controlled_down_steps", controlled_down_steps_);
  this->get_parameter("controlled_down_hip_q", controlled_down_hip_q_);
  this->get_parameter("controlled_down_thigh_q", controlled_down_thigh_q_);
  this->get_parameter("controlled_down_calf_q", controlled_down_calf_q_);
  this->get_parameter("controlled_down_gain_scale",
                      controlled_down_gain_scale_);
  this->get_parameter("status_period_ms", status_period_ms_);

  ComponentMode parsed_mode;
  if (component_mode_param_ == "dog_only") {
    parsed_mode = ComponentMode::kDogOnly;
  } else if (component_mode_param_ == "arm_only") {
    parsed_mode = ComponentMode::kArmOnly;
  } else if (component_mode_param_ == "both") {
    parsed_mode = ComponentMode::kBoth;
  } else {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "component_mode must be dog_only|arm_only|both, got '%s'",
        component_mode_param_.c_str());
    return false;
  }

  const bool numeric_valid =
      a2_state_max_age_ms_ > 0 && piper_state_max_age_ms_ > 0 &&
      maximum_state_skew_ms_ >= 0 && status_period_ms_ > 0 &&
      standup_stage1_steps_ > 0 && standup_stage2_steps_ > 0 &&
      controlled_down_steps_ > 0 && std::isfinite(max_remote_vx_) &&
      max_remote_vx_ >= 0.0 && std::isfinite(max_remote_vy_) &&
      max_remote_vy_ >= 0.0 && std::isfinite(max_remote_yaw_) &&
      max_remote_yaw_ >= 0.0 && std::isfinite(remote_deadzone_) &&
      remote_deadzone_ >= 0.0 && remote_deadzone_ <= 1.0 &&
      std::isfinite(arm_goal_radius_m_) &&
      std::isfinite(arm_goal_pitch_rad_) && std::isfinite(arm_goal_yaw_rad_) &&
      std::isfinite(standup_rear_alpha_lead_) &&
      std::isfinite(standup_front_alpha_lag_) &&
      std::isfinite(standup_kp_start_) && standup_kp_start_ >= 0.0 &&
      std::isfinite(standup_kd_start_) && standup_kd_start_ >= 0.0 &&
      std::isfinite(controlled_down_hip_q_) &&
      std::isfinite(controlled_down_thigh_q_) &&
      std::isfinite(controlled_down_calf_q_) &&
      std::isfinite(controlled_down_gain_scale_) &&
      controlled_down_gain_scale_ > 0.0;
  if (!numeric_valid) {
    RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                          "Stage2 direct numeric parameters are invalid");
    return false;
  }

  const bool live_flags_requested =
      enable_motion_ && live_acknowledged_ && !validate_live_site_only_;
  if (live_flags_requested && !live_site_contract_loaded_) {
    parameter_error_ =
        "live transition refused: no startup-verified site contract; restart "
        "the process with enable_motion=true and live_acknowledged=true";
    RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 3000, "%s",
                          parameter_error_.c_str());
    return false;
  }
  parameter_error_.clear();
  const bool requested_now = live_flags_requested;
  if (parsed_mode != previous_component_mode_ ||
      requested_now != previous_live_requested_) {
    component_mode_ = parsed_mode;
    reset_policy_runtime();
    reset_live_lifecycle();
    reset_remote_edges();
    RCLCPP_INFO(this->get_logger(),
                "Stage2 output gate changed: enable_motion=%s "
                "live_acknowledged=%s component_mode=%s; two-A handover reset",
                enable_motion_ ? "true" : "false",
                live_acknowledged_ ? "true" : "false", component_name());
  } else {
    component_mode_ = parsed_mode;
  }
  previous_component_mode_ = parsed_mode;
  previous_live_requested_ = requested_now;
  return true;
}

bool Stage2DirectNode::get_a2_state(
    a2_lowlevel::A2LowStateSnapshot &state, std::string &reason) {
  state = latest_state();
  if (!state.has_state) {
    reason = "A2 LowState missing";
    last_a2_age_ms_ = -1.0;
    return false;
  }
  last_a2_age_ms_ =
      std::chrono::duration<double, std::milli>(state.receive_age()).count();
  if (last_a2_age_ms_ > a2_state_max_age_ms_) {
    reason = "A2 LowState stale";
    return false;
  }
  if (!all_finite(state.quaternion) || !all_finite(state.joint_q) ||
      !all_finite(state.joint_dq)) {
    reason = "A2 LowState contains NaN/Inf";
    return false;
  }
  return true;
}

bool Stage2DirectNode::get_synchronized_snapshot(
    const a2_lowlevel::A2LowStateSnapshot &a2, RobotSnapshot &snapshot,
    PiperSnapshot &piper, std::string &reason) {
  {
    std::lock_guard<std::mutex> lock(piper_state_mutex_);
    piper = latest_piper_state_;
  }
  if (!piper.has_state) {
    reason = "PiPER JointState missing";
    last_piper_age_ms_ = -1.0;
    last_skew_ms_ = -1.0;
    return false;
  }
  const auto now = SteadyClock::now();
  last_piper_age_ms_ = std::chrono::duration<double, std::milli>(
                            now - piper.received_steady_time)
                            .count();
  if (last_piper_age_ms_ > piper_state_max_age_ms_) {
    reason = "PiPER JointState stale";
    return false;
  }
  last_skew_ms_ = std::abs(std::chrono::duration<double, std::milli>(
                               a2.received_steady_time -
                               piper.received_steady_time)
                               .count());
  if (last_skew_ms_ > maximum_state_skew_ms_) {
    reason = "A2/PiPER receive-time skew exceeds limit";
    return false;
  }

  snapshot.root_quaternion_wxyz = a2.quaternion;
  snapshot.dog_joint_position_rad = map_a2_to_training(a2.joint_q);
  snapshot.dog_joint_velocity_rad_s = map_a2_to_training(a2.joint_dq);
  snapshot.arm_joint_position_rad = piper.joint_position_rad;
  return true;
}

PolicyCommand Stage2DirectNode::make_policy_command(
    const a2_lowlevel::A2RemoteState &remote) const {
  PolicyCommand command;
  command.locomotion_vx_vy_yaw = {
      static_cast<float>(remote.ly * max_remote_vx_),
      static_cast<float>(-remote.lx * max_remote_vy_),
      static_cast<float>(-remote.rx * max_remote_yaw_)};
  command.arm_goal_radius_pitch_yaw = {
      static_cast<float>(arm_goal_radius_m_),
      static_cast<float>(arm_goal_pitch_rad_),
      static_cast<float>(arm_goal_yaw_rad_)};
  return command;
}

RuntimeOutput Stage2DirectNode::run_policy_tick(
    const RobotSnapshot &snapshot, const PolicyCommand &command) {
  RuntimeOutput output = runtime_->tick(snapshot, command);
  last_inference_ms_ = output.inference_latency_ms;
  if (live_requested()) {
    if (output.inference_latency_ms > inference_deadline_s_ * 1000.0) {
      ++consecutive_deadline_misses_;
    } else {
      consecutive_deadline_misses_ = 0;
    }
    if (consecutive_deadline_misses_ >=
        consecutive_deadline_miss_limit_) {
      std::ostringstream message;
      message << "inference deadline exceeded "
              << consecutive_deadline_misses_ << " consecutive ticks ("
              << output.inference_latency_ms << " ms > "
              << inference_deadline_s_ * 1000.0 << " ms)";
      throw std::runtime_error(message.str());
    }
  }
  return output;
}

Stage2DirectNode::RemoteEdges Stage2DirectNode::update_remote_edges(
    const a2_lowlevel::A2RemoteState &remote) {
  RemoteEdges edges;
  if (!remote.valid) {
    return edges;
  }
  if (have_previous_remote_buttons_) {
    edges.a_rising = remote.buttons.a && !previous_remote_a_;
    edges.b_rising = remote.buttons.b && !previous_remote_b_;
  }
  previous_remote_a_ = remote.buttons.a;
  previous_remote_b_ = remote.buttons.b;
  have_previous_remote_buttons_ = true;
  return edges;
}

void Stage2DirectNode::reset_remote_edges() {
  have_previous_remote_buttons_ = false;
  previous_remote_a_ = false;
  previous_remote_b_ = false;
}

void Stage2DirectNode::reset_policy_runtime() {
  if (runtime_) {
    runtime_->reset();
  }
  warm_frames_ = 0;
  consecutive_deadline_misses_ = 0;
  last_inference_ms_ = -1.0;
}

void Stage2DirectNode::reset_live_lifecycle() {
  phase_ = Phase::kIdleBlocked;
  standup_step_ = 0;
  controlled_down_step_ = 0;
  standup_start_training_.fill(0.0f);
  controlled_down_start_training_.fill(0.0f);
  piper_hold_position_.fill(0.0f);
  have_piper_hold_ = false;
  warm_frames_ = 0;
}

void Stage2DirectNode::control_tick() {
  if (!refresh_parameters()) {
    publish_status("blocked", parameter_error_.empty()
                                  ? "invalid runtime parameter"
                                  : parameter_error_,
                   false);
    return;
  }
  if (validate_live_site_only_) {
    publish_status("ready", "live site contract validation passed", false);
    return;
  }

  a2_lowlevel::A2LowStateSnapshot a2;
  std::string reason;
  if (!get_a2_state(a2, reason)) {
    reset_policy_runtime();
    if (live_requested()) {
      reset_live_lifecycle();
    }
    publish_status(a2.has_state ? "blocked" : "waiting", reason, false);
    return;
  }

  const a2_lowlevel::A2RemoteState remote = a2_lowlevel::decode_a2_remote(
      a2.wireless_remote, static_cast<float>(remote_deadzone_));
  const RemoteEdges edges = update_remote_edges(remote);
  if (remote.buttons.select) {
    handle_select_stop();
    publish_status("ready", "Select stop active", false);
    return;
  }
  if (live_requested() &&
      (phase_ == Phase::kControlledDown || phase_ == Phase::kHoldProne)) {
    publish_controlled_down();
    publish_status("ready", "controlled down path active", false);
    return;
  }
  if (live_requested() && remote.valid && remote.buttons.l2 &&
      remote.buttons.b) {
    start_controlled_down(a2);
    publish_status("ready", "L2+B controlled down started", true);
    return;
  }
  if (!remote.valid) {
    reset_policy_runtime();
    if (live_requested()) {
      reset_live_lifecycle();
    }
    reset_remote_edges();
    publish_status("blocked", "remote packet contains NaN/Inf", false);
    return;
  }

  PiperSnapshot piper;
  RobotSnapshot snapshot;
  if (!get_synchronized_snapshot(a2, snapshot, piper, reason)) {
    reset_policy_runtime();
    if (live_requested()) {
      reset_live_lifecycle();
    }
    publish_status(piper.has_state ? "blocked" : "waiting", reason, false);
    return;
  }
  const PolicyCommand command = make_policy_command(remote);

  try {
    if (!live_requested()) {
      handle_shadow(snapshot, command);
    } else {
      handle_live(a2, piper, snapshot, remote, edges, command);
    }
  } catch (const std::exception &error) {
    RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                          "Stage2 direct tick failed: %s", error.what());
    reset_policy_runtime();
    if (live_requested()) {
      reset_live_lifecycle();
    }
    publish_status("blocked", error.what(), true);
  }
}

void Stage2DirectNode::handle_shadow(const RobotSnapshot &snapshot,
                                     const PolicyCommand &command) {
  static_cast<void>(run_policy_tick(snapshot, command));
  publish_status("ready", "dual actor shadow tick passed", false);
}

void Stage2DirectNode::handle_live(
    const a2_lowlevel::A2LowStateSnapshot &a2, const PiperSnapshot &piper,
    const RobotSnapshot &snapshot, const a2_lowlevel::A2RemoteState &remote,
    const RemoteEdges &edges, const PolicyCommand &command) {
  switch (phase_) {
    case Phase::kIdleBlocked:
      if (edges.a_rising) {
        start_standup(a2, piper);
        publish_standup_step();
        publish_piper_hold();
      }
      publish_status("ready", "waiting for first A", false);
      return;

    case Phase::kStandUpInterpolating:
      if (edges.b_rising) {
        cancel_handover();
        return;
      }
      publish_standup_step();
      publish_piper_hold();
      publish_status("ready", "stand-up interpolation", false);
      return;

    case Phase::kStandHoldWaitingForA:
      if (edges.b_rising) {
        cancel_handover();
        return;
      }
      publish_default_a2_hold();
      publish_piper_hold();
      if (edges.a_rising) {
        if (!sticks_centered(remote)) {
          RCLCPP_WARN(this->get_logger(),
                      "Second A refused: lx/rx/ly must be centered");
        } else {
          reset_policy_runtime();
          phase_ = Phase::kPolicyWarmupHold;
          RCLCPP_INFO(this->get_logger(),
                      "Second A accepted: entering 30-frame Stage2 warmup hold");
        }
      }
      publish_status("ready", "holding default pose; waiting for second A",
                     false);
      return;

    case Phase::kPolicyWarmupHold: {
      if (edges.b_rising) {
        cancel_handover();
        return;
      }
      publish_default_a2_hold();
      publish_piper_hold();
      static_cast<void>(run_policy_tick(snapshot, command));
      warm_frames_ = std::min<std::size_t>(
          warm_frames_ + 1, runtime_->contract().dog.history_frames);
      if (warm_frames_ >= runtime_->contract().dog.history_frames) {
        std::array<float, 12> dog_limiter_seed{};
        std::copy(runtime_->contract().dog.default_position_rad.begin(),
                  runtime_->contract().dog.default_position_rad.end(),
                  dog_limiter_seed.begin());
        std::array<float, 6> arm_limiter_seed{};
        if (component_has_arm()) {
          if (!have_piper_hold_) {
            throw std::runtime_error(
                "PiPER hold position missing at limiter handover");
          }
          arm_limiter_seed = piper_hold_position_;
        } else {
          std::copy(runtime_->contract().arm.default_position_rad.begin(),
                    runtime_->contract().arm.default_position_rad.end(),
                    arm_limiter_seed.begin());
        }
        runtime_->seed_output_targets(dog_limiter_seed, arm_limiter_seed);
        phase_ = Phase::kPolicyActive;
        RCLCPP_INFO(this->get_logger(),
                    "Stage2 warmup complete: PolicyActive starts next tick");
      }
      publish_status("ready", "policy warmup hold", false);
      return;
    }

    case Phase::kPolicyActive: {
      const RuntimeOutput output = run_policy_tick(snapshot, command);
      publish_active_output(output);
      publish_status("ready", "policy output active", false);
      return;
    }

    case Phase::kControlledDown:
    case Phase::kHoldProne:
      publish_controlled_down();
      return;
  }
}

void Stage2DirectNode::handle_select_stop() {
  reset_policy_runtime();
  reset_live_lifecycle();
  if (live_requested()) {
    publish_zero();
    request_piper_stop("Select");
  }
}

void Stage2DirectNode::start_controlled_down(
    const a2_lowlevel::A2LowStateSnapshot &state) {
  reset_policy_runtime();
  controlled_down_start_training_ = map_a2_to_training(state.joint_q);
  controlled_down_step_ = 0;
  phase_ = Phase::kControlledDown;
  request_piper_stop("L2+B controlled down");
  publish_controlled_down();
}

void Stage2DirectNode::publish_controlled_down() {
  if (!live_requested()) {
    reset_live_lifecycle();
    return;
  }
  if (phase_ == Phase::kControlledDown) {
    const int current_step =
        std::min(controlled_down_step_ + 1, controlled_down_steps_);
    const double alpha = smoothstep01(
        static_cast<double>(current_step) / controlled_down_steps_);
    if (!publish_joint_commands(build_controlled_down_commands(alpha))) {
      reset_live_lifecycle();
      return;
    }
    controlled_down_step_ = current_step;
    if (controlled_down_step_ >= controlled_down_steps_) {
      phase_ = Phase::kHoldProne;
      RCLCPP_WARN(this->get_logger(),
                  "Controlled down complete: holding prone; stop node, verify "
                  "no-lowcmd, then restore motion service");
    }
    return;
  }
  if (phase_ == Phase::kHoldProne &&
      !publish_joint_commands(build_controlled_down_commands(1.0))) {
    reset_live_lifecycle();
  }
}

void Stage2DirectNode::cancel_handover() {
  reset_policy_runtime();
  reset_live_lifecycle();
  if (live_requested()) {
    publish_zero();
    request_piper_stop("B handover cancel");
  }
}

void Stage2DirectNode::start_standup(
    const a2_lowlevel::A2LowStateSnapshot &state,
    const PiperSnapshot &piper) {
  reset_policy_runtime();
  standup_start_training_ = map_a2_to_training(state.joint_q);
  piper_hold_position_ = piper.joint_position_rad;
  have_piper_hold_ = true;
  standup_step_ = 0;
  phase_ = Phase::kStandUpInterpolating;
  RCLCPP_INFO(this->get_logger(),
              "First A accepted: starting A2 stand-up interpolation");
}

void Stage2DirectNode::publish_standup_step() {
  const int total_steps = standup_stage1_steps_ + standup_stage2_steps_;
  const int current_step = std::min(standup_step_ + 1, total_steps);
  const double alpha =
      std::clamp(static_cast<double>(current_step) / total_steps, 0.0, 1.0);
  const double front_alpha =
      smoothstep01(alpha - standup_front_alpha_lag_);
  const double rear_alpha =
      smoothstep01(alpha + standup_rear_alpha_lead_);
  if (!publish_joint_commands(
          build_standup_commands(front_alpha, rear_alpha,
                                 smoothstep01(alpha)))) {
    reset_live_lifecycle();
    return;
  }
  standup_step_ = current_step;
  if (standup_step_ >= total_steps) {
    phase_ = Phase::kStandHoldWaitingForA;
    RCLCPP_INFO(this->get_logger(),
                "A2 stand-up complete: holding default pose, waiting second A");
  }
}

void Stage2DirectNode::publish_default_a2_hold() {
  std::array<float, 12> target{};
  std::copy(runtime_->contract().dog.default_position_rad.begin(),
            runtime_->contract().dog.default_position_rad.end(), target.begin());
  if (!publish_joint_commands(build_a2_commands(target))) {
    reset_live_lifecycle();
  }
}

void Stage2DirectNode::publish_piper_hold() {
  if (component_has_arm() && have_piper_hold_) {
    publish_piper_target(piper_hold_position_);
  }
}

void Stage2DirectNode::publish_active_output(const RuntimeOutput &output) {
  if (component_has_dog()) {
    if (!publish_joint_commands(build_a2_commands(output.dog_target_rad))) {
      reset_policy_runtime();
      reset_live_lifecycle();
      return;
    }
  } else {
    publish_default_a2_hold();
  }
  if (component_has_arm()) {
    publish_piper_target(output.arm_target_rad);
  }
}

void Stage2DirectNode::publish_piper_target(
    const std::array<float, 6> &positions_rad) {
  trajectory_msgs::msg::JointTrajectory message;
  message.header.stamp = this->get_clock()->now().to_msg();
  message.joint_names.assign(kPiperJointNames.begin(), kPiperJointNames.end());
  trajectory_msgs::msg::JointTrajectoryPoint point;
  point.positions.assign(positions_rad.begin(), positions_rad.end());
  point.time_from_start.nanosec = 20'000'000;
  message.points.push_back(std::move(point));
  piper_command_pub_->publish(message);
}

void Stage2DirectNode::request_piper_stop(const std::string &reason) {
  if (piper_stop_request_in_flight_) {
    return;
  }
  if (!piper_stop_client_->service_is_ready()) {
    RCLCPP_ERROR(this->get_logger(),
                 "PiPER stop service unavailable for %s: %s", reason.c_str(),
                 piper_stop_service_.c_str());
    return;
  }
  piper_stop_request_in_flight_ = true;
  auto request = std::make_shared<std_srvs::srv::Trigger::Request>();
  piper_stop_client_->async_send_request(
      request,
      [this, reason](rclcpp::Client<std_srvs::srv::Trigger>::SharedFuture future) {
        piper_stop_request_in_flight_ = false;
        const auto response = future.get();
        if (response->success) {
          RCLCPP_WARN(this->get_logger(), "PiPER stop accepted for %s: %s",
                      reason.c_str(), response->message.c_str());
        } else {
          RCLCPP_ERROR(this->get_logger(), "PiPER stop rejected for %s: %s",
                       reason.c_str(), response->message.c_str());
        }
      });
}

std::array<float, 12> Stage2DirectNode::map_a2_to_training(
    const std::array<float, a2_lowlevel::kA2JointCount> &values) const {
  std::array<float, 12> training{};
  for (std::size_t index = 0; index < training.size(); ++index) {
    training[index] = values[kTrainingToA2Index[index]];
  }
  return training;
}

std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
Stage2DirectNode::build_a2_commands(
    const std::array<float, 12> &training_targets, double gain_scale) const {
  std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount> commands{};
  for (std::size_t training_index = 0; training_index < training_targets.size();
       ++training_index) {
    auto &command = commands[kTrainingToA2Index[training_index]];
    command.q = training_targets[training_index];
    command.dq = 0.0f;
    command.tau = 0.0f;
    command.kp = static_cast<float>((training_index < 8 ? 140.0 : 220.0) *
                                    gain_scale);
    command.kd = static_cast<float>((training_index < 8 ? 5.0 : 9.0) *
                                    gain_scale);
  }
  return commands;
}

std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
Stage2DirectNode::build_standup_commands(double front_alpha,
                                         double rear_alpha,
                                         double gain_factor) const {
  const double safe_front = std::clamp(front_alpha, 0.0, 1.0);
  const double safe_rear = std::clamp(rear_alpha, 0.0, 1.0);
  const double safe_gain = std::clamp(gain_factor, 0.0, 1.0);
  std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount> commands{};
  for (std::size_t index = 0; index < standup_start_training_.size(); ++index) {
    const double alpha = is_rear_training_joint(index) ? safe_rear : safe_front;
    const double target =
        standup_start_training_[index] +
        (runtime_->contract().dog.default_position_rad[index] -
         standup_start_training_[index]) *
            alpha;
    const double final_kp = index < 8 ? 140.0 : 220.0;
    const double final_kd = index < 8 ? 5.0 : 9.0;
    auto &command = commands[kTrainingToA2Index[index]];
    command.q = static_cast<float>(target);
    command.kp = static_cast<float>(
        standup_kp_start_ + (final_kp - standup_kp_start_) * safe_gain);
    command.kd = static_cast<float>(
        standup_kd_start_ + (final_kd - standup_kd_start_) * safe_gain);
  }
  return commands;
}

std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
Stage2DirectNode::build_controlled_down_commands(double alpha) const {
  std::array<float, 12> target{};
  std::fill_n(target.begin(), 4, static_cast<float>(controlled_down_hip_q_));
  std::fill_n(target.begin() + 4, 4,
              static_cast<float>(controlled_down_thigh_q_));
  std::fill_n(target.begin() + 8, 4,
              static_cast<float>(controlled_down_calf_q_));
  const double safe_alpha = std::clamp(alpha, 0.0, 1.0);
  for (std::size_t index = 0; index < target.size(); ++index) {
    target[index] = static_cast<float>(
        controlled_down_start_training_[index] +
        (target[index] - controlled_down_start_training_[index]) * safe_alpha);
  }
  return build_a2_commands(target, controlled_down_gain_scale_);
}

bool Stage2DirectNode::component_has_dog() const {
  return component_mode_ == ComponentMode::kDogOnly ||
         component_mode_ == ComponentMode::kBoth;
}

bool Stage2DirectNode::component_has_arm() const {
  return component_mode_ == ComponentMode::kArmOnly ||
         component_mode_ == ComponentMode::kBoth;
}

bool Stage2DirectNode::live_requested() const {
  return enable_motion_ && live_acknowledged_ &&
         !validate_live_site_only_ && live_site_contract_loaded_;
}

bool Stage2DirectNode::sticks_centered(
    const a2_lowlevel::A2RemoteState &remote) const {
  return std::abs(remote.lx) <= kRemoteZeroEpsilon &&
         std::abs(remote.rx) <= kRemoteZeroEpsilon &&
         std::abs(remote.ly) <= kRemoteZeroEpsilon;
}

const char *Stage2DirectNode::component_name() const {
  switch (component_mode_) {
    case ComponentMode::kDogOnly:
      return "dog_only";
    case ComponentMode::kArmOnly:
      return "arm_only";
    case ComponentMode::kBoth:
      return "both";
  }
  return "invalid";
}

const char *Stage2DirectNode::phase_name() const {
  switch (phase_) {
    case Phase::kIdleBlocked:
      return "IdleBlocked";
    case Phase::kStandUpInterpolating:
      return "StandUpInterpolating";
    case Phase::kStandHoldWaitingForA:
      return "StandHoldWaitingForA";
    case Phase::kPolicyWarmupHold:
      return "PolicyWarmupHold";
    case Phase::kPolicyActive:
      return "PolicyActive";
    case Phase::kControlledDown:
      return "ControlledDown";
    case Phase::kHoldProne:
      return "HoldProne";
  }
  return "Unknown";
}

void Stage2DirectNode::publish_status(const char *state,
                                      const std::string &reason, bool force) {
  const auto now = SteadyClock::now();
  if (!force && last_status_time_ != SteadyClock::time_point{} &&
      now - last_status_time_ <
          std::chrono::milliseconds(status_period_ms_)) {
    return;
  }
  last_status_time_ = now;
  const char *a2_output = "not_published";
  const char *piper_output = "not_published";
  if (live_requested()) {
    if (phase_ == Phase::kStandUpInterpolating) {
      a2_output = "standup_interpolation";
      piper_output = component_has_arm() ? "measured_position_hold"
                                         : "not_published";
    } else if (phase_ == Phase::kStandHoldWaitingForA ||
               phase_ == Phase::kPolicyWarmupHold) {
      a2_output = "default_position_hold";
      piper_output = component_has_arm() ? "measured_position_hold"
                                         : "not_published";
    } else if (phase_ == Phase::kPolicyActive) {
      a2_output = component_has_dog() ? "dog_actor_target"
                                      : "default_position_hold";
      piper_output = component_has_arm() ? "arm_actor_target"
                                         : "not_published";
    } else if (phase_ == Phase::kControlledDown ||
               phase_ == Phase::kHoldProne) {
      a2_output = phase_ == Phase::kControlledDown ? "controlled_down"
                                                   : "prone_hold";
      piper_output = "stop_requested_not_published";
    }
  }
  std::ostringstream payload;
  payload << std::fixed << std::setprecision(3)
          << "contract=verified;site="
          << (live_site_contract_loaded_ ? "verified" : "not_loaded")
          << ";mode="
          << (validate_live_site_only_
                  ? "site_validation"
                  : (live_requested() ? "live" : "shadow"))
          << ";state=" << state
          << ";phase=" << (live_requested() ? phase_name() : "Shadow")
          << ";component=" << component_name() << ";warm_frames="
          << warm_frames_ << ";a2_output=" << a2_output
          << ";piper_output=" << piper_output
          << ";a2_age_ms=" << last_a2_age_ms_
          << ";piper_age_ms=" << last_piper_age_ms_ << ";skew_ms="
          << last_skew_ms_ << ";inference_ms=" << last_inference_ms_
          << ";deadline_misses=" << consecutive_deadline_misses_
          << ";reason=" << clean_status_reason(reason);
  std_msgs::msg::String message;
  message.data = payload.str();
  status_pub_->publish(message);
}

}  // namespace a2_piper_stage2_direct

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  try {
    auto node =
        std::make_shared<a2_piper_stage2_direct::Stage2DirectNode>();
    rclcpp::spin(node);
  } catch (const std::exception &error) {
    RCLCPP_FATAL(rclcpp::get_logger("a2_piper_stage2_direct"), "%s",
                 error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}

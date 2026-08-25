#ifndef A2_PIPER_STAGE2_DIRECT_STAGE2_DIRECT_NODE_HPP_
#define A2_PIPER_STAGE2_DIRECT_STAGE2_DIRECT_NODE_HPP_

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

#include "a2_lowlevel/a2_lowlevel_interface.h"
#include "a2_lowlevel/a2_remote.h"
#include "a2_piper_stage2_direct/stage2_runtime.hpp"
#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/float64_multi_array.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "trajectory_msgs/msg/joint_trajectory.hpp"

namespace a2_piper_stage2_direct {

class Stage2DirectNode : public a2_lowlevel::A2LowLevelInterface {
 public:
  explicit Stage2DirectNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

 private:
  using SteadyClock = std::chrono::steady_clock;

  enum class ComponentMode { kDogOnly, kArmOnly, kBoth };
  enum class Phase {
    kIdleBlocked,
    kInitHoldWaitingForPiperGate,
    kStandUpInterpolating,
    kStandHoldWaitingForA,
    kPolicyWarmupHold,
    kPolicyActive,
    kResetInterpolating,
    kResetHoldWaitingForAOrStop,
    kProneInterpolating,
    kHoldProne,
  };

  struct PiperSnapshot {
    bool has_state = false;
    std::array<float, 6> joint_position_rad{};
    SteadyClock::time_point received_steady_time{};
  };

  struct RemoteEdges {
    bool a_rising = false;
    bool b_rising = false;
  };

  void piper_state_callback(const sensor_msgs::msg::JointState::SharedPtr msg);
  void piper_diagnostics_callback(
      const diagnostic_msgs::msg::DiagnosticArray::SharedPtr msg);
  void arm_goal_callback(
      const std_msgs::msg::Float64MultiArray::SharedPtr msg);
  void control_tick();
  bool refresh_parameters();
  bool get_a2_state(a2_lowlevel::A2LowStateSnapshot &state,
                    std::string &reason);
  bool get_synchronized_snapshot(const a2_lowlevel::A2LowStateSnapshot &a2,
                                 RobotSnapshot &snapshot,
                                 PiperSnapshot &piper,
                                 std::string &reason);
  PolicyCommand make_policy_command(const a2_lowlevel::A2RemoteState &remote);
  RuntimeOutput run_policy_tick(const RobotSnapshot &snapshot,
                                const PolicyCommand &command);

  RemoteEdges update_remote_edges(const a2_lowlevel::A2RemoteState &remote);
  void reset_remote_edges();
  void reset_policy_runtime();
  void reset_live_lifecycle();
  void handle_shadow(const RobotSnapshot &snapshot,
                     const PolicyCommand &command);
  void handle_live(const a2_lowlevel::A2LowStateSnapshot &a2,
                   const PiperSnapshot &piper, const RobotSnapshot &snapshot,
                   const a2_lowlevel::A2RemoteState &remote,
                   const RemoteEdges &edges, const PolicyCommand &command);
  void handle_select_stop();
  void start_reset_transition(const a2_lowlevel::A2LowStateSnapshot &a2,
                              const PiperSnapshot &piper);
  void publish_reset_transition();
  void start_prone_transition(const a2_lowlevel::A2LowStateSnapshot &a2,
                              const PiperSnapshot &piper);
  void publish_prone_transition();
  void cancel_handover();

  void start_standup(const a2_lowlevel::A2LowStateSnapshot &state,
                     const PiperSnapshot &piper);
  void publish_initial_position_hold();
  void publish_standup_step();
  void publish_default_a2_hold();
  void publish_piper_hold();
  void publish_active_output(const RuntimeOutput &output);
  void publish_piper_target(const std::array<float, 6> &positions_rad);
  void request_piper_handover_ready();
  void request_piper_stop(const std::string &reason);
  void start_arm_goal_trajectory_callback(
      const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
      std::shared_ptr<std_srvs::srv::Trigger::Response> response);
  bool piper_command_gate_open() const;

  std::array<float, 12> map_a2_to_training(
      const std::array<float, a2_lowlevel::kA2JointCount> &values) const;
  std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
  build_a2_commands(const std::array<float, 12> &training_targets,
                    double gain_scale = 1.0) const;
  std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
  build_standup_commands(double front_alpha, double rear_alpha,
                         double gain_factor) const;
  std::array<a2_lowlevel::A2JointCommand, a2_lowlevel::kA2JointCount>
  build_stop_transition_commands(
      const std::array<float, 12> &start,
      const std::array<float, 12> &target, double alpha) const;

  bool component_has_dog() const;
  bool component_has_arm() const;
  bool live_requested() const;
  bool sticks_centered(const a2_lowlevel::A2RemoteState &remote) const;
  const char *component_name() const;
  const char *phase_name() const;
  void publish_status(const char *state, const std::string &reason,
                      bool force = false);

  std::unique_ptr<DualPolicyRuntime> runtime_;
  std::string bundle_dir_;
  std::string site_config_;
  std::string piper_state_topic_;
  std::string piper_diagnostics_topic_;
  std::string piper_command_topic_;
  std::string piper_resume_service_;
  std::string piper_enable_service_;
  std::string piper_stop_service_;
  std::string arm_goal_topic_;
  std::string trajectory_start_service_;
  std::string status_topic_;
  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr piper_state_sub_;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
      piper_diagnostics_sub_;
  rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr
      arm_goal_sub_;
  rclcpp::Publisher<trajectory_msgs::msg::JointTrajectory>::SharedPtr
      piper_command_pub_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr piper_resume_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr piper_enable_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr piper_stop_client_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr trajectory_start_server_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_pub_;
  rclcpp::CallbackGroup::SharedPtr control_callback_group_;
  rclcpp::TimerBase::SharedPtr control_timer_;

  mutable std::mutex piper_state_mutex_;
  PiperSnapshot latest_piper_state_;
  bool have_piper_diagnostics_ = false;
  bool piper_command_gate_open_ = false;
  SteadyClock::time_point piper_diagnostics_received_steady_time_{};

  bool enable_motion_ = false;
  bool live_acknowledged_ = false;
  bool validate_live_site_only_ = false;
  bool live_site_contract_loaded_ = false;
  bool previous_live_requested_ = false;
  std::string parameter_error_;
  std::string component_mode_param_ = "both";
  ComponentMode component_mode_ = ComponentMode::kBoth;
  ComponentMode previous_component_mode_ = ComponentMode::kBoth;
  int a2_state_max_age_ms_ = 200;
  int piper_state_max_age_ms_ = 200;
  int maximum_state_skew_ms_ = 50;
  double max_remote_vx_ = 0.8;
  double max_remote_vy_ = 0.5;
  double max_remote_yaw_ = 0.6;
  double remote_deadzone_ = 0.08;
  double arm_goal_radius_m_ = 0.6;
  double arm_goal_pitch_rad_ = 0.0;
  double arm_goal_yaw_rad_ = 0.0;
  bool arm_goal_trajectory_enabled_ = false;
  std::array<double, 3> arm_goal_trajectory_start_ = {0.4, 1.0472, 0.0};
  std::array<double, 3> arm_goal_trajectory_end_ = {0.4, -1.2566, 0.0};
  double arm_goal_trajectory_approach_duration_s_ = 4.0;
  double arm_goal_trajectory_duration_s_ = 6.0;
  double arm_goal_trajectory_return_duration_s_ = 4.0;
  int policy_handover_steps_ = 50;
  int standup_stage1_steps_ = 150;
  int standup_stage2_steps_ = 150;
  double standup_rear_alpha_lead_ = 0.10;
  double standup_front_alpha_lag_ = 0.04;
  double standup_kp_start_ = 3.0;
  double standup_kd_start_ = 0.5;
  int stop_reset_steps_ = 250;
  int prone_interpolation_steps_ = 250;
  std::array<float, 12> prone_target_training_rad_ = {
      0.3602f, -0.3789f, 0.3382f, -0.3506f,
      1.1862f, 1.1942f, 1.2177f, 1.1831f,
      -2.7570f, -2.7380f, -2.7485f, -2.7468f};
  double stop_gain_scale_ = 1.0;
  int status_period_ms_ = 200;
  double inference_deadline_s_ = 0.0;
  int consecutive_deadline_miss_limit_ = 0;
  int consecutive_deadline_misses_ = 0;
  bool arm_goal_trajectory_started_ = false;
  bool arm_goal_position_tracking_active_ = false;
  SteadyClock::time_point arm_goal_trajectory_started_at_{};
  double arm_goal_trajectory_progress_ = 0.0;
  std::array<double, 3> position_arm_goal_ = {0.0, 0.0, 0.0};
  std::array<double, 3> current_arm_goal_ = {0.0, 0.0, 0.0};
  std::array<double, 2> last_body_pitch_roll_plan_ = {0.0, 0.0};
  std::array<double, 2> last_base_roll_pitch_ = {0.0, 0.0};

  Phase phase_ = Phase::kIdleBlocked;
  int standup_step_ = 0;
  int stop_transition_step_ = 0;
  int policy_handover_step_ = 0;
  std::size_t warm_frames_ = 0;
  std::array<float, 12> standup_start_training_{};
  std::array<float, 12> stop_start_training_{};
  std::array<float, 6> piper_start_position_{};
  std::array<float, 6> stop_piper_start_position_{};
  std::array<float, 6> piper_hold_position_{};
  bool have_piper_hold_ = false;
  bool piper_prone_stop_requested_ = false;
  bool have_previous_remote_buttons_ = false;
  bool previous_remote_a_ = false;
  bool previous_remote_b_ = false;
  bool piper_ready_request_attempted_ = false;
  std::atomic_bool piper_ready_request_in_flight_{false};
  std::atomic_bool piper_stop_request_in_flight_{false};

  double last_a2_age_ms_ = -1.0;
  double last_piper_age_ms_ = -1.0;
  double last_skew_ms_ = -1.0;
  double last_inference_ms_ = -1.0;
  SteadyClock::time_point last_status_time_{};
};

}  // namespace a2_piper_stage2_direct

#endif  // A2_PIPER_STAGE2_DIRECT_STAGE2_DIRECT_NODE_HPP_

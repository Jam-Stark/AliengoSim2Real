#ifndef A2_LOWLEVEL_A2_POLICY_DEPLOY_NODE_H_
#define A2_LOWLEVEL_A2_POLICY_DEPLOY_NODE_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "ManagerEnv.hpp"
#include "a2_lowlevel/a2_lowlevel_interface.h"
#include "a2_lowlevel/a2_remote.h"
#include "rclcpp/rclcpp.hpp"

namespace a2_lowlevel {

class A2PolicyDeployNode : public A2LowLevelInterface,
                           public ManagerBasedEnv {
 public:
  static constexpr int kPolicyId = 0;
  static constexpr std::size_t kTrainingJointCount = 12;
  static constexpr std::size_t kPerFrameObsDim = 46;
  static constexpr std::size_t kHistoryLength = 32;
  static constexpr std::size_t kFlattenedObsDim =
      kPerFrameObsDim * kHistoryLength;
  static constexpr double kDeployControlHz = 50.0;

  explicit A2PolicyDeployNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());

  void initObsManager() override;

 private:
  struct PolicyContract {
    double action_clip = 100.0;
    double action_scale = 0.25;
    double base_ang_vel_scale = 0.25;
    double joint_vel_scale = 0.05;
    double gait_frequency_hz = 2.0;
    double sim_dt = 0.005;
    int control_decimation = 4;
    std::array<float, kTrainingJointCount> default_joint_pos{};
    std::array<float, 3> command_scales{{2.0f, 2.0f, 0.25f}};
  };

  enum class CommandSource {
    kStatic,
    kRemote,
  };

  enum class StandupPhase {
    kIdleBlocked,
    kStandUpInterpolating,
    kStandHoldWaitingForA,
    kPolicyWarmupHold,
    kPolicyActive,
  };

  struct RemoteButtonEdges {
    bool a_rising = false;
    bool b_rising = false;
  };

  void load_and_validate_policy_contract(const std::string &path);
  void register_a2_policy();
  void control_loop();
  bool refresh_runtime_params();
  void reset_runtime_state();
  void reset_standup_state();
  bool ensure_motion_preconditions();
  bool update_command_from_source(const A2LowStateSnapshot &state,
                                  const A2RemoteState *remote);
  bool update_command_from_remote(const A2RemoteState &remote);
  RemoteButtonEdges update_remote_button_edges(const A2RemoteState &remote);
  void reset_remote_button_tracking();
  bool remote_requests_local_stop(const A2RemoteState &remote) const;
  bool handle_remote_local_stop(const A2RemoteState &remote);
  bool handle_invalid_remote_packet();
  bool handle_required_standup_remote(
      const A2LowStateSnapshot &state, const A2RemoteState &remote,
      const RemoteButtonEdges &edges);
  void start_standup_from_state(const A2LowStateSnapshot &state);
  bool cancel_standup_with_b();
  bool publish_standup_interpolation();
  bool publish_default_stand_hold();
  bool run_policy_warmup_hold();
  bool compute_and_validate_policy_observation();
  bool validate_policy_action_for_handover();
  void set_zero_command();
  bool is_history_warm() const;
  bool is_standing_command() const;
  void advance_gait_clock();
  void log_enable_state_if_changed();
  void log_command_source_if_changed();
  const char *standup_phase_name(StandupPhase phase) const;

  SimpleTensor get_projected_gravity_xy();
  SimpleTensor get_base_ang_vel();
  SimpleTensor get_joint_pos_rel();
  SimpleTensor get_joint_vel();
  SimpleTensor get_gait_clock();
  SimpleTensor get_command();

  std::shared_ptr<ObservationTerm> make_projected_gravity_term();
  std::shared_ptr<ObservationTerm> make_base_ang_vel_term();
  std::shared_ptr<ObservationTerm> make_joint_pos_term();
  std::shared_ptr<ObservationTerm> make_joint_vel_term();
  std::shared_ptr<ActionObsTerm> make_last_action_term();
  std::shared_ptr<ObservationTerm> make_gait_clock_term();
  std::shared_ptr<ObservationTerm> make_command_term();
  std::shared_ptr<ActionTerm> make_raw_action_term() const;

  std::array<float, kTrainingJointCount> map_a2_to_training(
      const std::array<float, kA2JointCount> &a2_values) const;
  std::array<A2JointCommand, kA2JointCount> build_low_level_commands(
      const std::vector<float> &raw_action,
      std::array<float, kTrainingJointCount> &clipped_raw) const;
  std::array<A2JointCommand, kA2JointCount> build_standup_commands(
      double front_alpha, double rear_alpha, double kp_factor) const;
  bool vector_is_finite(const std::vector<float> &values) const;

  PolicyContract contract_;
  std::string policy_path_;
  std::string policy_json_path_;
  bool enable_motion_ = false;
  bool last_logged_enable_motion_ = false;
  bool has_logged_enable_motion_ = false;
  CommandSource command_source_ = CommandSource::kStatic;
  CommandSource last_logged_command_source_ = CommandSource::kStatic;
  bool has_logged_command_source_ = false;
  std::string command_source_param_ = "static";
  double cmd_vx_ = 0.0;
  double cmd_vy_ = 0.0;
  double cmd_yaw_ = 0.0;
  double max_remote_vx_ = 0.8;
  double max_remote_vy_ = 0.5;
  double max_remote_yaw_ = 0.6;
  double remote_deadzone_ = 0.08;
  bool require_standup_before_policy_ = true;
  int standup_stage1_steps_ = 150;
  int standup_stage2_steps_ = 150;
  double standup_rear_alpha_lead_ = 0.10;
  double standup_front_alpha_lag_ = 0.04;
  double standup_kp_start_ = 3.0;
  double standup_kd_start_ = 0.5;
  double standup_final_gain_scale_ = 1.0;
  StandupPhase standup_phase_ = StandupPhase::kIdleBlocked;
  int standup_step_ = 0;
  std::array<float, kTrainingJointCount> standup_start_pos_{};
  bool have_previous_remote_buttons_ = false;
  bool previous_remote_a_pressed_ = false;
  bool previous_remote_b_pressed_ = false;
  std::chrono::milliseconds state_timeout_{200};
  rclcpp::TimerBase::SharedPtr control_timer_;
  std::size_t warm_frames_ = 0;
  bool history_warm_logged_ = false;
  std::array<float, kTrainingJointCount> last_raw_action_{};
  double gait_phase_ = 0.0;
  SimpleTensor gravity_;
};

}  // namespace a2_lowlevel

#endif  // A2_LOWLEVEL_A2_POLICY_DEPLOY_NODE_H_

#ifndef A2_PIPER_STAGE2_DIRECT_STAGE2_RUNTIME_HPP_
#define A2_PIPER_STAGE2_DIRECT_STAGE2_RUNTIME_HPP_

#include <array>
#include <cstddef>
#include <vector>

#include <torch/script.h>

#include "a2_piper_stage2_direct/stage2_contract.hpp"

namespace a2_piper_stage2_direct {

struct RobotSnapshot {
  std::array<float, 4> root_quaternion_wxyz{};
  std::array<float, 12> dog_joint_position_rad{};
  std::array<float, 12> dog_joint_velocity_rad_s{};
  std::array<float, 6> arm_joint_position_rad{};
};

struct PolicyCommand {
  std::array<float, 3> locomotion_vx_vy_yaw{};
  std::array<float, 3> arm_goal_radius_pitch_yaw{};
};

struct RuntimeOutput {
  std::array<float, 12> dog_raw_action{};
  std::array<float, 8> arm_raw_output{};
  std::array<float, 2> body_pitch_roll_plan_rad{};
  std::array<float, 12> dog_target_rad{};
  std::array<float, 6> arm_target_rad{};
  double inference_latency_ms = 0.0;
};

class FrameHistory {
 public:
  FrameHistory(std::size_t frames, std::size_t frame_dim);

  void reset();
  void append(const std::vector<float> &frame);
  const std::vector<float> &flattened() const;
  std::vector<float> preview(const std::vector<float> &frame) const;

 private:
  std::size_t frames_;
  std::size_t frame_dim_;
  bool initialized_ = false;
  std::vector<float> data_;
};

class PositionActionProcessor {
 public:
  PositionActionProcessor(
      const ActorContract &contract,
      const std::unordered_map<std::string, std::pair<float, float>> &limits,
      std::size_t control_dim);

  void reset();
  void seed_previous_target(const std::vector<float> &target_rad);
  std::vector<float> process(const std::vector<float> &raw_actor_output);

 private:
  const ActorContract &contract_;
  const std::unordered_map<std::string, std::pair<float, float>> &limits_;
  std::size_t control_dim_;
  std::vector<float> previous_target_;
};

class TorchScriptActors {
 public:
  explicit TorchScriptActors(const Stage2Contract &contract);

  std::vector<float> infer_dog(const std::vector<float> &input);
  std::vector<float> infer_arm(const std::vector<float> &input);

 private:
  std::vector<float> infer(const char *name, torch::jit::script::Module &module,
                           const std::vector<float> &input,
                           std::size_t expected_input_dim,
                           std::size_t expected_output_dim);
  static void validate_parameters(const char *name,
                                  torch::jit::script::Module &module);

  torch::jit::script::Module dog_;
  torch::jit::script::Module arm_;
};

class DualPolicyRuntime {
 public:
  explicit DualPolicyRuntime(Stage2Contract contract);

  void reset();
  void seed_output_targets(const std::array<float, 12> &dog_target_rad,
                           const std::array<float, 6> &arm_target_rad);
  RuntimeOutput tick(const RobotSnapshot &snapshot,
                     const PolicyCommand &command);
  const Stage2Contract &contract() const { return contract_; }

 private:
  std::vector<float> build_arm_frame(const RobotSnapshot &snapshot,
                                     const PolicyCommand &command) const;
  std::vector<float> build_dog_frame(
      const RobotSnapshot &snapshot, const PolicyCommand &command,
      const std::array<float, 2> &body_plan) const;
  void validate_snapshot(const RobotSnapshot &snapshot) const;
  void validate_command(const PolicyCommand &command) const;
  void advance_gait_phase(const PolicyCommand &command);

  Stage2Contract contract_;
  TorchScriptActors actors_;
  FrameHistory dog_history_;
  FrameHistory arm_history_;
  PositionActionProcessor dog_actions_;
  PositionActionProcessor arm_actions_;
  std::array<float, 12> previous_dog_raw_{};
  std::array<float, 6> previous_arm_control_raw_{};
  std::array<float, 2> committed_plan_{};
  double gait_phase_ = 0.0;
};

}  // namespace a2_piper_stage2_direct

#endif  // A2_PIPER_STAGE2_DIRECT_STAGE2_RUNTIME_HPP_

#include "a2_piper_stage2_direct/stage2_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace a2_piper_stage2_direct {
namespace {

constexpr double kPi = 3.14159265358979323846;

template <typename Container>
void require_finite(const Container &values, const char *name) {
  if (!std::all_of(values.begin(), values.end(),
                   [](float value) { return std::isfinite(value); })) {
    throw std::runtime_error(std::string(name) + " contains NaN/Inf");
  }
}

std::array<float, 4> normalized_quaternion(
    const std::array<float, 4> &quaternion_wxyz) {
  require_finite(quaternion_wxyz, "root quaternion");
  double squared_norm = 0.0;
  for (const float value : quaternion_wxyz) {
    squared_norm += static_cast<double>(value) * value;
  }
  if (!(squared_norm > 0.0) || !std::isfinite(squared_norm)) {
    throw std::runtime_error("root quaternion has zero/invalid norm");
  }
  const float inverse_norm =
      static_cast<float>(1.0 / std::sqrt(squared_norm));
  std::array<float, 4> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] = quaternion_wxyz[index] * inverse_norm;
  }
  return result;
}

std::array<float, 3> projected_gravity(
    const std::array<float, 4> &quaternion_wxyz) {
  const auto q = normalized_quaternion(quaternion_wxyz);
  const float w = q[0];
  const float x = q[1];
  const float y = q[2];
  const float z = q[3];
  return {2.0f * (w * y - x * z), -2.0f * (y * z + w * x),
          2.0f * (x * x + y * y) - 1.0f};
}

std::array<float, 2> base_roll_pitch(
    const std::array<float, 4> &quaternion_wxyz) {
  const auto q = normalized_quaternion(quaternion_wxyz);
  const double w = q[0];
  const double x = q[1];
  const double y = q[2];
  const double z = q[3];
  const double roll =
      std::atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
  const double pitch_argument =
      std::clamp(2.0 * (w * y - z * x), -1.0, 1.0);
  return {static_cast<float>(roll),
          static_cast<float>(std::asin(pitch_argument))};
}

void append(std::vector<float> &target, const float value) {
  target.push_back(value);
}

template <std::size_t N>
void append(std::vector<float> &target, const std::array<float, N> &values) {
  target.insert(target.end(), values.begin(), values.end());
}

template <std::size_t N>
std::array<float, N> to_array(const std::vector<float> &values,
                              const char *name) {
  if (values.size() != N) {
    throw std::runtime_error(std::string(name) + " has wrong dimension");
  }
  std::array<float, N> result{};
  std::copy(values.begin(), values.end(), result.begin());
  return result;
}

}  // namespace

FrameHistory::FrameHistory(std::size_t frames, std::size_t frame_dim)
    : frames_(frames), frame_dim_(frame_dim), data_(frames * frame_dim, 0.0f) {
  if (frames_ == 0 || frame_dim_ == 0) {
    throw std::invalid_argument("Stage2 history dimensions must be positive");
  }
}

void FrameHistory::reset() {
  initialized_ = false;
  std::fill(data_.begin(), data_.end(), 0.0f);
}

void FrameHistory::append(const std::vector<float> &frame) {
  if (frame.size() != frame_dim_) {
    throw std::runtime_error("Stage2 history frame dimension mismatch");
  }
  require_finite(frame, "history frame");
  if (!initialized_) {
    for (std::size_t index = 0; index < frames_; ++index) {
      std::copy(frame.begin(), frame.end(),
                data_.begin() + static_cast<std::ptrdiff_t>(index * frame_dim_));
    }
    initialized_ = true;
    return;
  }
  std::move(data_.begin() + static_cast<std::ptrdiff_t>(frame_dim_), data_.end(),
            data_.begin());
  std::copy(frame.begin(), frame.end(),
            data_.end() - static_cast<std::ptrdiff_t>(frame_dim_));
}

const std::vector<float> &FrameHistory::flattened() const {
  if (!initialized_) {
    throw std::runtime_error("Stage2 history has no valid frame");
  }
  return data_;
}

std::vector<float> FrameHistory::preview(
    const std::vector<float> &frame) const {
  if (!initialized_) {
    throw std::runtime_error("Stage2 history has no valid frame");
  }
  if (frame.size() != frame_dim_) {
    throw std::runtime_error("Stage2 preview frame dimension mismatch");
  }
  require_finite(frame, "preview frame");
  std::vector<float> result(data_.size());
  std::copy(data_.begin() + static_cast<std::ptrdiff_t>(frame_dim_), data_.end(),
            result.begin());
  std::copy(frame.begin(), frame.end(),
            result.end() - static_cast<std::ptrdiff_t>(frame_dim_));
  return result;
}

PositionActionProcessor::PositionActionProcessor(
    const ActorContract &contract,
    const std::unordered_map<std::string, std::pair<float, float>> &limits,
    std::size_t control_dim)
    : contract_(contract),
      limits_(limits),
      control_dim_(control_dim),
      previous_target_(contract.default_position_rad) {
  if (control_dim_ != contract_.joint_order.size() ||
      contract_.default_position_rad.size() != control_dim_ ||
      contract_.max_target_delta_rad.size() != control_dim_) {
    throw std::runtime_error("Stage2 action processor dimension mismatch for " +
                             contract_.name);
  }
}

void PositionActionProcessor::reset() {
  previous_target_ = contract_.default_position_rad;
}

void PositionActionProcessor::seed_previous_target(
    const std::vector<float> &target_rad) {
  if (target_rad.size() != control_dim_) {
    throw std::runtime_error(contract_.name +
                             " action-limiter seed dimension mismatch");
  }
  require_finite(target_rad, "action-limiter seed");
  for (std::size_t index = 0; index < control_dim_; ++index) {
    const auto limit = limits_.at(contract_.joint_order[index]);
    if (target_rad[index] < limit.first || target_rad[index] > limit.second) {
      throw std::runtime_error(contract_.name +
                               " action-limiter seed outside live limit for " +
                               contract_.joint_order[index]);
    }
  }
  previous_target_ = target_rad;
}

std::vector<float> PositionActionProcessor::process(
    const std::vector<float> &raw_actor_output) {
  if (raw_actor_output.size() != contract_.output_dim) {
    throw std::runtime_error(contract_.name +
                             " actor output dimension mismatch");
  }
  require_finite(raw_actor_output, "actor output");
  std::vector<float> result(control_dim_);
  for (std::size_t index = 0; index < control_dim_; ++index) {
    float target = contract_.default_position_rad[index] +
                   contract_.action_scale_rad * raw_actor_output[index];
    target = std::clamp(target, contract_.processed_clip_rad.first,
                        contract_.processed_clip_rad.second);
    const auto limit = limits_.at(contract_.joint_order[index]);
    target = std::clamp(target, limit.first, limit.second);
    const float delta = std::clamp(
        target - previous_target_[index],
        -contract_.max_target_delta_rad[index],
        contract_.max_target_delta_rad[index]);
    result[index] = previous_target_[index] + delta;
  }
  previous_target_ = result;
  return result;
}

TorchScriptActors::TorchScriptActors(const Stage2Contract &contract)
    : dog_(torch::jit::load(contract.dog.model_path.string(), torch::kCPU)),
      arm_(torch::jit::load(contract.arm.model_path.string(), torch::kCPU)) {
  dog_.eval();
  arm_.eval();
  validate_parameters("dog", dog_);
  validate_parameters("arm", arm_);
  infer_dog(std::vector<float>(contract.dog.input_dim, 0.0f));
  infer_arm(std::vector<float>(contract.arm.input_dim, 0.0f));
}

std::vector<float> TorchScriptActors::infer_dog(
    const std::vector<float> &input) {
  return infer("dog", dog_, input, 1620, 12);
}

std::vector<float> TorchScriptActors::infer_arm(
    const std::vector<float> &input) {
  return infer("arm", arm_, input, 600, 8);
}

std::vector<float> TorchScriptActors::infer(
    const char *name, torch::jit::script::Module &module,
    const std::vector<float> &input, std::size_t expected_input_dim,
    std::size_t expected_output_dim) {
  if (input.size() != expected_input_dim) {
    throw std::runtime_error(std::string(name) +
                             " actor input dimension mismatch");
  }
  require_finite(input, "actor input");
  torch::NoGradGuard no_grad;
  torch::Tensor tensor =
      torch::from_blob(const_cast<float *>(input.data()),
                       {1, static_cast<int64_t>(expected_input_dim)},
                       torch::TensorOptions().dtype(torch::kFloat32))
          .clone();
  const torch::jit::IValue value = module.forward({tensor});
  if (!value.isTensor()) {
    throw std::runtime_error(std::string(name) +
                             " actor must return one Tensor");
  }
  torch::Tensor output = value.toTensor().to(torch::kCPU).to(torch::kFloat32).contiguous();
  if (output.dim() != 2 || output.size(0) != 1 ||
      output.size(1) != static_cast<int64_t>(expected_output_dim)) {
    throw std::runtime_error(std::string(name) +
                             " actor output shape mismatch");
  }
  if (!torch::isfinite(output).all().item<bool>()) {
    throw std::runtime_error(std::string(name) +
                             " actor output contains NaN/Inf");
  }
  std::vector<float> result(expected_output_dim);
  std::memcpy(result.data(), output.data_ptr<float>(),
              result.size() * sizeof(float));
  return result;
}

void TorchScriptActors::validate_parameters(const char *name,
                                            torch::jit::script::Module &module) {
  for (const torch::Tensor &parameter : module.parameters()) {
    if (!torch::isfinite(parameter).all().item<bool>()) {
      throw std::runtime_error(std::string(name) +
                               " actor parameter contains NaN/Inf");
    }
  }
}

DualPolicyRuntime::DualPolicyRuntime(Stage2Contract contract)
    : contract_(std::move(contract)),
      actors_(contract_),
      dog_history_(contract_.dog.history_frames, contract_.dog.frame_dim),
      arm_history_(contract_.arm.history_frames, contract_.arm.frame_dim),
      dog_actions_(contract_.dog, contract_.joint_limits_rad, 12),
      arm_actions_(contract_.arm, contract_.joint_limits_rad, 6) {}

void DualPolicyRuntime::reset() {
  dog_history_.reset();
  arm_history_.reset();
  dog_actions_.reset();
  arm_actions_.reset();
  previous_dog_raw_.fill(0.0f);
  previous_arm_control_raw_.fill(0.0f);
  committed_plan_.fill(0.0f);
  gait_phase_ = 0.0;
}

void DualPolicyRuntime::seed_output_targets(
    const std::array<float, 12> &dog_target_rad,
    const std::array<float, 6> &arm_target_rad) {
  dog_actions_.seed_previous_target(
      std::vector<float>(dog_target_rad.begin(), dog_target_rad.end()));
  arm_actions_.seed_previous_target(
      std::vector<float>(arm_target_rad.begin(), arm_target_rad.end()));
}

RuntimeOutput DualPolicyRuntime::tick(const RobotSnapshot &snapshot,
                                      const PolicyCommand &command) {
  validate_snapshot(snapshot);
  validate_command(command);
  const std::vector<float> arm_frame = build_arm_frame(snapshot, command);
  const std::vector<float> committed_dog_frame =
      build_dog_frame(snapshot, command, committed_plan_);
  arm_history_.append(arm_frame);
  dog_history_.append(committed_dog_frame);

  const auto started = std::chrono::steady_clock::now();
  const std::vector<float> arm_raw = actors_.infer_arm(arm_history_.flattened());
  std::array<float, 2> plan = {
      std::clamp(arm_raw[6], -1.0f, 1.0f) * contract_.plan_scale_rad,
      std::clamp(arm_raw[7], -1.0f, 1.0f) * contract_.plan_scale_rad};
  const std::vector<float> preview_dog_frame =
      build_dog_frame(snapshot, command, plan);
  const std::vector<float> dog_raw =
      actors_.infer_dog(dog_history_.preview(preview_dog_frame));
  const double latency_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - started)
                                .count();

  RuntimeOutput output;
  output.dog_raw_action = to_array<12>(dog_raw, "dog raw action");
  output.arm_raw_output = to_array<8>(arm_raw, "arm raw output");
  output.body_pitch_roll_plan_rad = plan;
  output.dog_target_rad =
      to_array<12>(dog_actions_.process(dog_raw), "dog target");
  output.arm_target_rad =
      to_array<6>(arm_actions_.process(arm_raw), "arm target");
  output.inference_latency_ms = latency_ms;

  previous_dog_raw_ = output.dog_raw_action;
  std::copy_n(output.arm_raw_output.begin(), previous_arm_control_raw_.size(),
              previous_arm_control_raw_.begin());
  committed_plan_ = plan;
  advance_gait_phase(command);
  return output;
}

std::vector<float> DualPolicyRuntime::build_arm_frame(
    const RobotSnapshot &snapshot, const PolicyCommand &command) const {
  std::vector<float> frame;
  frame.reserve(20);
  for (std::size_t index = 0; index < snapshot.arm_joint_position_rad.size();
       ++index) {
    append(frame, snapshot.arm_joint_position_rad[index] -
                      contract_.arm.default_position_rad[index]);
  }
  append(frame, previous_arm_control_raw_);
  append(frame, command.arm_goal_radius_pitch_yaw);
  append(frame, 0.0f);
  append(frame, 0.0f);
  append(frame, 0.0f);
  append(frame, base_roll_pitch(snapshot.root_quaternion_wxyz));
  if (frame.size() != contract_.arm.frame_dim) {
    throw std::runtime_error("arm observation frame dimension mismatch");
  }
  return frame;
}

std::vector<float> DualPolicyRuntime::build_dog_frame(
    const RobotSnapshot &snapshot, const PolicyCommand &command,
    const std::array<float, 2> &body_plan) const {
  std::vector<float> frame;
  frame.reserve(54);
  append(frame, projected_gravity(snapshot.root_quaternion_wxyz));
  for (std::size_t index = 0; index < snapshot.dog_joint_position_rad.size();
       ++index) {
    append(frame, snapshot.dog_joint_position_rad[index] -
                      contract_.dog.default_position_rad[index]);
  }
  for (const float velocity : snapshot.dog_joint_velocity_rad_s) {
    append(frame, velocity * 0.05f);
  }
  append(frame, previous_dog_raw_);
  append(frame, command.locomotion_vx_vy_yaw[0] * 2.0f);
  append(frame, command.locomotion_vx_vy_yaw[1] * 2.0f);
  append(frame, command.locomotion_vx_vy_yaw[2] * 0.25f);
  append(frame, body_plan);
  append(frame, command.arm_goal_radius_pitch_yaw);
  append(frame, 0.0f);
  append(frame, 0.0f);
  append(frame, 0.0f);
  append(frame, base_roll_pitch(snapshot.root_quaternion_wxyz));
  append(frame, static_cast<float>(std::sin(2.0 * kPi * gait_phase_)));
  append(frame, static_cast<float>(std::cos(2.0 * kPi * gait_phase_)));
  if (frame.size() != contract_.dog.frame_dim) {
    throw std::runtime_error("dog observation frame dimension mismatch");
  }
  return frame;
}

void DualPolicyRuntime::validate_snapshot(const RobotSnapshot &snapshot) const {
  require_finite(snapshot.root_quaternion_wxyz, "root quaternion");
  require_finite(snapshot.dog_joint_position_rad, "dog joint position");
  require_finite(snapshot.dog_joint_velocity_rad_s, "dog joint velocity");
  require_finite(snapshot.arm_joint_position_rad, "arm joint position");
  static_cast<void>(normalized_quaternion(snapshot.root_quaternion_wxyz));
}

void DualPolicyRuntime::validate_command(const PolicyCommand &command) const {
  require_finite(command.locomotion_vx_vy_yaw, "locomotion command");
  require_finite(command.arm_goal_radius_pitch_yaw, "arm goal");
}

void DualPolicyRuntime::advance_gait_phase(const PolicyCommand &command) {
  if (std::all_of(command.locomotion_vx_vy_yaw.begin(),
                  command.locomotion_vx_vy_yaw.end(),
                  [](float value) { return value == 0.0f; })) {
    gait_phase_ = 0.0;
    return;
  }
  gait_phase_ = std::fmod(
      gait_phase_ + contract_.gait_frequency_hz * contract_.policy_period_s,
      1.0);
}

}  // namespace a2_piper_stage2_direct

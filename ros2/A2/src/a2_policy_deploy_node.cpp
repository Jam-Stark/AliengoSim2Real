#include "a2_lowlevel/a2_policy_deploy_node.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <json/json.h>

#ifndef A2_POLICY_DEFAULT_PATH
#define A2_POLICY_DEFAULT_PATH "policy/A2_policy/policy.pt"
#endif

#ifndef A2_POLICY_DEFAULT_JSON_PATH
#define A2_POLICY_DEFAULT_JSON_PATH "policy/A2_policy/policy.json"
#endif

namespace a2_lowlevel {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kTrainingJointCountLocal = 12;
constexpr double kStandingCmdVxThreshold = 0.1;
constexpr double kStandingCmdVyThreshold = 0.1;
constexpr double kStandingCmdYawThreshold = 0.2;
constexpr double kRemoteZeroEpsilon = 1e-5;
constexpr int kStandupLogStepInterval = 50;
constexpr std::array<float, kTrainingJointCountLocal> kExpectedDefaultJointPos =
    {0.0f, 0.0f,  0.0f,  0.0f, 0.5f, 0.5f,
     0.5f, 0.5f, -1.0f, -1.0f, -1.0f, -1.0f};
constexpr std::array<float, 3> kExpectedCommandScales = {2.0f, 2.0f, 0.25f};

constexpr std::array<const char *, kTrainingJointCountLocal>
    kExpectedTrainingJointNames = {
        "FL_hip_joint",   "FR_hip_joint",   "RL_hip_joint",
        "RR_hip_joint",   "FL_thigh_joint", "FR_thigh_joint",
        "RL_thigh_joint", "RR_thigh_joint", "FL_calf_joint",
        "FR_calf_joint",  "RL_calf_joint",  "RR_calf_joint"};

// training index -> A2 low-level index
constexpr std::array<std::size_t, kTrainingJointCountLocal> kTrainingToA2Index =
    {3, 0, 9, 6, 4, 1, 10, 7, 5, 2, 11, 8};

std::vector<PolicySpec> make_policy_specs(const std::string &policy_path) {
  return {PolicySpec::MLP(policy_path, "a2_policy")};
}

std::filesystem::path canonical_or_absolute(const std::string &path) {
  std::filesystem::path fs_path(path);
  if (std::filesystem::exists(fs_path)) {
    return std::filesystem::weakly_canonical(fs_path);
  }
  if (fs_path.is_absolute()) {
    return fs_path;
  }
  return std::filesystem::absolute(fs_path);
}

Json::Value read_json(const std::string &path) {
  std::ifstream file(path);
  if (!file.good()) {
    throw std::runtime_error("Failed to open policy JSON: " + path);
  }

  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;
  Json::Value root;
  std::string errors;
  if (!Json::parseFromStream(builder, file, &root, &errors)) {
    throw std::runtime_error("Failed to parse policy JSON " + path + ": " +
                             errors);
  }
  if (!root.isObject()) {
    throw std::runtime_error("Policy JSON root must be an object: " + path);
  }
  return root;
}

double require_double(const Json::Value &root, const char *name) {
  if (!root.isMember(name) || !root[name].isNumeric()) {
    throw std::runtime_error("Policy JSON missing numeric field: " +
                             std::string(name));
  }
  return root[name].asDouble();
}

int require_int(const Json::Value &root, const char *name) {
  if (!root.isMember(name) || !root[name].isInt()) {
    throw std::runtime_error("Policy JSON missing integer field: " +
                             std::string(name));
  }
  return root[name].asInt();
}

template <std::size_t N>
std::array<float, N> require_float_array(const Json::Value &root,
                                         const char *name) {
  if (!root.isMember(name) || !root[name].isArray() ||
      root[name].size() != N) {
    throw std::runtime_error("Policy JSON field has wrong array length: " +
                             std::string(name));
  }

  std::array<float, N> values{};
  for (std::size_t i = 0; i < N; ++i) {
    if (!root[name][static_cast<Json::ArrayIndex>(i)].isNumeric()) {
      throw std::runtime_error("Policy JSON array contains non-number: " +
                               std::string(name));
    }
    values[i] =
        static_cast<float>(root[name][static_cast<Json::ArrayIndex>(i)]
                               .asDouble());
  }
  return values;
}

void require_joint_names(const Json::Value &root, const char *name) {
  if (!root.isMember(name) || !root[name].isArray() ||
      root[name].size() != kExpectedTrainingJointNames.size()) {
    throw std::runtime_error("Policy JSON field has wrong joint-name length: " +
                             std::string(name));
  }

  for (std::size_t i = 0; i < kExpectedTrainingJointNames.size(); ++i) {
    const Json::Value &value = root[name][static_cast<Json::ArrayIndex>(i)];
    if (!value.isString() || value.asString() != kExpectedTrainingJointNames[i]) {
      throw std::runtime_error("Policy JSON " + std::string(name) +
                               " order mismatch at index " +
                               std::to_string(i));
    }
  }
}

void require_close(double actual, double expected, const char *name,
                   double tolerance = 1e-6) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error("Policy JSON contract mismatch for " +
                             std::string(name) + ": expected " +
                             std::to_string(expected) + ", got " +
                             std::to_string(actual));
  }
}

template <std::size_t N>
void require_array_close(const std::array<float, N> &actual,
                         const std::array<float, N> &expected,
                         const char *name, float tolerance = 1e-6f) {
  for (std::size_t i = 0; i < N; ++i) {
    if (std::abs(actual[i] - expected[i]) > tolerance) {
      throw std::runtime_error(
          "Policy JSON contract mismatch for " + std::string(name) +
          " at index " + std::to_string(i) + ": expected " +
          std::to_string(expected[i]) + ", got " + std::to_string(actual[i]));
    }
  }
}

bool is_finite_array(const std::array<float, 3> &values) {
  return std::all_of(values.begin(), values.end(),
                     [](float value) { return std::isfinite(value); });
}

bool is_finite_array(const std::array<float, 4> &values) {
  return std::all_of(values.begin(), values.end(),
                     [](float value) { return std::isfinite(value); });
}

bool is_finite_joints(const A2LowStateSnapshot &state) {
  return std::all_of(state.joint_q.begin(), state.joint_q.end(),
                     [](float value) { return std::isfinite(value); }) &&
         std::all_of(state.joint_dq.begin(), state.joint_dq.end(),
                     [](float value) { return std::isfinite(value); });
}

bool is_finite_nonnegative(double value) {
  return std::isfinite(value) && value >= 0.0;
}

double smoothstep01(double value) {
  const double x = std::clamp(value, 0.0, 1.0);
  return x * x * (3.0 - 2.0 * x);
}

bool is_rear_training_joint(std::size_t training_idx) {
  return training_idx == 2 || training_idx == 3 || training_idx == 6 ||
         training_idx == 7 || training_idx == 10 || training_idx == 11;
}

bool remote_sticks_are_zero_for_handover(const A2RemoteState &remote) {
  return std::abs(remote.lx) <= kRemoteZeroEpsilon &&
         std::abs(remote.rx) <= kRemoteZeroEpsilon &&
         std::abs(remote.ly) <= kRemoteZeroEpsilon;
}

std::string format_remote_button_names(const A2RemoteState &remote) {
  const auto names = pressed_a2_remote_button_names(remote);
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

std::string format_float_values(const std::vector<float> &values,
                                std::size_t max_count) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(4) << "[";
  const std::size_t count = std::min(values.size(), max_count);
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << values[i];
  }
  if (values.size() > count) {
    out << ", ...";
  }
  out << "]";
  return out.str();
}

}  // namespace

A2PolicyDeployNode::A2PolicyDeployNode(const rclcpp::NodeOptions &options)
    : A2LowLevelInterface(options),
      ManagerBasedEnv(make_policy_specs(A2_POLICY_DEFAULT_PATH),
                      InferenceDevice::CPU),
      gravity_(SimpleTensor::wrap({0.0f, 0.0f, -1.0f})) {
  policy_path_ = this->declare_parameter<std::string>(
      "policy_path", A2_POLICY_DEFAULT_PATH);
  policy_json_path_ = this->declare_parameter<std::string>(
      "policy_json_path", A2_POLICY_DEFAULT_JSON_PATH);
  enable_motion_ = this->declare_parameter<bool>("enable_motion", false);
  command_source_param_ =
      this->declare_parameter<std::string>("command_source", "static");
  cmd_vx_ = this->declare_parameter<double>("cmd_vx", 0.0);
  cmd_vy_ = this->declare_parameter<double>("cmd_vy", 0.0);
  cmd_yaw_ = this->declare_parameter<double>("cmd_yaw", 0.0);
  max_remote_vx_ =
      this->declare_parameter<double>("max_remote_vx", 0.8);
  max_remote_vy_ =
      this->declare_parameter<double>("max_remote_vy", 0.5);
  max_remote_yaw_ =
      this->declare_parameter<double>("max_remote_yaw", 0.6);
  remote_deadzone_ =
      this->declare_parameter<double>("remote_deadzone", 0.08);
  require_standup_before_policy_ =
      this->declare_parameter<bool>("require_standup_before_policy", true);
  monitor_policy_aux_ =
      this->declare_parameter<bool>("monitor_policy_aux", false);
  publish_aux_debug_ =
      this->declare_parameter<bool>("publish_aux_debug", false);
  aux_debug_topic_ =
      this->declare_parameter<std::string>("aux_debug_topic", "/a2/policy_aux");
  policy_aux_expected_dim_ =
      this->declare_parameter<int>("policy_aux_expected_dim", 6);
  policy_aux_print_period_sec_ =
      this->declare_parameter<double>("policy_aux_print_period_sec", 0.2);
  brake_gate_enabled_ =
      this->declare_parameter<bool>("brake_gate_enabled", false);
  brake_force_x_threshold_ =
      this->declare_parameter<double>("brake_force_x_threshold", -0.6);
  brake_min_cmd_vx_ =
      this->declare_parameter<double>("brake_min_cmd_vx", 0.2);
  brake_max_abs_yaw_ =
      this->declare_parameter<double>("brake_max_abs_yaw", 0.10);
  brake_hold_steps_ =
      this->declare_parameter<int>("brake_hold_steps", 2);
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
  standup_final_gain_scale_ =
      this->declare_parameter<double>("standup_final_gain_scale", 1.0);
  int state_timeout_ms = 200;
  this->get_parameter("state_timeout_ms", state_timeout_ms);
  state_timeout_ =
      std::chrono::milliseconds(std::max(1, state_timeout_ms));

  policy_specs[kPolicyId] = PolicySpec::MLP(policy_path_, "a2_policy");
  policy_paths[kPolicyId] = policy_path_;
  policy_description[kPolicyId] = "a2_policy";

  load_and_validate_policy_contract(policy_json_path_);
  init_manager();
  ensure_aux_debug_publisher();

  const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / kDeployControlHz));
  control_timer_ = this->create_wall_timer(
      period, [this]() { control_loop(); });

  RCLCPP_INFO(this->get_logger(),
              "A2 policy deploy ready: policy=%s, json=%s, control_hz=%.1f, "
              "enable_motion=%s, command_source=%s, publish_aux_debug=%s, "
              "aux_debug_topic=%s, brake_gate_enabled=%s, "
              "brake_force_x_threshold=%.3f, brake_min_cmd_vx=%.3f, "
              "brake_max_abs_yaw=%.3f, brake_hold_steps=%d",
              canonical_or_absolute(policy_path_).string().c_str(),
              canonical_or_absolute(policy_json_path_).string().c_str(),
              kDeployControlHz, enable_motion_ ? "true" : "false",
              command_source_param_.c_str(),
              publish_aux_debug_ ? "true" : "false",
              aux_debug_topic_.c_str(),
              brake_gate_enabled_ ? "true" : "false",
              brake_force_x_threshold_, brake_min_cmd_vx_,
              brake_max_abs_yaw_, brake_hold_steps_);
}

void A2PolicyDeployNode::load_and_validate_policy_contract(
    const std::string &path) {
  const Json::Value root = read_json(path);

  const int action_dim = require_int(root, "action_dim");
  const int per_frame_obs_dim = require_int(root, "per_frame_obs_dim");
  const int history_length = require_int(root, "history_length");
  const double action_scale = require_double(root, "action_scale");
  const double sim_dt = require_double(root, "sim_dt");
  const int control_decimation = require_int(root, "control_decimation");

  if (action_dim != static_cast<int>(kTrainingJointCount)) {
    throw std::runtime_error("Policy JSON action_dim must be 12.");
  }
  if (per_frame_obs_dim != static_cast<int>(kPerFrameObsDim)) {
    throw std::runtime_error("Policy JSON per_frame_obs_dim must be 46.");
  }
  if (history_length != static_cast<int>(kHistoryLength)) {
    throw std::runtime_error("Policy JSON history_length must be 32.");
  }
  require_close(action_scale, 0.25, "action_scale");
  require_close(sim_dt, 0.005, "sim_dt");
  if (control_decimation != 4) {
    throw std::runtime_error("Policy JSON control_decimation must be 4.");
  }
  require_close(1.0 / (sim_dt * control_decimation), kDeployControlHz,
                "deploy_control_hz", 1e-3);

  require_joint_names(root, "joint_names");
  require_joint_names(root, "obs_joint_names");

  contract_.action_clip = require_double(root, "action_clip");
  contract_.action_scale = action_scale;
  contract_.base_ang_vel_scale = require_double(root, "base_ang_vel_scale");
  require_close(contract_.base_ang_vel_scale, 0.25, "base_ang_vel_scale");
  contract_.joint_vel_scale = require_double(root, "joint_vel_scale");
  require_close(contract_.joint_vel_scale, 0.05, "joint_vel_scale");
  contract_.gait_frequency_hz = require_double(root, "gait_frequency_hz");
  require_close(contract_.gait_frequency_hz, 2.0, "gait_frequency_hz");
  contract_.sim_dt = sim_dt;
  contract_.control_decimation = control_decimation;
  contract_.default_joint_pos =
      require_float_array<kTrainingJointCount>(root, "default_joint_pos");
  require_array_close(contract_.default_joint_pos, kExpectedDefaultJointPos,
                      "default_joint_pos");
  contract_.command_scales = require_float_array<3>(root, "command_scales");
  require_array_close(contract_.command_scales, kExpectedCommandScales,
                      "command_scales");

  const auto obs_default =
      require_float_array<kTrainingJointCount>(root, "obs_default_joint_pos");
  if (obs_default != contract_.default_joint_pos) {
    throw std::runtime_error(
        "Policy JSON default_joint_pos and obs_default_joint_pos differ.");
  }

  RCLCPP_INFO(this->get_logger(),
              "Validated A2 policy contract: action_dim=12, per_frame_obs=46, "
              "history=32, flattened_obs=%zu, action_scale=%.3f, "
              "action_clip=%.3f",
              kFlattenedObsDim, contract_.action_scale,
              contract_.action_clip);
}

void A2PolicyDeployNode::initObsManager() {
  obs_terms.clear();
  action_terms.clear();
  action_obs_terms.clear();
  register_a2_policy();
}

void A2PolicyDeployNode::register_a2_policy() {
  std::vector<std::shared_ptr<ObservationTerm>> obs;
  obs.push_back(make_projected_gravity_term());
  obs.push_back(make_base_ang_vel_term());
  obs.push_back(make_joint_pos_term());
  obs.push_back(make_joint_vel_term());
  obs.push_back(make_last_action_term());
  obs.push_back(make_gait_clock_term());
  obs.push_back(make_command_term());
  registerTerms(obs, make_raw_action_term());
}

std::shared_ptr<ObservationTerm>
A2PolicyDeployNode::make_projected_gravity_term() {
  auto term =
      std::make_shared<ObservationTerm>("projected_gravity_xy", kHistoryLength);
  term->func = [this]() { return get_projected_gravity_xy(); };
  return term;
}

std::shared_ptr<ObservationTerm> A2PolicyDeployNode::make_base_ang_vel_term() {
  auto term =
      std::make_shared<ObservationTerm>("base_ang_vel", kHistoryLength);
  term->func = [this]() { return get_base_ang_vel(); };
  term->scale = contract_.base_ang_vel_scale;
  return term;
}

std::shared_ptr<ObservationTerm> A2PolicyDeployNode::make_joint_pos_term() {
  auto term =
      std::make_shared<ObservationTerm>("joint_q_default_rel", kHistoryLength);
  term->func = [this]() { return get_joint_pos_rel(); };
  return term;
}

std::shared_ptr<ObservationTerm> A2PolicyDeployNode::make_joint_vel_term() {
  auto term = std::make_shared<ObservationTerm>("joint_dq", kHistoryLength);
  term->func = [this]() { return get_joint_vel(); };
  term->scale = contract_.joint_vel_scale;
  return term;
}

std::shared_ptr<ActionObsTerm> A2PolicyDeployNode::make_last_action_term() {
  auto term = std::make_shared<ActionObsTerm>("last_raw_action", kHistoryLength);
  term->init(kTrainingJointCount);
  return term;
}

std::shared_ptr<ObservationTerm> A2PolicyDeployNode::make_gait_clock_term() {
  auto term = std::make_shared<ObservationTerm>("gait_clock", kHistoryLength);
  term->func = [this]() { return get_gait_clock(); };
  return term;
}

std::shared_ptr<ObservationTerm> A2PolicyDeployNode::make_command_term() {
  auto term = std::make_shared<ObservationTerm>("command", kHistoryLength);
  term->func = [this]() { return get_command(); };
  return term;
}

std::shared_ptr<ActionTerm> A2PolicyDeployNode::make_raw_action_term() const {
  return std::make_shared<ActionTerm>();
}

SimpleTensor A2PolicyDeployNode::get_projected_gravity_xy() {
  const auto state = latest_state();
  std::vector<float> quat = {state.quaternion[0], state.quaternion[1],
                             state.quaternion[2], state.quaternion[3]};
  const SimpleTensor projected =
      QuatRotateInverse(SimpleTensor::wrap(quat), gravity_);
  if (projected.numel() < 2) {
    return SimpleTensor::wrap({0.0f, 0.0f});
  }
  return SimpleTensor::wrap({projected[0], projected[1]});
}

SimpleTensor A2PolicyDeployNode::get_base_ang_vel() {
  const auto state = latest_state();
  return SimpleTensor::wrap({state.gyroscope[0], state.gyroscope[1],
                             state.gyroscope[2]});
}

SimpleTensor A2PolicyDeployNode::get_joint_pos_rel() {
  const auto state = latest_state();
  const auto training_q = map_a2_to_training(state.joint_q);
  std::vector<float> values(kTrainingJointCount, 0.0f);
  for (std::size_t i = 0; i < kTrainingJointCount; ++i) {
    values[i] = training_q[i] - contract_.default_joint_pos[i];
  }
  return SimpleTensor::wrap(values);
}

SimpleTensor A2PolicyDeployNode::get_joint_vel() {
  const auto state = latest_state();
  const auto training_dq = map_a2_to_training(state.joint_dq);
  return SimpleTensor::wrap(
      std::vector<float>(training_dq.begin(), training_dq.end()));
}

SimpleTensor A2PolicyDeployNode::get_gait_clock() {
  return SimpleTensor::wrap({static_cast<float>(std::sin(gait_phase_)),
                             static_cast<float>(std::cos(gait_phase_))});
}

SimpleTensor A2PolicyDeployNode::get_command() {
  return SimpleTensor::wrap(
      {static_cast<float>(cmd_vx_ * contract_.command_scales[0]),
       static_cast<float>(cmd_vy_ * contract_.command_scales[1]),
       static_cast<float>(cmd_yaw_ * contract_.command_scales[2])});
}

std::array<float, A2PolicyDeployNode::kTrainingJointCount>
A2PolicyDeployNode::map_a2_to_training(
    const std::array<float, kA2JointCount> &a2_values) const {
  std::array<float, kTrainingJointCount> training_values{};
  for (std::size_t training_idx = 0; training_idx < kTrainingJointCount;
       ++training_idx) {
    training_values[training_idx] =
        a2_values[kTrainingToA2Index[training_idx]];
  }
  return training_values;
}

std::array<A2JointCommand, kA2JointCount>
A2PolicyDeployNode::build_low_level_commands(
    const std::vector<float> &raw_action,
    std::array<float, kTrainingJointCount> &clipped_raw) const {
  std::array<float, kTrainingJointCount> target_q{};
  for (std::size_t i = 0; i < kTrainingJointCount; ++i) {
    const float clipped = std::clamp(
        raw_action[i], static_cast<float>(-contract_.action_clip),
        static_cast<float>(contract_.action_clip));
    clipped_raw[i] = clipped;
    target_q[i] = contract_.default_joint_pos[i] +
                  static_cast<float>(contract_.action_scale) * clipped;
  }

  std::array<A2JointCommand, kA2JointCount> commands{};
  for (std::size_t training_idx = 0; training_idx < kTrainingJointCount;
       ++training_idx) {
    const std::size_t a2_idx = kTrainingToA2Index[training_idx];
    auto &command = commands[a2_idx];
    command.q = target_q[training_idx];
    command.dq = 0.0f;
    command.tau = 0.0f;
    if (training_idx < 4 || (training_idx >= 4 && training_idx < 8)) {
      command.kp = 140.0f;
      command.kd = 5.0f;
    } else {
      command.kp = 220.0f;
      command.kd = 9.0f;
    }
  }

  return commands;
}

std::array<A2JointCommand, kA2JointCount>
A2PolicyDeployNode::build_standup_commands(double front_alpha,
                                           double rear_alpha,
                                           double kp_factor) const {
  const double safe_front_alpha = std::clamp(front_alpha, 0.0, 1.0);
  const double safe_rear_alpha = std::clamp(rear_alpha, 0.0, 1.0);
  const double safe_kp_factor = std::clamp(kp_factor, 0.0, 1.0);

  std::array<A2JointCommand, kA2JointCount> commands{};
  for (std::size_t training_idx = 0; training_idx < kTrainingJointCount;
       ++training_idx) {
    const bool rear_joint = is_rear_training_joint(training_idx);
    const double joint_alpha = rear_joint ? safe_rear_alpha : safe_front_alpha;
    const double target =
        static_cast<double>(standup_start_pos_[training_idx]) +
        (static_cast<double>(contract_.default_joint_pos[training_idx]) -
         static_cast<double>(standup_start_pos_[training_idx])) *
            joint_alpha;
    const double final_kp =
        (training_idx < 8 ? 140.0 : 220.0) * standup_final_gain_scale_;
    const double final_kd =
        (training_idx < 8 ? 5.0 : 9.0) * standup_final_gain_scale_;
    const double kp =
        standup_kp_start_ + (final_kp - standup_kp_start_) * safe_kp_factor;
    const double kd =
        standup_kd_start_ + (final_kd - standup_kd_start_) * safe_kp_factor;

    const std::size_t a2_idx = kTrainingToA2Index[training_idx];
    auto &command = commands[a2_idx];
    command.q = static_cast<float>(target);
    command.dq = 0.0f;
    command.tau = 0.0f;
    command.kp = static_cast<float>(kp);
    command.kd = static_cast<float>(kd);
  }

  return commands;
}

void A2PolicyDeployNode::control_loop() {
  if (!refresh_runtime_params()) {
    reset_runtime_state();
    reset_standup_state();
    return;
  }

  log_enable_state_if_changed();
  log_command_source_if_changed();

  if (!enable_motion_ && standup_phase_ != StandupPhase::kIdleBlocked) {
    reset_runtime_state();
    reset_standup_state();
  }

  if (!ensure_motion_preconditions()) {
    return;
  }

  const auto state = latest_state();
  A2RemoteState remote;
  RemoteButtonEdges remote_edges;
  const A2RemoteState *remote_for_command = nullptr;
  if (command_source_ == CommandSource::kRemote) {
    remote = decode_a2_remote(
        state.wireless_remote, static_cast<float>(remote_deadzone_));
    remote_for_command = &remote;

    if (remote_requests_local_stop(remote)) {
      handle_remote_local_stop(remote);
      return;
    }
    if (!remote.valid) {
      handle_invalid_remote_packet();
      return;
    }

    remote_edges = update_remote_button_edges(remote);
  }

  if (require_standup_before_policy_ &&
      command_source_ == CommandSource::kStatic && enable_motion_) {
    set_zero_command();
    reset_runtime_state();
    reset_standup_state();
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "A2 policy motion refused: require_standup_before_policy=true blocks "
        "command_source=static. Use command_source=remote for two-A stand-up "
        "handover, or explicitly set require_standup_before_policy=false.");
    return;
  }

  if (require_standup_before_policy_ &&
      command_source_ == CommandSource::kRemote && enable_motion_) {
    if (handle_required_standup_remote(state, remote, remote_edges)) {
      return;
    }
  }

  if (!update_command_from_source(state, remote_for_command)) {
    return;
  }

  if (is_standing_command()) {
    gait_phase_ = 0.0;
  }

  if (!compute_and_validate_policy_observation()) {
    return;
  }

  warm_frames_ = std::min<std::size_t>(warm_frames_ + 1, kHistoryLength);
  if (!is_history_warm()) {
    RCLCPP_INFO_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 policy history warming: %zu/%zu fresh frames. Motion publish is "
        "blocked.",
        warm_frames_, kHistoryLength);
    advance_gait_clock();
    return;
  }

  if (!history_warm_logged_) {
    RCLCPP_INFO(this->get_logger(),
                "A2 policy history warm: %zu frames. Motion remains gated by "
                "enable_motion.",
                warm_frames_);
    history_warm_logged_ = true;
  }

  if (!enable_motion_ && !monitor_policy_aux_ && !publish_aux_debug_) {
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "A2 policy publish refused because enable_motion=false.");
    obs_actions[kPolicyId] = SimpleTensor::wrap(
        std::vector<float>(last_raw_action_.begin(), last_raw_action_.end()));
    advance_gait_clock();
    return;
  }

  SimpleTensor action;
  try {
    action = computeAction(kPolicyId);
  } catch (const std::exception &e) {
    RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                          "A2 policy inference failed: %s", e.what());
    reset_policy_states(kPolicyId);
    return;
  }

  const auto raw_action = ManagerBasedEnv::toVector<float>(action);
  const SimpleTensor aux = policys[kPolicyId].get_last_aux_output();
  publish_policy_aux_debug(aux);
  const bool aux_values_finite =
      log_policy_aux_output(action, aux, false);
  if (apply_brake_gate(aux)) {
    return;
  }

  if (raw_action.size() != kTrainingJointCount ||
      !vector_is_finite(raw_action)) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "Refusing A2 policy publish because action dim/values are invalid: "
        "dim=%zu expected=%zu.",
        raw_action.size(), kTrainingJointCount);
    obs_actions[kPolicyId] = SimpleTensor::wrap(
        std::vector<float>(last_raw_action_.begin(), last_raw_action_.end()));
    return;
  }

  std::array<float, kTrainingJointCount> clipped_raw{};
  if (!enable_motion_) {
    for (std::size_t i = 0; i < kTrainingJointCount; ++i) {
      clipped_raw[i] = std::clamp(
          raw_action[i], static_cast<float>(-contract_.action_clip),
          static_cast<float>(contract_.action_clip));
    }
    last_raw_action_ = clipped_raw;
    obs_actions[kPolicyId] = SimpleTensor::wrap(
        std::vector<float>(last_raw_action_.begin(), last_raw_action_.end()));
    if (!aux_values_finite) {
      RCLCPP_WARN_THROTTLE(
          this->get_logger(), *this->get_clock(), 1000,
          "A2 policy aux monitor saw NaN/Inf auxiliary output. LowCmd remains "
          "blocked because enable_motion=false.");
    }
    advance_gait_clock();
    return;
  }

  auto commands = build_low_level_commands(raw_action, clipped_raw);
  if (!publish_joint_commands(commands)) {
    obs_actions[kPolicyId] = SimpleTensor::wrap(
        std::vector<float>(last_raw_action_.begin(), last_raw_action_.end()));
    return;
  }

  last_raw_action_ = clipped_raw;
  obs_actions[kPolicyId] = SimpleTensor::wrap(
      std::vector<float>(last_raw_action_.begin(), last_raw_action_.end()));
  advance_gait_clock();
}

bool A2PolicyDeployNode::refresh_runtime_params() {
  this->get_parameter("enable_motion", enable_motion_);
  this->get_parameter("command_source", command_source_param_);
  this->get_parameter("cmd_vx", cmd_vx_);
  this->get_parameter("cmd_vy", cmd_vy_);
  this->get_parameter("cmd_yaw", cmd_yaw_);
  this->get_parameter("max_remote_vx", max_remote_vx_);
  this->get_parameter("max_remote_vy", max_remote_vy_);
  this->get_parameter("max_remote_yaw", max_remote_yaw_);
  this->get_parameter("remote_deadzone", remote_deadzone_);
  this->get_parameter("require_standup_before_policy",
                      require_standup_before_policy_);
  this->get_parameter("monitor_policy_aux", monitor_policy_aux_);
  this->get_parameter("publish_aux_debug", publish_aux_debug_);
  std::string requested_aux_debug_topic = aux_debug_topic_;
  this->get_parameter("aux_debug_topic", requested_aux_debug_topic);
  if (requested_aux_debug_topic != aux_debug_topic_) {
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 5000,
        "A2 aux debug topic parameter changed from '%s' to '%s' at runtime; "
        "keeping existing publisher topic.",
        aux_debug_topic_.c_str(), requested_aux_debug_topic.c_str());
  }
  ensure_aux_debug_publisher();
  this->get_parameter("policy_aux_expected_dim", policy_aux_expected_dim_);
  this->get_parameter("policy_aux_print_period_sec",
                      policy_aux_print_period_sec_);
  this->get_parameter("brake_gate_enabled", brake_gate_enabled_);
  this->get_parameter("brake_force_x_threshold",
                      brake_force_x_threshold_);
  this->get_parameter("brake_min_cmd_vx", brake_min_cmd_vx_);
  this->get_parameter("brake_max_abs_yaw", brake_max_abs_yaw_);
  this->get_parameter("brake_hold_steps", brake_hold_steps_);
  this->get_parameter("standup_stage1_steps", standup_stage1_steps_);
  this->get_parameter("standup_stage2_steps", standup_stage2_steps_);
  this->get_parameter("standup_rear_alpha_lead", standup_rear_alpha_lead_);
  this->get_parameter("standup_front_alpha_lag", standup_front_alpha_lag_);
  this->get_parameter("standup_kp_start", standup_kp_start_);
  this->get_parameter("standup_kd_start", standup_kd_start_);
  this->get_parameter("standup_final_gain_scale",
                      standup_final_gain_scale_);
  if (monitor_policy_aux_ &&
      (policy_aux_expected_dim_ < 0 ||
       !std::isfinite(policy_aux_print_period_sec_) ||
       policy_aux_print_period_sec_ <= 0.0)) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "A2 policy aux monitor params invalid: policy_aux_expected_dim must "
        "be >=0 and policy_aux_print_period_sec must be finite positive. "
        "Refusing policy publish/monitor.");
    return false;
  }

  if (!std::isfinite(brake_force_x_threshold_) ||
      !is_finite_nonnegative(brake_min_cmd_vx_) ||
      !is_finite_nonnegative(brake_max_abs_yaw_) ||
      brake_hold_steps_ < 1) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "A2 brake gate params invalid: brake_force_x_threshold must be "
        "finite, brake_min_cmd_vx and brake_max_abs_yaw must be finite "
        "nonnegative, and brake_hold_steps must be >=1. Refusing policy "
        "publish/monitor.");
    return false;
  }

  if (standup_stage1_steps_ < 1 || standup_stage2_steps_ < 1 ||
      !std::isfinite(standup_rear_alpha_lead_) ||
      !std::isfinite(standup_front_alpha_lag_) ||
      !is_finite_nonnegative(standup_kp_start_) ||
      !is_finite_nonnegative(standup_kd_start_) ||
      !is_finite_nonnegative(standup_final_gain_scale_)) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "A2 stand-up params invalid: steps must be >=1, lead/lag finite, "
        "and gains/final scale finite nonnegative. Refusing policy publish.");
    return false;
  }

  if (command_source_param_ == "static") {
    command_source_ = CommandSource::kStatic;
  } else if (command_source_param_ == "remote") {
    command_source_ = CommandSource::kRemote;
  } else {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "A2 policy command_source must be 'static' or 'remote', got '%s'.",
        command_source_param_.c_str());
    return false;
  }

  if (command_source_ == CommandSource::kStatic &&
      (!std::isfinite(cmd_vx_) || !std::isfinite(cmd_vy_) ||
       !std::isfinite(cmd_yaw_))) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "A2 static command params contain NaN/Inf; refusing policy publish.");
    return false;
  }

  if (command_source_ == CommandSource::kRemote) {
    set_zero_command();
    if (!is_finite_nonnegative(max_remote_vx_) ||
        !is_finite_nonnegative(max_remote_vy_) ||
        !is_finite_nonnegative(max_remote_yaw_) ||
        !std::isfinite(remote_deadzone_)) {
      RCLCPP_ERROR_THROTTLE(
          this->get_logger(), *this->get_clock(), 3000,
          "A2 remote params contain invalid values; refusing policy publish.");
      return false;
    }

    remote_deadzone_ = std::clamp(std::abs(remote_deadzone_), 0.0, 1.0);
  }

  int state_timeout_ms = static_cast<int>(state_timeout_.count());
  this->get_parameter("state_timeout_ms", state_timeout_ms);
  state_timeout_ =
      std::chrono::milliseconds(std::max(1, state_timeout_ms));
  return true;
}

bool A2PolicyDeployNode::ensure_motion_preconditions() {
  if (!has_fresh_state(state_timeout_)) {
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 policy publish refused because %s is missing or stale.",
        lowstate_topic().c_str());
    reset_runtime_state();
    reset_standup_state();
    return false;
  }

  const auto state = latest_state();
  if (!is_finite_array(state.quaternion) || !is_finite_array(state.gyroscope) ||
      !is_finite_joints(state)) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 policy publish refused because LowState contains NaN/Inf.");
    reset_runtime_state();
    reset_standup_state();
    return false;
  }

  return true;
}

bool A2PolicyDeployNode::update_command_from_source(
    const A2LowStateSnapshot &state, const A2RemoteState *remote) {
  if (command_source_ == CommandSource::kStatic) {
    return true;
  }

  A2RemoteState decoded_remote;
  if (remote == nullptr) {
    decoded_remote = decode_a2_remote(
        state.wireless_remote, static_cast<float>(remote_deadzone_));
    return update_command_from_remote(decoded_remote);
  }
  return update_command_from_remote(*remote);
}

bool A2PolicyDeployNode::update_command_from_remote(
    const A2RemoteState &remote) {
  const std::string button_names = format_remote_button_names(remote);

  if (remote_requests_local_stop(remote)) {
    return handle_remote_local_stop(remote);
  }

  if (!remote.valid) {
    return handle_invalid_remote_packet();
  }

  RCLCPP_DEBUG_THROTTLE(
      this->get_logger(), *this->get_clock(), 1000,
      "A2 remote decode: lx=%.3f rx=%.3f ry=%.3f ly=%.3f buttons=%s",
      remote.lx, remote.rx, remote.ry, remote.ly, button_names.c_str());

  cmd_vx_ = static_cast<double>(remote.ly) * max_remote_vx_;
  cmd_vy_ = -static_cast<double>(remote.lx) * max_remote_vy_;
  cmd_yaw_ = -static_cast<double>(remote.rx) * max_remote_yaw_;
  RCLCPP_DEBUG_THROTTLE(
      this->get_logger(), *this->get_clock(), 1000,
      "A2 remote command: vx=%.3f vy=%.3f yaw=%.3f (ry=%.3f unused)",
      cmd_vx_, cmd_vy_, cmd_yaw_, remote.ry);
  return true;
}

bool A2PolicyDeployNode::remote_requests_local_stop(
    const A2RemoteState &remote) const {
  return remote.buttons.select || (remote.buttons.l2 && remote.buttons.b);
}

A2PolicyDeployNode::RemoteButtonEdges
A2PolicyDeployNode::update_remote_button_edges(const A2RemoteState &remote) {
  RemoteButtonEdges edges;
  if (!remote.valid) {
    return edges;
  }

  if (have_previous_remote_buttons_) {
    edges.a_rising = remote.buttons.a && !previous_remote_a_pressed_;
    edges.b_rising = remote.buttons.b && !previous_remote_b_pressed_;
  }

  previous_remote_a_pressed_ = remote.buttons.a;
  previous_remote_b_pressed_ = remote.buttons.b;
  have_previous_remote_buttons_ = true;
  return edges;
}

void A2PolicyDeployNode::reset_remote_button_tracking() {
  have_previous_remote_buttons_ = false;
  previous_remote_a_pressed_ = false;
  previous_remote_b_pressed_ = false;
}

bool A2PolicyDeployNode::handle_remote_local_stop(
    const A2RemoteState &remote) {
  const std::string button_names = format_remote_button_names(remote);
  set_zero_command();
  reset_runtime_state();
  reset_standup_state();
  if (enable_motion_) {
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 1000,
        "A2 remote local stop requested by buttons=%s. Resetting policy/"
        "stand-up runtime and publishing zero LowCmd.",
        button_names.c_str());
    publish_zero();
  } else {
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 1000,
        "A2 remote local stop requested by buttons=%s. Runtime reset; zero "
        "LowCmd not published because enable_motion=false.",
        button_names.c_str());
  }
  return false;
}

bool A2PolicyDeployNode::handle_invalid_remote_packet() {
  set_zero_command();
  reset_runtime_state();
  reset_standup_state();
  reset_remote_button_tracking();
  RCLCPP_ERROR_THROTTLE(
      this->get_logger(), *this->get_clock(), 2000,
      "A2 remote packet has invalid stick floats; refusing policy publish and "
      "resetting stand-up handover to blocked.");
  return false;
}

bool A2PolicyDeployNode::handle_required_standup_remote(
    const A2LowStateSnapshot &state, const A2RemoteState &remote,
    const RemoteButtonEdges &edges) {
  if (standup_phase_ == StandupPhase::kPolicyActive) {
    return false;
  }

  if (standup_phase_ == StandupPhase::kStandUpInterpolating ||
      standup_phase_ == StandupPhase::kStandHoldWaitingForA ||
      standup_phase_ == StandupPhase::kPolicyWarmupHold) {
    if (edges.b_rising) {
      return cancel_standup_with_b();
    }
  }

  switch (standup_phase_) {
    case StandupPhase::kIdleBlocked:
      set_zero_command();
      if (edges.a_rising) {
        start_standup_from_state(state);
        return publish_standup_interpolation();
      }
      RCLCPP_INFO_THROTTLE(
          this->get_logger(), *this->get_clock(), 3000,
          "A2 policy waiting for first A press: enable_motion=true, "
          "command_source=remote, stand-up handover required. No LowCmd is "
          "published while idle-blocked.");
      return true;

    case StandupPhase::kStandUpInterpolating:
      return publish_standup_interpolation();

    case StandupPhase::kStandHoldWaitingForA:
      if (edges.a_rising) {
        if (!remote_sticks_are_zero_for_handover(remote)) {
          RCLCPP_WARN_THROTTLE(
              this->get_logger(), *this->get_clock(), 1000,
              "A2 stand-up handover refused: sticks must be centered after "
              "deadzone before second A (lx=%.3f rx=%.3f ly=%.3f).",
              remote.lx, remote.rx, remote.ly);
        } else {
          reset_runtime_state();
          set_zero_command();
          gait_phase_ = 0.0;
          standup_phase_ = StandupPhase::kPolicyWarmupHold;
          RCLCPP_INFO(
              this->get_logger(),
              "A2 stand-up handover accepted: entering PolicyWarmupHold. "
              "Publishing default stand pose until policy history and first "
              "action validation are ready.");
        }
      }
      publish_default_stand_hold();
      return true;

    case StandupPhase::kPolicyWarmupHold:
      return run_policy_warmup_hold();

    case StandupPhase::kPolicyActive:
      return false;
  }

  return true;
}

void A2PolicyDeployNode::start_standup_from_state(
    const A2LowStateSnapshot &state) {
  standup_start_pos_ = map_a2_to_training(state.joint_q);
  reset_runtime_state();
  set_zero_command();
  gait_phase_ = 0.0;
  standup_step_ = 0;
  standup_phase_ = StandupPhase::kStandUpInterpolating;
  RCLCPP_INFO(
      this->get_logger(),
      "A2 stand-up started by first A press: interpolating current joint q to "
      "policy default_joint_pos over %d steps.",
      standup_stage1_steps_ + standup_stage2_steps_);
}

bool A2PolicyDeployNode::cancel_standup_with_b() {
  set_zero_command();
  reset_runtime_state();
  reset_standup_state();
  if (enable_motion_) {
    RCLCPP_WARN(this->get_logger(),
                "A2 stand-up handover canceled by B. Publishing zero LowCmd.");
    publish_zero();
  } else {
    RCLCPP_WARN(this->get_logger(),
                "A2 stand-up handover canceled by B. Zero LowCmd not "
                "published because enable_motion=false.");
  }
  return true;
}

bool A2PolicyDeployNode::publish_standup_interpolation() {
  if (!enable_motion_) {
    reset_runtime_state();
    reset_standup_state();
    return true;
  }

  const int total_steps = standup_stage1_steps_ + standup_stage2_steps_;
  const int current_step = std::min(standup_step_ + 1, total_steps);
  const double alpha = std::clamp(
      static_cast<double>(current_step) / static_cast<double>(total_steps),
      0.0, 1.0);
  const double front_alpha =
      smoothstep01(alpha - standup_front_alpha_lag_);
  const double rear_alpha = smoothstep01(alpha + standup_rear_alpha_lead_);
  const double kp_factor = smoothstep01(alpha);

  const auto commands =
      build_standup_commands(front_alpha, rear_alpha, kp_factor);
  if (!publish_joint_commands(commands)) {
    reset_runtime_state();
    reset_standup_state();
    return true;
  }

  if (current_step == 1 ||
      current_step % kStandupLogStepInterval == 0 ||
      current_step == total_steps) {
    const int stage =
        current_step <= standup_stage1_steps_ ? 1 : 2;
    RCLCPP_INFO(
        this->get_logger(),
        "A2 stand-up phase=%s stage=%d step=%d/%d alpha=%.3f "
        "front_alpha=%.3f rear_alpha=%.3f kp_factor=%.3f",
        standup_phase_name(standup_phase_), stage, current_step, total_steps,
        alpha, front_alpha, rear_alpha, kp_factor);
  }

  standup_step_ = current_step;
  if (standup_step_ >= total_steps) {
    standup_phase_ = StandupPhase::kStandHoldWaitingForA;
    RCLCPP_INFO(
        this->get_logger(),
        "A2 stand-up interpolation complete: holding policy default pose and "
        "waiting for second A press. Center sticks before handover.");
  }
  return true;
}

bool A2PolicyDeployNode::publish_default_stand_hold() {
  if (!enable_motion_) {
    reset_runtime_state();
    reset_standup_state();
    return false;
  }

  const auto commands = build_standup_commands(1.0, 1.0, 1.0);
  if (!publish_joint_commands(commands)) {
    reset_runtime_state();
    reset_standup_state();
    return false;
  }
  return true;
}

bool A2PolicyDeployNode::run_policy_warmup_hold() {
  if (!publish_default_stand_hold()) {
    return true;
  }

  set_zero_command();
  gait_phase_ = 0.0;
  if (!compute_and_validate_policy_observation()) {
    reset_standup_state();
    return true;
  }

  warm_frames_ = std::min<std::size_t>(warm_frames_ + 1, kHistoryLength);
  if (!is_history_warm()) {
    RCLCPP_INFO_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 policy handover warmup hold: %zu/%zu fresh frames. Holding "
        "default stand pose.",
        warm_frames_, kHistoryLength);
    advance_gait_clock();
    return true;
  }

  if (!history_warm_logged_) {
    RCLCPP_INFO(
        this->get_logger(),
        "A2 policy handover history warm: validating one policy action while "
        "continuing to hold default stand pose.");
    history_warm_logged_ = true;
  }

  if (!validate_policy_action_for_handover()) {
    advance_gait_clock();
    return true;
  }

  standup_phase_ = StandupPhase::kPolicyActive;
  RCLCPP_INFO(this->get_logger(),
              "A2 policy handover complete: entering PolicyActive. First "
              "policy action publish is allowed on the next control cycle.");
  advance_gait_clock();
  return true;
}

bool A2PolicyDeployNode::compute_and_validate_policy_observation() {
  try {
    computeObs(kPolicyId);
  } catch (const std::exception &e) {
    RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                          "A2 policy observation update failed: %s",
                          e.what());
    reset_runtime_state();
    return false;
  }

  if (policcy_obs.size() <= kPolicyId ||
      policcy_obs[kPolicyId].numel() !=
          static_cast<int64_t>(kFlattenedObsDim) ||
      !vector_is_finite(policcy_obs[kPolicyId].data_)) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "Refusing A2 policy publish because observation is invalid.");
    reset_runtime_state();
    return false;
  }

  return true;
}

bool A2PolicyDeployNode::validate_policy_action_for_handover() {
  SimpleTensor action;
  try {
    action = computeAction(kPolicyId);
  } catch (const std::exception &e) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 policy handover inference validation failed: %s. Continuing to "
        "hold default stand pose.",
        e.what());
    reset_policy_states(kPolicyId);
    return false;
  }

  const auto raw_action = ManagerBasedEnv::toVector<float>(action);
  const SimpleTensor aux = policys[kPolicyId].get_last_aux_output();
  publish_policy_aux_debug(aux);
  log_policy_aux_output(action, aux, false);
  obs_actions[kPolicyId] = SimpleTensor::wrap(
      std::vector<float>(last_raw_action_.begin(), last_raw_action_.end()));
  if (raw_action.size() != kTrainingJointCount ||
      !vector_is_finite(raw_action)) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 policy handover validation refused invalid action: dim=%zu "
        "expected=%zu. Continuing to hold default stand pose.",
        raw_action.size(), kTrainingJointCount);
    reset_policy_states(kPolicyId);
    return false;
  }

  return true;
}

bool A2PolicyDeployNode::should_log_policy_aux(bool force) {
  if (!monitor_policy_aux_) {
    return false;
  }
  if (force || !have_policy_aux_log_time_) {
    last_policy_aux_log_time_ = std::chrono::steady_clock::now();
    have_policy_aux_log_time_ = true;
    return true;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed =
      std::chrono::duration<double>(now - last_policy_aux_log_time_).count();
  if (elapsed < policy_aux_print_period_sec_) {
    return false;
  }

  last_policy_aux_log_time_ = now;
  return true;
}

bool A2PolicyDeployNode::log_policy_aux_output(const SimpleTensor &action,
                                               const SimpleTensor &aux,
                                               bool force) {
  if (!monitor_policy_aux_) {
    return true;
  }

  const bool action_finite = vector_is_finite(action.data_);
  const bool aux_finite = vector_is_finite(aux.data_);
  const bool should_log = should_log_policy_aux(force || !aux_finite);
  if (!should_log) {
    return aux_finite;
  }

  const std::vector<float> action_values = action.data_;
  const std::vector<float> aux_values = aux.data_;
  const std::string action_preview = format_float_values(action_values, 8);

  if (!action_finite) {
    RCLCPP_ERROR(this->get_logger(),
                 "A2 policy aux monitor: action_dim=%lld action_first8=%s "
                 "contains NaN/Inf.",
                 static_cast<long long>(action.numel()),
                 action_preview.c_str());
  }

  if (!aux_finite) {
    RCLCPP_WARN(this->get_logger(),
                "A2 policy aux monitor: action_dim=%lld action_first8=%s "
                "aux_dim=%lld expected_aux_dim=%d aux_first8=%s contains "
                "NaN/Inf; monitor mode will not publish LowCmd.",
                static_cast<long long>(action.numel()),
                action_preview.c_str(),
                static_cast<long long>(aux.numel()), policy_aux_expected_dim_,
                format_float_values(aux_values, 8).c_str());
    return false;
  }

  if (aux.numel() == 0) {
    RCLCPP_INFO(this->get_logger(),
                "A2 policy aux monitor: action_dim=%lld action_first8=%s "
                "aux_dim=0 expected_aux_dim=%d; model may not return "
                "tuple[1].",
                static_cast<long long>(action.numel()),
                action_preview.c_str(), policy_aux_expected_dim_);
    return true;
  }

  if (aux.numel() == 6 &&
      policy_aux_expected_dim_ == static_cast<int>(aux.numel())) {
    RCLCPP_INFO(
        this->get_logger(),
        "A2 policy aux monitor: action_dim=%lld action_first8=%s aux_dim=6 "
        "expected_aux_dim=%d pred_base_lin_vel=[%.4f, %.4f, %.4f] "
        "pred_base_force_local=[%.4f, %.4f, %.4f]",
        static_cast<long long>(action.numel()), action_preview.c_str(),
        policy_aux_expected_dim_, aux_values[0], aux_values[1], aux_values[2],
        aux_values[3], aux_values[4], aux_values[5]);
    return true;
  }

  if (aux.numel() == 6) {
    RCLCPP_WARN(
        this->get_logger(),
        "A2 policy aux monitor: action_dim=%lld action_first8=%s aux_dim=6 "
        "expected_aux_dim=%d pred_base_lin_vel=[%.4f, %.4f, %.4f] "
        "pred_base_force_local=[%.4f, %.4f, %.4f] aux_first8=%s; layout "
        "unverified because aux_dim != expected_aux_dim.",
        static_cast<long long>(action.numel()), action_preview.c_str(),
        policy_aux_expected_dim_, aux_values[0], aux_values[1], aux_values[2],
        aux_values[3], aux_values[4], aux_values[5],
        format_float_values(aux_values, 8).c_str());
    return true;
  }

  RCLCPP_WARN(this->get_logger(),
              "A2 policy aux monitor: action_dim=%lld action_first8=%s "
              "aux_dim=%lld expected_aux_dim=%d aux_first8=%s; layout "
              "unverified.",
              static_cast<long long>(action.numel()), action_preview.c_str(),
              static_cast<long long>(aux.numel()), policy_aux_expected_dim_,
              format_float_values(aux_values, 8).c_str());
  return true;
}

void A2PolicyDeployNode::ensure_aux_debug_publisher() {
  if (!publish_aux_debug_ || aux_debug_pub_) {
    return;
  }

  aux_debug_pub_ =
      this->create_publisher<std_msgs::msg::Float32MultiArray>(
          aux_debug_topic_, 10);
  RCLCPP_INFO(this->get_logger(),
              "A2 policy aux debug publisher enabled: topic=%s",
              aux_debug_topic_.c_str());
}

void A2PolicyDeployNode::publish_policy_aux_debug(const SimpleTensor &aux) {
  if (!publish_aux_debug_) {
    return;
  }
  ensure_aux_debug_publisher();
  if (!aux_debug_pub_) {
    return;
  }

  std_msgs::msg::Float32MultiArray msg;
  msg.data = aux.data_;
  aux_debug_pub_->publish(msg);
}

bool A2PolicyDeployNode::brake_force_triggered(double force_x) const {
  if (brake_force_x_threshold_ >= 0.0) {
    return force_x >= brake_force_x_threshold_;
  }
  return force_x <= brake_force_x_threshold_;
}

void A2PolicyDeployNode::publish_brake_zero_command(double force_x) {
  RCLCPP_WARN_THROTTLE(
      this->get_logger(), *this->get_clock(), 1000,
      "A2 brake gate active: publishing zero LowCmd instead of policy joint "
      "command (force_x=%.4f threshold=%.4f cmd_vx=%.3f cmd_yaw=%.3f "
      "hold_count=%d/%d).",
      force_x, brake_force_x_threshold_, cmd_vx_, cmd_yaw_,
      brake_hold_count_, brake_hold_steps_);
  publish_zero();
  set_zero_command();
  last_raw_action_.fill(0.0f);
  if (!obs_actions.empty()) {
    obs_actions[kPolicyId] = SimpleTensor::wrap(
        std::vector<float>(last_raw_action_.begin(), last_raw_action_.end()));
  }
  gait_phase_ = 0.0;
}

bool A2PolicyDeployNode::apply_brake_gate(const SimpleTensor &aux) {
  if (!enable_motion_) {
    reset_brake_gate_state();
    return false;
  }

  if (!brake_gate_enabled_) {
    if (brake_active_ || brake_hold_count_ > 0) {
      RCLCPP_INFO(
          this->get_logger(),
          "A2 brake gate disabled: clearing previous brake latch state.");
    }
    reset_brake_gate_state();
    return false;
  }

  const bool eligible =
      cmd_vx_ >= brake_min_cmd_vx_ &&
      std::abs(cmd_yaw_) <= brake_max_abs_yaw_ && !is_standing_command();
  if (!eligible) {
    if (brake_active_ || brake_hold_count_ > 0) {
      RCLCPP_INFO(
          this->get_logger(),
          "A2 brake gate released/not eligible: cmd_vx=%.3f min=%.3f "
          "cmd_yaw=%.3f max_abs=%.3f standing=%s hold_count=%d/%d.",
          cmd_vx_, brake_min_cmd_vx_, cmd_yaw_, brake_max_abs_yaw_,
          is_standing_command() ? "true" : "false", brake_hold_count_,
          brake_hold_steps_);
    }
    reset_brake_gate_state();
    return false;
  }

  if (brake_active_) {
    const bool aux_valid = aux.numel() >= 6 && vector_is_finite(aux.data_);
    const double force_x =
        aux_valid ? aux.data_[3]
                  : std::numeric_limits<double>::quiet_NaN();
    publish_brake_zero_command(force_x);
    return true;
  }

  const bool aux_finite = vector_is_finite(aux.data_);
  if (aux.numel() < 6 || !aux_finite) {
    brake_hold_count_ = 0;
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 1000,
        "A2 brake gate skipped: aux invalid for force gate "
        "(aux_dim=%lld, finite=%s). Need dim>=6 finite aux; no brake trigger.",
        static_cast<long long>(aux.numel()),
        aux_finite ? "true" : "false");
    return false;
  }

  const double force_x = static_cast<double>(aux.data_[3]);
  const bool triggered = brake_force_triggered(force_x);
  if (triggered) {
    brake_hold_count_ = std::min(brake_hold_count_ + 1, brake_hold_steps_);
    RCLCPP_INFO_THROTTLE(
        this->get_logger(), *this->get_clock(), 500,
        "A2 brake gate force trigger: force_x=%.4f threshold=%.4f "
        "cmd_vx=%.3f cmd_yaw=%.3f hold_count=%d/%d.",
        force_x, brake_force_x_threshold_, cmd_vx_, cmd_yaw_,
        brake_hold_count_, brake_hold_steps_);
    if (brake_hold_count_ >= brake_hold_steps_) {
      brake_active_ = true;
      RCLCPP_WARN(
          this->get_logger(),
          "A2 brake gate activated: force_x=%.4f threshold=%.4f "
          "cmd_vx=%.3f cmd_yaw=%.3f hold_count=%d/%d. Current tick will "
          "publish zero LowCmd and skip policy joint command.",
          force_x, brake_force_x_threshold_, cmd_vx_, cmd_yaw_,
          brake_hold_count_, brake_hold_steps_);
      publish_brake_zero_command(force_x);
      return true;
    }
  } else if (brake_hold_count_ > 0) {
    RCLCPP_INFO_THROTTLE(
        this->get_logger(), *this->get_clock(), 1000,
        "A2 brake gate force trigger cleared before latch: force_x=%.4f "
        "threshold=%.4f hold_count=%d/%d.",
        force_x, brake_force_x_threshold_, brake_hold_count_,
        brake_hold_steps_);
    brake_hold_count_ = 0;
  }

  return false;
}

void A2PolicyDeployNode::reset_brake_gate_state() {
  brake_hold_count_ = 0;
  brake_active_ = false;
}

void A2PolicyDeployNode::set_zero_command() {
  cmd_vx_ = 0.0;
  cmd_vy_ = 0.0;
  cmd_yaw_ = 0.0;
}

bool A2PolicyDeployNode::is_history_warm() const {
  return warm_frames_ >= kHistoryLength;
}

bool A2PolicyDeployNode::is_standing_command() const {
  return std::abs(cmd_vx_) < kStandingCmdVxThreshold &&
         std::abs(cmd_vy_) < kStandingCmdVyThreshold &&
         std::abs(cmd_yaw_) < kStandingCmdYawThreshold;
}

void A2PolicyDeployNode::advance_gait_clock() {
  if (is_standing_command()) {
    gait_phase_ = 0.0;
    return;
  }

  gait_phase_ += 2.0 * kPi * contract_.gait_frequency_hz / kDeployControlHz;
  gait_phase_ = std::fmod(gait_phase_, 2.0 * kPi);
  if (gait_phase_ < 0.0) {
    gait_phase_ += 2.0 * kPi;
  }
}

void A2PolicyDeployNode::reset_runtime_state() {
  warm_frames_ = 0;
  history_warm_logged_ = false;
  have_policy_aux_log_time_ = false;
  reset_brake_gate_state();
  gait_phase_ = 0.0;
  last_raw_action_.fill(0.0f);
  if (!obs_terms.empty()) {
    reset_observation_buffers(kPolicyId);
  }
  if (!obs_actions.empty()) {
    obs_actions[kPolicyId] = SimpleTensor::wrap(
        std::vector<float>(last_raw_action_.begin(), last_raw_action_.end()));
  }
  reset_policy_states(kPolicyId);
}

void A2PolicyDeployNode::reset_standup_state() {
  standup_phase_ = StandupPhase::kIdleBlocked;
  standup_step_ = 0;
  standup_start_pos_.fill(0.0f);
}

void A2PolicyDeployNode::log_enable_state_if_changed() {
  if (has_logged_enable_motion_ &&
      enable_motion_ == last_logged_enable_motion_) {
    return;
  }
  RCLCPP_INFO(this->get_logger(), "A2 policy enable_motion=%s",
              enable_motion_ ? "true" : "false");
  if (has_logged_enable_motion_ && !enable_motion_) {
    reset_runtime_state();
    reset_standup_state();
  }
  has_logged_enable_motion_ = true;
  last_logged_enable_motion_ = enable_motion_;
}

void A2PolicyDeployNode::log_command_source_if_changed() {
  if (has_logged_command_source_ &&
      command_source_ == last_logged_command_source_) {
    return;
  }

  const char *name =
      command_source_ == CommandSource::kRemote ? "remote" : "static";
  RCLCPP_INFO(this->get_logger(),
              "A2 policy command_source=%s, remote_limits=[vx=%.3f, "
              "vy=%.3f, yaw=%.3f], remote_deadzone=%.3f",
              name, max_remote_vx_, max_remote_vy_, max_remote_yaw_,
              remote_deadzone_);
  if (has_logged_command_source_) {
    reset_runtime_state();
    reset_standup_state();
  }
  reset_remote_button_tracking();
  has_logged_command_source_ = true;
  last_logged_command_source_ = command_source_;
}

const char *A2PolicyDeployNode::standup_phase_name(StandupPhase phase) const {
  switch (phase) {
    case StandupPhase::kIdleBlocked:
      return "IdleBlocked";
    case StandupPhase::kStandUpInterpolating:
      return "StandUpInterpolating";
    case StandupPhase::kStandHoldWaitingForA:
      return "StandHoldWaitingForA";
    case StandupPhase::kPolicyWarmupHold:
      return "PolicyWarmupHold";
    case StandupPhase::kPolicyActive:
      return "PolicyActive";
  }
  return "Unknown";
}

bool A2PolicyDeployNode::vector_is_finite(
    const std::vector<float> &values) const {
  return std::all_of(values.begin(), values.end(),
                     [](float value) { return std::isfinite(value); });
}

}  // namespace a2_lowlevel

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<a2_lowlevel::A2PolicyDeployNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}

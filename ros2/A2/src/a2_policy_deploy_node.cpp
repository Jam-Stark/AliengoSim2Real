#include "a2_lowlevel/a2_policy_deploy_node.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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
      this->declare_parameter<double>("max_remote_vx", 0.4);
  max_remote_vy_ =
      this->declare_parameter<double>("max_remote_vy", 0.25);
  max_remote_yaw_ =
      this->declare_parameter<double>("max_remote_yaw", 0.6);
  remote_deadzone_ =
      this->declare_parameter<double>("remote_deadzone", 0.08);

  int state_timeout_ms = 200;
  this->get_parameter("state_timeout_ms", state_timeout_ms);
  state_timeout_ =
      std::chrono::milliseconds(std::max(1, state_timeout_ms));

  policy_specs[kPolicyId] = PolicySpec::MLP(policy_path_, "a2_policy");
  policy_paths[kPolicyId] = policy_path_;
  policy_description[kPolicyId] = "a2_policy";

  load_and_validate_policy_contract(policy_json_path_);
  init_manager();

  const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / kDeployControlHz));
  control_timer_ = this->create_wall_timer(
      period, [this]() { control_loop(); });

  RCLCPP_INFO(this->get_logger(),
              "A2 policy deploy ready: policy=%s, json=%s, control_hz=%.1f, "
              "enable_motion=%s, command_source=%s",
              canonical_or_absolute(policy_path_).string().c_str(),
              canonical_or_absolute(policy_json_path_).string().c_str(),
              kDeployControlHz, enable_motion_ ? "true" : "false",
              command_source_param_.c_str());
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

void A2PolicyDeployNode::control_loop() {
  if (!refresh_runtime_params()) {
    reset_runtime_state();
    return;
  }

  log_enable_state_if_changed();
  log_command_source_if_changed();

  if (!ensure_motion_preconditions()) {
    return;
  }

  if (!update_command_from_source(latest_state())) {
    return;
  }

  if (is_standing_command()) {
    gait_phase_ = 0.0;
  }

  try {
    computeObs(kPolicyId);
  } catch (const std::exception &e) {
    RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                          "A2 policy observation update failed: %s",
                          e.what());
    reset_runtime_state();
    return;
  }

  if (policcy_obs.size() <= kPolicyId ||
      policcy_obs[kPolicyId].numel() !=
          static_cast<int64_t>(kFlattenedObsDim) ||
      !vector_is_finite(policcy_obs[kPolicyId].data_)) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "Refusing A2 policy publish because observation is invalid.");
    reset_runtime_state();
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

  if (!enable_motion_) {
    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "A2 policy publish refused because enable_motion=false.");
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
    return false;
  }

  const auto state = latest_state();
  if (!is_finite_array(state.quaternion) || !is_finite_array(state.gyroscope) ||
      !is_finite_joints(state)) {
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 policy publish refused because LowState contains NaN/Inf.");
    reset_runtime_state();
    return false;
  }

  return true;
}

bool A2PolicyDeployNode::update_command_from_source(
    const A2LowStateSnapshot &state) {
  if (command_source_ == CommandSource::kStatic) {
    return true;
  }

  const auto remote = decode_a2_remote(
      state.wireless_remote, static_cast<float>(remote_deadzone_));
  return update_command_from_remote(remote);
}

bool A2PolicyDeployNode::update_command_from_remote(
    const A2RemoteState &remote) {
  const std::string button_names = format_remote_button_names(remote);

  if (remote_requests_local_stop(remote)) {
    set_zero_command();
    reset_runtime_state();
    if (enable_motion_) {
      RCLCPP_WARN_THROTTLE(
          this->get_logger(), *this->get_clock(), 1000,
          "A2 remote local stop requested by buttons=%s. Resetting policy "
          "runtime and publishing zero LowCmd.",
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

  if (!remote.valid) {
    set_zero_command();
    reset_runtime_state();
    RCLCPP_ERROR_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 remote packet has invalid stick floats; refusing policy publish.");
    return false;
  }

  RCLCPP_DEBUG_THROTTLE(
      this->get_logger(), *this->get_clock(), 1000,
      "A2 remote decode: lx=%.3f rx=%.3f ry=%.3f ly=%.3f buttons=%s",
      remote.lx, remote.rx, remote.ry, remote.ly, button_names.c_str());

  if (!remote.buttons.l2) {
    set_zero_command();
    RCLCPP_INFO_THROTTLE(
        this->get_logger(), *this->get_clock(), 2000,
        "A2 remote L2 gate is not held; policy command forced to [0,0,0].");
    return true;
  }

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

void A2PolicyDeployNode::log_enable_state_if_changed() {
  if (has_logged_enable_motion_ &&
      enable_motion_ == last_logged_enable_motion_) {
    return;
  }
  RCLCPP_INFO(this->get_logger(), "A2 policy enable_motion=%s",
              enable_motion_ ? "true" : "false");
  if (has_logged_enable_motion_ && !enable_motion_) {
    reset_runtime_state();
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
  }
  has_logged_command_source_ = true;
  last_logged_command_source_ = command_source_;
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

#include "a2_piper_stage2_direct/stage2_contract.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace a2_piper_stage2_direct {
namespace {

const std::vector<std::string> kDogJointOrder = {
    "FL_hip_joint",   "FR_hip_joint",   "RL_hip_joint",
    "RR_hip_joint",   "FL_thigh_joint", "FR_thigh_joint",
    "RL_thigh_joint", "RR_thigh_joint", "FL_calf_joint",
    "FR_calf_joint",  "RL_calf_joint",  "RR_calf_joint"};
const std::vector<std::string> kArmJointOrder = {
    "arm_j1", "arm_j2", "arm_j3", "arm_j4", "arm_j5", "arm_j6"};

const YAML::Node require_node(const YAML::Node &parent, const char *key,
                              const std::string &context) {
  const YAML::Node value = parent[key];
  if (!value.IsDefined()) {
    throw std::runtime_error("Manifest missing " + context + "." + key);
  }
  return value;
}

template <typename T>
T require_scalar(const YAML::Node &parent, const char *key,
                 const std::string &context) {
  const YAML::Node value = require_node(parent, key, context);
  if (!value.IsScalar()) {
    throw std::runtime_error("Manifest field " + context + "." + key +
                             " must be scalar");
  }
  return value.as<T>();
}

void require_string(const YAML::Node &parent, const char *key,
                    const std::string &expected,
                    const std::string &context) {
  const std::string actual = require_scalar<std::string>(parent, key, context);
  if (actual != expected) {
    throw std::runtime_error("Manifest field " + context + "." + key +
                             " must be '" + expected + "', got '" + actual +
                             "'");
  }
}

void require_close(double actual, double expected, const std::string &name,
                   double tolerance = 1.0e-9) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
    std::ostringstream message;
    message << "Manifest field " << name << " must be " << expected
            << ", got " << actual;
    throw std::runtime_error(message.str());
  }
}

std::vector<std::string> require_strings(const YAML::Node &parent,
                                         const char *key,
                                         const std::string &context) {
  const YAML::Node values = require_node(parent, key, context);
  if (!values.IsSequence()) {
    throw std::runtime_error("Manifest field " + context + "." + key +
                             " must be a sequence");
  }
  std::vector<std::string> result;
  result.reserve(values.size());
  for (const auto &value : values) {
    result.push_back(value.as<std::string>());
  }
  return result;
}

std::vector<float> require_floats(const YAML::Node &parent, const char *key,
                                  std::size_t expected_size,
                                  const std::string &context) {
  const YAML::Node values = require_node(parent, key, context);
  if (!values.IsSequence() || values.size() != expected_size) {
    throw std::runtime_error("Manifest field " + context + "." + key +
                             " has wrong vector length");
  }
  std::vector<float> result;
  result.reserve(values.size());
  for (const auto &value : values) {
    const float parsed = value.as<float>();
    if (!std::isfinite(parsed)) {
      throw std::runtime_error("Manifest field " + context + "." + key +
                               " contains NaN/Inf");
    }
    result.push_back(parsed);
  }
  return result;
}

void require_exact_strings(const std::vector<std::string> &actual,
                           const std::vector<std::string> &expected,
                           const std::string &name) {
  if (actual != expected) {
    throw std::runtime_error("Manifest joint/order mismatch for " + name);
  }
  const std::unordered_set<std::string> unique(actual.begin(), actual.end());
  if (unique.size() != actual.size()) {
    throw std::runtime_error("Manifest contains duplicate names in " + name);
  }
}

void require_shape(const YAML::Node &model, const char *key,
                   std::size_t expected_dim, const std::string &context) {
  const YAML::Node shape = require_node(model, key, context);
  if (!shape.IsSequence() || shape.size() != 2 ||
      shape[0].as<std::string>() != "B" ||
      shape[1].as<std::size_t>() != expected_dim) {
    throw std::runtime_error("Manifest field " + context + "." + key +
                             " has an unsupported shape");
  }
}

void require_slice(const YAML::Node &parent, const char *key,
                   std::size_t begin, std::size_t end,
                   const std::string &context) {
  const YAML::Node slice = require_node(parent, key, context);
  if (!slice.IsSequence() || slice.size() != 2 ||
      slice[0].as<std::size_t>() != begin ||
      slice[1].as<std::size_t>() != end) {
    throw std::runtime_error("Manifest field " + context + "." + key +
                             " has an unsupported slice");
  }
}

void require_observation_blocks(
    const YAML::Node &actor, std::size_t expected_frame_dim,
    const std::vector<std::tuple<std::string, std::size_t, std::size_t>>
        &expected,
    const std::string &context) {
  if (require_scalar<std::size_t>(actor, "frame_dim", context) !=
      expected_frame_dim) {
    throw std::runtime_error("Manifest " + context + ".frame_dim mismatch");
  }
  const YAML::Node blocks = require_node(actor, "blocks", context);
  if (!blocks.IsSequence() || blocks.size() != expected.size()) {
    throw std::runtime_error("Manifest " + context + ".blocks mismatch");
  }
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const auto &[name, begin, end] = expected[index];
    const YAML::Node block = blocks[index];
    require_string(block, "name", name, context + ".blocks[" +
                                           std::to_string(index) + "]");
    require_slice(block, "slice", begin, end,
                  context + ".blocks[" + std::to_string(index) + "]");
    if (require_scalar<std::size_t>(
            block, "dim",
            context + ".blocks[" + std::to_string(index) + "]") !=
        end - begin) {
      throw std::runtime_error("Manifest observation block dimension mismatch");
    }
  }
}

ActorContract load_actor(const YAML::Node &root,
                         const std::filesystem::path &bundle_dir,
                         const std::string &name,
                         const std::vector<std::string> &expected_joint_order,
                         std::size_t expected_input_dim,
                         std::size_t expected_output_dim,
                         std::size_t expected_frame_dim) {
  const YAML::Node models = require_node(root, "models", "root");
  const YAML::Node model = require_node(models, name.c_str(), "models");
  require_string(model, "format", "torchscript", "models." + name);
  require_string(model, "method", "forward", "models." + name);
  require_shape(model, "input_shape", expected_input_dim, "models." + name);
  require_shape(model, "output_shape", expected_output_dim, "models." + name);

  const YAML::Node history_root = require_node(root, "history", "root");
  const YAML::Node history =
      require_node(history_root, name.c_str(), "history");
  const std::size_t frame_dim =
      require_scalar<std::size_t>(history, "frame_dim", "history." + name);
  const std::size_t frames =
      require_scalar<std::size_t>(history, "frames", "history." + name);
  const std::size_t flattened = require_scalar<std::size_t>(
      history, "flattened_dim", "history." + name);
  if (frame_dim != expected_frame_dim || frames != 30 ||
      flattened != expected_input_dim || frame_dim * frames != flattened) {
    throw std::runtime_error("Manifest history contract mismatch for " + name);
  }

  const YAML::Node actions = require_node(root, "actions", "root");
  const YAML::Node action = require_node(actions, name.c_str(), "actions");
  if (require_scalar<std::size_t>(action, "actor_output_dim",
                                  "actions." + name) != expected_output_dim) {
    throw std::runtime_error("Manifest actor output dimension mismatch for " +
                             name);
  }
  require_string(action, "mode", "position_offset", "actions." + name);
  const YAML::Node actor_clip = require_node(action, "actor_clip", "actions." + name);
  if (!actor_clip.IsNull()) {
    throw std::runtime_error("Stage2 actor_clip must remain null for " + name);
  }

  ActorContract result;
  result.name = name;
  result.model_path =
      bundle_dir / require_scalar<std::string>(model, "file", "models." + name);
  result.input_dim = expected_input_dim;
  result.output_dim = expected_output_dim;
  result.frame_dim = frame_dim;
  result.history_frames = frames;
  result.joint_order = require_strings(action, "joint_order", "actions." + name);
  require_exact_strings(result.joint_order, expected_joint_order,
                        "actions." + name + ".joint_order");
  result.default_position_rad = require_floats(
      action, "default_joint_position_rad", expected_joint_order.size(),
      "actions." + name);
  result.max_target_delta_rad = require_floats(
      action, "max_target_delta_per_policy_tick_rad",
      expected_joint_order.size(), "actions." + name);
  result.action_scale_rad =
      require_scalar<float>(action, "scale_rad", "actions." + name);
  require_close(result.action_scale_rad, 0.25, "actions." + name + ".scale_rad");
  const auto processed = require_floats(
      action, "processed_target_clip_rad", 2, "actions." + name);
  result.processed_clip_rad = {processed[0], processed[1]};
  require_close(processed[0], -100.0,
                "actions." + name + ".processed_target_clip_rad[0]");
  require_close(processed[1], 100.0,
                "actions." + name + ".processed_target_clip_rad[1]");
  if (!std::filesystem::is_regular_file(result.model_path)) {
    throw std::runtime_error("Stage2 model file not found: " +
                             result.model_path.string());
  }
  return result;
}

const YAML::Node require_site_node(const YAML::Node &parent, const char *key,
                                   const std::string &context) {
  const YAML::Node value = parent[key];
  if (!value.IsDefined()) {
    throw std::runtime_error("Site config missing " + context + "." + key);
  }
  return value;
}

template <typename T>
T require_site_scalar(const YAML::Node &parent, const char *key,
                      const std::string &context) {
  const YAML::Node value = require_site_node(parent, key, context);
  if (!value.IsScalar()) {
    throw std::runtime_error("Site config field " + context + "." + key +
                             " must be scalar");
  }
  return value.as<T>();
}

void require_site_string(const YAML::Node &parent, const char *key,
                         const std::string &expected,
                         const std::string &context) {
  const std::string actual =
      require_site_scalar<std::string>(parent, key, context);
  if (actual != expected) {
    throw std::runtime_error("Site config field " + context + "." + key +
                             " must be '" + expected + "', got '" + actual +
                             "'");
  }
}

void require_exact_site_keys(const YAML::Node &mapping,
                             const std::vector<std::string> &expected,
                             const std::string &context) {
  if (!mapping.IsMap() || mapping.size() != expected.size()) {
    throw std::runtime_error("Site config field " + context +
                             " must map every fixed joint name exactly once");
  }
  const std::unordered_set<std::string> expected_names(expected.begin(),
                                                        expected.end());
  std::unordered_set<std::string> seen;
  for (const auto &entry : mapping) {
    if (!entry.first.IsScalar()) {
      throw std::runtime_error("Site config field " + context +
                               " contains a non-scalar joint name");
    }
    const std::string name = entry.first.as<std::string>();
    if (expected_names.count(name) == 0 || !seen.emplace(name).second) {
      throw std::runtime_error("Site config field " + context +
                               " contains unknown/duplicate joint '" + name +
                               "'");
    }
  }
}

std::unordered_map<std::string, std::pair<float, float>>
require_site_position_limits(const YAML::Node &site_limits, const char *key,
                             const std::vector<std::string> &joint_order) {
  const std::string context = std::string("site_limits.") + key;
  const YAML::Node mapping = require_site_node(site_limits, key, "site_limits");
  require_exact_site_keys(mapping, joint_order, context);
  std::unordered_map<std::string, std::pair<float, float>> result;
  for (const std::string &name : joint_order) {
    const YAML::Node bounds = mapping[name];
    if (!bounds.IsSequence() || bounds.size() != 2) {
      throw std::runtime_error("Site config field " + context + "." + name +
                               " must be [lower, upper]");
    }
    const float lower = bounds[0].as<float>();
    const float upper = bounds[1].as<float>();
    if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper) {
      throw std::runtime_error("Invalid site position limit for joint '" + name +
                               "'");
    }
    result.emplace(name, std::make_pair(lower, upper));
  }
  return result;
}

std::unordered_map<std::string, float> require_site_target_rates(
    const YAML::Node &site_limits, const char *key,
    const std::vector<std::string> &joint_order) {
  const std::string context = std::string("site_limits.") + key;
  const YAML::Node mapping = require_site_node(site_limits, key, "site_limits");
  require_exact_site_keys(mapping, joint_order, context);
  std::unordered_map<std::string, float> result;
  for (const std::string &name : joint_order) {
    const YAML::Node value = mapping[name];
    if (!value.IsScalar()) {
      throw std::runtime_error("Site config field " + context + "." + name +
                               " must be a positive scalar");
    }
    const float parsed = value.as<float>();
    if (!std::isfinite(parsed) || parsed <= 0.0f) {
      throw std::runtime_error("Invalid site target rate for joint '" + name +
                               "'");
    }
    result.emplace(name, parsed);
  }
  return result;
}

void apply_live_actor_limits(
    ActorContract &actor,
    std::unordered_map<std::string, std::pair<float, float>> &effective_limits,
    const std::unordered_map<std::string, std::pair<float, float>> &site_limits,
    const std::unordered_map<std::string, float> &site_rates) {
  for (std::size_t index = 0; index < actor.joint_order.size(); ++index) {
    const std::string &name = actor.joint_order[index];
    const auto manifest_limit = effective_limits.at(name);
    const auto site_limit = site_limits.at(name);
    const std::pair<float, float> intersection{
        std::max(manifest_limit.first, site_limit.first),
        std::min(manifest_limit.second, site_limit.second)};
    if (intersection.first > intersection.second) {
      throw std::runtime_error("Site/manifest position-limit intersection is "
                               "empty for joint '" +
                               name + "'");
    }
    const float default_position = actor.default_position_rad[index];
    if (default_position < intersection.first ||
        default_position > intersection.second) {
      throw std::runtime_error("Default joint position lies outside effective "
                               "live limit for joint '" +
                               name + "'");
    }
    const float manifest_rate = actor.max_target_delta_rad[index];
    if (!std::isfinite(manifest_rate) || manifest_rate <= 0.0f) {
      throw std::runtime_error("Manifest target rate must be positive for live "
                               "joint '" +
                               name + "'");
    }
    effective_limits.at(name) = intersection;
    actor.max_target_delta_rad[index] =
        std::min(manifest_rate, site_rates.at(name));
  }
}

}  // namespace

Stage2Contract Stage2Contract::load(
    const std::filesystem::path &requested_bundle_dir) {
  try {
    Stage2Contract contract;
    contract.bundle_dir = std::filesystem::weakly_canonical(requested_bundle_dir);
    const std::filesystem::path manifest_path =
        contract.bundle_dir / "policy_manifest.yaml";
    if (!std::filesystem::is_regular_file(manifest_path)) {
      throw std::runtime_error("Stage2 manifest not found: " +
                               manifest_path.string());
    }
    const YAML::Node root = YAML::LoadFile(manifest_path.string());
    require_string(root, "schema", "lmp_stage2_dual_policy_source_contract",
                   "root");
    if (require_scalar<int>(root, "schema_version", "root") != 1) {
      throw std::runtime_error("Unsupported Stage2 manifest schema_version");
    }

    const YAML::Node numeric = require_node(root, "numeric", "root");
    require_string(numeric, "dtype", "float32", "numeric");
    require_string(numeric, "angle_unit", "rad", "numeric");
    require_string(numeric, "angular_velocity_unit", "rad/s", "numeric");
    require_string(numeric, "observation_normalization", "none", "numeric");
    require_string(numeric, "observation_clip", "none", "numeric");
    require_string(numeric, "observation_corruption", "disabled", "numeric");

    const YAML::Node timing = require_node(root, "timing", "root");
    contract.policy_period_s =
        require_scalar<double>(timing, "policy_period_s", "timing");
    require_close(contract.policy_period_s, 0.02, "timing.policy_period_s");
    require_close(require_scalar<double>(timing, "policy_frequency_hz", "timing"),
                  50.0, "timing.policy_frequency_hz");
    require_close(require_scalar<double>(timing, "training_sim_dt_s", "timing"),
                  0.005, "timing.training_sim_dt_s");
    if (require_scalar<int>(timing, "training_decimation", "timing") != 4) {
      throw std::runtime_error("Manifest training_decimation must be 4");
    }

    const YAML::Node history = require_node(root, "history", "root");
    require_string(history, "layout", "frame_major", "history");
    require_string(history, "flatten_order", "oldest_to_newest", "history");
    require_string(history, "previous_action_semantics",
                   "previous_actor_raw_output_not_processed_hardware_target",
                   "history");
    const YAML::Node reset = require_node(history, "reset", "history");
    require_string(reset, "rule",
                   "repeat_first_valid_post_reset_frame_30_times",
                   "history.reset");
    if (!require_scalar<bool>(reset, "all_zero_flat_history_is_invalid",
                              "history.reset")) {
      throw std::runtime_error("All-zero Stage2 history must remain invalid");
    }

    const YAML::Node base_frame = require_node(root, "base_frame", "root");
    require_exact_strings(require_strings(base_frame, "ros_quaternion_order",
                                          "base_frame"),
                          {"x", "y", "z", "w"},
                          "base_frame.ros_quaternion_order");
    require_string(base_frame, "quaternion_semantics",
                   "active_rotation_from_root_link_body_to_world", "base_frame");

    const YAML::Node protocol = require_node(root, "inference_protocol", "root");
    if (require_scalar<bool>(protocol, "preview_mutates_persistent_history",
                             "inference_protocol")) {
      throw std::runtime_error("Dog preview must not mutate persistent history");
    }

    contract.dog = load_actor(root, contract.bundle_dir, "dog", kDogJointOrder,
                              1620, 12, 54);
    contract.arm = load_actor(root, contract.bundle_dir, "arm", kArmJointOrder,
                              600, 8, 20);

    const YAML::Node observations = require_node(root, "observations", "root");
    require_observation_blocks(
        require_node(observations, "dog", "observations"), 54,
        {{"projected_gravity_b", 0, 3},
         {"leg_joint_position_relative", 3, 15},
         {"leg_joint_velocity_scaled", 15, 27},
         {"previous_raw_dog_action", 27, 39},
         {"dog_command", 39, 44},
         {"arm_goal", 44, 50},
         {"base_roll_pitch", 50, 52},
         {"gait_clock", 52, 54}},
        "observations.dog");
    require_observation_blocks(
        require_node(observations, "arm", "observations"), 20,
        {{"arm_joint_position_relative", 0, 6},
         {"previous_raw_arm_control_action", 6, 12},
         {"arm_goal", 12, 18},
         {"base_roll_pitch", 18, 20}},
        "observations.arm");

    const YAML::Node commands = require_node(root, "commands", "root");
    const YAML::Node gait = require_node(commands, "gait", "commands");
    contract.gait_frequency_hz =
        require_scalar<double>(gait, "frequency_hz", "commands.gait");
    require_close(contract.gait_frequency_hz, 2.0,
                  "commands.gait.frequency_hz");
    require_string(gait, "standing_behavior",
                   "reset_and_freeze_phase_at_zero", "commands.gait");

    const YAML::Node actions = require_node(root, "actions", "root");
    const YAML::Node arm_action = require_node(actions, "arm", "actions");
    require_slice(arm_action, "control_slice", 0, 6, "actions.arm");
    require_slice(arm_action, "plan_slice", 6, 8, "actions.arm");
    const YAML::Node plan = require_node(arm_action, "plan", "actions.arm");
    require_string(plan, "postprocess",
                   "clip_actor_output_to_minus1_plus1_then_multiply_0_4",
                   "actions.arm.plan");
    contract.plan_scale_rad = 0.4f;

    const YAML::Node limits = require_node(
        require_node(root, "joint_limits", "root"), "entries", "joint_limits");
    if (!limits.IsSequence()) {
      throw std::runtime_error("Manifest joint_limits.entries must be a sequence");
    }
    for (const auto &entry : limits) {
      const std::string name =
          require_scalar<std::string>(entry, "name", "joint_limits.entries");
      const float lower =
          require_scalar<float>(entry, "lower", "joint_limits.entries");
      const float upper =
          require_scalar<float>(entry, "upper", "joint_limits.entries");
      if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper ||
          !contract.joint_limits_rad.emplace(name, std::make_pair(lower, upper))
               .second) {
        throw std::runtime_error("Invalid or duplicate manifest joint limit: " +
                                 name);
      }
    }
    for (const auto &name : kDogJointOrder) {
      if (contract.joint_limits_rad.count(name) == 0) {
        throw std::runtime_error("Manifest lacks joint limit for " + name);
      }
    }
    for (const auto &name : kArmJointOrder) {
      if (contract.joint_limits_rad.count(name) == 0) {
        throw std::runtime_error("Manifest lacks joint limit for " + name);
      }
    }
    return contract;
  } catch (const YAML::Exception &error) {
    throw std::runtime_error("Failed to parse Stage2 policy_manifest.yaml: " +
                             std::string(error.what()));
  }
}

LiveSiteContract LiveSiteContract::load_and_apply(
    const std::filesystem::path &requested_site_config,
    Stage2Contract &policy_contract) {
  try {
    if (!std::filesystem::is_regular_file(requested_site_config)) {
      throw std::runtime_error("Live site config not found: " +
                               requested_site_config.string());
    }
    const YAML::Node root = YAML::LoadFile(requested_site_config.string());
    require_site_string(root, "schema", "a2_piper_stage2_site", "root");
    if (require_site_scalar<int>(root, "schema_version", "root") != 1) {
      throw std::runtime_error("Unsupported live site schema_version");
    }

    const YAML::Node topology = require_site_node(root, "topology", "root");
    require_site_string(topology, "mode", "a2_direct_lowcmd", "topology");
    const YAML::Node safety = require_site_node(root, "safety", "root");
    if (!require_site_scalar<bool>(safety, "output_enabled", "safety")) {
      throw std::runtime_error(
          "Live site config requires safety.output_enabled=true");
    }

    const YAML::Node site_limits =
        require_site_node(root, "site_limits", "root");
    const auto dog_position_limits = require_site_position_limits(
        site_limits, "a2_joint_position_rad", kDogJointOrder);
    const auto arm_position_limits = require_site_position_limits(
        site_limits, "piper_joint_position_rad", kArmJointOrder);
    const auto dog_target_rates = require_site_target_rates(
        site_limits, "a2_target_rate_rad_per_policy_tick", kDogJointOrder);
    const auto arm_target_rates = require_site_target_rates(
        site_limits, "piper_target_rate_rad_per_policy_tick", kArmJointOrder);

    const YAML::Node timing = require_site_node(root, "timing", "root");
    LiveSiteContract result;
    result.site_config =
        std::filesystem::weakly_canonical(requested_site_config);
    result.inference_deadline_s = require_site_scalar<double>(
        timing, "inference_deadline_s", "timing");
    result.consecutive_deadline_miss_limit = require_site_scalar<int>(
        timing, "consecutive_deadline_miss_limit", "timing");
    if (!std::isfinite(result.inference_deadline_s) ||
        result.inference_deadline_s <= 0.0 ||
        result.consecutive_deadline_miss_limit <= 0) {
      throw std::runtime_error(
          "Live timing deadline and consecutive miss limit must be positive");
    }

    Stage2Contract effective = policy_contract;
    apply_live_actor_limits(effective.dog, effective.joint_limits_rad,
                            dog_position_limits, dog_target_rates);
    apply_live_actor_limits(effective.arm, effective.joint_limits_rad,
                            arm_position_limits, arm_target_rates);
    policy_contract = std::move(effective);
    return result;
  } catch (const YAML::Exception &error) {
    throw std::runtime_error("Failed to parse live site config '" +
                             requested_site_config.string() + "': " +
                             error.what());
  }
}

}  // namespace a2_piper_stage2_direct

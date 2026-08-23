#ifndef A2_PIPER_STAGE2_DIRECT_STAGE2_CONTRACT_HPP_
#define A2_PIPER_STAGE2_DIRECT_STAGE2_CONTRACT_HPP_

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace a2_piper_stage2_direct {

struct ActorContract {
  std::string name;
  std::filesystem::path model_path;
  std::size_t input_dim = 0;
  std::size_t output_dim = 0;
  std::size_t frame_dim = 0;
  std::size_t history_frames = 0;
  std::vector<std::string> joint_order;
  std::vector<float> default_position_rad;
  std::vector<float> max_target_delta_rad;
  float action_scale_rad = 0.0f;
  std::pair<float, float> processed_clip_rad{0.0f, 0.0f};
};

struct Stage2Contract {
  std::filesystem::path bundle_dir;
  ActorContract dog;
  ActorContract arm;
  double policy_period_s = 0.0;
  double gait_frequency_hz = 0.0;
  float plan_scale_rad = 0.0f;
  std::unordered_map<std::string, std::pair<float, float>> joint_limits_rad;

  static Stage2Contract load(const std::filesystem::path &bundle_dir);
};

struct LiveSiteContract {
  std::filesystem::path site_config;
  double inference_deadline_s = 0.0;
  int consecutive_deadline_miss_limit = 0;

  static LiveSiteContract load_and_apply(
      const std::filesystem::path &site_config, Stage2Contract &policy_contract);
};

}  // namespace a2_piper_stage2_direct

#endif  // A2_PIPER_STAGE2_DIRECT_STAGE2_CONTRACT_HPP_

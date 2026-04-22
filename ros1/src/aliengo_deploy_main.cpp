#include "aliengo_deploy/aliengo_deploy_node.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// ============================================================
// Argument parsing helpers
// ============================================================

namespace {

bool parseBool(const std::string &value, bool &out) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") {
        out = true; return true;
    }
    if (lower == "0" || lower == "false" || lower == "off" || lower == "no") {
        out = false; return true;
    }
    return false;
}

bool parseDevice(const std::string &value, InferenceDevice &out) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "cpu") { out = InferenceDevice::CPU; return true; }
    if (lower == "cuda" || lower == "gpu") { out = InferenceDevice::CUDA; return true; }
    return false;
}

struct RuntimeOptions {
    std::string policy_path = DEFAULT_ALIENGO_POLICY_PATH;
    InferenceDevice device = InferenceDevice::CPU;
    bool use_local_gamepad = false;
    float gait_frequency = aliengo::kGaitFrequencyHz;
};

RuntimeOptions parseArgs(int argc, char **argv) {
    RuntimeOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto eq = arg.find('=');
        if (eq == std::string::npos) continue;

        std::string key = arg.substr(0, eq);
        std::string val = arg.substr(eq + 1);

        if (key == "policy_path" || key == "policy") {
            opts.policy_path = val;
            std::cout << "[Info] Policy path: " << val << std::endl;
        } else if (key == "device" || key == "inference_device") {
            if (!parseDevice(val, opts.device)) {
                std::cerr << "[Warn] Invalid device: " << val << std::endl;
            } else {
                std::cout << "[Info] Inference device: "
                          << (opts.device == InferenceDevice::CUDA ? "CUDA" : "CPU")
                          << std::endl;
            }
        } else if (key == "use_local_gamepad" || key == "gamepad") {
            if (!parseBool(val, opts.use_local_gamepad)) {
                std::cerr << "[Warn] Invalid gamepad value: " << val << std::endl;
            }
        } else if (key == "gait_frequency" || key == "gait_freq") {
            try {
                opts.gait_frequency = std::stof(val);
                std::cout << "[Info] Gait frequency: " << opts.gait_frequency
                          << " Hz" << std::endl;
            } catch (...) {
                std::cerr << "[Warn] Invalid gait_frequency: " << val << std::endl;
            }
        }
    }

    return opts;
}

} // namespace

// ============================================================
// Main
// ============================================================

int main(int argc, char **argv) {
    ros::init(argc, argv, "aliengo_deploy");
    ros::NodeHandle nh;

    // Parse command-line arguments
    RuntimeOptions opts = parseArgs(argc, argv);

    // Also read from ROS parameter server (launch file params override)
    std::string param_policy_path;
    if (nh.getParam("policy_path", param_policy_path)) {
        opts.policy_path = param_policy_path;
        std::cout << "[Info] Policy path from param: " << param_policy_path
                  << std::endl;
    }

    bool param_gamepad = false;
    if (nh.getParam("use_local_gamepad", param_gamepad)) {
        opts.use_local_gamepad = param_gamepad;
    }

    double param_gait_freq = 0.0;
    if (nh.getParam("gait_frequency", param_gait_freq) && param_gait_freq > 0.0) {
        opts.gait_frequency = static_cast<float>(param_gait_freq);
    }

    std::string param_device;
    if (nh.getParam("inference_device", param_device)) {
        parseDevice(param_device, opts.device);
    }

    // Build policy spec
    std::vector<PolicySpec> policy_list;
    policy_list.push_back(PolicySpec::MLP(opts.policy_path, "aliengo_locomotion"));

    // Create node
    std::cout << "\n============================================" << std::endl;
    std::cout << "  Aliengo ROS1 RL Policy Deployment" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "  Policy path:      " << opts.policy_path << std::endl;
    std::cout << "  Inference device:  "
              << (opts.device == InferenceDevice::CUDA ? "CUDA" : "CPU")
              << std::endl;
    std::cout << "  Gait frequency:   " << opts.gait_frequency << " Hz" << std::endl;
    std::cout << "  Local gamepad:    "
              << (opts.use_local_gamepad ? "enabled" : "disabled") << std::endl;
    std::cout << "  Control freq:     " << (1.0f / aliengo::kControlDt) << " Hz"
              << std::endl;
    std::cout << "  Obs total dim:    " << aliengo::kObsTotalDim << std::endl;
    std::cout << "  Action dim:       " << aliengo::kActionDim << std::endl;
    std::cout << "============================================\n" << std::endl;

    AliengoDeployNode node(nh, policy_list, opts.device);

    // Initialize ManagerBasedEnv (loads model, tests inference)
    node.init_manager();

    // Optional gamepad
    node.initGamepad(opts.use_local_gamepad);

    // Start control loop
    node.start();

    // Spin
    ros::spin();

    return 0;
}

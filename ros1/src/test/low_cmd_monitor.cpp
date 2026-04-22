/**
 * low_cmd_monitor.cpp
 *
 * 订阅 low_cmd 话题，验证并打印 aliengo_deploy 节点的输出。
 *
 * 功能：
 *   - 实时显示 12 个电机的 [q, Kp, Kd] 值
 *   - 检测 NaN / Inf
 *   - 检测关节位置超限
 *   - 统计发布频率
 *   - 检查关节映射是否合理（对称性检查）
 */

#include <ros/ros.h>
#include <unitree_legged_msgs/LowCmd.h>

#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

static constexpr int kNumJoints = 12;

// Joint limits (generous, for sanity check)
static constexpr float kJointPosMin = -3.5f;
static constexpr float kJointPosMax = 3.5f;
static constexpr float kKpMax = 500.0f;
static constexpr float kKdMax = 50.0f;

// SDK joint names
static const char *kJointNames[kNumJoints] = {
    "FR_hip ", "FR_thigh", "FR_calf",
    "FL_hip ", "FL_thigh", "FL_calf",
    "RR_hip ", "RR_thigh", "RR_calf",
    "RL_hip ", "RL_thigh", "RL_calf",
};

// Statistics
static size_t g_msg_count = 0;
static size_t g_nan_count = 0;
static size_t g_limit_count = 0;
static auto g_start_time = std::chrono::steady_clock::now();
static auto g_last_print_time = std::chrono::steady_clock::now();

bool isFiniteFloat(float v) {
    return std::isfinite(v);
}

void lowCmdCallback(const unitree_legged_msgs::LowCmd::ConstPtr &msg) {
    ++g_msg_count;
    bool has_nan = false;
    bool has_limit = false;

    // Validate all 12 motors
    for (int i = 0; i < kNumJoints; ++i) {
        float q = msg->motorCmd[i].q;
        float kp = msg->motorCmd[i].Kp;
        float kd = msg->motorCmd[i].Kd;
        float dq = msg->motorCmd[i].dq;
        float tau = msg->motorCmd[i].tau;

        if (!isFiniteFloat(q) || !isFiniteFloat(kp) || !isFiniteFloat(kd) ||
            !isFiniteFloat(dq) || !isFiniteFloat(tau)) {
            has_nan = true;
            ROS_ERROR("NaN/Inf detected in motor %d (%s): q=%.3f kp=%.3f kd=%.3f",
                      i, kJointNames[i], q, kp, kd);
        }

        // Only check limits when Kp > 0 (active position control)
        if (kp > 0.1f) {
            if (q < kJointPosMin || q > kJointPosMax) {
                has_limit = true;
                ROS_WARN_THROTTLE(1.0,
                    "Joint limit warning: motor %d (%s) q=%.3f outside [%.1f, %.1f]",
                    i, kJointNames[i], q, kJointPosMin, kJointPosMax);
            }
            if (kp > kKpMax) {
                ROS_WARN_THROTTLE(1.0,
                    "Excessive Kp: motor %d (%s) Kp=%.1f > %.1f",
                    i, kJointNames[i], kp, kKpMax);
            }
        }
    }

    if (has_nan) ++g_nan_count;
    if (has_limit) ++g_limit_count;

    // Periodic display (every ~1 second)
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - g_last_print_time).count();
    if (elapsed >= 1.0) {
        double total_elapsed = std::chrono::duration<double>(now - g_start_time).count();
        double freq = g_msg_count / total_elapsed;

        std::cout << "\033[2J\033[H";  // Clear screen
        std::cout << "=== Low Cmd Monitor ===" << std::endl;
        std::cout << "Messages: " << g_msg_count
                  << "  Freq: " << std::fixed << std::setprecision(1) << freq << " Hz"
                  << "  NaN: " << g_nan_count
                  << "  Limit: " << g_limit_count << std::endl;
        std::cout << std::endl;

        // Header
        std::cout << std::left
                  << std::setw(12) << "Motor"
                  << std::setw(10) << "q(rad)"
                  << std::setw(10) << "dq"
                  << std::setw(8) << "Kp"
                  << std::setw(8) << "Kd"
                  << std::setw(8) << "tau"
                  << std::setw(8) << "mode"
                  << std::endl;
        std::cout << std::string(64, '-') << std::endl;

        for (int i = 0; i < kNumJoints; ++i) {
            float q = msg->motorCmd[i].q;
            float dq = msg->motorCmd[i].dq;
            float kp = msg->motorCmd[i].Kp;
            float kd = msg->motorCmd[i].Kd;
            float tau = msg->motorCmd[i].tau;
            int mode = msg->motorCmd[i].mode;

            // Mark active motors
            bool active = (kp > 0.1f && std::abs(q) < 100.0f);
            std::string marker = active ? " *" : "  ";

            std::cout << std::left
                      << std::setw(12) << (std::string(kJointNames[i]) + marker)
                      << std::setw(10) << std::fixed << std::setprecision(3) << q
                      << std::setw(10) << std::setprecision(3) << dq
                      << std::setw(8) << std::setprecision(1) << kp
                      << std::setw(8) << std::setprecision(2) << kd
                      << std::setw(8) << std::setprecision(3) << tau
                      << std::setw(8) << mode
                      << std::endl;
        }

        // Symmetry check: compare FR vs FL
        if (msg->motorCmd[0].Kp > 0.1f && msg->motorCmd[3].Kp > 0.1f) {
            std::cout << std::endl << "Symmetry (FR vs FL hip): "
                      << "FR=" << std::setprecision(3) << msg->motorCmd[0].q
                      << "  FL=" << msg->motorCmd[3].q
                      << "  diff=" << std::abs(msg->motorCmd[0].q - msg->motorCmd[3].q)
                      << std::endl;
        }

        std::cout << std::endl
                  << "(* = active position control)" << std::endl;

        g_last_print_time = now;
    }
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "low_cmd_monitor");
    ros::NodeHandle nh;

    ros::Subscriber sub = nh.subscribe("low_cmd", 10, lowCmdCallback);

    ROS_INFO("Low cmd monitor started. Waiting for messages on /low_cmd...");

    ros::spin();
    return 0;
}

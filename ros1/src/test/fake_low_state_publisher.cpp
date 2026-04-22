/**
 * fake_low_state_publisher.cpp
 *
 * 模拟 Aliengo 机器人，以 500Hz 发布合成的 LowState 消息。
 * 替代 ros_udp bridge，用于在没有实机的情况下测试 aliengo_deploy 节点。
 *
 * 功能：
 *   - 合成 IMU 四元数/角速度（可选正弦扰动）
 *   - 合成 12 关节位置/速度（初始为默认站姿）
 *   - 通过键盘注入遥控器按键（A/B/Start/Select/L2+B）
 *   - 订阅 low_cmd 并用其更新关节状态（简单积分模拟）
 *
 * 键盘控制：
 *   a - 模拟遥控器 A 键（使能策略）
 *   b - 模拟遥控器 B 键（停止）
 *   e - 模拟 L2+B（紧急制动）
 *   s - 模拟 Start（清零速度指令）
 *   r - 模拟 Select（重置策略）
 *   w/x - 增加/减少 vx 命令（通过摇杆 ly）
 *   d/c - 增加/减少 wz 命令（通过摇杆 rx）
 *   q - 退出
 */

#include <ros/ros.h>
#include <unitree_legged_msgs/LowCmd.h>
#include <unitree_legged_msgs/LowState.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <termios.h>
#include <thread>
#include <unistd.h>

// ============================================================
// Aliengo constants (duplicated here to avoid header dep)
// ============================================================
static constexpr int kNumJoints = 12;

// SDK motor order default positions
// SDK: FR_hip(0) FR_thigh(1) FR_calf(2) FL_hip(3) FL_thigh(4) FL_calf(5)
//      RR_hip(6) RR_thigh(7) RR_calf(8) RL_hip(9) RL_thigh(10) RL_calf(11)
static const float kDefaultJointPosSDK[kNumJoints] = {
    -0.1f, 0.5f, -1.0f,   // FR
     0.1f, 0.5f, -1.0f,   // FL
    -0.1f, 0.5f, -1.0f,   // RR
     0.1f, 0.5f, -1.0f,   // RL
};

// Wireless remote key masks
static constexpr uint16_t kKeyA      = 1u << 8;
static constexpr uint16_t kKeyB      = 1u << 9;
static constexpr uint16_t kKeyStart  = 1u << 2;
static constexpr uint16_t kKeySelect = 1u << 3;
static constexpr uint16_t kKeyL2     = 1u << 5;

// ============================================================
// Global state
// ============================================================
static std::mutex g_state_mutex;
static std::array<float, kNumJoints> g_joint_pos;
static std::array<float, kNumJoints> g_joint_vel;
static std::atomic<uint16_t> g_keys{0};
static std::atomic<float> g_stick_ly{0.0f};   // forward cmd
static std::atomic<float> g_stick_rx{0.0f};   // yaw cmd
static std::atomic<bool> g_running{true};

// ============================================================
// low_cmd callback — simple joint state update
// ============================================================
void lowCmdCallback(const unitree_legged_msgs::LowCmd::ConstPtr &msg) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    for (int i = 0; i < kNumJoints; ++i) {
        float q_target = msg->motorCmd[i].q;
        float kp = msg->motorCmd[i].Kp;
        // Simple model: if Kp > 0, move towards target
        if (kp > 0.1f && std::abs(q_target) < 100.0f) {
            // Exponential smoothing towards target
            float alpha = std::min(1.0f, kp * 0.02f / 60.0f);
            g_joint_pos[i] += alpha * (q_target - g_joint_pos[i]);
            g_joint_vel[i] = alpha * (q_target - g_joint_pos[i]) / 0.02f;
        } else {
            // Zero command or PosStop — hold position, decay velocity
            g_joint_vel[i] *= 0.95f;
        }
    }
}

// ============================================================
// Fill wirelessRemote bytes
// ============================================================
void fillWirelessRemote(uint8_t data[40], uint16_t keys, float lx, float ly,
                        float rx, float ry) {
    std::memset(data, 0, 40);
    // head
    data[0] = 0xFE;
    data[1] = 0xEF;
    // keys: bytes 2-3
    std::memcpy(&data[2], &keys, sizeof(uint16_t));
    // lx: bytes 4-7
    std::memcpy(&data[4], &lx, sizeof(float));
    // rx: bytes 8-11
    std::memcpy(&data[8], &rx, sizeof(float));
    // ry: bytes 12-15
    std::memcpy(&data[12], &ry, sizeof(float));
    // L2: bytes 16-19
    float l2 = (keys & kKeyL2) ? 1.0f : 0.0f;
    std::memcpy(&data[16], &l2, sizeof(float));
    // ly: bytes 20-23
    std::memcpy(&data[20], &ly, sizeof(float));
}

// ============================================================
// Non-blocking keyboard input thread
// ============================================================
void keyboardThread() {
    // Set terminal to raw mode
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    newt.c_cc[VMIN] = 0;
    newt.c_cc[VTIME] = 1;  // 100ms timeout
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::cout << "\n=== Fake Low State Publisher - Keyboard Controls ===\n"
              << "  a = Enable policy (A button)\n"
              << "  b = Stop (B button)\n"
              << "  e = Emergency stop (L2+B)\n"
              << "  s = Clear command (Start)\n"
              << "  r = Reset policy (Select)\n"
              << "  w/x = Increase/Decrease forward speed (ly)\n"
              << "  d/c = Increase/Decrease yaw speed (rx)\n"
              << "  0 = Zero all sticks\n"
              << "  q = Quit\n"
              << "====================================================\n\n";

    while (g_running.load()) {
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            switch (c) {
            case 'a':
                g_keys.store(kKeyA, std::memory_order_relaxed);
                std::cout << "[Key] A (enable)\n";
                break;
            case 'b':
                g_keys.store(kKeyB, std::memory_order_relaxed);
                std::cout << "[Key] B (stop)\n";
                break;
            case 'e':
                g_keys.store(kKeyL2 | kKeyB, std::memory_order_relaxed);
                std::cout << "[Key] L2+B (emergency)\n";
                break;
            case 's':
                g_keys.store(kKeyStart, std::memory_order_relaxed);
                std::cout << "[Key] Start (clear cmd)\n";
                break;
            case 'r':
                g_keys.store(kKeySelect, std::memory_order_relaxed);
                std::cout << "[Key] Select (reset)\n";
                break;
            case 'w': {
                float ly = g_stick_ly.load() + 0.1f;
                ly = std::min(ly, 1.0f);
                g_stick_ly.store(ly);
                std::cout << "[Stick] ly=" << ly << " (forward)\n";
            } break;
            case 'x': {
                float ly = g_stick_ly.load() - 0.1f;
                ly = std::max(ly, -1.0f);
                g_stick_ly.store(ly);
                std::cout << "[Stick] ly=" << ly << " (backward)\n";
            } break;
            case 'd': {
                float rx = g_stick_rx.load() + 0.1f;
                rx = std::min(rx, 1.0f);
                g_stick_rx.store(rx);
                std::cout << "[Stick] rx=" << rx << " (yaw right)\n";
            } break;
            case 'c': {
                float rx = g_stick_rx.load() - 0.1f;
                rx = std::max(rx, -1.0f);
                g_stick_rx.store(rx);
                std::cout << "[Stick] rx=" << rx << " (yaw left)\n";
            } break;
            case '0':
                g_stick_ly.store(0.0f);
                g_stick_rx.store(0.0f);
                std::cout << "[Stick] zeroed\n";
                break;
            case 'q':
                g_running.store(false);
                std::cout << "[Quit]\n";
                break;
            default:
                // Release all keys on any other input
                break;
            }
        } else {
            // No key pressed — release all buttons (edge detection needs release)
            g_keys.store(0, std::memory_order_relaxed);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

// ============================================================
// Main
// ============================================================
int main(int argc, char **argv) {
    ros::init(argc, argv, "fake_low_state_publisher");
    ros::NodeHandle nh;

    // Initialize joint positions to default stand pose
    for (int i = 0; i < kNumJoints; ++i) {
        g_joint_pos[i] = kDefaultJointPosSDK[i];
        g_joint_vel[i] = 0.0f;
    }

    // Publisher
    ros::Publisher pub = nh.advertise<unitree_legged_msgs::LowState>("low_state", 1);

    // Subscribe to low_cmd for feedback
    ros::Subscriber sub = nh.subscribe("low_cmd", 1, lowCmdCallback);

    // Start keyboard thread
    std::thread kb_thread(keyboardThread);

    // Publish at 500 Hz (matching ros_udp bridge rate)
    ros::Rate rate(500);
    uint32_t tick = 0;

    ROS_INFO("Fake low_state publisher started at 500 Hz.");
    ROS_INFO("Press keys in this terminal to simulate controller.");

    while (ros::ok() && g_running.load()) {
        unitree_legged_msgs::LowState state;

        // Head
        state.head[0] = 0xFE;
        state.head[1] = 0xEF;
        state.levelFlag = 0xFF;
        state.tick = tick++;

        // IMU: standing upright quaternion [w,x,y,z] = [1,0,0,0]
        // Add tiny sinusoidal perturbation to test projected_gravity
        float t = tick * 0.002f;  // time in seconds
        float pitch_perturb = 0.02f * std::sin(0.5f * t);  // ~1 degree
        state.imu.quaternion[0] = std::cos(pitch_perturb / 2.0f);
        state.imu.quaternion[1] = 0.0f;
        state.imu.quaternion[2] = std::sin(pitch_perturb / 2.0f);
        state.imu.quaternion[3] = 0.0f;

        // Gyroscope: small noise
        state.imu.gyroscope[0] = 0.01f * std::sin(1.3f * t);
        state.imu.gyroscope[1] = 0.01f * std::cos(0.7f * t);
        state.imu.gyroscope[2] = 0.005f * std::sin(2.1f * t);

        // Accelerometer
        state.imu.accelerometer[0] = 0.0f;
        state.imu.accelerometer[1] = 0.0f;
        state.imu.accelerometer[2] = -9.81f;

        // RPY
        state.imu.rpy[0] = 0.0f;
        state.imu.rpy[1] = pitch_perturb;
        state.imu.rpy[2] = 0.0f;

        // Motor states
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            for (int i = 0; i < kNumJoints; ++i) {
                state.motorState[i].mode = 0x0A;
                state.motorState[i].q = g_joint_pos[i];
                state.motorState[i].dq = g_joint_vel[i];
                state.motorState[i].ddq = 0.0f;
                state.motorState[i].tauEst = 0.0f;
                state.motorState[i].q_raw = g_joint_pos[i];
                state.motorState[i].dq_raw = g_joint_vel[i];
                state.motorState[i].temperature = 30;
            }
        }
        // Remaining motors 12-19: zero
        for (int i = kNumJoints; i < 20; ++i) {
            state.motorState[i].q = 0.0f;
            state.motorState[i].dq = 0.0f;
        }

        // Foot force sensors
        for (int i = 0; i < 4; ++i) {
            state.footForce[i] = 50;     // ~50N per foot
            state.footForceEst[i] = 50;
        }

        // Wireless remote
        uint16_t keys = g_keys.load(std::memory_order_relaxed);
        float ly = g_stick_ly.load(std::memory_order_relaxed);
        float rx = g_stick_rx.load(std::memory_order_relaxed);
        fillWirelessRemote(state.wirelessRemote.data(), keys, 0.0f, ly, rx, 0.0f);

        pub.publish(state);
        ros::spinOnce();
        rate.sleep();
    }

    g_running.store(false);
    if (kb_thread.joinable()) kb_thread.join();

    ROS_INFO("Fake low_state publisher shut down.");
    return 0;
}

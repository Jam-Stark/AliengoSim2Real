#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace aliengo {

// ============================================================
// AliengoUdpTransport — Direct UDP communication with
// Aliengo v3.0.0 motion controller.
//
// Bypasses ros_udp and the Unitree SDK .so entirely.
// Uses raw POSIX sockets + manual packet packing/unpacking
// based on the experimentally verified wire format:
//   - LowState: 820 bytes, motor struct = 32 bytes
//   - LowCmd:   730 bytes, motor struct = 33 bytes
// ============================================================

// Wire format constants (Aliengo v3.0.0)
constexpr int kWireHeaderSize        = 10;   // levelFlag+commVersion+robotID+SN+bandWidth
constexpr int kWireImuSize           = 53;   // quat[4]+gyro[3]+acc[3]+rpy[3]+temp
constexpr int kWireMotorStateSize    = 32;   // on-wire motor state block
constexpr int kWireMotorCmdSize      = 33;   // mode+q+dq+tau+Kp+Kd+reserve[3]
constexpr int kWireLedSize           = 3;
constexpr int kWireLowStateSize      = 820;  // confirmed empirically
constexpr int kWireLowCmdSize        = 730;  // header(10)+motorCmd(20*33)+led(4*3)+wireless(40)+reserve(4)+crc(4)
constexpr int kWireMotorStateOffset  = kWireHeaderSize + kWireImuSize;  // 63
constexpr int kWireTailOffset        = kWireMotorStateOffset + 20 * kWireMotorStateSize;  // 703
constexpr int kWireWirelessOffset    = 206;  // footForce(8)+footForceEst(8)+tick(4) = 20

/// Parsed robot state from UDP LowState packet
struct RobotState {
    float imu_quaternion[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float imu_gyroscope[3]  = {};
    float imu_accelerometer[3] = {};
    float imu_rpy[3] = {};
    int8_t imu_temperature = 0;

    struct Motor {
        uint8_t mode = 0;
        float q  = 0.0f;
        float dq = 0.0f;
    };
    Motor motors[20] = {};

    int16_t footForce[4] = {};
    uint8_t wirelessRemote[40] = {};
    uint32_t tick = 0;
    bool valid = false;
};

/// Motor command for a single joint
struct MotorCommand {
    uint8_t mode = 0x0A;
    float q   = 2.146e9f;   // PosStopF
    float dq  = 16000.0f;   // VelStopF
    float tau = 0.0f;
    float Kp  = 0.0f;
    float Kd  = 0.0f;
};

class AliengoUdpTransport {
public:
    /// @param target_ip   Aliengo motion controller IP (default: 192.168.123.10)
    /// @param target_port UDP target port (default: 8007)
    /// @param local_port  Local UDP bind port (default: 8091)
    AliengoUdpTransport(const std::string &target_ip = "192.168.123.10",
                        int target_port = 8007,
                        int local_port = 8091);
    ~AliengoUdpTransport();

    /// Start background send/recv threads
    void start();
    /// Stop background threads and close socket
    void stop();

    /// Thread-safe read of latest robot state
    RobotState getState() const;

    /// Thread-safe write of motor commands (20 motors)
    void setCommand(const MotorCommand cmd[20]);

    /// Convenience: set all motors to safe zero-torque mode
    void setZeroCommand();

    /// Has received valid state at least once?
    bool hasReceivedState() const {
        return has_received_.load(std::memory_order_relaxed);
    }

    /// Recv count (for monitoring)
    uint64_t recvCount() const {
        return recv_count_.load(std::memory_order_relaxed);
    }

private:
    void sendLoop();
    void recvLoop();

    void buildCmdPacket(uint8_t *buf, int len) const;
    void parseStatePacket(const uint8_t *buf, int len);

    std::string target_ip_;
    int target_port_;
    int local_port_;
    int sock_fd_ = -1;

    mutable std::mutex state_mutex_;
    RobotState latest_state_;

    mutable std::mutex cmd_mutex_;
    MotorCommand motor_cmds_[20];

    std::atomic<bool> running_{false};
    std::atomic<bool> has_received_{false};
    std::atomic<uint64_t> recv_count_{0};

    std::thread send_thread_;
    std::thread recv_thread_;
};

} // namespace aliengo

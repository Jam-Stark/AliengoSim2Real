#include "aliengo_deploy/aliengo_udp_transport.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aliengo {

// ============================================================
// Helpers: read/write little-endian primitives from raw bytes
// ============================================================

static inline float readFloatLE(const uint8_t *p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

static inline void writeFloatLE(uint8_t *p, float v) {
    std::memcpy(p, &v, 4);
}

static inline int16_t readInt16LE(const uint8_t *p) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

static inline uint32_t readUint32LE(const uint8_t *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// ============================================================
// Construction / Destruction
// ============================================================

AliengoUdpTransport::AliengoUdpTransport(
    const std::string &target_ip, int target_port, int local_port)
    : target_ip_(target_ip),
      target_port_(target_port),
      local_port_(local_port) {
    setZeroCommand();
}

AliengoUdpTransport::~AliengoUdpTransport() {
    stop();
}

// ============================================================
// Start / Stop
// ============================================================

void AliengoUdpTransport::start() {
    if (running_.load()) return;

    // Create UDP socket
    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_ < 0) {
        std::cerr << "[UdpTransport] Failed to create socket: "
                  << strerror(errno) << std::endl;
        return;
    }

    // Set recv timeout (100ms)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Bind to local port
    struct sockaddr_in local_addr;
    std::memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(static_cast<uint16_t>(local_port_));

    if (bind(sock_fd_, reinterpret_cast<struct sockaddr *>(&local_addr),
             sizeof(local_addr)) < 0) {
        std::cerr << "[UdpTransport] Failed to bind port " << local_port_
                  << ": " << strerror(errno) << std::endl;
        close(sock_fd_);
        sock_fd_ = -1;
        return;
    }

    running_.store(true);
    send_thread_ = std::thread(&AliengoUdpTransport::sendLoop, this);
    recv_thread_ = std::thread(&AliengoUdpTransport::recvLoop, this);

    std::cout << "[UdpTransport] Started. Target: " << target_ip_
              << ":" << target_port_
              << ", Local port: " << local_port_ << std::endl;
}

void AliengoUdpTransport::stop() {
    if (!running_.load()) return;
    running_.store(false);

    if (send_thread_.joinable()) send_thread_.join();
    if (recv_thread_.joinable()) recv_thread_.join();

    if (sock_fd_ >= 0) {
        close(sock_fd_);
        sock_fd_ = -1;
    }

    std::cout << "[UdpTransport] Stopped. Total recv: "
              << recv_count_.load() << std::endl;
}

// ============================================================
// Thread-safe state / command access
// ============================================================

RobotState AliengoUdpTransport::getState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return latest_state_;
}

void AliengoUdpTransport::setCommand(const MotorCommand cmd[20]) {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    std::memcpy(motor_cmds_, cmd, sizeof(MotorCommand) * 20);
}

void AliengoUdpTransport::setZeroCommand() {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    for (int i = 0; i < 20; ++i) {
        motor_cmds_[i].mode = 0x0A;
        motor_cmds_[i].q = 2.146e9f;     // PosStopF
        motor_cmds_[i].dq = 16000.0f;    // VelStopF
        motor_cmds_[i].tau = 0.0f;
        motor_cmds_[i].Kp = 0.0f;
        motor_cmds_[i].Kd = 0.0f;
    }
}

// ============================================================
// Send Thread (500 Hz)
// ============================================================

void AliengoUdpTransport::sendLoop() {
    struct sockaddr_in target_addr;
    std::memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(static_cast<uint16_t>(target_port_));
    inet_pton(AF_INET, target_ip_.c_str(), &target_addr.sin_addr);

    uint8_t buf[kWireLowCmdSize];

    while (running_.load()) {
        buildCmdPacket(buf, kWireLowCmdSize);
        sendto(sock_fd_, buf, kWireLowCmdSize, 0,
               reinterpret_cast<struct sockaddr *>(&target_addr),
               sizeof(target_addr));

        // 500 Hz
        std::this_thread::sleep_for(std::chrono::microseconds(2000));
    }
}

// ============================================================
// Recv Thread
// ============================================================

void AliengoUdpTransport::recvLoop() {
    uint8_t buf[2048];

    while (running_.load()) {
        ssize_t n = recv(sock_fd_, buf, sizeof(buf), 0);
        if (n > 0) {
            parseStatePacket(buf, static_cast<int>(n));
            recv_count_.fetch_add(1, std::memory_order_relaxed);
            if (!has_received_.load(std::memory_order_relaxed)) {
                has_received_.store(true, std::memory_order_relaxed);
                std::cout << "[UdpTransport] First state received ("
                          << n << " bytes)." << std::endl;
            }
        }
    }
}

// ============================================================
// Build LowCmd packet (730 bytes, Aliengo v3.0.0 wire format)
// ============================================================

void AliengoUdpTransport::buildCmdPacket(uint8_t *buf, int len) const {
    std::memset(buf, 0, len);

    // Header (10 bytes)
    buf[0] = 0xFF;  // levelFlag = LOWLEVEL
    // commVersion(2), robotID(2), SN(4), bandWidth(1) = 0

    // MotorCmd[20], each 33 bytes starting at offset 10
    int off = kWireHeaderSize;
    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        for (int i = 0; i < 20; ++i) {
            buf[off] = motor_cmds_[i].mode;
            writeFloatLE(&buf[off + 1], motor_cmds_[i].q);
            writeFloatLE(&buf[off + 5], motor_cmds_[i].dq);
            writeFloatLE(&buf[off + 9], motor_cmds_[i].tau);
            writeFloatLE(&buf[off + 13], motor_cmds_[i].Kp);
            writeFloatLE(&buf[off + 17], motor_cmds_[i].Kd);
            // reserve[3] = 0 (already zeroed)
            off += kWireMotorCmdSize;
        }
    }
    // LED[4] (12 bytes) + wirelessRemote[40] + reserve(4) + crc(4) = 0
}

// ============================================================
// Parse LowState packet (820 bytes, Aliengo v3.0.0 wire format)
// ============================================================

void AliengoUdpTransport::parseStatePacket(const uint8_t *buf, int len) {
    if (len < kWireMotorStateOffset) return;  // at least header + IMU

    RobotState state;
    state.valid = true;

    // --- IMU (at offset 10, size 53) ---
    int off = kWireHeaderSize;
    for (int i = 0; i < 4; ++i)
        state.imu_quaternion[i] = readFloatLE(&buf[off + i * 4]);
    off += 16;
    for (int i = 0; i < 3; ++i)
        state.imu_gyroscope[i] = readFloatLE(&buf[off + i * 4]);
    off += 12;
    for (int i = 0; i < 3; ++i)
        state.imu_accelerometer[i] = readFloatLE(&buf[off + i * 4]);
    off += 12;
    for (int i = 0; i < 3; ++i)
        state.imu_rpy[i] = readFloatLE(&buf[off + i * 4]);
    off += 12;
    state.imu_temperature = static_cast<int8_t>(buf[off]);

    // --- Motors (at offset 63, each 32 bytes) ---
    for (int i = 0; i < 20; ++i) {
        int moff = kWireMotorStateOffset + i * kWireMotorStateSize;
        if (moff + 9 > len) break;
        state.motors[i].mode = buf[moff];
        state.motors[i].q = readFloatLE(&buf[moff + 1]);
        state.motors[i].dq = readFloatLE(&buf[moff + 5]);
    }

    // --- Tail (at offset 703) ---
    if (kWireTailOffset + 20 <= len) {
        int toff = kWireTailOffset;
        for (int i = 0; i < 4; ++i)
            state.footForce[i] = readInt16LE(&buf[toff + i * 2]);
        // footForceEst at +8, tick at +16
        state.tick = readUint32LE(&buf[toff + 16]);
    }

    // --- WirelessRemote (at absolute offset 206, confirmed by byte diff) ---
    if (kWireWirelessOffset + 40 <= len) {
        std::memcpy(state.wirelessRemote, &buf[kWireWirelessOffset], 40);
    }

    // Store
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        latest_state_ = state;
    }
}

} // namespace aliengo

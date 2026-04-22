#include "aliengo_deploy/aliengo_deploy_node.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace aliengo;

// ============================================================
// Construction / Destruction
// ============================================================

AliengoDeployNode::AliengoDeployNode(
    ros::NodeHandle &nh,
    const std::vector<PolicySpec> &policy_specs,
    InferenceDevice device)
    : ManagerBasedEnv(policy_specs, device),
      nh_(nh),
      gait_clock_(kGaitFrequencyHz, kControlDt) {
    gravity_ = SimpleTensor::wrap({0.0f, 0.0f, -1.0f});
    initInterfaces();
}

AliengoDeployNode::~AliengoDeployNode() {
    if (pad_) {
        pad_->unreadGamePad();
    }
}

// ============================================================
// ROS Interface Initialization
// ============================================================

void AliengoDeployNode::initInterfaces() {
    initLowCmd();

    // Publisher: low_cmd (to ros_udp bridge)
    low_cmd_pub_ = nh_.advertise<unitree_legged_msgs::LowCmd>("low_cmd", 1);

    // Subscriber: low_state (from ros_udp bridge)
    low_state_sub_ = nh_.subscribe("low_state", 1,
                                   &AliengoDeployNode::lowStateCallback, this);
}

void AliengoDeployNode::initLowCmd() {
    low_cmd_.head[0] = 0xFE;
    low_cmd_.head[1] = 0xEF;
    low_cmd_.levelFlag = 0xFF;  // LOWLEVEL

    for (int i = 0; i < 20; ++i) {
        low_cmd_.motorCmd[i].mode = kMotorModeServo;
        low_cmd_.motorCmd[i].q = kPosStopF;
        low_cmd_.motorCmd[i].Kp = 0;
        low_cmd_.motorCmd[i].dq = kVelStopF;
        low_cmd_.motorCmd[i].Kd = 0;
        low_cmd_.motorCmd[i].tau = 0;
    }
}

// ============================================================
// Start Control Loop
// ============================================================

void AliengoDeployNode::start() {
    if (timer_started_) return;

    reset_observation_buffers();
    reset_policy_states();
    gait_clock_.reset();

    control_timer_ = nh_.createTimer(
        ros::Duration(kControlDt),
        &AliengoDeployNode::controlLoop, this);
    timer_started_ = true;

    ROS_INFO("Aliengo deploy node started. Control freq: %.0f Hz. "
             "Waiting for low_state...", 1.0f / kControlDt);
    ROS_INFO("Press A on remote to enable policy. Press B to stop.");
}

// ============================================================
// Low State Callback
// ============================================================

void AliengoDeployNode::lowStateCallback(
    const unitree_legged_msgs::LowState::ConstPtr &msg) {
    {
        std::lock_guard<std::mutex> lock(low_state_mutex_);
        low_state_ = *msg;
    }

    bool was_first = !has_low_state_.load(std::memory_order_relaxed);
    has_low_state_.store(true, std::memory_order_relaxed);
    if (was_first) {
        ROS_INFO("First low_state received.");
    }

    // Decode wireless remote from LowState
    aliengo::WirelessRemoteState remote_state;
    {
        std::lock_guard<std::mutex> lock(low_state_mutex_);
        remote_state = remote_decoder_.decode(low_state_.wirelessRemote.data());
    }

    // Process buttons (edge detection)
    processRemoteButtons(remote_state);

    // Update command from sticks
    setCommandFromRemote(remote_state.lx, remote_state.ly, remote_state.rx);

    prev_remote_state_ = remote_state;
}

// ============================================================
// Main Control Loop (50 Hz timer callback)
// ============================================================

void AliengoDeployNode::controlLoop(const ros::TimerEvent & /*event*/) {
    if (!has_low_state_.load(std::memory_order_relaxed)) {
        zeroLowCmd();
        low_cmd_pub_.publish(low_cmd_);
        return;
    }

    // Advance gait clock every tick (even when stopped, for warm-start)
    gait_clock_.step();

    if (is_stop_.load(std::memory_order_relaxed)) {
        bool has_stop_posture = false;
        {
            std::lock_guard<std::mutex> lock(stop_mutex_);
            has_stop_posture = (stop_state_ != StopState::Idle);
        }
        if (has_stop_posture) {
            writeStopPostureCmd();
            return;
        }

        zeroLowCmd();
        low_cmd_pub_.publish(low_cmd_);
        return;
    }

    // ---- Policy inference ----
    SimpleTensor action = manager_step(0);  // single policy, id=0
    auto act = toVector<float>(action);

    if (static_cast<int>(act.size()) < kActionDim) {
        ROS_ERROR_THROTTLE(2.0,
            "Policy action dim is %zu, expected %d", act.size(), kActionDim);
        zeroLowCmd();
        low_cmd_pub_.publish(low_cmd_);
        return;
    }

    writeActionToCmd(act);
    low_cmd_pub_.publish(low_cmd_);
}

// ============================================================
// Zero / Action write
// ============================================================

void AliengoDeployNode::zeroLowCmd() {
    for (int i = 0; i < 12; ++i) {
        low_cmd_.motorCmd[i].q = kPosStopF;
        low_cmd_.motorCmd[i].dq = kVelStopF;
        low_cmd_.motorCmd[i].Kp = 0.0f;
        low_cmd_.motorCmd[i].Kd = 0.0f;
        low_cmd_.motorCmd[i].tau = 0.0f;
    }
}

void AliengoDeployNode::writeActionToCmd(const std::vector<float> &action) {
    // action is post-processed by ManagerBasedEnv::computeAction:
    //   act[i] = raw_action[i] * scale[i] + default_pos[i]
    // So act[i] is already the absolute joint position target in policy order.
    // We need to map from policy order to SDK motor order.

    for (int i = 0; i < kNumJoints; ++i) {
        int sdk_idx = kJointMap[i];
        low_cmd_.motorCmd[sdk_idx].mode = kMotorModeServo;
        low_cmd_.motorCmd[sdk_idx].q = action[i];
        low_cmd_.motorCmd[sdk_idx].dq = 0.0f;
        low_cmd_.motorCmd[sdk_idx].Kp = kKp[i];
        low_cmd_.motorCmd[sdk_idx].Kd = kKd[i];
        low_cmd_.motorCmd[sdk_idx].tau = 0.0f;
    }
}

// ============================================================
// Observation Extraction
// ============================================================

SimpleTensor AliengoDeployNode::getProjectedGravityXY() {
    std::vector<float> quat_data = {1.0f, 0.0f, 0.0f, 0.0f};
    if (has_low_state_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(low_state_mutex_);
        // Unitree IMU quaternion order: [w, x, y, z]
        quat_data[0] = low_state_.imu.quaternion[0];
        quat_data[1] = low_state_.imu.quaternion[1];
        quat_data[2] = low_state_.imu.quaternion[2];
        quat_data[3] = low_state_.imu.quaternion[3];
    }
    // Full 3D projected gravity
    SimpleTensor proj_grav = QuatRotateInverse(
        SimpleTensor::wrap(quat_data), gravity_);
    // Take only [x, y] — 2 dimensions
    auto full = toVector<float>(proj_grav);
    return SimpleTensor::wrap({full[0], full[1]});
}

SimpleTensor AliengoDeployNode::getBaseAngVel() {
    std::array<float, 3> gyro = {0.0f, 0.0f, 0.0f};
    if (has_low_state_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(low_state_mutex_);
        gyro[0] = low_state_.imu.gyroscope[0];
        gyro[1] = low_state_.imu.gyroscope[1];
        gyro[2] = low_state_.imu.gyroscope[2];
    }
    return SimpleTensor::wrap({gyro[0], gyro[1], gyro[2]});
}

SimpleTensor AliengoDeployNode::getJointPos() {
    std::vector<float> pos(kNumJoints, 0.0f);
    if (has_low_state_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(low_state_mutex_);
        for (int i = 0; i < kNumJoints; ++i) {
            int sdk_idx = kJointMap[i];
            pos[i] = low_state_.motorState[sdk_idx].q - kDefaultJointPos[i];
        }
    }
    return SimpleTensor::wrap(pos);
}

SimpleTensor AliengoDeployNode::getJointVel() {
    std::vector<float> vel(kNumJoints, 0.0f);
    if (has_low_state_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(low_state_mutex_);
        for (int i = 0; i < kNumJoints; ++i) {
            int sdk_idx = kJointMap[i];
            vel[i] = low_state_.motorState[sdk_idx].dq - kDefaultJointVel[i];
        }
    }
    return SimpleTensor::wrap(vel);
}

SimpleTensor AliengoDeployNode::getCommand() {
    auto cmd = getCommandVector();
    applyCommandDeadzone(cmd);
    // Apply observation-level command scales
    return SimpleTensor::wrap({
        cmd[0] * kCmdScaleVx,
        cmd[1] * kCmdScaleVy,
        cmd[2] * kCmdScaleWz
    });
}

SimpleTensor AliengoDeployNode::getGaitClockObs() {
    return SimpleTensor::wrap({
        gait_clock_.sin_phase(),
        gait_clock_.cos_phase()
    });
}

// ============================================================
// Observation Term Factories
// ============================================================

std::shared_ptr<ObservationTerm>
AliengoDeployNode::makeGravityTerm(int history) {
    auto term = std::make_shared<ObservationTerm>("projected_gravity_xy", history);
    term->func = [this]() { return getProjectedGravityXY(); };
    // scale = 1.0 (default, no extra scaling)
    return term;
}

std::shared_ptr<ObservationTerm>
AliengoDeployNode::makeAngVelTerm(int history) {
    auto term = std::make_shared<ObservationTerm>("base_ang_vel", history);
    term->func = [this]() { return getBaseAngVel(); };
    term->scale = kObsScaleAngVel;  // 0.25
    return term;
}

std::shared_ptr<ObservationTerm>
AliengoDeployNode::makeJointPosTerm(int history) {
    auto term = std::make_shared<ObservationTerm>("joint_pos_rel", history);
    term->func = [this]() { return getJointPos(); };
    term->scale = kObsScaleJointPos;  // 1.0
    return term;
}

std::shared_ptr<ObservationTerm>
AliengoDeployNode::makeJointVelTerm(int history) {
    auto term = std::make_shared<ObservationTerm>("joint_vel_rel", history);
    term->func = [this]() { return getJointVel(); };
    term->scale = kObsScaleJointVel;  // 0.05
    return term;
}

std::shared_ptr<ActionObsTerm>
AliengoDeployNode::makeLastActionTerm(int history) {
    auto term = std::make_shared<ActionObsTerm>("last_action", history);
    term->init(kActionDim);  // 12
    return term;
}

std::shared_ptr<ObservationTerm>
AliengoDeployNode::makeGaitClockTerm(int history) {
    auto term = std::make_shared<ObservationTerm>("gait_clock", history);
    term->func = [this]() { return getGaitClockObs(); };
    // scale = 1.0 (default)
    return term;
}

std::shared_ptr<ObservationTerm>
AliengoDeployNode::makeCommandTerm(int history) {
    auto term = std::make_shared<ObservationTerm>("commands", history);
    term->func = [this]() { return getCommand(); };
    // Command scales are already applied inside getCommand()
    return term;
}

std::shared_ptr<ActionTerm>
AliengoDeployNode::makeActionTerm() {
    auto action = std::make_shared<ActionTerm>();

    // default_action in policy order
    std::vector<float> default_pos(kDefaultJointPos,
                                   kDefaultJointPos + kNumJoints);
    action->default_action = SimpleTensor::wrap(default_pos);

    // action scale (uniform 0.25)
    std::vector<float> scale_vec(kActionScaleVec,
                                 kActionScaleVec + kNumJoints);
    action->scale_ = SimpleTensor::wrap(scale_vec);

    return action;
}

// ============================================================
// Register observation/action pipeline for the Aliengo policy
// ============================================================

void AliengoDeployNode::registerAliengoPolicy() {
    std::vector<std::shared_ptr<ObservationTerm>> obs;

    // Observation order must match training:
    // [projected_gravity_xy(2) | base_ang_vel(3) | joint_pos(12) |
    //  joint_vel(12)           | last_action(12) | gait_clock(2) | commands(3)]
    // Each with history_length = 32, flattened term-major oldest→newest

    obs.push_back(makeGravityTerm(kHistoryLength));    // 2 × 32 = 64
    obs.push_back(makeAngVelTerm(kHistoryLength));     // 3 × 32 = 96
    obs.push_back(makeJointPosTerm(kHistoryLength));   // 12 × 32 = 384
    obs.push_back(makeJointVelTerm(kHistoryLength));   // 12 × 32 = 384
    obs.push_back(makeLastActionTerm(kHistoryLength)); // 12 × 32 = 384
    obs.push_back(makeGaitClockTerm(kHistoryLength));  // 2 × 32 = 64
    obs.push_back(makeCommandTerm(kHistoryLength));    // 3 × 32 = 96
    // Total: 64+96+384+384+384+64+96 = 1472 ✓

    registerTerms(obs, makeActionTerm());
}

void AliengoDeployNode::initObsManager() {
    obs_terms.clear();
    action_terms.clear();
    action_obs_terms.clear();

    for (const auto &spec : policy_specs) {
        // All policies for Aliengo use the same obs layout
        registerAliengoPolicy();
    }
}

// ============================================================
// Command Input
// ============================================================

void AliengoDeployNode::setCommandFromRemote(float lx, float ly, float rx) {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    // Unitree remote convention:
    //   ly forward → positive vx
    //  -lx left    → positive vy
    //  -rx left    → positive wz
    cmd_[0] = ly * kPadScaleVx;
    cmd_[1] = -lx * kPadScaleVy;
    cmd_[2] = -rx * kPadScaleWz;
}

void AliengoDeployNode::setCommandFromGamepad(float lx_raw, float ly_raw,
                                               float rx_raw) {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    cmd_[0] = -ly_raw * kPadScaleVx;
    cmd_[1] = -lx_raw * kPadScaleVy;
    cmd_[2] = -rx_raw * kPadScaleWz;
}

void AliengoDeployNode::clearCommand() {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    std::fill(cmd_.begin(), cmd_.end(), 0.0f);
}

std::vector<float> AliengoDeployNode::getCommandVector() const {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    return cmd_;
}

void AliengoDeployNode::applyCommandDeadzone(std::vector<float> &cmd) const {
    if (cmd.size() < 3) return;
    if (std::abs(cmd[0]) < kCmdDeadzoneVx &&
        std::abs(cmd[1]) < kCmdDeadzoneVy &&
        std::abs(cmd[2]) < kCmdDeadzoneWz) {
        cmd[0] = 0.0f;
        cmd[1] = 0.0f;
        cmd[2] = 0.0f;
    }
}

// ============================================================
// Remote Button Handling
// ============================================================

void AliengoDeployNode::processRemoteButtons(
    const aliengo::WirelessRemoteState &state) {

    auto edges = remote_decoder_.computeEdges(prev_remote_state_, state);

    // A button: enable policy
    if (edges.a) {
        cancelStopSequence();
        is_stop_.store(false, std::memory_order_relaxed);
        gait_clock_.reset();
        reset_observation_buffers();
        reset_policy_states();
        ROS_INFO("Policy ENABLED by remote A button.");
    }

    // B button: controlled stop
    if (edges.b) {
        startStopSequence();
        ROS_WARN("Controlled STOP requested by remote B button.");
    }

    // L2 + B: emergency damping stop
    if (edges.b && state.key(kKeyL2)) {
        is_stop_.store(true, std::memory_order_relaxed);
        // Set all motors to damping mode
        for (int i = 0; i < 12; ++i) {
            low_cmd_.motorCmd[i].mode = kMotorModeServo;
            low_cmd_.motorCmd[i].q = kPosStopF;
            low_cmd_.motorCmd[i].dq = kVelStopF;
            low_cmd_.motorCmd[i].Kp = 0.0f;
            low_cmd_.motorCmd[i].Kd = kDampingKd;
            low_cmd_.motorCmd[i].tau = 0.0f;
        }
        low_cmd_pub_.publish(low_cmd_);
        ROS_WARN("EMERGENCY DAMPING STOP (L2+B).");
    }

    // Start button: clear velocity command
    if (edges.start) {
        clearCommand();
        ROS_INFO("Velocity command cleared by Start.");
    }

    // Select button: reset policy state
    if (edges.select) {
        request_policy_state_reset(0);
        ROS_INFO("Policy state reset requested by Select.");
    }
}

// ============================================================
// Safety: Stop Posture Sequence
// ============================================================

std::array<float, kNumJoints>
AliengoDeployNode::getCurrentLegJointPos() const {
    std::array<float, kNumJoints> pos;
    // Default to stand pose if no state yet
    for (int i = 0; i < kNumJoints; ++i) {
        pos[i] = kStopStandPos[i];
    }
    if (!has_low_state_.load(std::memory_order_relaxed)) return pos;

    std::lock_guard<std::mutex> lock(low_state_mutex_);
    for (int i = 0; i < kNumJoints; ++i) {
        int sdk_idx = kJointMap[i];
        pos[i] = low_state_.motorState[sdk_idx].q;
    }
    return pos;
}

void AliengoDeployNode::startStopSequence() {
    std::lock_guard<std::mutex> lock(stop_mutex_);
    if (stop_state_ != StopState::Idle) return;

    stop_start_pos_ = getCurrentLegJointPos();
    stop_state_ = StopState::ToStand;
    stop_step_ = 0;
    is_stop_.store(true, std::memory_order_relaxed);
}

void AliengoDeployNode::cancelStopSequence() {
    std::lock_guard<std::mutex> lock(stop_mutex_);
    if (stop_state_ == StopState::Idle) return;
    stop_state_ = StopState::Idle;
    stop_step_ = 0;
}

void AliengoDeployNode::writeStopPostureCmd() {
    std::array<float, kNumJoints> target;
    bool reached_hold = false;

    {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        if (stop_state_ == StopState::Idle) return;

        if (stop_state_ == StopState::ToStand) {
            float alpha = std::min(1.0f,
                static_cast<float>(stop_step_ + 1) /
                static_cast<float>(kStopStepsToStand));
            for (int i = 0; i < kNumJoints; ++i) {
                target[i] = lerp(stop_start_pos_[i], kStopStandPos[i], alpha);
            }
            ++stop_step_;
            if (stop_step_ >= kStopStepsToStand) {
                stop_state_ = StopState::ToDown;
                stop_step_ = 0;
            }
        } else if (stop_state_ == StopState::ToDown) {
            float alpha = std::min(1.0f,
                static_cast<float>(stop_step_ + 1) /
                static_cast<float>(kStopStepsToDown));
            for (int i = 0; i < kNumJoints; ++i) {
                target[i] = lerp(kStopStandPos[i], kStopDownPos[i], alpha);
            }
            ++stop_step_;
            if (stop_step_ >= kStopStepsToDown) {
                stop_state_ = StopState::Hold;
                stop_step_ = 0;
                reached_hold = true;
            }
        } else {
            // Hold
            for (int i = 0; i < kNumJoints; ++i) {
                target[i] = kStopDownPos[i];
            }
        }
    }

    // Write target to low_cmd using joint map
    for (int i = 0; i < kNumJoints; ++i) {
        int sdk_idx = kJointMap[i];
        low_cmd_.motorCmd[sdk_idx].mode = kMotorModeServo;
        low_cmd_.motorCmd[sdk_idx].q = target[i];
        low_cmd_.motorCmd[sdk_idx].dq = 0.0f;
        low_cmd_.motorCmd[sdk_idx].Kp = kStopKp;
        low_cmd_.motorCmd[sdk_idx].Kd = kStopKd;
        low_cmd_.motorCmd[sdk_idx].tau = 0.0f;
    }

    low_cmd_pub_.publish(low_cmd_);

    if (reached_hold) {
        ROS_INFO("Stop posture reached. Holding down pose.");
    }
}

// ============================================================
// Policy Runtime Reset Callback
// ============================================================

void AliengoDeployNode::on_policy_runtime_state_reset(int id) {
    reset_observation_buffers(id);
    gait_clock_.reset();
    ROS_INFO("Policy runtime state reset for id=%d", id);
}

// ============================================================
// Gamepad Initialization
// ============================================================

void AliengoDeployNode::initGamepad(bool enable) {
    if (!enable) {
        ROS_INFO("Local gamepad disabled. Use wireless remote only.");
        return;
    }

    pad_ = std::make_shared<GamePad>();
    pad_->showGamePads();
    if (pad_->GamePadpads.empty()) {
        ROS_WARN("No local gamepads found.");
        return;
    }

    const std::string gp_id = pad_->GamePadpads.begin()->first;
    if (pad_->openGamePad(gp_id) < 0) {
        ROS_WARN("Failed to open gamepad %s", gp_id.c_str());
        return;
    }

    pad_->bindGamePadValues([this](GamePadValues map) {
        setCommandFromGamepad(
            static_cast<float>(map.lx) / 32767.0f,
            static_cast<float>(map.ly) / 32767.0f,
            static_cast<float>(map.rx) / 32767.0f);

        // Gamepad buttons (no edge detection — held = active)
        if (map.a && is_stop_.load(std::memory_order_relaxed)) {
            cancelStopSequence();
            is_stop_.store(false, std::memory_order_relaxed);
            gait_clock_.reset();
            reset_observation_buffers();
            reset_policy_states();
            ROS_INFO("Policy ENABLED by local gamepad A.");
        }
        if (map.b && !is_stop_.load(std::memory_order_relaxed)) {
            startStopSequence();
            ROS_WARN("Controlled STOP by local gamepad B.");
        }
    });

    pad_->readGamePad();
    ROS_INFO("Local gamepad [%s] initialized.", gp_id.c_str());
}

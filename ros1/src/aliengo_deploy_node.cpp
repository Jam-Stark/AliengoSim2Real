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
      gait_clock_(kGaitFrequencyHz, kControlDt),
      force_gate_(kControlDt) {
    gravity_ = SimpleTensor::wrap({0.0f, 0.0f, -1.0f});

    // Read gate_enabled and brake_enabled params
    nh_.param("gate_enabled", gate_enabled_, true);
    nh_.param("brake_enabled", brake_enabled_, true);
    // Read CSV logging param
    std::string csv_path;
    nh_.param<std::string>("force_log_csv", csv_path, "");
    if (!csv_path.empty()) {
        initCsvLog(csv_path);
    }

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

    // Publisher: force estimator output
    force_est_pub_ = nh_.advertise<geometry_msgs::WrenchStamped>("force_estimator", 10);

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

        // Even when stopped, keep gait clock running for warm-start
        gait_clock_.setStanding(true);
        gait_clock_.step();

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

    // ---- Extract pred_est (force estimator) from policy auxiliary output ----
    SimpleTensor aux = policys[0].get_last_aux_output();
    last_pred_est_ = aux.defined() ? toVector<float>(aux) : std::vector<float>();

    // ---- Standing / Walking gate ----
    auto cmd = getCommandVector();
    if (gate_enabled_ && last_pred_est_.size() >= 3) {
        float force_local[3] = {last_pred_est_[3], last_pred_est_[4], last_pred_est_[5]};
        // pred_est layout: [base_lin_vel(3), base_forces_local(3)]
        // forces are indices 3,4,5
        if (last_pred_est_.size() >= 6) {
            force_local[0] = last_pred_est_[3];
            force_local[1] = last_pred_est_[4];
            force_local[2] = last_pred_est_[5];
        }
        float cmd_arr[3] = {cmd[0], cmd[1], cmd[2]};
        current_gait_mode_ = force_gate_.update(cmd_arr, force_local);
    } else {
        // No gate: always walking when enabled
        float cmd_arr[3] = {cmd[0], cmd[1], cmd[2]};
        bool cmd_zero = std::abs(cmd[0]) < kCmdDeadzoneVx &&
                        std::abs(cmd[1]) < kCmdDeadzoneVy &&
                        std::abs(cmd[2]) < kCmdDeadzoneWz;
        current_gait_mode_ = cmd_zero ? aliengo::MODE_STANDING
                                      : aliengo::MODE_COMMAND_WALKING;
    }

    // ---- Update gait clock based on mode ----
    gait_clock_.setStanding(current_gait_mode_ == aliengo::MODE_STANDING);
    gait_clock_.step();

    // ---- Brake command gate ----
    bool brake_active = false;
    if (brake_enabled_ && last_pred_est_.size() >= 6) {
        float force_local[3] = {last_pred_est_[3], last_pred_est_[4], last_pred_est_[5]};
        float cmd_arr[3] = {cmd[0], cmd[1], cmd[2]};
        bool is_walking = (current_gait_mode_ != aliengo::MODE_STANDING);
        const auto &bs = brake_gate_.update(cmd_arr, force_local, is_walking);
        brake_active = bs.active;

        if (brake_active) {
            // Zero out command sent to observation (affects next step)
            {
                std::lock_guard<std::mutex> lock(cmd_mutex_);
                std::fill(cmd_.begin(), cmd_.end(), 0.0f);
            }
            ROS_WARN_THROTTLE(2.0, "BRAKE ACTIVE: est_force_x=%.1f N, zeroing command",
                              bs.est_force_x_n);
        }
    }

    // ---- Publish force estimator + CSV log ----
    if (!last_pred_est_.empty()) {
        publishForceEstimator(last_pred_est_);
    }
    logCsvRow(cmd, last_pred_est_, current_gait_mode_);

    // ---- Write motor commands ----
    if (brake_active) {
        // When braking, hold current default pose instead of policy output
        zeroLowCmd();
    } else {
        writeActionToCmd(act);
    }
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

// ============================================================
// Force Estimator Publishing + CSV Logging
// ============================================================

void AliengoDeployNode::publishForceEstimator(const std::vector<float> &pred_est) {
    geometry_msgs::WrenchStamped msg;
    msg.header.stamp = ros::Time::now();
    msg.header.frame_id = "base_link";

    // pred_est layout: [base_lin_vel(3), base_forces_local(3)]
    if (pred_est.size() >= 6) {
        msg.wrench.force.x = pred_est[3];
        msg.wrench.force.y = pred_est[4];
        msg.wrench.force.z = pred_est[5];
        // Also publish estimated velocity in torque fields (repurposed)
        msg.wrench.torque.x = pred_est[0];
        msg.wrench.torque.y = pred_est[1];
        msg.wrench.torque.z = pred_est[2];
    } else if (pred_est.size() >= 3) {
        msg.wrench.force.x = pred_est[0];
        msg.wrench.force.y = pred_est[1];
        msg.wrench.force.z = pred_est[2];
    }

    force_est_pub_.publish(msg);
}

void AliengoDeployNode::initCsvLog(const std::string &path) {
    csv_log_.open(path, std::ios::out | std::ios::trunc);
    if (!csv_log_.is_open()) {
        ROS_WARN("Failed to open CSV log file: %s", path.c_str());
        return;
    }
    csv_logging_enabled_ = true;
    // Write header
    csv_log_ << "step,time_s,"
             << "cmd_vx,cmd_vy,cmd_wz,"
             << "pred_lin_vel_x,pred_lin_vel_y,pred_lin_vel_z,"
             << "pred_force_x,pred_force_y,pred_force_z,"
             << "mode,mode_name,"
             << "force_xy_raw,force_excess,force_baseline,"
             << "enter_score,exit_score,dir_consistency,"
             << "brake_eligible,brake_active,brake_hold,brake_est_fx"
             << std::endl;
    ROS_INFO("CSV force estimator log opened: %s", path.c_str());
}

void AliengoDeployNode::logCsvRow(const std::vector<float> &cmd,
                                   const std::vector<float> &pred_est,
                                   aliengo::GaitMode mode) {
    if (!csv_logging_enabled_ || !csv_log_.is_open()) return;

    ++log_step_count_;
    double time_s = log_step_count_ * static_cast<double>(aliengo::kControlDt);

    // Command
    float cx = cmd.size() > 0 ? cmd[0] : 0.0f;
    float cy = cmd.size() > 1 ? cmd[1] : 0.0f;
    float cz = cmd.size() > 2 ? cmd[2] : 0.0f;

    // Pred est
    float lv0 = 0.f, lv1 = 0.f, lv2 = 0.f;
    float f0 = 0.f, f1 = 0.f, f2 = 0.f;
    if (pred_est.size() >= 3) { lv0 = pred_est[0]; lv1 = pred_est[1]; lv2 = pred_est[2]; }
    if (pred_est.size() >= 6) { f0 = pred_est[3]; f1 = pred_est[4]; f2 = pred_est[5]; }

    // Gate state
    const auto &gs = force_gate_.state();
    const auto &bs = brake_gate_.state();

    csv_log_ << log_step_count_ << "," << time_s << ","
             << cx << "," << cy << "," << cz << ","
             << lv0 << "," << lv1 << "," << lv2 << ","
             << f0 << "," << f1 << "," << f2 << ","
             << static_cast<int>(mode) << "," << aliengo::gaitModeName(mode) << ","
             << gs.force_xy_raw << "," << gs.force_excess << "," << gs.force_baseline << ","
             << gs.enter_score << "," << gs.exit_score << "," << gs.dir_consistency << ","
             << (bs.eligible ? 1 : 0) << "," << (bs.active ? 1 : 0) << ","
             << bs.hold_counter << "," << bs.est_force_x_n
             << std::endl;
}

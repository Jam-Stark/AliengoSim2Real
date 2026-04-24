#pragma once

#include "aliengo_deploy/aliengo_constants.h"
#include "aliengo_deploy/aliengo_udp_transport.h"
#include "aliengo_deploy/brake_command_gate.h"
#include "aliengo_deploy/force_mode_switcher.h"
#include "aliengo_deploy/gait_clock.h"
#include "aliengo_deploy/wireless_remote_decoder.h"

#include "ManagerEnv.hpp"
#include "gamepad.h"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ros/ros.h>
#include <geometry_msgs/WrenchStamped.h>
#include <unitree_legged_msgs/LowCmd.h>
#include <unitree_legged_msgs/LowState.h>

// ============================================================
// AliengoDeployNode
//
// ROS1 deployment node for Aliengo RL locomotion policy.
// Inherits ManagerBasedEnv for policy inference pipeline.
// ============================================================

class AliengoDeployNode : public ManagerBasedEnv {
public:
    explicit AliengoDeployNode(
        ros::NodeHandle &nh,
        const std::vector<PolicySpec> &policy_specs,
        const aliengo::ForceGateParams &force_gate_params = aliengo::ForceGateParams(),
        InferenceDevice device = InferenceDevice::CPU);
    ~AliengoDeployNode();

    /// Initialize and start the control loop
    void start();

    /// Initialize optional local USB gamepad
    void initGamepad(bool enable);

    /// Override: register observation and action terms for the policy
    void initObsManager() override;

protected:
    void on_policy_runtime_state_reset(int id) override;

private:
    // ---- ROS I/O ----
    void initInterfaces();
    void initLowCmd();

    void lowStateCallback(const unitree_legged_msgs::LowState::ConstPtr &msg);
    void controlLoop(const ros::TimerEvent &event);

    // ---- Observation extraction ----
    SimpleTensor getProjectedGravityXY();
    SimpleTensor getBaseAngVel();
    SimpleTensor getJointPos();
    SimpleTensor getJointVel();
    SimpleTensor getCommand();
    SimpleTensor getGaitClockObs();

    // ---- Observation term factories ----
    std::shared_ptr<ObservationTerm> makeGravityTerm(int history);
    std::shared_ptr<ObservationTerm> makeAngVelTerm(int history);
    std::shared_ptr<ObservationTerm> makeJointPosTerm(int history);
    std::shared_ptr<ObservationTerm> makeJointVelTerm(int history);
    std::shared_ptr<ActionObsTerm>   makeLastActionTerm(int history);
    std::shared_ptr<ObservationTerm> makeGaitClockTerm(int history);
    std::shared_ptr<ObservationTerm> makeCommandTerm(int history);
    std::shared_ptr<ActionTerm>      makeActionTerm();

    void registerAliengoPolicy();

    // ---- Action writing ----
    void zeroLowCmd();
    void writeActionToCmd(const std::vector<float> &action);

    // ---- Command input ----
    void setCommandFromRemote(float lx, float ly, float rx);
    void setCommandFromGamepad(float lx_raw, float ly_raw, float rx_raw);
    void clearCommand();
    std::vector<float> getCommandVector() const;
    void applyCommandDeadzone(std::vector<float> &cmd) const;

    // ---- Controller / button handling ----
    void processRemoteButtons(const aliengo::WirelessRemoteState &state);

    // ---- Safety ----
    enum class StopState { Idle, ToStand, ToDown, Hold };

    void startStopSequence();
    void cancelStopSequence();
    void writeStopPostureCmd();
    std::array<float, aliengo::kNumJoints> getCurrentLegJointPos() const;

    // ---- Lerp helper ----
    static float lerp(float a, float b, float t) {
        return (1.0f - t) * a + t * b;
    }

    // ---- Direct UDP transport (bypasses ros_udp) ----
    std::unique_ptr<aliengo::AliengoUdpTransport> udp_transport_;
    bool use_direct_udp_ = true;        // default: direct UDP to robot

    void readUdpStateIntoLowState();     // copy RobotState -> low_state_
    void writeActionToUdp(const std::vector<float> &action);

    // ---- ROS handles ----
    ros::NodeHandle &nh_;
    ros::Publisher low_cmd_pub_;
    ros::Publisher force_est_pub_;          // publish pred_est as WrenchStamped
    ros::Subscriber low_state_sub_;
    ros::Timer control_timer_;

    // ---- State ----
    unitree_legged_msgs::LowCmd low_cmd_;
    unitree_legged_msgs::LowState low_state_;
    mutable std::mutex low_state_mutex_;
    mutable std::mutex cmd_mutex_;
    std::atomic<bool> has_low_state_{false};
    std::atomic<bool> is_stop_{true};
    bool timer_started_ = false;

    // ---- Gait clock ----
    aliengo::GaitClock gait_clock_;

    // ---- Standing / Walking gate ----
    aliengo::ForceModeSwitcher force_gate_;
    aliengo::ForceGatePreset force_gate_preset_ = aliengo::ForceGatePreset::V2;
    aliengo::GaitMode current_gait_mode_ = aliengo::MODE_STANDING;
    bool gate_enabled_ = true;              // can be toggled via param

    // ---- Brake gate ----
    aliengo::BrakeCommandGate brake_gate_;
    bool brake_enabled_ = true;             // can be toggled via param

    // ---- Wireless remote ----
    aliengo::WirelessRemoteDecoder remote_decoder_;
    aliengo::WirelessRemoteState prev_remote_state_;

    // ---- Local gamepad ----
    std::shared_ptr<GamePad> pad_;

    // ---- Command ----
    std::vector<float> cmd_ = {0.0f, 0.0f, 0.0f};

    // ---- Gravity constant ----
    SimpleTensor gravity_;

    // ---- Force estimator logging ----
    std::vector<float> last_pred_est_;      // last pred_est from policy
    std::ofstream csv_log_;                 // CSV file for force estimator
    bool csv_logging_enabled_ = false;
    uint64_t log_step_count_ = 0;

    // ---- Stop posture ----
    StopState stop_state_ = StopState::Idle;
    int stop_step_ = 0;
    std::array<float, aliengo::kNumJoints> stop_start_pos_{};
    mutable std::mutex stop_mutex_;

    // ---- Stand-up interpolation (before policy handover) ----
    bool is_standing_up_ = false;
    int stand_up_step_ = 0;
    std::array<float, aliengo::kNumJoints> stand_up_start_pos_{};
    void writeStandUpCmd();

    // ---- Helpers ----
    void publishForceEstimator(const std::vector<float> &pred_est);
    void logCsvRow(const std::vector<float> &cmd,
                   const std::vector<float> &pred_est,
                   aliengo::GaitMode mode);
    void initCsvLog(const std::string &path);
};

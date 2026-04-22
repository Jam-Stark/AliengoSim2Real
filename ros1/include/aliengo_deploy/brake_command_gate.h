#pragma once

#include <cmath>
#include <string>

namespace aliengo {

// ============================================================
// BrakeCommandGate — C++ port of BrakeCommandGate
// from scripts/mujoco/smart_gait.py
//
// A protective gate that zeroes out the velocity command and
// estimated guidance force when it detects strong negative x-axis
// force (backward braking) while the robot is walking forward.
//
// In the original MuJoCo deployment this is phase-locked to
// specific scheduler phases. For ROS1 real-deploy, we simplify:
// - eligible whenever the robot is walking (not standing)
// - latch remains active until mode returns to standing or
//   command drops to zero
// ============================================================

struct BrakeGateParams {
    bool enabled                = true;
    float force_x_threshold_n   = -32.0f;   // trigger when est_force_x <= this
    float min_cmd_vx            = 0.2f;      // only brake when cmd_vx >= this
    float max_cmd_wz            = 0.10f;     // only brake when |cmd_wz| <= this
    int hold_steps              = 2;         // consecutive steps above threshold to trigger
    // Command deadzone (same as standing/walking gate)
    float cmd_deadzone_vx       = 0.1f;
    float cmd_deadzone_vy       = 0.1f;
    float cmd_deadzone_wz       = 0.2f;
};

struct BrakeGateState {
    bool eligible       = false;
    bool active         = false;
    bool latched        = false;
    int hold_counter    = 0;
    float est_force_x_n = 0.0f;
    float est_force_y_n = 0.0f;
    float threshold_x_n = -32.0f;
};

class BrakeCommandGate {
public:
    explicit BrakeCommandGate(const BrakeGateParams &params = BrakeGateParams())
        : params_(params) {
        state_.threshold_x_n = params_.force_x_threshold_n;
    }

    /// Call every control tick after policy inference and gate update.
    /// @param cmd_vel_b          velocity command [vx, vy, wz] (pre-brake)
    /// @param base_force_est_local estimated force [fx, fy, fz] in local frame
    /// @param is_walking          true if current gait mode is walking (not standing)
    /// @return current BrakeGateState after update
    const BrakeGateState &update(const float cmd_vel_b[3],
                                  const float base_force_est_local[3],
                                  bool is_walking) {
        // Extract estimated forces
        state_.est_force_x_n = std::isfinite(base_force_est_local[0])
                                   ? base_force_est_local[0] : 0.0f;
        state_.est_force_y_n = std::isfinite(base_force_est_local[1])
                                   ? base_force_est_local[1] : 0.0f;

        // If mode returned to standing or command became zero, release latch
        if (!is_walking || !commandNonzero(cmd_vel_b)) {
            releaseLatch();
            state_.eligible = false;
            state_.active = false;
            return state_;
        }

        // If already latched, stay active
        if (state_.latched) {
            state_.eligible = true;
            state_.active = true;
            return state_;
        }

        // Check eligibility
        state_.eligible = params_.enabled &&
                          is_walking &&
                          commandNonzero(cmd_vel_b) &&
                          cmd_vel_b[0] >= params_.min_cmd_vx &&
                          std::abs(cmd_vel_b[2]) <= params_.max_cmd_wz;

        if (!state_.eligible) {
            state_.hold_counter = 0;
            state_.active = false;
            return state_;
        }

        // Check trigger sample
        bool trigger = (state_.est_force_x_n <= params_.force_x_threshold_n);

        if (trigger) {
            ++state_.hold_counter;
        } else {
            state_.hold_counter = 0;
        }

        // Activate if held long enough
        if (state_.hold_counter >= params_.hold_steps) {
            state_.active = true;
            state_.latched = true;
        } else {
            state_.active = false;
        }

        return state_;
    }

    const BrakeGateState &state() const { return state_; }
    const BrakeGateParams &params() const { return params_; }

    void reset() {
        state_ = BrakeGateState{};
        state_.threshold_x_n = params_.force_x_threshold_n;
    }

private:
    bool commandNonzero(const float cmd[3]) const {
        return !(std::abs(cmd[0]) < params_.cmd_deadzone_vx &&
                 std::abs(cmd[1]) < params_.cmd_deadzone_vy &&
                 std::abs(cmd[2]) < params_.cmd_deadzone_wz);
    }

    void releaseLatch() {
        state_.latched = false;
        state_.hold_counter = 0;
        state_.active = false;
    }

    BrakeGateParams params_;
    BrakeGateState state_;
};

} // namespace aliengo

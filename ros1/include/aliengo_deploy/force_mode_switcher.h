#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace aliengo {

// ============================================================
// Standing / Walking Gate — C++ port of ForceModeSwitcher
// from scripts/mujoco/smart_gait.py
//
// Three modes:
//   MODE_STANDING       — gait phase frozen at 0
//   MODE_FORCE_WALKING  — triggered by estimated external force
//   MODE_COMMAND_WALKING— triggered by nonzero velocity command
// ============================================================

enum GaitMode {
    MODE_STANDING = 0,
    MODE_FORCE_WALKING = 1,
    MODE_COMMAND_WALKING = 2,
};

inline const char* gaitModeName(GaitMode m) {
    switch (m) {
        case MODE_STANDING:        return "standing";
        case MODE_FORCE_WALKING:   return "force_walking";
        case MODE_COMMAND_WALKING: return "command_walking";
    }
    return "unknown";
}

/// GATE_PRESET_V2 parameters (tuned for base-only noisy estimator)
struct ForceGateParams {
    float evidence_cap_n         = 10.0f;
    float baseline_tau_s         = 1.0f;
    float baseline_margin_n      = 0.34f;
    float baseline_update_max_n  = 1.00f;
    float enter_threshold_n      = 8.00f;
    float exit_threshold_n       = 0.15f;
    float enter_tau_on_s         = 0.08f;
    float enter_tau_off_s        = 0.22f;
    float exit_tau_on_s          = 0.04f;
    float exit_tau_off_s         = 0.10f;
    float enter_score_threshold  = 0.90f;
    float exit_score_threshold   = 0.60f;
    float dir_consistency_min_force_n = 1.00f;
    float dir_consistency_tau_s  = 0.16f;
    float dir_consistency_threshold = 0.72f;
    float switch_cooldown_s      = 1.00f;
    float enter_hold_s           = 0.12f;
    float exit_hold_s            = 0.04f;
    // Command deadzone thresholds
    float cmd_deadzone_vx        = 0.1f;
    float cmd_deadzone_vy        = 0.1f;
    float cmd_deadzone_wz        = 0.2f;
};

struct ForceGateState {
    GaitMode mode               = MODE_STANDING;
    GaitMode force_state        = MODE_STANDING;  // internal force sub-state
    float force_baseline        = 0.0f;
    float enter_score           = 0.0f;
    float exit_score            = 0.0f;
    float dir_consistency       = 0.0f;
    float enter_timer           = 0.0f;
    float exit_timer            = 0.0f;
    float cooldown_timer        = 0.0f;
    float force_excess          = 0.0f;
    float force_xy_raw          = 0.0f;
    float force_xy_cap          = 0.0f;
    // Previous force direction for consistency check
    float prev_dir_x            = 0.0f;
    float prev_dir_y            = 1.0f;
};

class ForceModeSwitcher {
public:
    explicit ForceModeSwitcher(float dt, const ForceGateParams& params = ForceGateParams())
        : dt_(dt), params_(params) {}

    /// Call every control tick.
    /// @param cmd_vel_b  velocity command [vx, vy, wz] in body frame
    /// @param base_force_est_local  estimated base force [fx, fy, fz] in local frame
    /// @return current GaitMode after update
    GaitMode update(const float cmd_vel_b[3], const float base_force_est_local[3]) {
        // ---- Step 1: Is command nonzero? ----
        bool cmd_zero = isCommandZero(cmd_vel_b);

        if (!cmd_zero) {
            // Direct command walking — reset force gate state
            state_.mode = MODE_COMMAND_WALKING;
            resetForceGateInternal();
            return state_.mode;
        }

        // ---- Step 2: Compute force evidence ----
        float fx = base_force_est_local[0];
        float fy = base_force_est_local[1];
        state_.force_xy_raw = std::sqrt(fx * fx + fy * fy);
        state_.force_xy_cap = std::min(state_.force_xy_raw, params_.evidence_cap_n);

        // ---- Step 3: Update baseline (only in standing, not in cooldown) ----
        if (state_.force_state == MODE_STANDING &&
            state_.cooldown_timer <= 0.0f &&
            state_.force_xy_cap <= state_.force_baseline + params_.baseline_update_max_n) {
            float alpha = expDecayAlpha(params_.baseline_tau_s);
            state_.force_baseline += alpha * (state_.force_xy_cap - state_.force_baseline);
        }

        // ---- Step 4: Compute force excess ----
        state_.force_excess = std::max(
            state_.force_xy_cap - state_.force_baseline - params_.baseline_margin_n, 0.0f);

        // ---- Step 5: Direction consistency ----
        if (state_.force_xy_raw >= params_.dir_consistency_min_force_n) {
            float inv_norm = 1.0f / std::max(state_.force_xy_raw, 1e-6f);
            float dir_x = fx * inv_norm;
            float dir_y = fy * inv_norm;
            float dot = dir_x * state_.prev_dir_x + dir_y * state_.prev_dir_y;
            float alignment = std::max(dot, 0.0f);
            float dir_alpha = expDecayAlpha(params_.dir_consistency_tau_s);
            state_.dir_consistency += dir_alpha * (alignment - state_.dir_consistency);
            state_.prev_dir_x = dir_x;
            state_.prev_dir_y = dir_y;
        }

        // ---- Step 6: Compute enter/exit targets ----
        float enter_force_target = clamp01(state_.force_excess / params_.enter_threshold_n);
        float dir_term = (state_.dir_consistency >= params_.dir_consistency_threshold) ? 1.0f : 0.0f;
        float enter_target = enter_force_target * dir_term;

        float exit_target = 1.0f - clamp01(state_.force_excess / params_.exit_threshold_n);

        // ---- Step 7: Smooth enter/exit scores ----
        smoothScore(state_.enter_score, enter_target,
                    params_.enter_tau_on_s, params_.enter_tau_off_s);
        smoothScore(state_.exit_score, exit_target,
                    params_.exit_tau_on_s, params_.exit_tau_off_s);

        // ---- Step 8: Cooldown countdown ----
        if (state_.cooldown_timer > 0.0f) {
            state_.cooldown_timer -= dt_;
        }

        // ---- Step 9: State machine transitions ----
        if (state_.force_state == MODE_STANDING) {
            // Try to enter force_walking
            if (state_.cooldown_timer <= 0.0f &&
                state_.enter_score >= params_.enter_score_threshold) {
                state_.enter_timer += dt_;
                if (state_.enter_timer >= params_.enter_hold_s) {
                    state_.force_state = MODE_FORCE_WALKING;
                    state_.cooldown_timer = params_.switch_cooldown_s;
                    state_.exit_timer = 0.0f;
                    state_.exit_score = 0.0f;
                }
            } else {
                state_.enter_timer = 0.0f;
            }
        } else {
            // force_state == MODE_FORCE_WALKING, try to exit
            if (state_.exit_score >= params_.exit_score_threshold) {
                state_.exit_timer += dt_;
                if (state_.exit_timer >= params_.exit_hold_s) {
                    state_.force_state = MODE_STANDING;
                    state_.cooldown_timer = params_.switch_cooldown_s;
                    state_.enter_timer = 0.0f;
                    state_.enter_score = 0.0f;
                }
            } else {
                state_.exit_timer = 0.0f;
            }
        }

        state_.mode = state_.force_state;
        return state_.mode;
    }

    GaitMode mode() const { return state_.mode; }
    const ForceGateState& state() const { return state_; }
    const ForceGateParams& params() const { return params_; }

    void reset() {
        state_ = ForceGateState{};
    }

private:
    bool isCommandZero(const float cmd[3]) const {
        return std::abs(cmd[0]) < params_.cmd_deadzone_vx &&
               std::abs(cmd[1]) < params_.cmd_deadzone_vy &&
               std::abs(cmd[2]) < params_.cmd_deadzone_wz;
    }

    void resetForceGateInternal() {
        state_.force_state = MODE_STANDING;
        state_.enter_score = 0.0f;
        state_.exit_score = 0.0f;
        state_.enter_timer = 0.0f;
        state_.exit_timer = 0.0f;
        state_.cooldown_timer = 0.0f;
        state_.dir_consistency = 0.0f;
    }

    float expDecayAlpha(float tau_s) const {
        if (tau_s <= 0.0f) return 1.0f;
        return 1.0f - std::exp(-dt_ / tau_s);
    }

    void smoothScore(float& score, float target, float tau_on, float tau_off) {
        float tau = (target > score) ? tau_on : tau_off;
        float alpha = expDecayAlpha(tau);
        score += alpha * (target - score);
    }

    static float clamp01(float v) {
        return std::max(0.0f, std::min(1.0f, v));
    }

    float dt_;
    ForceGateParams params_;
    ForceGateState state_;
};

} // namespace aliengo

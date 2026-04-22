#pragma once

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace aliengo {

/// Gait phase clock that produces sin/cos observations.
/// Supports mode-controlled stepping:
///   - standing mode:  phase frozen at 0
///   - walking mode:   phase advances at the configured frequency
class GaitClock {
public:
    /// @param frequency_hz  Gait frequency in Hz (e.g. 2.0)
    /// @param dt            Control period in seconds (e.g. 0.02 for 50 Hz)
    explicit GaitClock(float frequency_hz = 2.0f, float dt = 0.02f)
        : freq_(frequency_hz), dt_(dt), phase_(0.0f), standing_(false) {}

    /// Advance the phase by one control tick.
    /// If standing mode is active, phase is frozen at 0.
    void step() {
        if (standing_) {
            phase_ = 0.0f;
            return;
        }
        phase_ += freq_ * dt_;
        // Keep phase in [0, 1) to avoid floating precision loss over time
        phase_ -= static_cast<float>(static_cast<int>(phase_));
    }

    /// Reset phase to zero
    void reset() { phase_ = 0.0f; }

    /// Set standing mode (phase frozen at 0)
    void setStanding(bool is_standing) { standing_ = is_standing; }

    /// Query standing mode
    bool isStanding() const { return standing_; }

    /// Current phase in [0, 1)
    float phase() const { return phase_; }

    /// sin(2π × phase)
    float sin_phase() const {
        return std::sin(2.0f * static_cast<float>(M_PI) * phase_);
    }

    /// cos(2π × phase)
    float cos_phase() const {
        return std::cos(2.0f * static_cast<float>(M_PI) * phase_);
    }

    /// Set frequency
    void setFrequency(float hz) { freq_ = hz; }

    /// Set dt
    void setDt(float dt) { dt_ = dt; }

private:
    float freq_;
    float dt_;
    float phase_;
    bool standing_;
};

} // namespace aliengo

#pragma once

#include <array>
#include <cstdint>

// ============================================================
// Aliengo Robot Constants for RL Policy Deployment
// ============================================================

namespace aliengo {

// ------------------------------------------------------------
// Control timing
// ------------------------------------------------------------
constexpr int kControlPeriodMs = 20;       // 50 Hz control loop
constexpr float kControlDt = 0.02f;        // seconds

// ------------------------------------------------------------
// Dimensions
// ------------------------------------------------------------
constexpr int kNumJoints = 12;
constexpr int kObsPerFrame = 46;
constexpr int kHistoryLength = 32;
constexpr int kObsTotalDim = kObsPerFrame * kHistoryLength;  // 1472
constexpr int kActionDim = 12;
constexpr int kPredEstDim = 6;             // auxiliary estimator output (unused for control)

// Motor mode for Aliengo PMSM servo
constexpr uint8_t kMotorModeServo = 0x0A;

// PosStop / VelStop sentinel values (from Unitree SDK)
constexpr float kPosStopF = 2.146E+9f;
constexpr float kVelStopF = 16000.0f;

// ------------------------------------------------------------
// Joint order mapping: Policy index -> SDK motorCmd index
//
// Policy order:
//   [0] FL_hip  [1] FR_hip  [2] RL_hip  [3] RR_hip
//   [4] FL_thigh[5] FR_thigh[6] RL_thigh[7] RR_thigh
//   [8] FL_calf [9] FR_calf [10]RL_calf [11]RR_calf
//
// SDK motorCmd order:
//   [0] FR_hip  [1] FR_thigh [2] FR_calf
//   [3] FL_hip  [4] FL_thigh [5] FL_calf
//   [6] RR_hip  [7] RR_thigh [8] RR_calf
//   [9] RL_hip  [10]RL_thigh [11]RL_calf
// ------------------------------------------------------------
constexpr int kJointMap[kNumJoints] = {
    3,   // policy[0]  FL_hip   -> sdk[3]
    0,   // policy[1]  FR_hip   -> sdk[0]
    9,   // policy[2]  RL_hip   -> sdk[9]
    6,   // policy[3]  RR_hip   -> sdk[6]
    4,   // policy[4]  FL_thigh -> sdk[4]
    1,   // policy[5]  FR_thigh -> sdk[1]
    10,  // policy[6]  RL_thigh -> sdk[10]
    7,   // policy[7]  RR_thigh -> sdk[7]
    5,   // policy[8]  FL_calf  -> sdk[5]
    2,   // policy[9]  FR_calf  -> sdk[2]
    11,  // policy[10] RL_calf  -> sdk[11]
    8,   // policy[11] RR_calf  -> sdk[8]
};

// ------------------------------------------------------------
// Default joint positions (policy order, radians)
// ------------------------------------------------------------
constexpr float kDefaultJointPos[kNumJoints] = {
     0.1f, -0.1f,  0.1f, -0.1f,   // hip:   FL, FR, RL, RR
     0.5f,  0.5f,  0.5f,  0.5f,   // thigh: FL, FR, RL, RR
    -1.0f, -1.0f, -1.0f, -1.0f,   // calf:  FL, FR, RL, RR
};

// Default joint velocities (all zero)
constexpr float kDefaultJointVel[kNumJoints] = {
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
};

// ------------------------------------------------------------
// Action processing
// ------------------------------------------------------------
constexpr float kActionScale = 0.25f;

// Per-joint action scale (policy order) - uniform 0.25 for all
constexpr float kActionScaleVec[kNumJoints] = {
    0.25f, 0.25f, 0.25f, 0.25f,
    0.25f, 0.25f, 0.25f, 0.25f,
    0.25f, 0.25f, 0.25f, 0.25f,
};

// ------------------------------------------------------------
// PD gains (policy order)
// hip/thigh: Kp=48, Kd=1.75  |  calf: Kp=72, Kd=3.15
// Deployment PD gains for the current Aliengo policy; may need tuning for real robot
// ------------------------------------------------------------

constexpr float kKp[12] = {
    48.0f, 48.0f, 48.0f, 48.0f,    // hip
    48.0f, 48.0f, 48.0f, 48.0f,    // thigh
    72.0f, 72.0f, 72.0f, 72.0f,    // calf
};
constexpr float kKd[12] = {
    1.75f, 1.75f, 1.75f, 1.75f,    // hip
    1.75f, 1.75f, 1.75f, 1.75f,    // thigh
    3.15f, 3.15f, 3.15f, 3.15f,    // calf
};




// ------------------------------------------------------------
// Observation scales (applied in obs extraction, NOT noise)
//
// Obs layout per frame (46 dim):
//   [0:2]   projected_gravity_b[x,y]   scale=1.0
//   [2:5]   base_ang_vel_b             scale=0.25
//   [5:17]  joint_pos - default        scale=1.0
//   [17:29] joint_vel * 0.05           scale=0.05
//   [29:41] last_action_raw            scale=1.0
//   [41:43] gait_clock [sin,cos]       scale=1.0
//   [43:46] commands * [2.0,2.0,0.25]  scale=per-element
// ------------------------------------------------------------
constexpr float kObsScaleAngVel = 0.25f;
constexpr float kObsScaleJointPos = 1.0f;
constexpr float kObsScaleJointVel = 0.05f;
constexpr float kObsScaleAction = 1.0f;
constexpr float kCmdScaleVx = 2.0f;
constexpr float kCmdScaleVy = 2.0f;
constexpr float kCmdScaleWz = 0.25f;

// Obs term dimensions
constexpr int kObsDimGravity = 2;
constexpr int kObsDimAngVel = 3;
constexpr int kObsDimJointPos = 12;
constexpr int kObsDimJointVel = 12;
constexpr int kObsDimAction = 12;
constexpr int kObsDimGaitClock = 2;
constexpr int kObsDimCommand = 3;

// Command deadzone: zero out if all below threshold
constexpr float kCmdDeadzoneVx = 0.1f;
constexpr float kCmdDeadzoneVy = 0.1f;
constexpr float kCmdDeadzoneWz = 0.2f;

// ------------------------------------------------------------
// Gait clock
// ------------------------------------------------------------
constexpr float kGaitFrequencyHz = 2.0f;

// ------------------------------------------------------------
// Safety / stop posture (SDK order for direct motor write)
// These are approximate Aliengo-suitable values
// ------------------------------------------------------------
// Stand pose (policy order) - same as default
constexpr float kStopStandPos[kNumJoints] = {
     0.1f, -0.1f,  0.1f, -0.1f,
     0.5f,  0.5f,  0.5f,  0.5f,
    -1.0f, -1.0f, -1.0f, -1.0f,
};

// Down/crouch pose (policy order)
constexpr float kStopDownPos[kNumJoints] = {
     0.1f, -0.1f,  0.1f, -0.1f,
     1.2f,  1.2f,  1.2f,  1.2f,
    -2.5f, -2.5f, -2.5f, -2.5f,
};

constexpr float kStopKp = 60.0f;
constexpr float kStopKd = 5.0f;
constexpr int kStopStepsToStand = 50;    // 1.0 s at 50 Hz
constexpr int kStopStepsToDown = 90;     // 1.8 s at 50 Hz

// Stand-up interpolation (before policy handover)
constexpr int kStandUpStage1Steps = 150;   // 3.0 s: extend calfs
constexpr int kStandUpStage2Steps = 150;   // 3.0 s: raise thighs + fine-tune hips
constexpr float kStandUpRearAlphaLead = 0.10f;   // rear legs progress earlier to reduce backward pitch
constexpr float kStandUpFrontAlphaLag = 0.04f;   // front legs progress slightly later
constexpr float kStandUpKpStart = 3.0f;    // gentle initial Kp
constexpr float kStandUpKdStart = 0.5f;    // gentle initial Kd

// Damping mode Kd for emergency stop
constexpr float kDampingKd = 3.0f;

// Command pad scale (joystick raw -> physical units)
constexpr float kPadScaleVx = 1.0f;
constexpr float kPadScaleVy = 1.0f;
constexpr float kPadScaleWz = 1.0f;

} // namespace aliengo

from types import SimpleNamespace

from piper_bridge.model import PiperFeedback
from piper_bridge.sdk_adapter import PiperSdkAdapter


def healthy_feedback() -> PiperFeedback:
    return PiperFeedback(
        positions_rad=(0.0,) * 6,
        velocities_rad_s=(0.0,) * 6,
        efforts_nm=(0.0,) * 6,
        sdk_timestamp_s=1.0,
        joint_hz=50.0,
        status_hz=100.0,
        arm_status=0,
        ctrl_mode=1,
        speed_sample_count=(1,) * 6,
    )


def test_resume_sends_explicit_resume_and_waits_for_normal_status(monkeypatch) -> None:
    calls = []
    fake_piper = SimpleNamespace(
        MotionCtrl_1=lambda *args: calls.append(args),
    )
    adapter = PiperSdkAdapter("can0", control_period_s=0.02, speed_percent=5)
    adapter._piper = fake_piper
    monkeypatch.setattr("piper_bridge.sdk_adapter.time.sleep", lambda _seconds: None)
    monkeypatch.setattr(
        adapter,
        "read_feedback",
        lambda require_speed_samples=False: healthy_feedback(),
    )

    feedback = adapter.resume(0.1)

    assert feedback.arm_status == 0
    assert calls == [(0x02, 0, 0)] * 5


def test_joint_command_uses_tested_move_j_contract() -> None:
    calls = []
    fake_piper = SimpleNamespace(
        MotionCtrl_2=lambda *args: calls.append(("mode", args)),
        JointCtrl=lambda *args: calls.append(("joint", args)),
    )
    adapter = PiperSdkAdapter("can0", control_period_s=0.02, speed_percent=5)
    adapter._piper = fake_piper

    adapter.command_joint_positions((0.0, 1.5707963267948966, -1.5707963267948966, 0.0, 0.0, 0.0))

    assert calls[0] == ("mode", (0x01, 0x01, 5, 0x00))
    assert calls[1] == ("joint", (0, 90000, -90000, 0, 0, 0))

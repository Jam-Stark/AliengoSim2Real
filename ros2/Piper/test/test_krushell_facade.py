from types import SimpleNamespace

from piper_bridge.krushell_facade import PiperSdkRos2Facade


class FakeClient:
    def __init__(self) -> None:
        self.latest_state = SimpleNamespace(
            positions_rad=(0.0, 0.1, -0.2, 0.3, -0.4, 0.5),
            velocities_rad_s=(1.0, 2.0, 3.0, 4.0, 5.0, 6.0),
            efforts_nm=(0.1, 0.2, 0.3, 0.4, 0.5, 0.6),
            received_wall_time_s=100.0,
            received_monotonic_s=0.0,
            measured_hz=50.0,
        )
        self.latest_diagnostics = SimpleNamespace(
            values={
                "arm_status": "0",
                "ctrl_mode": "1",
                "status_hz": "100.0",
                "joint_control_mode": "move_j_mit_high_follow",
                "motion_ctrl_2": "0x01,0x01,0,0xAD",
                "command_gate_open": "true",
            }
        )
        self.published = None
        self.stopped = False

    def pump(self, _timeout_s: float = 0.0) -> None:
        return None

    def wait_for_state(self, _timeout_s: float):
        return self.latest_state

    def wait_for_diagnostics(self, _timeout_s: float):
        return self.latest_diagnostics

    def enable(self):
        return True, "enabled"

    def disable(self):
        return True, "disabled"

    def stop(self):
        self.stopped = True
        return True, "stopped"

    def publish_joint_positions(self, positions):
        self.published = positions

    def diagnostics_are_fresh(self, _max_age_s: float) -> bool:
        return True


def test_facade_converts_sdk_joint_command_to_radians() -> None:
    client = FakeClient()
    facade = PiperSdkRos2Facade(client)
    facade.JointCtrl(0, 90000, -90000, 0, 0, 0)
    assert client.published[1] == 1.5707963267948966
    assert client.published[2] == -1.5707963267948966


def test_facade_exposes_one_velocity_sample_per_remote_state(monkeypatch) -> None:
    client = FakeClient()
    facade = PiperSdkRos2Facade(client, speed_sample_wait_s=0.0)
    monkeypatch.setattr(facade, "_state_is_fresh", lambda _state: True)

    first = facade.GetArmHighSpdInfoAverage(1.0, 2.0)
    second = facade.GetArmHighSpdInfoAverage(2.0, 3.0)

    assert first.motor_speed == (1000, 2000, 3000, 4000, 5000, 6000)
    assert first.sample_count == (1, 1, 1, 1, 1, 1)
    assert second.sample_count == (0, 0, 0, 0, 0, 0)


def test_facade_maps_quick_stop_to_bridge_service() -> None:
    client = FakeClient()
    facade = PiperSdkRos2Facade(client)
    facade.enabled = True
    facade.MotionCtrl_1(1, 0, 0)
    assert client.stopped
    assert not facade.enabled


def test_facade_rejects_unsupported_motion_mode() -> None:
    client = FakeClient()
    facade = PiperSdkRos2Facade(client)
    try:
        facade.MotionCtrl_2(0x01, 0x02, 0, 0xAD)
    except RuntimeError as exc:
        assert "MOVE J" in str(exc)
    else:
        raise AssertionError("unsupported motion mode was accepted")


def test_facade_accepts_krushell_move_j_mit_high_follow_mode() -> None:
    facade = PiperSdkRos2Facade(FakeClient())
    facade.MotionCtrl_2(0x01, 0x01, 0, 0xAD)

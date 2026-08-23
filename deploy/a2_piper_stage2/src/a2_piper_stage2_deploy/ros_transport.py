"""Unavailable Python external-semantic ROS mode.

The deployed robot path is the C++ ``a2_piper_stage2_direct`` node. It composes
``A2LowLevelInterface`` exactly like the proven main-branch A2 policy deployment
and uses the PC2 PiPER semantic bridge. This Python module intentionally refuses
to invent a second external named-A2 transport.
"""

from __future__ import annotations

from .site import SiteConfig


_A2_BRIDGE_BLOCKER = (
    "Python external-semantic ROS mode is unavailable. Use the C++ "
    "a2_piper_stage2_direct node through scripts/stage2_gate.sh; it composes the "
    "existing A2LowLevelInterface and preserves the proven LowCmd boundary. Do "
    "not publish Stage2 targets to /lowcmd from Python."
)


def run_ros_shadow(site: SiteConfig) -> None:
    del site
    raise RuntimeError(_A2_BRIDGE_BLOCKER)


def run_ros_live(site: SiteConfig, live_requested: bool) -> None:
    if not live_requested:
        raise ValueError("Live output requires the explicit --live flag")
    site.require_live_ready()
    raise RuntimeError(_A2_BRIDGE_BLOCKER)

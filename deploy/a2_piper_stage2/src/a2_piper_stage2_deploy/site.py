"""Site configuration loading without inventing unresolved hardware facts."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml


@dataclass(frozen=True)
class SiteConfig:
    path: Path
    raw: dict[str, Any]

    @classmethod
    def load(cls, path: str | Path) -> "SiteConfig":
        site_path = Path(path).expanduser().resolve()
        with site_path.open("r", encoding="utf-8") as stream:
            raw = yaml.safe_load(stream)
        if raw["schema"] != "a2_piper_stage2_site" or raw["schema_version"] != 1:
            raise ValueError("Unsupported site configuration schema")
        return cls(site_path, raw)

    def unresolved_fields(self) -> tuple[str, ...]:
        return tuple(_find_to_verify(self.raw))

    @property
    def output_enabled(self) -> bool:
        return bool(self.raw["safety"]["output_enabled"])

    def require_live_ready(self) -> None:
        unresolved = self.unresolved_fields()
        if unresolved:
            raise ValueError("Live site configuration still contains TO_VERIFY: " + ", ".join(unresolved))
        if not self.output_enabled:
            raise ValueError("Live output requires safety.output_enabled: true")


def _find_to_verify(value: Any, prefix: str = "") -> list[str]:
    if isinstance(value, dict):
        found: list[str] = []
        for key, child in value.items():
            child_prefix = f"{prefix}.{key}" if prefix else str(key)
            found.extend(_find_to_verify(child, child_prefix))
        return found
    if isinstance(value, list):
        found = []
        for index, child in enumerate(value):
            found.extend(_find_to_verify(child, f"{prefix}[{index}]"))
        return found
    return [prefix] if value == "TO_VERIFY" else []

"""Automatic work resolution: keep the neural pass at a target rate.

The network's cost follows the pixel count it is handed (see TECHNICAL.md,
"Processing at a reduced resolution"), and how many milliseconds that is on a
given card next to a given game cannot be known in advance. So the governor
measures: while the measured NR rate stays under the target it steps the work
scale down, and it steps back up toward the user's own value when the rate has
room. It only ever works below the scale the user chose, never above it.

A step down that does not raise the rate (the source itself is slow, so the
network was never the limit) is taken back and not tried again for a while.
"""
from __future__ import annotations

from typing import Optional

STEP = 0.05
FLOOR = 0.40
LOW_AFTER = 4.0
HIGH_AFTER = 30.0
VERIFY_AFTER = 6.0
COOLDOWN = 3.0
FUTILE_PAUSE = 300.0
LOW_RATIO = 0.90
HIGH_RATIO = 0.98
GAIN_NEEDED = 1.08
UP_WAIT_MAX = 240.0
FLAPPING_WITHIN = 60.0


class QualityGovernor:
    def __init__(self, *, step: float = STEP, floor: float = FLOOR) -> None:
        self.step = float(step)
        self.floor = float(floor)
        self.ceiling: Optional[float] = None
        self._last_set: Optional[float] = None
        self._low_since: Optional[float] = None
        self._high_since: Optional[float] = None
        self._cooldown_until = 0.0
        self._futile_until = 0.0
        self._verify_at: Optional[float] = None
        self._verify_fps = 0.0
        self._verify_back = 0.0
        self._last_up_at: Optional[float] = None
        self._up_wait = HIGH_AFTER

    def reset(self) -> None:
        self._low_since = None
        self._high_since = None
        self._verify_at = None

    def observe(self, now: float, fps: float, scale: float,
                target: float) -> Optional[float]:
        """The scale to switch to, or None to stay as it is."""
        scale = float(scale)
        if self._last_set is None or abs(scale - self._last_set) > 1e-6:
            self.ceiling = scale
            self._last_set = scale
            self.reset()
            return None
        if not (fps > 0.0) or not (target > 0.0):
            self.reset()
            return None
        if now < self._futile_until:
            return None

        if self._verify_at is not None:
            if now < self._verify_at:
                return None
            self._verify_at = None
            if fps < self._verify_fps * GAIN_NEEDED and fps < target * LOW_RATIO:
                self._futile_until = now + FUTILE_PAUSE
                self._last_set = self._verify_back
                self._cooldown_until = now + COOLDOWN
                return self._verify_back
            return None

        if fps < target * LOW_RATIO:
            self._high_since = None
            if self._low_since is None:
                self._low_since = now
            lowered = round(scale - self.step, 2)
            if (now - self._low_since >= LOW_AFTER and now >= self._cooldown_until
                    and lowered >= self.floor - 1e-9):
                if (self._last_up_at is not None
                        and now - self._last_up_at < FLAPPING_WITHIN):
                    self._up_wait = min(self._up_wait * 2.0, UP_WAIT_MAX)
                self._low_since = None
                self._verify_at = now + VERIFY_AFTER
                self._verify_fps = fps
                self._verify_back = scale
                self._cooldown_until = now + COOLDOWN
                self._last_set = lowered
                return lowered
            return None

        self._low_since = None
        ceiling = self.ceiling if self.ceiling is not None else scale
        if fps >= target * HIGH_RATIO and scale < ceiling - 1e-9:
            if self._high_since is None:
                self._high_since = now
            if now - self._high_since >= self._up_wait and now >= self._cooldown_until:
                raised = min(ceiling, round(scale + self.step, 2))
                self._high_since = None
                self._last_up_at = now
                self._cooldown_until = now + COOLDOWN
                self._last_set = raised
                return raised
        else:
            self._high_since = None
        return None


__all__ = ["QualityGovernor"]

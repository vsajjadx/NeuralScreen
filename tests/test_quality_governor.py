"""The automatic work resolution: steps down under load, back up when there is room."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "app"))

from quality_governor import QualityGovernor, FLOOR


def run(gov, scale, seconds, fps_of, start=0.0, target=60.0):
    now = start
    changes = []
    while now < start + seconds:
        new = gov.observe(now, fps_of(scale), scale, target)
        if new is not None:
            scale = new
            changes.append((round(now, 1), new))
        now += 0.5
    return scale, changes, now


def main():
    gov = QualityGovernor()
    scale, _, now = run(gov, 0.65, 3, lambda s: 60.0)
    assert scale == 0.65

    gov = QualityGovernor()
    cost = lambda s: 60.0 - (s - 0.65) * 200.0
    scale, changes, now = run(gov, 0.80, 60, cost)
    assert scale < 0.80, "load never lowered the scale"
    assert cost(scale) >= 54.0 or scale <= FLOOR + 1e-9

    gov = QualityGovernor()
    scale, changes, now = run(gov, 0.80, 40, cost)
    low = scale
    scale, changes, now = run(gov, scale, 400, lambda s: 70.0, start=now)
    assert low < scale <= 0.80 + 1e-9, "never came back up toward the user's value"

    gov = QualityGovernor()
    scale, changes, now = run(gov, 0.65, 60, lambda s: 30.0)
    assert changes and changes[-1][1] == 0.65, "a futile step was not taken back"
    scale, changes, now = run(gov, scale, 120, lambda s: 30.0, start=now)
    assert not changes, "retried inside the pause"

    gov = QualityGovernor()
    scale, _, now = run(gov, 0.50, 120, lambda s: 10.0)
    assert scale >= FLOOR - 1e-9

    gov = QualityGovernor()
    scale, _, now = run(gov, 0.80, 20, cost)
    assert scale < 0.80
    assert gov.observe(now, 60.0, 0.90, 60.0) is None
    assert gov.ceiling == 0.90, "the user's own change did not become the ceiling"

    print("test_quality_governor: ok")


if __name__ == "__main__":
    main()

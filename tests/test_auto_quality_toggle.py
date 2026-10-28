"""The Automatic resolution switch: shown under Boost, flips the config, persists, restores."""
import os
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "app"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pygame
import commands
import settings_io
from test_ui_buttons import build, find, paint, click


def main():
    failures = []
    pygame.init()
    pygame.display.set_mode((64, 64))

    menu = build()
    menu.set_state({"nr_small": False})
    menu.layout(3840, 2160)
    paint(menu)
    if find(menu, "toggle", "auto_quality") is not None:
        failures.append("the switch is offered without Boost, where the work size does nothing")

    menu.set_state({"nr_small": True, "work_size": "2496x1404", "auto_quality": False})
    menu.layout(3840, 2160)
    paint(menu)
    toggle = find(menu, "toggle", "auto_quality")
    if toggle is None:
        failures.append("no Automatic resolution switch under Boost")
    else:
        out = click(menu, toggle)
        if out != [("toggle", "auto_quality")]:
            failures.append(f"switch emitted {out}")

    st = SimpleNamespace(
        cfg={"auto_quality": False}, work_scale=0.65, _aq_ceiling=None,
        display=SimpleNamespace(menu=MagicMock()))
    with patch.object(settings_io, "save_menu_layout") as save, \
            patch.object(commands.pipeline, "request_apply") as apply_, \
            patch.object(settings_io, "queued_apply",
                         return_value=(0.65, "Natural", {})):
        commands.apply_menu_action(st, ("toggle", "auto_quality"))
        if st.cfg["auto_quality"] is not True or apply_.called:
            failures.append("turning it on changed the scale or did not set the config")
        st.work_scale, st._aq_ceiling = 0.50, 0.65
        commands.apply_menu_action(st, ("toggle", "auto_quality"))
        if st.cfg["auto_quality"] is not False:
            failures.append("turning it off did not clear the config")
        if not apply_.called or abs(apply_.call_args.args[1] - 0.65) > 1e-9:
            failures.append("turning it off did not put the user's scale back")
        if st._aq_ceiling is not None:
            failures.append("the remembered scale outlived the switch")
        if save.call_count < 2:
            failures.append("the switch was not written to the config file")

    import json
    cfg = json.loads((ROOT / "config.default.json").read_text(encoding="utf-8"))
    cfg.update({"auto_quality": "yes", "auto_quality_target": "abc"})
    cfg = settings_io._validate_config(cfg)
    if cfg["auto_quality"] is not True:
        failures.append("a hostile boolean was not read as the bool it means")
    if cfg["auto_quality_target"] != 60:
        failures.append(f"a hostile target gave {cfg['auto_quality_target']!r}")
    cfg.update({"auto_quality_target": 100000})
    if settings_io._validate_config(cfg)["auto_quality_target"] > settings_io.FRAME_LIMIT_CUSTOM_MAX:
        failures.append("the target is not capped")

    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print("OK: Automatic resolution switch")
    return 0


if __name__ == "__main__":
    sys.exit(main())

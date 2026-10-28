"""Interface strings and the brand palette - shared by every window.

Split out of settings_ui because the overlay menu needs exactly the same
things and settings_ui drags tkinter along. The single source of truth: string
and colour edits happen here.

The localizations below are the only place in the project where non-English
text belongs.
"""

from __future__ import annotations

DEFAULT_LANG = "en"

# --- Localization: EVERY UI + HUD/alert string ---------------------------
STRINGS = {
    "en": {
        "theme": "Theme",
        "monitor": "Monitor",
        "gpu": "GPU",
        "gpu_switched": "GPU switched - the worker restarted",
        "gpu_hint": "capture and network share one card - RTX 30/40/50",
"gpu_nr_fail": "This GPU cannot run the neural pass - the picture stays unprocessed",
"gpu_reverted": "That GPU cannot run the neural pass - back to the previous card",
"gpu_split": "The chosen GPU cannot capture the selected display - frames go through Python",
        "shot_dir_btn": "Screenshot folder...",
        "change_folder": "Change folder...",
        "nr_not_running": "NR is on, but the worker is not processing - restart it or pick another source",
        "refresh_list": "Refresh list",
        "autostart": "Autostart with Windows",
        "rec_indicator": "Recording indicator",
        "gpu_record": "Record on the GPU",
        "gpu_record_hint": "no FPS cost; the menu is not recorded",
        "fps_overlay": "Frame counter",
        "nr_passes": "NR passes",
        "pass_params": "Own set for passes 2+",
        "pass_params_hint": "Passes 2 and later use these values; pass 1 still follows the profile",
        "tray_on_minimise": "Minimise to tray",
        "tray_on_close": "Close to tray",
        "keep_speed_when_hidden": "Keep full speed when hidden",
        "keep_speed_hint": "costs power and heat",
        "tray_hint": "the neural pass keeps running in the tray",
        "tray_missing": "The tray icon is not available",
        "spout": "Spout2 output (OBS)",
        "spout_hint": "adds a Spout2 Capture source in OBS, in any mode",
        "autostart_on": "Autostart ON",
        "autostart_off": "Autostart OFF",
        "autostart_err": "Autostart failed",
        "open_on_start": "Open menu on launch",
        "github": "Help",
        "split": "Before / after wipe",
        "sec_processing": "processing",
        "sec_compare": "comparison",
        "lens": "Camera",
        "lens_bend": "Fisheye",
        "lens_vignette": "Vignette",
        "lens_aberration": "Chromatic aberration",
        "lens_sensor": "Small sensor",
        "lens_soft": "Soft corners",
        "notifications": "Notifications",
        "notify_all": "All",
        "notify_errors": "Errors",
        "notify_none": "None",
        "process_priority": "Process priority",
        "prio_normal": "Normal",
        "prio_above": "Above normal",
        "prio_high": "High",
        "lens_fov": "Field of view",
        "lens_fov_hint": "set it to the game's FOV; clicks off the centre land elsewhere",
        "lens_noise": "Webcam noise",
        "sec_resolution": "resolution",
        "sec_effect": "effect",
        "sec_source": "source",
        "sec_view": "appearance",
        "sec_actions": "actions",
        "tab_capture": "Capture",
        "tab_rec": "Media",
        "sec_convert": "file conversion",
        "convert_pick": "Convert a file...",
        "convert_hint": "the picture your sliders are set to, applied to a file",
        "convert_working": "Converting {name}...",
        "convert_done": "Converted: {path}",
        "convert_failed": "Conversion failed: {details}",
        "convert_busy": "A conversion is already running",
        "tab_keys": "Keys",
        "tab_app": "Program",
        "sec_capture": "capture",
        "sec_recording": "recording",
        "sec_behaviour": "behaviour",
        "sec_hotkeys": "hotkeys",
        "hotkeys_enabled": "Global hotkeys",
        "hotkey_none": "none",
        "hotkey_clear": "Backspace clears a key",
        "hotkeys_off_hint": "the numpad is free for other programs while they are off",
        "about_version": "Version",
        "about_driver": "Driver",
        "about_windows": "Windows",
        "sec_about": "about",
        "compatibility": "Compatibility",
        "compatibility_pass": "validated",
        "compatibility_unsupported": "unsupported",
        "compatibility_quarantined": "temporarily quarantined",
        "compatibility_failed": "failed",
        "compatibility_not_run": "not run",
        "diagnostics_create": "Create diagnostic package",
        "diagnostics_working": "Creating diagnostic package...",
        "diagnostics_saved": "Diagnostic package: {path}",
        "diagnostics_failed": "Could not create diagnostic package",
        "sec_windows": "windows",
        "collapse": "Collapse",
        "one_window": "One window",
        "fullscreen": "Fullscreen",
        "fs_active": "Fullscreen is already active",
        "mode_fullscreen": "Fullscreen",
        "mode_window": "Window mode",
        "windows_btn": "Select window...",
        "save_preset": "Save preset",
        "delete_preset": "Delete preset",
        "windows_title": "Select window",
        "win_list_empty": "No windows found",
        "back": "Back",
        "exit_full": "Quit and unload from memory",
        "exit_note": "processing stops, the overlay disappears",
        "style": "Model",
        "style_0": "Default",
        "style_1": "Natural",
        "style_2": "Cinematic",
        "theme_light": "Light",
        "theme_dark": "Dark",
        "theme_contrast": "Contrast",
        "hk_nr": "Neural Rendering on/off",
        "hk_menu": "Menu",
        "hk_shot": "Screenshot",
        "hk_record": "Recording",
        "hk_window": "One-window mode",
        "hk_quit": "Quit",
        "hk_scale_up": "Processing resolution up",
        "hk_scale_down": "Processing resolution down",
        "hk_nr_passes_up": "NR passes up",
        "hk_nr_passes_down": "NR passes down",
        "nr_passes_runs": "runs {live}",
        "hotkey_hint": "the keys live here, and nowhere else in the menu",
        "hotkey_press": "press a key...",
        "hotkey_bad": "key combination not recognised",
        "wipe_zero": "0%",
        "split_hint": "share of the frame left unprocessed; 0% = wipe off",
        "off": "off",
        "ui_scale": "Scale",
        "nr_res": "Resolution the network runs at",
        "nr_res_hint": "lower = more FPS, weaker neural effect",
        "nr_res_low": "more FPS",
        "nr_res_high": "finer detail",
        "nr_res_full": "full",
        "boost": "Boost",
        "hdr_on": "HDR display is using SDR capture. HDR brightness and colours are not preserved.",
        "hdr_mode": "HDR compatibility (experimental)",
        "hdr_mode_hint": "captures an HDR screen in FP16; restarts the worker",
        "hdr_mode_on": "HDR compatibility ON",
        "hdr_mode_off": "HDR compatibility OFF",
        "monitor_hint": "a layer over the whole monitor - it cannot be dragged",
        "gpu_no_nr": "no neural pass",
        "win_mode_on": "One window: processing the focused window",
        "win_mode_off": "Full screen again",
        "spout_on": "Spout2 output ON",
        "status_on": "processing",
        "status_off": "not processing",
        "status_fg_only": "frame generation",
        "status_fg_waiting": "frame generation starting",
        "spout_off": "Spout2 output OFF",
        "win_none": "No window to capture - focus one first",
        "win_fail": "This window cannot be captured",
        "mon_fail": "This monitor cannot be captured",
        "github_opened": "Opened in browser",
        "started": "NeuralScreen is running · Num2 for menu",
        "hotkeys": "Num1 NR · Num2 menu · Num3 shot · Num0 record · Ctrl+Alt+Q quit",
        "numlock_off": "Num Lock is off - the numpad hotkeys will not work",
        "title": "NeuralScreen",
        "settings_title": "Settings",
        "profile_modified": "modified · revert",
        "profile": "Profile",
        "intensity": "Intensity",
        "local_tone": "Local tone",
        "local_structure": "Local structure",
        "skin_structure": "Skin structure",
        "language": "Language",
        "apply": "Apply",
        "close": "Close",
        "exit": "Exit",
        "screenshot": "Screenshot",
        "record": "Record",
        "record_stop": "Stop recording",
        "record_stop_short": "Stop",
        "nr_on": "DLSS 5 ON",
        "nr_off": "DLSS 5 OFF",
        "record_on": "REC ● (Num0 to stop)",
        "record_off": "Recording saved",
        "shot_fail": "Screenshot failed",
        "rec_save_fail": "Recording may be incomplete",
        "save_fail": "Could not save settings",
        "work_scale_changed": "Work scale {:.2f} ({:d}x{:d})",
        "settings_applied": "Settings applied",
        "lang_ru": "Русский",
        "lang_en": "English",
        "lang_fr": "Français",
        "lang_de": "Deutsch",
        "lang_es": "Español",
        "lang_it": "Italiano",
        "lang_pt": "Português",
        "lang_pl": "Polski",
        "lang_uk": "Українська",
        "lang_zh": "中文",
        "lang_ja": "日本語",
        "lang_ko": "한국어",
    },
}


for _strings in STRINGS.values():
    _strings.update({
        "motion_backend": "Motion estimation",
        "motion_nvofa": "NVOFA",
        "motion_hint": "Restarts worker; CPU fallback if unavailable",
        "motion_fallback": "NVOFA unavailable - using CPU DIS",
        "motion_restarted": "Motion backend changed - worker restarted",
    })


def tr(lang: str, key: str) -> str:
    """Translate a STRINGS key; an unknown key comes back unchanged."""
    return STRINGS.get(lang, STRINGS[DEFAULT_LANG]).get(key, key)

for _table in STRINGS.values():
    _table.update({















    })

STRINGS['en'].update({'frame_generation': 'FSR Frame Generation', 'frame_multiplier': 'Output frame multiplier', 'fg_capped': 'this card caps at x{live}; x{want} is retried next time'})

for _strings in STRINGS.values():
    _strings.update({
        "fg_on": "FSR FG ON",
        "fg_off": "FSR FG OFF",
        "fg_fail": "Frame Generation could not start on this GPU - the switch is back off",
        "hk_framegen": "Frame Generation on/off",
    })

# The camera colour (#164, v2.6.0).
STRINGS["en"].update({"lens_saturation": 'Saturation', "lens_exposure": 'Exposure'})
STRINGS["en"].update({"lens_contrast": 'Contrast', "lens_warmth": 'Warmth', "lens_tint": 'Tint'})
# The colour block (#164, v2.6.0).
STRINGS["en"].update({"fold_colour": 'Colour', "colour_as_is": 'As it is', "colour_changed": 'Changed: {n}'})
# #157: window mode follows the focus (v2.6.0).
STRINGS["en"].update({"window_follow": 'Follow the focused window', "window_follow_hint": 'Window mode: the window in focus for 2.5 s becomes the source'})
# The NR model preset (310.9.16, v2.6.0).
STRINGS["en"].update({"nr_preset": 'NR preset', "nr_preset_default": 'Auto', "nr_preset_restarted": 'NR preset changed - worker restarted'})
# #175: the BYO NR runtime fell back to the bundled one (v2.6.1).
STRINGS["en"].update({"byo_fallback": 'The NR runtime in native\\libraries\\ does not run on this card - using the bundled one'})
# #174: the recording codec (v2.6.1).
STRINGS["en"].update({"record_codec": 'Recording codec'})
# #176: a borderless captured window (v2.6.1).
STRINGS["en"].update({"borderless_on": 'Borderless, full monitor', "borderless_off": 'Restore the frame'})
# #179: a runtime with one NR model (v2.6.2).
STRINGS["en"].update({"nr_preset_one": 'one model{v}'})
# #181: NR off for the session after repeated driver resets (v2.6.2).
STRINGS["en"].update({"gpu_reset_off": 'The GPU driver reset again under NR - NR is off for this session (Num1 turns it on)'})
# #157: dim while rebuilding (v2.6.0).
STRINGS["en"].update({"dim_on_rebuild": 'Dim while rebuilding'})
# FG interface protection (v2.6.0).
STRINGS["en"].update({"fg_ui_protect": 'Protect the interface in FG', "fg_ui_protect_hint": 'still HUD and text keep the real frame'})
STRINGS["en"].update({"auto_quality": 'Automatic resolution', "auto_quality_hint": 'lowers the work size when NR falls under the target'})
STRINGS["ru"].update({"auto_quality": 'Авто-разрешение', "auto_quality_hint": 'снижает рабочий размер, если NR отстаёт от цели'})
STRINGS["fr"].update({"auto_quality": 'Résolution automatique', "auto_quality_hint": 'réduit la taille de travail si NR passe sous la cible'})
STRINGS["de"].update({"auto_quality": 'Automatische Auflösung', "auto_quality_hint": 'senkt die Arbeitsgröße, wenn NR unter das Ziel fällt'})
STRINGS["es"].update({"auto_quality": 'Resolución automática', "auto_quality_hint": 'reduce el tamaño de trabajo si NR baja del objetivo'})
STRINGS["it"].update({"auto_quality": 'Risoluzione automatica', "auto_quality_hint": "riduce la dimensione di lavoro se NR scende sotto l'obiettivo"})
STRINGS["pt"].update({"auto_quality": 'Resolução automática', "auto_quality_hint": 'reduz o tamanho de trabalho se o NR ficar abaixo da meta'})
STRINGS["pl"].update({"auto_quality": 'Automatyczna rozdzielczość', "auto_quality_hint": 'zmniejsza rozmiar roboczy, gdy NR spada poniżej celu'})
STRINGS["uk"].update({"auto_quality": 'Авто-роздільність', "auto_quality_hint": 'зменшує робочий розмір, якщо NR відстає від цілі'})
STRINGS["zh"].update({"auto_quality": '自动分辨率', "auto_quality_hint": 'NR 低于目标帧率时自动降低处理分辨率'})
STRINGS["ja"].update({"auto_quality": '自動解像度', "auto_quality_hint": 'NR が目標を下回ると処理サイズを下げる'})
STRINGS["ko"].update({"auto_quality": '자동 해상도', "auto_quality_hint": 'NR이 목표 아래로 떨어지면 처리 크기를 낮춤'})
# Redesign B (v2.6.0): the state tiles and the fold-out headers.
STRINGS["en"].update({"tile_on": "On", "tile_off": "Off", "fold_net": "Network", "fold_tune": "Fine tuning", "fold_source": "Source and comparison"})
# The camera hotkey (#164): its row in the hotkey list and the pop-up it shows.
STRINGS["en"].update({"hk_lens": "Camera on/off", "lens_on": "Camera ON", "lens_off": "Camera OFF"})












for _strings in STRINGS.values():
    _strings.update({
        "frame_limit": "Frame limit",
        "frame_limit_hint": "caps the source frames; generated ones are extra",
        "frame_limit_30": "30 fps",
        "frame_limit_60": "60 fps",
        "frame_limit_custom": "Custom",
        "frame_limit_unlimited": "Unlimited",
        "frame_limit_custom_value": "Custom limit",
        "nr_short": "NR",
        "fg_short": "FG",
        "screenshot_mode": "Screenshot saving",
        "screenshot_ask": "Save As",
        "screenshot_auto": "Save automatically",
        "screenshot_format": "Screenshot format",
        "shot_saved": "Screenshot saved: {path}",
        "record_dir_btn": "Recording folder...",
        "select_shot_dir": "Select the screenshot folder",
        "select_record_dir": "Select the recording folder",
        "shot_folder_set": "Screenshot folder: {path}",
        "record_folder_set": "Recording folder: {path}",
        "record_finalizing": "Finalizing...",
        "record_started": "Recording: {details}",
        "record_saved": "Recording saved: {path}",
        "record_saved_cut": "Recording saved, cut short: {path}",
        "record_failed_stage": "Recording failed ({stage}); partial: {path}",
        "record_state": "State",
        "record_format": "Format",
        "record_path": "Path",
        "record_status_recording": "Recording",
        "record_status_finalizing": "Finalizing",
        "record_status_published": "Saved",
        "record_status_failed": "Failed",
    })

# --- M3: strings that were verbatim English in ten languages ---











# --- The conversion page (the queue, its rows and the output choices) -------
# One block per language, like the ones above. `convert_pick` is replaced on
# purpose: the button no longer opens a picker for one file, it opens the
# page that holds the queue.
STRINGS["en"].update({
    'convert': 'Convert',
    'convert_title': 'File conversion',
    'convert_pick': 'Convert files...',
    'convert_add': 'Add files...',
    'convert_dialog': 'Choose images or videos to convert',
    'convert_empty': 'Nothing here yet. Add images or videos, or drop them anywhere on the screen while this page is open.',
    'convert_page_hint': 'Each file gets the look the panel shows when it is added. With NR off on the main page it converts faster.',
    'sec_convert_queue': 'files',
    'sec_convert_output': 'output',
    'convert_queued': 'waiting',
    'convert_kind_video': 'video',
    'convert_kind_image': 'image',
    'convert_starting': 'starting the network...',
    'convert_processing': 'processing...',
    'convert_writing': 'writing the file...',
    'convert_frames': '{n} frames',
    'convert_left': '{time} left',
    'convert_done_row': 'done in {time}',
    'convert_audio_aac': 'audio as AAC',
    'convert_audio_dropped': 'no audio',
    'convert_unprocessed': '{n} frames not processed',
    'convert_cancelled': 'stopped',
    'convert_err_denied': 'cannot write there - pick a folder below',
    'convert_err_unreadable': 'this file cannot be read',
    'convert_err_worker': 'the network stopped - try again',
    'convert_err_encode': 'the file could not be written',
    'convert_err_unknown': 'failed - details in the log',
    'convert_act_stop': 'Stop',
    'convert_act_remove': 'Remove',
    'convert_act_show': 'Show',
    'convert_act_retry': 'Retry',
    'convert_clear': 'Clear finished',
    'convert_stop_all': 'Stop all',
    'convert_dest': 'Save to',
    'convert_dest_source': 'Beside original',
    'convert_dest_folder': 'One folder',
    'convert_codec': 'Video codec',
    'convert_codec_auto': 'Auto',
    'convert_quality': 'Quality',
    'convert_quality_high': 'Best',
    'convert_quality_balanced': 'Balanced',
    'convert_quality_small': 'Smaller',
    'convert_image_format': 'Image format',
    'convert_image_keep': 'Original',
    'convert_audio': 'Keep audio',
    'convert_added': 'Added to the conversion queue: {n}',
    'convert_refused': 'Not added ({n}): unsupported format or already queued',
    'convert_stopped': 'Conversion stopped',
    'convert_batch_done': 'Converted {done} of {total} files',
    'select_convert_dir': 'Select the folder for converted files',
})













# --- 22.09 audit pass: messages that were missing, hardcoded or one-language -----
# A failed menu action, a hotkey that cannot be had, the preset alerts (they
# were English in every language), a source the converter refuses, and the
# compatibility dialog (it was Russian in every language).
_AUDIT_STRINGS = {
    "en": {
        "action_failed": "That did not work - the details are in NeuralScreen.log",
        "hotkey_taken": "{} is already used by another action",
        "hotkey_in_use": "{} is taken by another program - it will not work",
        "preset_saved": "Preset saved: {}",
        "preset_deleted": "Preset deleted: {}",
        "convert_err_hdr": "HDR video cannot be converted yet",
        "convert_err_too_large": "too large - 7680x4320 at most",
        "compat_title": "NeuralScreen - compatibility check",
        "compat_blocked": (
            "NeuralScreen will not start screen capture: the compatibility "
            "check did not pass.\n\nResult: {verdict}.\n\nDiagnostic package:\n"
            "{bundle}\n\n\"Retry\" clears this verdict and runs the check again."),
        "compat_verdict": "{status}; stage {stage}; passed {passed}/{attempted}, expected {expected}",
        "compat_not_run": "the check did not run",
        "compat_bundle_failed": "could not be created; details in NeuralScreen.log",
        "compat_driver_old": "The NVIDIA driver on this computer ({driver}) is too old for the neural renderer: the runtime reported it out of date before the check failed. Install the current NVIDIA driver, then press Retry.",
        "compat_no_effect": "The neural renderer answered every check with success but did not change the picture at all: on this graphics card (or with this runtime) it does not run. NeuralScreen needs an RTX 30, 40 or 50 card and the bundled runtime.",
    },
}
for _lang, _extra in _AUDIT_STRINGS.items():
    STRINGS[_lang].update(_extra)
del _lang, _extra

# Experimental desktop frame generation

Off by default. Enable Frame Generation in the processing menu and select
an output multiplier from 2x to 5x. Requires
`native/amd_fidelityfx_loader_dx12.dll` and
`native/amd_fidelityfx_framegeneration_dx12.dll` (AMD FidelityFX SDK 2.3.0,
tracked in the repository).

The backend is AMD FSR Frame Generation (FG 4.0.1) through the FidelityFX API,
used without AMD's swap chain: only the interpolation runs there, and the
presenter below keeps pacing the frames. It replaced DLSS Frame Generation,
which is no longer used or shipped. The FidelityFX API has no capability query
for the multiplier: a runtime that refuses a step is walked down to the one it
takes, and a refusal at 2x turns the switch back off.

Generated frames follow neural rendering. A separate presenter
paces bounded frame slots and drops outdated generated frames. It still shares
the GPU command queue with neural rendering; this does not guarantee lower latency
or higher displayed FPS under GPU saturation.

HDR capture stays FP16 scRGB. The edited result is converted to HDR10 PQ for FG,
which is given as a PQ transfer function (scRGB is not used for FG). Ordinary HDR presentation continues to use scRGB.
Recording and Spout export real SDR frames, not generated frames.

This is experimental: depth is flat and motion is estimated from capture.
Occlusion and UI artifacts remain possible. HUD detection is a separate proposal.
FG runs on both present paths: with NR off it presents the raw capture rather
than the neural frame, so the doubled rate survives turning the neural pass off.
An FG failure returns that frame to ordinary presentation.

Validation:
- `runtime/python.exe tests/test_framegen_controls.py`
- `runtime/python.exe tests/test_frame_generation.py --run`
- `runtime/python.exe tests/test_frame_generation.py --hdr --dynamic --check-pixels`
- `native/test-hdr.bat`

Native tests require an NVIDIA GPU; HDR tests require an HDR-enabled display.

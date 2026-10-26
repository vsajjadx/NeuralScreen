# Experimental desktop frame generation

Off by default. Enable Frame Generation in the processing menu and select
an output multiplier from 2x to 5x. Requires
`native/amd_fidelityfx_loader_dx12.dll` and
`native/amd_fidelityfx_framegeneration_dx12.dll` (AMD FidelityFX SDK 2.3.0,
AMD-signed), supplied separately from the source repository:
`native\fetch-fsr-runtime.bat` downloads the pinned copies and checks their
SHA-256.

The backend is AMD FSR Frame Generation. The context is created through the
FidelityFX API (`ffxCreateContext`) and used without FSR's swap chain proxy
(`FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY`): per frame the worker
calls `ffxConfigure`, the prepare dispatch and the generation dispatch into
textures it owns, and its own presenter shows them. The multiplier stops at 5x
because the dispatch carries four output targets; a provider that refuses a step
is walked down to the one it takes.

The runtime picks the newest provider for the GPU: FSR Frame Generation 4.0.1
(the ML one) needs an AMD Radeon 9000-series GPU, Windows 11 and DirectX 12
Agility SDK 1.4.9. Where it cannot be created, or refuses even 2x, it is set
aside and the legacy analytical provider (Shader Model 6.2) is tried; the log
names the one in use (`[fg] FSR provider: ...`). Which GPUs run which provider is
not validated yet - see the compatibility table in the README.

Generated frames follow neural rendering. A separate presenter
paces bounded frame slots and drops outdated generated frames. It still shares
the GPU command queue with neural rendering; this does not guarantee lower latency
or higher displayed FPS under GPU saturation.

HDR capture stays FP16 scRGB. The edited result is converted to HDR10 PQ for FG
(the FSR context is told the transfer function). Ordinary HDR presentation
continues to use scRGB. Recording and Spout export real SDR frames, not
generated frames.

This is experimental: depth is flat and motion is estimated from capture.
Occlusion and UI artifacts remain possible. HUD detection is a separate proposal.
FG runs on both present paths: with NR off it presents the raw capture rather
than the neural frame, so the doubled rate survives turning the neural pass off.
An FG failure returns that frame to ordinary presentation.

Bring your own runtime: AMD-signed `amd_fidelityfx_loader_dx12.dll` and
`amd_fidelityfx_framegeneration_dx12.dll` from the same SDK release, placed in
`native/libraries/`, win over the bundled pair after an AMD signature check.

Validation:
- `runtime/python.exe tests/test_framegen_controls.py`
- `runtime/python.exe tests/test_frame_generation.py --run`
- `runtime/python.exe tests/test_frame_generation.py --hdr --dynamic --check-pixels`
- `native/test-hdr.bat`

Native tests require a Windows GPU with the FidelityFX runtime in `native/`;
HDR tests require an HDR-enabled display.

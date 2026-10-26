// Experimental desktop frame generation on AMD FSR (the FidelityFX API of FSR
// SDK 2.3: FSR Frame Generation 4.0.1 and the legacy analytical provider that
// ships in the same DLL). Capture has no engine depth: use a flat plane and the
// estimated motion field. Keep this opt-in; UI/occlusions can distort.
//
// The worker owns its swap chain and its pacing (FgPresenter below), so the
// FSR frame generation swap chain proxy is NOT used: the context only
// interpolates (FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY) into
// textures this file allocates, and the presenter shows them as it always did.
// Per frame the call order the SDK requires is ffxConfigure, then the prepare
// dispatch (depth, motion vectors, camera), then the frame generation dispatch.
#include "include/fsr/api/include/dx12/ffx_api_dx12.h"
#include "include/fsr/framegeneration/include/ffx_framegeneration.h"

// The FidelityFX entry points, bound at run time from the signed loader
// (amd_fidelityfx_loader_dx12.dll), which in turn maps the effect DLL
// amd_fidelityfx_framegeneration_dx12.dll that sits next to it. Bound by
// GetProcAddress rather than an import library so that the absence of the AMD
// runtime stays what the absence of the NVIDIA one was: the FG switch refuses
// politely and nothing else breaks.
static struct FfxApi {
    HMODULE module = nullptr, provider = nullptr;
    PfnFfxCreateContext create = nullptr;
    PfnFfxDestroyContext destroy = nullptr;
    PfnFfxConfigure configure = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxDispatch dispatch = nullptr;
} g_ffx;

// The exception code of the last FidelityFX call that faulted inside AMD's
// runtime, 0 when it did not. NR's create and evaluate have always been
// guarded so; an old driver that faults instead of refusing (#145's shape)
// would otherwise take the whole worker down with FG on - no failure line, a
// restart, and the same again on the next frame with FG. Each call below is
// its own function: __try cannot share a body with objects that unwind.
static DWORD g_fg_fault = 0;
static ffxReturnCode_t FgCreateGuarded(ffxContext *context, ffxCreateContextDescHeader *desc)
{
    g_fg_fault = 0;
    __try
    {
        if (TestFailureOnce("fg-create-fault"))
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        return g_ffx.create(context, desc, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_fg_fault = GetExceptionCode(); }
    return FFX_API_RETURN_ERROR;
}
static ffxReturnCode_t FgConfigureGuarded(ffxContext *context, const ffxConfigureDescHeader *desc)
{
    g_fg_fault = 0;
    __try { return g_ffx.configure(context, desc); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_fg_fault = GetExceptionCode(); }
    return FFX_API_RETURN_ERROR;
}
static ffxReturnCode_t FgDispatchGuarded(ffxContext *context, const ffxDispatchDescHeader *desc)
{
    g_fg_fault = 0;
    __try
    {
        if (TestFailureOnce("fg-evaluate-fault"))
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        return g_ffx.dispatch(context, desc);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_fg_fault = GetExceptionCode(); }
    return FFX_API_RETURN_ERROR;
}
static ffxReturnCode_t FgQueryGuarded(ffxContext *context, ffxQueryDescHeader *desc)
{
    g_fg_fault = 0;
    __try { return g_ffx.query(context, desc); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_fg_fault = GetExceptionCode(); }
    return FFX_API_RETURN_ERROR;
}
static ffxReturnCode_t FgDestroyGuarded(ffxContext *context)
{
    g_fg_fault = 0;
    __try { return g_ffx.destroy(context, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_fg_fault = GetExceptionCode(); }
    return FFX_API_RETURN_ERROR;
}

// What the runtime says about itself goes to the log (errors only): a refusal
// then names its own reason instead of a bare return code.
static void FgRuntimeMessage(uint32_t type, const wchar_t *message)
{
    Log("[fg] FidelityFX %s: %ls", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning",
        message != nullptr ? message : L"(no text)");
}

// The most generated frames per real one: ONE (2x). The dispatch description
// carries four output targets, but the SDK 2.3 providers write only outputs[0]
// and return OK for any count, so a higher step is never refused - it would
// present frames nobody wrote. Raise with FG_MULTIPLIER_MAX in app/protocol.py
// when a provider fills more.
static constexpr unsigned kFgMaxGenerated = 1;

struct FgSlot {
    winrt::com_ptr<ID3D12Resource> real, interpolated[kFgMaxGenerated];
    unsigned count = 1;
    int state = 0; // free, writing, ready, presenting; protected by mutex
    UINT64 sequence = 0;
    bool interpolate = false;
    double interval = 0.016;
};
static struct FgState {
    // The FidelityFX frame generation context, and the id and name of the
    // provider it was created on (the newest the runtime offers for this GPU,
    // or the one a refusal left over).
    ffxContext context = nullptr;
    uint64_t provider_id = 0;
    VideoTex depth;
    winrt::com_ptr<ID3D12Resource> output[kFgMaxGenerated];
    // FidelityFX wants a frame id that goes up by exactly one per frame; any
    // other step resets the interpolation. Counted per context.
    uint64_t frame_id = 0;
    UINT w = 0, height = 0, mw = 0, mh = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    FgSlot slots[3];
    std::mutex mutex;
    std::condition_variable wake;
    std::thread thread;
    std::atomic<bool> stop{false}, failed{false};
    // Set with `stop`: the presenter's vblank wait wakes on it at once. Only
    // the condition variable woke on `stop` before, and a presenter standing
    // in the 2 s waitable wait (then a 2 s copy wait) held every caller of
    // StopFgPresentation - a resize, a format change, ClosePresent, the FG
    // toggle - for up to ~4 s (#138's residual).
    HANDLE stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    UINT64 sequence = 0;
    // `last` is the call time of the last NEW picture (the interval when the
    // source has no clock), `last_slot` of the last slot queued at all, and
    // `last_source` the source's own time of the last new picture.
    std::chrono::steady_clock::time_point last, last_slot;
    double last_source = 0.0;
    // Frames the capture did not renew and FG therefore did not take (#132).
    std::atomic<unsigned> held{0};
    bool history = false;
    // The store goes under the mutex (see StopFgPresentation).
    ~FgState()
    {
        { std::lock_guard<std::mutex> lock(mutex); stop = true; }
        wake.notify_all();
        if (thread.joinable()) thread.join();
    }
} g_fg;
// g_fg_present_fence lives in dlss5-feed-host64.cpp near the FgPresent
// forward declaration: PresentFrame consumes it for the defer-tail token,
// and the include of this file sits below that call site.

static int g_fg_ui_enabled = -1;
static unsigned g_fg_count = 1;
// What we actually ask the runtime for, 0-based: 1 = 2x, 3 = 4x, 4 = 5x.
//
// It starts at the 5x ceiling and drops only when a create or dispatch is
// actually refused - the runtime itself answered "not that step". Refusals on
// a provider that stops at 2x (5x, 4x, then 3x) land it on 2x and Frame
// Generation keeps running; before this, the first refusal killed the feature
// outright. Switching FG off and on re-arms the user's choice, so a newer
// runtime or driver update gets a fair try without a restart.
static unsigned g_fg_count_limit = kFgMaxGenerated;
// Set when a refusal was answered by stepping the multiplier down: that frame
// is a retry, not a failure. FgPresent reads and clears it.
static bool g_fg_retry = false;

// Providers that were created fine but refused to dispatch on this GPU (the
// ML provider on a card without the hardware it needs, say), by provider id:
// the next create skips them and takes the newest provider that is left. A
// fact about the GPU, so it lasts as long as the process does.
static std::vector<uint64_t> g_fg_rejected_providers;
static const char *g_fg_provider_name = "";

// Step the multiplier down after a refusal. True when a lower step remains to
// try; false when 2x itself was refused, which is the real "cannot run here".
static bool FgStepDown(unsigned refused)
{
    if (g_fg_count <= 1) return false;
    Log("[fg] %ux refused 0x%08X; stepping down to %ux",
        g_fg_count + 1, refused, g_fg_count);
    g_fg_count_limit = g_fg_count - 1;
    return true;
}

static bool FgRequested()
{
    static const bool requested = [] {
        char value[8] = {};
        return GetEnvironmentVariableA("NS_FRAMEGEN", value, sizeof(value)) == 1 && value[0] == '1';
    }();
    return (g_fg_ui_enabled < 0 ? requested : g_fg_ui_enabled != 0) && !g_fg.failed;
}

static void StopFgPresentation()
{
    // Stored under the mutex. The presenter checks `stop` in its wait
    // predicate and then sleeps with no timeout; a store and notify that land
    // between the check and the sleep were lost, and the join below blocked
    // the worker for good (a stop on every FG toggle, resize, format change
    // and ClosePresent - rare, but a hang the client could only time out).
    { std::lock_guard<std::mutex> lock(g_fg.mutex); g_fg.stop = true; }
    if (g_fg.stop_event != nullptr) SetEvent(g_fg.stop_event);
    g_fg.wake.notify_all();
    if (g_fg.thread.joinable()) g_fg.thread.join();
    // The presenter has stopped: nothing waits on the handle any more.
    CloseFgWaitable();
    // A failed fence means the queue may still own every resource below.
    // Keep them alive until process teardown instead of releasing them from
    // the recovery path.
    if (g_submission_failed) { g_fg.history = false; return; }
    // Hand the swapchain back to the ordinary present path: default latency.
    if (g_present_swap != nullptr)
    {
        IDXGISwapChain2 *sc2 = nullptr;
        if (SUCCEEDED(g_present_swap->QueryInterface(
                __uuidof(IDXGISwapChain2), reinterpret_cast<void **>(&sc2)))
            && sc2 != nullptr)
        {
            sc2->SetMaximumFrameLatency(3);
            sc2->Release();
        }
    }
    for (auto &slot : g_fg.slots) { slot.real = nullptr; for (auto &image : slot.interpolated) image = nullptr; slot.state = 0; }
    g_fg.history = false;
}

static void CloseFgResources()
{
    StopFgPresentation();
    if (g_submission_failed) return;
    if (g_fg.context)
    {
        // The presenter stopped and every dispatch was waited on, so nothing
        // in flight references the context's resources. The SDK asks for
        // frame generation to be disabled before the context goes.
        ffxConfigureDescFrameGeneration off = {};
        off.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        off.frameGenerationEnabled = false;
        off.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
        off.frameID = g_fg.frame_id;
        FgConfigureGuarded(&g_fg.context, &off.header);
        FgDestroyGuarded(&g_fg.context);
        g_fg.context = nullptr;
    }
    g_fg.provider_id = 0;
    g_fg.frame_id = 0;
    if (g_fg.depth.tex) { g_fg.depth.tex->Release(); g_fg.depth.tex = nullptr; }
    if (g_fg.depth.upload) { g_fg.depth.upload->Release(); g_fg.depth.upload = nullptr; }
    for (auto &image : g_fg.output) image = nullptr;
    g_fg.w = g_fg.height = 0;
}

// The presenter has its own command list/allocator/fence. The producer never
// waits for frame pacing. Slots being displayed cannot be recycled by it.
// It submits on the present queue - the swap chain's own - so a frame due on
// screen is copied and shown while the producer's next NR pass is still
// running, not after it.
static void FgPresenter()
{
    winrt::com_ptr<ID3D12CommandAllocator> alloc;
    winrt::com_ptr<ID3D12GraphicsCommandList> list;
    winrt::com_ptr<ID3D12Fence> fence;
    if (FAILED(h.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(alloc.put()))) ||
        FAILED(h.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.get(), nullptr, IID_PPV_ARGS(list.put()))) ||
        FAILED(h.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()))))
    { g_fg.failed = true; return; }
    list->Close();
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) { g_fg.failed = true; return; }
    UINT64 value = 0, previous = 0, shown = 0;
    auto report = std::chrono::steady_clock::now();
    // Where the presenter's time goes, for the phase profiler (NS_PHASE=1):
    // the copy wait runs from ExecuteCommandLists to the copy's fence, so it
    // holds whatever the queue had to finish first; the vblank wait is the
    // compositor's latency waitable. A generated frame is "late" when its
    // slot had passed before it could be shown, "superseded" when a newer
    // real frame arrived first. Counted always, logged only with the profiler.
    struct PresentStat { double sum = 0.0, max = 0.0; unsigned n = 0; };
    PresentStat copy_gen, copy_real, vblank;
    unsigned copy_bins[6] = {}, planned = 0, shown_gen = 0, late = 0,
             superseded = 0, flat = 0, skipped = 0;
    double last_copy_ms = 0.0;
    auto add = [](PresentStat &s, double ms) { s.sum += ms; if (ms > s.max) s.max = ms; ++s.n; };
    auto bin_of = [](double ms) {
        return ms < 1.0 ? 0 : ms < 2.0 ? 1 : ms < 4.0 ? 2 : ms < 8.0 ? 3 : ms < 16.0 ? 4 : 5;
    };
    // One back buffer is free when the presenter starts with a count in hand
    // (see the drain below): the first present takes it without waiting.
    bool free_slot = false;
    auto wait_vblank = [&](DWORD ms) {
        if (free_slot) { free_slot = false; return static_cast<DWORD>(WAIT_OBJECT_0); }
        const auto t = std::chrono::steady_clock::now();
        const HANDLE both[2] = {g_fg_waitable, g_fg.stop_event};
        const DWORD r = g_fg.stop_event != nullptr
            ? WaitForMultipleObjects(2, both, FALSE, ms)
            : WaitForSingleObject(g_fg_waitable, ms);
        add(vblank, std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t).count());
        return r;
    };
    // R11: when the swapchain gave us a frame-latency waitable object, the
    // compositor paces us: waiting on it releases one back buffer one
    // vblank before the previous frame hits the screen. From here on every
    // present waits for that release, and the wall-clock deadlines become a
    // second-order hint rather than the pacing source. Without the waitable
    // (pre-8.1, blocked QI) the old wall-clock deadlines stay.
    //
    // The waitable is a semaphore that every retired present raises, and the
    // ordinary path presents without ever waiting on it: after a few seconds
    // of NR-only presenting it held ~40 counts (measured, F6), so one wait
    // here left the rest and no FG wait blocked for the whole session - the
    // log said "paced by the compositor" while the clock paced it. Every
    // count is drained; holding at least one means a back buffer is free now,
    // and the first present takes it without a wait.
    if (g_fg_waitable != nullptr)
    {
        unsigned drained = 0;
        while (WaitForSingleObject(g_fg_waitable, 0) == WAIT_OBJECT_0) ++drained;
        free_slot = drained > 0;
        if (drained > 1)
            Log("[fg] the latency waitable held %u counts from ordinary presenting - "
                "drained", drained);
    }
    auto present = [&](ID3D12Resource *source) {
        winrt::com_ptr<ID3D12Resource> bb;
        HRESULT hr = g_present_swap->GetBuffer(
            g_present_swap->GetCurrentBackBufferIndex(), IID_PPV_ARGS(bb.put()));
        if (FAILED(hr) || bb.get() == nullptr)
            return FailGpuWork("fg-present", "get-buffer-error",
                               FAILED(hr) ? hr : E_POINTER);
        hr = alloc->Reset();
        if (FAILED(hr)) return FailGpuWork("fg-present", "allocator-error", hr);
        hr = list->Reset(alloc.get(), nullptr);
        if (FAILED(hr)) return FailGpuWork("fg-present", "command-error", hr);
        auto pre = Transition(bb.get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1, &pre);
        list->CopyResource(bb.get(), source);
        auto post = Transition(bb.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        list->ResourceBarrier(1, &post);
        hr = list->Close();
        if (FAILED(hr)) return FailGpuWork("fg-present", "command-error", hr);
        ID3D12CommandList *commands[] = {list.get()};
        const auto t_copy = std::chrono::steady_clock::now();
        PresentQueue()->ExecuteCommandLists(1, commands);
        hr = PresentQueue()->Signal(fence.get(), ++value);
        if (FAILED(hr))
        {
            bb.detach();
            return FailGpuWork("fg-present-fence", "fence-error", hr);
        }
        hr = fence->SetEventOnCompletion(value, event);
        if (FAILED(hr))
        {
            bb.detach();
            return FailGpuWork("fg-present-fence", "fence-error", hr);
        }
        const DWORD waited = WaitForSingleObject(event, 2000);
        if (waited != WAIT_OBJECT_0)
        {
            bb.detach();
            return FailGpuWork("fg-present-fence",
                               waited == WAIT_TIMEOUT ? "fence-timeout" : "fence-error",
                               waited == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(WAIT_TIMEOUT)
                                                      : HRESULT_FROM_WIN32(GetLastError()));
        }
        if (fence->GetCompletedValue() == UINT64_MAX)
        {
            bb.detach();
            return FailGpuWork("fg-present-fence", "fence-error",
                               DXGI_ERROR_DEVICE_REMOVED);
        }
        if (fence->GetCompletedValue() < value)
        {
            bb.detach();
            return FailGpuWork("fg-present-fence", "fence-error", E_FAIL);
        }
        last_copy_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t_copy).count();
        HRESULT pr = g_present_swap->Present(0, 0);
        if (TestFailureOnce("fg-mode-changed")) pr = DXGI_STATUS_MODE_CHANGED;
        // A change still IN PROGRESS loses this frame and nothing more: a
        // rebuild here would recreate the FSR context and the window on
        // every present for as long as the transition lasts (another app's
        // fullscreen switch, say). The ordinary path pays no feature create,
        // so it may rebuild on both; FG waits for MODE_CHANGED.
        if (pr == DXGI_STATUS_MODE_CHANGE_IN_PROGRESS) return true;
        if (pr == DXGI_STATUS_MODE_CHANGED)
        {
            // The mode changed under us and the chain is left behind. A
            // colour-depth change (#58) brings no resize, so nothing else
            // would rebuild it: mark it stale like PresentStatus does, and
            // the frame loop's RebuildPresentIfStale builds the window again
            // (stopping this presenter first). Until then each present here
            // is lost - said once, not on every present.
            if (!g_present_stale.exchange(true))
                Log("[fg] present reports a mode change (0x%08X) - rebuilding the window",
                    (unsigned)pr);
            return true;
        }
        if (pr == DXGI_STATUS_OCCLUDED)
        {
            // The window is hidden (minimised target): benign.
            return true;
        }
        if (FAILED(pr)) return FailGpuWork("present", "present-error", pr);
        RevealOnFirstPresent();
        ++shown;
        return true;
    };
    while (!g_fg.stop && !g_fg.failed)
    {
        FgSlot *chosen = nullptr;
        {
            std::unique_lock<std::mutex> lock(g_fg.mutex);
            g_fg.wake.wait(lock, [] {
                if (g_fg.stop) return true;
                for (auto &s : g_fg.slots) if (s.state == 2) return true;
                return false;
            });
            if (g_fg.stop) break;
            for (auto &s : g_fg.slots)
                if (s.state == 2 && (!chosen || s.sequence > chosen->sequence)) chosen = &s;
            for (auto &s : g_fg.slots) if (s.state == 2 && &s != chosen) { s.state = 0; ++skipped; }
            chosen->state = 3;
        }
        const auto start = std::chrono::steady_clock::now();
        // If a frame was dropped, do not interpolate against an unseen frame.
        if (chosen->interpolate && chosen->sequence == previous + 1)
        {
            planned += chosen->count;
            for (unsigned index = 0; index < chosen->count && !g_fg.stop && !g_fg.failed; ++index)
            {
                const auto deadline = start + std::chrono::duration<double>(
                    chosen->interval * (index + 1) / (chosen->count + 1));
                // A delayed GPU copy must not cause a burst of obsolete generated frames.
                if (std::chrono::steady_clock::now() >= deadline) { ++late; continue; }
                if (g_fg_waitable != nullptr)
                {
                    // The compositor's pacing: wait for the back buffer to
                    // be released instead of sleeping to a wall-clock
                    // deadline that drifts against the vblank.
                    if (wait_vblank(2000) != WAIT_OBJECT_0 && !g_fg.stop)
                        Log("[fg] waitable timeout - the compositor stalled");
                }
                if (g_fg.stop) break;   // a stop met in the wait: present nothing more
                if (!present(chosen->interpolated[index].get())) g_fg.failed = true;
                else { ++shown_gen; add(copy_gen, last_copy_ms); ++copy_bins[bin_of(last_copy_ms)]; }
                std::unique_lock<std::mutex> lock(g_fg.mutex);
                if (g_fg.wake.wait_until(lock, deadline, [&] {
                    if (g_fg.stop) return true;
                    for (auto &slot : g_fg.slots)
                        if (slot.state == 2 && slot.sequence > chosen->sequence) return true;
                    return false;
                })) { superseded += chosen->count - index - 1; break; }
            }
        }
        else ++flat;
        if (!g_fg.stop && !g_fg.failed)
        {
            if (g_fg_waitable != nullptr
                && wait_vblank(2000) != WAIT_OBJECT_0 && !g_fg.stop)
                Log("[fg] waitable timeout on the real frame");
            if (g_fg.stop) {}
            else if (!present(chosen->real.get())) g_fg.failed = true;
            else add(copy_real, last_copy_ms);
        }
        previous = chosen->sequence;
        {
            std::lock_guard<std::mutex> lock(g_fg.mutex);
            chosen->state = 0;
        }
        const double elapsed = std::chrono::duration<double>(start - report).count();
        if (elapsed >= 2.0)
        {
            // The "[fg] displayed" prefix is a contract: settings_io parses it
            // for the HUD counter. The step is appended, not prefixed, so that
            // parser keeps working and the line still names the multiplier it
            // really ran at (a silent clamp used to claim a step that never
            // ran).
            Log("[fg] displayed %.1f FPS (real + generated, %ux); experimental flat-depth guides",
                shown / elapsed, chosen->count + 1);
            if (PhaseEnabled())
                Log("[phase] fg presenter: copy wait generated %.2f/%.2f real %.2f/%.2f ms | "
                    "generated by copy wait <1=%u 1-2=%u 2-4=%u 4-8=%u 8-16=%u 16+=%u | "
                    "vblank wait %.2f/%.2f ms | generated shown %u of %u (late %u, "
                    "superseded %u) | real skipped %u, without generated %u | "
                    "source not new, held %u (mean/max)",
                    copy_gen.n ? copy_gen.sum / copy_gen.n : 0.0, copy_gen.max,
                    copy_real.n ? copy_real.sum / copy_real.n : 0.0, copy_real.max,
                    copy_bins[0], copy_bins[1], copy_bins[2], copy_bins[3], copy_bins[4],
                    copy_bins[5], vblank.n ? vblank.sum / vblank.n : 0.0, vblank.max,
                    shown_gen, planned, late, superseded, skipped, flat,
                    g_fg.held.exchange(0));
            copy_gen = PresentStat{}; copy_real = PresentStat{}; vblank = PresentStat{};
            for (auto &b : copy_bins) b = 0;
            planned = shown_gen = late = superseded = flat = skipped = 0;
            shown = 0; report = start;
        }
    }
    CloseHandle(event);
    if (g_fg.failed) Log("[fg] presenter failed; returning to ordinary output");
}

// Maps the FidelityFX runtime out of one folder: the effect DLL first, by full
// path, then the loader. The loader looks for the effect DLL by name, and a
// module of that name already mapped is what Windows hands it - so both come
// from the same folder whichever folder that is (the BYO folder or native\).
// A BYO pair passes the AMD signature gate first; the bundled pair is the
// shipped one and is loaded as it always was.
static void FgUnloadRuntime()
{
    g_ffx.create = nullptr; g_ffx.destroy = nullptr; g_ffx.configure = nullptr;
    g_ffx.query = nullptr; g_ffx.dispatch = nullptr;
    if (g_ffx.module) { FreeLibrary(g_ffx.module); g_ffx.module = nullptr; }
    if (g_ffx.provider) { FreeLibrary(g_ffx.provider); g_ffx.provider = nullptr; }
}

static bool FgMapRuntime(const wchar_t *directory, bool byo)
{
    wchar_t provider_path[MAX_PATH], loader_path[MAX_PATH];
    wcscpy_s(provider_path, directory);
    wcscat_s(provider_path, L"amd_fidelityfx_framegeneration_dx12.dll");
    wcscpy_s(loader_path, directory);
    wcscat_s(loader_path, L"amd_fidelityfx_loader_dx12.dll");
    if (GetFileAttributesW(loader_path) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(provider_path) == INVALID_FILE_ATTRIBUTES)
        return false;
    if (byo)
    {
        // A writable folder next to the executable is the easiest DLL plant:
        // both files are verified (AMD signature, machine-root chain, product
        // name) before either is mapped.
        if (!NsGateByoDllAmd(provider_path, "amd_fidelityfx_framegeneration_dx12.dll") ||
            !NsGateByoDllAmd(loader_path, "amd_fidelityfx_loader_dx12.dll"))
        {
            Log("[fg] BYO refused: the FidelityFX files in native\\libraries\\ are not "
                "AMD-signed runtimes - falling back to the bundled copy (%ls)", loader_path);
            return false;
        }
    }
    g_ffx.provider = LoadLibraryExW(provider_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_ffx.provider)
    {
        Log("[fg] amd_fidelityfx_framegeneration_dx12.dll load failed: %lu", GetLastError());
        return false;
    }
    g_ffx.module = LoadLibraryExW(loader_path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_ffx.module)
    {
        Log("[fg] amd_fidelityfx_loader_dx12.dll load failed: %lu", GetLastError());
        FgUnloadRuntime();
        return false;
    }
    // Any failure from here on unloads the runtime again. The module was the
    // only "initialised" marker, so a half-done init (an export missing)
    // must not be kept: the next FG switch-on would skip this block and call
    // create through pointers never bound.
    g_ffx.create = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(g_ffx.module, "ffxCreateContext"));
    g_ffx.destroy = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(g_ffx.module, "ffxDestroyContext"));
    g_ffx.configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(g_ffx.module, "ffxConfigure"));
    g_ffx.query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(g_ffx.module, "ffxQuery"));
    g_ffx.dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(g_ffx.module, "ffxDispatch"));
    if (!g_ffx.create || !g_ffx.destroy || !g_ffx.configure || !g_ffx.query || !g_ffx.dispatch)
    {
        Log("[fg] the FidelityFX runtime is missing an entry point");
        FgUnloadRuntime();
        return false;
    }
    return true;
}

// One attempt at a frame generation context: the provider the runtime picks by
// itself (use_override false), or a named one.
static ffxReturnCode_t FgCreateContext(UINT w, UINT height, UINT mw, UINT mh,
                                       DXGI_FORMAT format, bool use_override, uint64_t provider)
{
    ffxCreateBackendDX12Desc backend = {};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = h.dev;
    ffxOverrideVersion override_version = {};
    override_version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    override_version.versionId = provider;
    backend.header.pNext = use_override ? &override_version.header : nullptr;
    ffxCreateContextDescFrameGenerationVersion version = {};
    version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    version.header.pNext = &backend.header;
    version.version = FFX_FRAMEGENERATION_VERSION;
    ffxCreateContextDescFrameGeneration create = {};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    create.header.pNext = &version.header;
    create.flags = format == DXGI_FORMAT_R10G10B10A2_UNORM ? FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE : 0u;
    create.displaySize = {w, height};
    create.maxRenderSize = {mw, mh};
    create.backBufferFormat = ffxApiGetSurfaceFormatDX12(format);
    g_fg.context = nullptr;
    return FgCreateGuarded(&g_fg.context, &create.header);
}

static void FgNameProvider()
{
    static char name[96];
    name[0] = 0;
    g_fg.provider_id = 0;
    if (g_fg.context == nullptr) { g_fg_provider_name = name; return; }
    ffxQueryGetProviderVersion pv = {};
    pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    if (FgQueryGuarded(&g_fg.context, &pv.header) == FFX_API_RETURN_OK)
    {
        g_fg.provider_id = pv.versionId;
        if (pv.versionName != nullptr) strncpy_s(name, pv.versionName, _TRUNCATE);
    }
    g_fg_provider_name = name;
}

static bool FgProviderRejected(uint64_t id)
{
    return std::find(g_fg_rejected_providers.begin(), g_fg_rejected_providers.end(), id)
           != g_fg_rejected_providers.end();
}

static bool EnsureFg(VideoState &v, DXGI_FORMAT format)
{
    const UINT w = v.upscale ? v.full_w : v.w, height = v.upscale ? v.full_h : v.hgt;
    if (g_fg.context && g_fg.w == w && g_fg.height == height && g_fg.mw == v.w &&
        g_fg.mh == v.hgt && g_fg.format == format && g_fg.thread.joinable()) return true;
    CloseFgResources();
    if (!g_ffx.module)
    {
        // The BYO library folder next to the worker, then the worker's own
        // directory: users drop the AMD FidelityFX DLLs into native\libraries\
        // to pick the build they want, and native\ stays the bundled fallback.
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (auto slash = wcsrchr(path, L'\\')) *(slash + 1) = 0;
        wchar_t directory[MAX_PATH]; wcscpy_s(directory, path);
        wchar_t libraries[MAX_PATH]; wcscpy_s(libraries, directory);
        wcscat_s(libraries, L"libraries\\");
        bool mapped = FgMapRuntime(libraries, true);
        if (mapped) Log("[fg] FG runtime from native\\libraries\\ (BYO, verified AMD signature)");
        if (!mapped) mapped = FgMapRuntime(directory, false);
        if (!mapped)
        {
            Log("[fg] amd_fidelityfx_loader_dx12.dll load failed: %lu", GetLastError());
            return false;
        }
        if (TestFailureOnce("fg-init"))
        {
            Log("[fg] Init -> 0x%08X", static_cast<unsigned>(FFX_API_RETURN_ERROR));
            FgUnloadRuntime();
            return false;
        }
        ffxConfigureDescGlobalDebug1 debug = {};
        debug.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
        debug.fpMessage = FgRuntimeMessage;
        debug.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_ERRORS;
        FgConfigureGuarded(nullptr, &debug.header);
        Log("[fg] Init -> 0x%08X", static_cast<unsigned>(FFX_API_RETURN_OK));
    }
    // The provider the runtime picks for this GPU is the newest one (FSR Frame
    // Generation 4, the ML one, where the hardware has it). A provider that
    // cannot be created here - or one an earlier dispatch refused - is not the
    // end of the feature: the runtime ships the older analytical provider as
    // well, and that one runs on any Shader Model 6.2 GPU. So the list of
    // providers is walked until one takes.
    ffxReturnCode_t created = FgCreateContext(w, height, v.w, v.hgt, format, false, 0);
    if (g_fg_fault != 0)
    {
        g_fg.context = nullptr;
        ReportFailure("fg-create", "seh", static_cast<HRESULT>(g_fg_fault));
        Log("[fg] CreateContext raised 0x%08X (caught) - Frame Generation off", g_fg_fault);
        return false;
    }
    if (created == FFX_API_RETURN_OK)
    {
        FgNameProvider();
        if (g_fg.provider_id != 0 && FgProviderRejected(g_fg.provider_id))
        {
            Log("[fg] provider %s refused to run on this GPU earlier - trying the others", g_fg_provider_name);
            FgDestroyGuarded(&g_fg.context);
            g_fg.context = nullptr;
            created = FFX_API_RETURN_ERROR;
        }
    }
    else
        Log("[fg] CreateContext (default provider) failed 0x%08X - trying the other providers", static_cast<unsigned>(created));
    if (created != FFX_API_RETURN_OK)
    {
        uint64_t count = 0;
        ffxQueryDescGetVersions list = {};
        list.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        list.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        list.device = h.dev;
        list.outputCount = &count;
        if (FgQueryGuarded(nullptr, &list.header) == FFX_API_RETURN_OK && count > 0 && count < 64)
        {
            std::vector<uint64_t> ids(static_cast<size_t>(count));
            std::vector<const char *> names(static_cast<size_t>(count));
            list.outputCount = &count;
            list.versionIds = ids.data();
            list.versionNames = names.data();
            if (FgQueryGuarded(nullptr, &list.header) == FFX_API_RETURN_OK)
            {
                for (uint64_t i = 0; i < count && created != FFX_API_RETURN_OK; ++i)
                {
                    if (FgProviderRejected(ids[static_cast<size_t>(i)])) continue;
                    created = FgCreateContext(w, height, v.w, v.hgt, format, true, ids[static_cast<size_t>(i)]);
                    Log("[fg] CreateContext on provider %s -> 0x%08X",
                        names[static_cast<size_t>(i)] != nullptr ? names[static_cast<size_t>(i)] : "?",
                        static_cast<unsigned>(created));
                    if (g_fg_fault != 0) { g_fg.context = nullptr; created = FFX_API_RETURN_ERROR; }
                    if (created == FFX_API_RETURN_OK) FgNameProvider();
                }
            }
        }
    }
    if (created != FFX_API_RETURN_OK || g_fg.context == nullptr)
    {
        // No provider takes this GPU/format: this is not a multiplier the
        // runtime refused, so there is no step to walk down to.
        Log("[fg] CreateContext failed 0x%08X - no frame generation provider can run here",
            static_cast<unsigned>(created));
        g_fg.context = nullptr;
        return false;
    }
    Log("[fg] FSR provider: %s", g_fg_provider_name);
    if (!CreateVideoTex(g_fg.depth, v.w, v.hgt, DXGI_FORMAT_R32_FLOAT, v.w * 4)) return false;
    std::vector<float> depth(static_cast<size_t>(v.w) * v.hgt, 0.5f);
    if (!FillUpload(g_fg.depth, reinterpret_cast<BYTE *>(depth.data()), v.w * 4, v.hgt) || !BeginCommands()) return false;
    CopyUpload(h.list, g_fg.depth);
    auto b = Transition(g_fg.depth.tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    h.list->ResourceBarrier(1, &b);
    if (!WaitFenceValue(h.fence, EndCommands(), 30000)) return false;
    for (unsigned i = 0; i < g_fg_count; ++i)
    {
        g_fg.output[i].attach(MakeTex(w, height, format, true));
        if (!g_fg.output[i]) return false;
    }
    for (auto &slot : g_fg.slots)
    {
        slot.real.attach(MakeTex(w, height, format, false));
        if (!slot.real) return false;
        for (unsigned i = 0; i < g_fg_count; ++i)
        {
            slot.interpolated[i].attach(MakeTex(w, height, format, true));
            if (!slot.interpolated[i]) return false;
        }
    }
    if (!BeginCommands()) return false;
    for (unsigned i = 0; i < g_fg_count; ++i)
    {
        auto to_copy = Transition(g_fg.output[i].get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        h.list->ResourceBarrier(1, &to_copy);
    }
    for (auto &slot : g_fg.slots) for (unsigned i = 0; i <= g_fg_count; ++i)
    {
        auto texture = i ? slot.interpolated[i - 1].get() : slot.real.get();
        auto to_copy = Transition(texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        h.list->ResourceBarrier(1, &to_copy);
    }
    if (!WaitFenceValue(h.fence, EndCommands(), 30000)) return false;
    g_fg.w = w; g_fg.height = height; g_fg.mw = v.w; g_fg.mh = v.hgt; g_fg.format = format;
    g_fg.stop = false;
    if (g_fg.stop_event != nullptr) ResetEvent(g_fg.stop_event);
    // R11 refined: latency 1 belongs to the FG presenter while it owns the
    // present loop. The ordinary NR path presents Present(0,0) per frame and
    // never consumes the waitable - with latency 1 the swapchain would hold
    // a single queued present and every ordinary present would block or drop
    // against the compositor (the fullscreen flicker). The waitable handle
    // is taken here, from the same thread that will wait on it.
    if (g_present_swap != nullptr)
    {
        IDXGISwapChain2 *sc2 = nullptr;
        if (SUCCEEDED(g_present_swap->QueryInterface(
                __uuidof(IDXGISwapChain2), reinterpret_cast<void **>(&sc2)))
            && sc2 != nullptr)
        {
            // Both calls need a swap chain created with
            // DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT; without it
            // they refuse and there is no handle. OpenPresent creates the
            // chain with it (and puts the ordinary path back on latency 3);
            // the log still says which pacing actually runs, in case a driver
            // refuses anyway.
            CloseFgWaitable();
            const HRESULT latency = sc2->SetMaximumFrameLatency(1);
            if (SUCCEEDED(latency)) g_fg_waitable = sc2->GetFrameLatencyWaitableObject();
            sc2->Release();
            if (g_fg_waitable != nullptr)
                Log("[fg] the presenter is paced by the compositor (latency 1)");
            else
                Log("[fg] the presenter is paced by the clock: the swap chain has no "
                    "latency waitable (0x%08X)", static_cast<unsigned>(latency));
        }
    }
    g_fg.thread = std::thread(FgPresenter);
    Log("[fg] %ux enabled at %ux%u, format=%u; FSR %s; flat depth and estimated motion (experimental)", g_fg_count + 1, w, height, format, g_fg_provider_name);
    return true;
}

// Opt-in pixel regression capture, never enabled in ordinary runs.
static void FgDump(ID3D12Resource *source, D3D12_RESOURCE_STATES state, unsigned index)
{
    char dir[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("NS_FG_DUMP", dir, sizeof(dir)) ||
        (g_fg.sequence != 30 && g_fg.sequence != 150 && g_fg.sequence != 300)) return;
    auto desc = source->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT rows; UINT64 rowbytes, bytes;
    h.dev->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &rowbytes, &bytes);
    D3D12_HEAP_PROPERTIES heap = {}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = bytes; bd.Height = 1; bd.DepthOrArraySize = bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    winrt::com_ptr<ID3D12Resource> rb;
    if (FAILED(h.dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(rb.put()))) || !BeginCommands()) return;
    auto pre = Transition(source, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) h.list->ResourceBarrier(1, &pre);
    D3D12_TEXTURE_COPY_LOCATION from = {}, to = {};
    from.pResource = source; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = rb.get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = fp;
    h.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    auto post = Transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) h.list->ResourceBarrier(1, &post);
    if (!WaitFenceValue(h.fence, EndCommands(), 30000, "fg-dump"))
    {
        if (g_submission_failed) rb.detach();
        return;
    }
    BYTE *data = nullptr; D3D12_RANGE read = {0, static_cast<SIZE_T>(bytes)}, written = {0, 0};
    if (FAILED(rb->Map(0, &read, reinterpret_cast<void **>(&data)))) return;
    char path[MAX_PATH]; sprintf_s(path, "%s/fg-%llu-%u.raw", dir, g_fg.sequence, index);
    FILE *file = nullptr;
    if (!fopen_s(&file, path, "wb") && file) {
        for (UINT y = 0; y < rows; ++y) fwrite(data + size_t(y) * fp.Footprint.RowPitch, 1, size_t(rowbytes), file);
        fclose(file);
    }
    rb->Unmap(0, &written);
}

// Export follows the source the viewer sees: in bypass that is the raw
// capture, not the neural result, which on this path is a frame the screen
// is not showing at all. Records into h.list.
static void ExportFgSource(VideoState &v, bool bypass)
{
    const auto export_rest = bypass ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                                    : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12Resource *export_src = LensApply(h.list, bypass ? v.color.tex : v.output,
                                           export_rest, LENS_EXPORT);
    auto spout_pre = Transition(export_src, export_rest, D3D12_RESOURCE_STATE_COPY_SOURCE);
    h.list->ResourceBarrier(1, &spout_pre);
    ExportCopy(h.list, export_src, g_fg.w, g_fg.height);
    auto spout_post = Transition(export_src, D3D12_RESOURCE_STATE_COPY_SOURCE, export_rest);
    h.list->ResourceBarrier(1, &spout_post);
}

// A picture the capture did not renew is not a frame to generate from (#132).
// A pointer-only DDA update or a WGC pool with nothing new hands over the
// same pixels; taken as a new real frame it cost a full set of FSR
// dispatches, and its slot - a few ms after the real one - superseded the
// generated frames of the real step. Nothing is queued, so the presenter
// keeps what it is showing (flip model: the last frame stays on screen). The
// export still runs, as it did for every frame, and the fence is the token
// the defer-tail contract needs from this call.
static constexpr int kFgHoldMs = 100;
static bool FgHold(VideoState &v, bool bypass)
{
    if (!BeginCommands()) { g_fg.failed = true; CloseFgResources(); return false; }
    ExportFgSource(v, bypass);
    const UINT64 fence = EndCommands();
    if (!WaitFenceValue(h.fence, fence, 30000, "fg-hold"))
    { g_fg.failed = true; CloseFgResources(); return false; }
    g_fg_present_fence = fence;
    SpoutBridgeSend();
    ++g_fg.held;
    return true;
}

static bool FgPresent(VideoState &v, ID3D12Resource *color, D3D12_RESOURCE_STATES state,
                      bool bypass)
{
    if (!FgRequested()) { StopFgPresentation(); return false; }
    if (!EnsureFg(v, color->GetDesc().Format))
    {
        // A refused multiplier stepped the ceiling down: this frame is a
        // retry, not a dead feature. Release what the failed attempt built and
        // let the next header rebuild at the lower step.
        if (g_fg_retry) { g_fg_retry = false; CloseFgResources(); return false; }
        g_fg.failed = true; CloseFgResources(); return false;
    }
    // The picture window is hidden (its target is minimised): nothing
    // presented into it reaches the screen, and a flip chain's hidden
    // presents are dropped silently - DXGI_STATUS_OCCLUDED never comes. A
    // slot queued every kFgHoldMs only stood the presenter in the
    // compositor's waitable (2 s timeouts) and in a copy wait on a chain
    // DWM was not retiring (H4). Hold instead: export and answer, queue
    // nothing; the first frame after the window is back presents again.
    if (g_present_revealed && !g_present_shown && !g_present_reshow)
        return FgHold(v, bypass);
    const auto now = std::chrono::steady_clock::now();
    // A frame that is not new is held - unless something else changed the
    // picture (a reset: the NR switch, the wipe, a stream reset), there is no
    // history yet, or the last slot is older than kFgHoldMs. That last case
    // is a plain real frame with nothing generated, so a still screen still
    // shows NR settling and a changed setting within a tenth of a second; it
    // can never cut into a slot's generated frames, which are all due within
    // the 50 ms interval clamp.
    const bool renewed = g_fg_source_fresh || g_fg_reset || !g_fg.history;
    if (!renewed && now - g_fg.last_slot < std::chrono::milliseconds(kFgHoldMs))
        return FgHold(v, bypass);
    // The interval is the source's: when the new picture was produced, not
    // when this loop reached it. The call time is the fallback where the
    // source has no clock (frames sent over the pipe).
    double interval = 0.016;
    if (g_fg.history && g_fg_source_fresh)
    {
        const double source = g_fg_source_seconds - g_fg.last_source;
        interval = g_fg_source_seconds > 0.0 && g_fg.last_source > 0.0 && source > 0.0
            ? source : std::chrono::duration<double>(now - g_fg.last).count();
    }
    const bool interpolate = g_fg.history && g_fg_source_fresh && !g_fg_reset && interval < 0.1;
    g_fg.last_slot = now;
    if (g_fg_source_fresh) { g_fg.last = now; g_fg.last_source = g_fg_source_seconds; }
    if (!BeginCommands()) { g_fg.failed = true; CloseFgResources(); return false; }
    const bool profile_fg = ProfileGpuBegin(PS_FG);
    const bool allow_interpolation = true;
    const bool hdr = g_fg.format == DXGI_FORMAT_R10G10B10A2_UNORM;
    // FidelityFX takes the frame id as the thread through the frames: it goes
    // up by exactly one per frame, and any other step resets the
    // interpolation. A frame that is not interpolated says so as well.
    const uint64_t frame_id = ++g_fg.frame_id;
    const bool reset = !interpolate;
    // The picture the generated frames are built from rests as a compute
    // read for the dispatch; the outputs as UAVs.
    auto pre = Transition(color, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) h.list->ResourceBarrier(1, &pre);
    for (unsigned index = 0; index < g_fg_count; ++index)
    {
        auto out_pre = Transition(g_fg.output[index].get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        h.list->ResourceBarrier(1, &out_pre);
    }
    // 1. Configure, once per frame. No swap chain and no callbacks: this
    //    context only interpolates, the worker's presenter shows the frames.
    ffxConfigureDescFrameGeneration configure = {};
    configure.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    configure.swapChain = nullptr;
    configure.presentCallback = nullptr;
    configure.frameGenerationCallback = nullptr;
    configure.frameGenerationEnabled = true;
    configure.allowAsyncWorkloads = false;
    configure.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    configure.onlyPresentGenerated = false;
    configure.generationRect = {0, 0, static_cast<int32_t>(g_fg.w), static_cast<int32_t>(g_fg.height)};
    configure.frameID = frame_id;
    // 2. Prepare: the flat depth, the estimated motion (in pixels, so a
    //    scale of one) and a camera that stands still - there is no engine to
    //    ask for one.
    ffxDispatchDescFrameGenerationPrepareV2 prepare = {};
    prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepare.frameID = frame_id;
    prepare.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    prepare.commandList = h.list;
    prepare.renderSize = {v.w, v.hgt};
    prepare.jitterOffset = {0.0f, 0.0f};
    prepare.motionVectorScale = {1.0f, 1.0f};
    prepare.frameTimeDelta = static_cast<float>(std::clamp(interval, 0.004, 0.1) * 1000.0);
    prepare.reset = reset;
    prepare.cameraNear = 0.1f;
    prepare.cameraFar = 1000.0f;
    prepare.cameraFovAngleVertical = 1.0f;
    prepare.viewSpaceToMetersFactor = 1.0f;
    prepare.depth = ffxApiGetResourceDX12(g_fg.depth.tex, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    prepare.motionVectors = ffxApiGetResourceDX12(v.mv.tex, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    prepare.cameraPosition[0] = prepare.cameraPosition[1] = prepare.cameraPosition[2] = 0.0f;
    prepare.cameraUp[0] = 0.0f; prepare.cameraUp[1] = 1.0f; prepare.cameraUp[2] = 0.0f;
    prepare.cameraRight[0] = 1.0f; prepare.cameraRight[1] = 0.0f; prepare.cameraRight[2] = 0.0f;
    prepare.cameraForward[0] = 0.0f; prepare.cameraForward[1] = 0.0f; prepare.cameraForward[2] = 1.0f;
    // 3. The generation itself: every generated frame of this real one in one
    //    dispatch.
    ffxDispatchDescFrameGeneration generate = {};
    generate.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    generate.commandList = h.list;
    generate.presentColor = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    for (unsigned index = 0; index < g_fg_count; ++index)
        generate.outputs[index] = ffxApiGetResourceDX12(g_fg.output[index].get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    generate.numGeneratedFrames = g_fg_count;
    generate.reset = reset;
    generate.backbufferTransferFunction = hdr ? FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ
                                              : FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    generate.minMaxLuminance[0] = 0.0f; generate.minMaxLuminance[1] = 1000.0f;
    generate.generationRect = configure.generationRect;
    generate.frameID = frame_id;
    ffxReturnCode_t result = FgConfigureGuarded(&g_fg.context, &configure.header);
    if (g_fg_fault == 0 && result == FFX_API_RETURN_OK)
        result = FgDispatchGuarded(&g_fg.context, &prepare.header);
    if (g_fg_fault == 0 && result == FFX_API_RETURN_OK)
        result = FgDispatchGuarded(&g_fg.context, &generate.header);
    if (g_fg_fault != 0)
    {
        AbortCommands();
        ReportFailure("fg-evaluate", "seh", static_cast<HRESULT>(g_fg_fault));
        Log("[fg] Evaluate raised 0x%08X (caught) - Frame Generation off", g_fg_fault);
        g_fg.failed = true;
        return false;
    }
    for (unsigned index = 0; index < g_fg_count; ++index)
    {
        auto out_post = Transition(g_fg.output[index].get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        h.list->ResourceBarrier(1, &out_post);
    }
    auto post = Transition(color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, state);
    if (state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) h.list->ResourceBarrier(1, &post);
    if (result != FFX_API_RETURN_OK)
    {
        if (profile_fg) ProfileGpuEnd(PS_FG);
        if (!ProfileWait(PS_FG, EndCommands(), 30000, "fg-evaluate"))
        { g_fg.failed = true; return false; }
        // A refused multiplier is not a broken feature, and it used to be
        // treated as one: FG died at every count, including on a provider
        // that runs 2x perfectly well. Step down; 2x is the floor.
        // No retry flag here: the create already succeeded, so the next frame
        // is an ordinary fresh build at the lower step and its own failure
        // must read as one.
        if (FgStepDown(result)) { CloseFgResources(); return false; }
        // 2x itself was refused. Before calling it a card that cannot run
        // frame generation: the provider that was created may simply be the
        // wrong one for this GPU (the ML provider needs hardware the card
        // may not have), and the runtime ships another. Reject this one and
        // let the next build take the next provider.
        if (g_fg.provider_id != 0 && !FgProviderRejected(g_fg.provider_id))
        {
            Log("[fg] provider %s refused to dispatch (0x%08X) - trying another one",
                g_fg_provider_name, static_cast<unsigned>(result));
            g_fg_rejected_providers.push_back(g_fg.provider_id);
            g_fg_count = std::min(g_fg_count, kFgMaxGenerated);
            g_fg_count_limit = kFgMaxGenerated;
            CloseFgResources();
            g_fg_retry = true;
            return false;
        }
        Log("[fg] Evaluate failed 0x%08X; falling back", static_cast<unsigned>(result));
        g_fg.failed = true; CloseFgResources(); return false;
    }
    if (profile_fg) ProfileGpuEnd(PS_FG);
    FgSlot *slot = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_fg.mutex);
        for (auto &s : g_fg.slots) if (s.state == 0) { slot = &s; break; }
        if (!slot) for (auto &s : g_fg.slots) if (s.state == 2) { slot = &s; break; }
        if (!slot)
        {
            // Starvation: every slot is mid-flight. The submitted work still
            // ends here - record its token, or PresentFrame hands the defer
            // contract a STALE g_fg_present_fence from an earlier frame and
            // the token-order guard kills the worker (the blink-out class).
            const UINT64 fence = EndCommands();
            if (!ProfileWait(PS_FG, fence, 30000, "fg-starvation"))
            { g_fg.failed = true; return false; }
            g_fg_present_fence = fence;
            return true;
        }
        slot->state = 1;
    }
    // The reserved slot is invisible to the presenter until the fence completes.
    // Hand it the generated textures and reuse its old buffers next frame.
    for (unsigned i = 0; i < g_fg_count; ++i)
        std::swap(g_fg.output[i], slot->interpolated[i]);
    {
        auto source = color;
        auto destination = slot->real.get();
        const auto rest = state;
        D3D12_RESOURCE_BARRIER before[] = {
            Transition(source, rest, D3D12_RESOURCE_STATE_COPY_SOURCE),
            Transition(destination, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
        h.list->ResourceBarrier(2, before);
        h.list->CopyResource(destination, source);
        D3D12_RESOURCE_BARRIER after[] = {
            Transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, rest),
            Transition(destination, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE)};
        h.list->ResourceBarrier(2, after);
    }
    // Interface protection (v2.6.0): where the client found a still interface
    // over a moving picture - a HUD, subtitles, a score - the generated frames
    // show the real frame's pixels. frame generation warps text with the motion around
    // it, and a still label has no motion of its own to follow. Copied before
    // the lens and the badge, so both go on the protected frame as on any.
    if (g_ui_rects.count > 0)
    {
        const auto desc = slot->real->GetDesc();
        const UINT fw = static_cast<UINT>(desc.Width), fh = desc.Height;
        for (unsigned i = 0; i < g_fg_count; ++i)
        {
            ID3D12Resource *gen = slot->interpolated[i].get();
            auto to_dst = Transition(gen, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            h.list->ResourceBarrier(1, &to_dst);
            D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
            dl.pResource = gen; dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            sl.pResource = slot->real.get(); sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            for (uint32_t r = 0; r < g_ui_rects.count; ++r)
            {
                const auto &q = g_ui_rects.rects[r];
                const UINT x0 = static_cast<UINT>(static_cast<uint64_t>(q[0]) * fw / 65535u);
                const UINT y0 = static_cast<UINT>(static_cast<uint64_t>(q[1]) * fh / 65535u);
                const UINT x1 = (std::min)(fw, x0 + static_cast<UINT>(static_cast<uint64_t>(q[2]) * fw / 65535u));
                const UINT y1 = (std::min)(fh, y0 + static_cast<UINT>(static_cast<uint64_t>(q[3]) * fh / 65535u));
                if (x1 <= x0 || y1 <= y0) continue;
                D3D12_BOX box = {x0, y0, 0, x1, y1, 1};
                h.list->CopyTextureRegion(&dl, x0, y0, 0, &sl, &box);
            }
            auto back = Transition(gen, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            h.list->ResourceBarrier(1, &back);
        }
    }
    // The fisheye lens goes on what the presenter shows - the real frame and
    // each generated one, all resting in COPY_SOURCE - never on what FSR
    // interpolated from: its motion field is the rectilinear picture's.
    LensInPlace(h.list, slot->real.get());
    for (unsigned i = 0; i < g_fg_count; ++i) LensInPlace(h.list, slot->interpolated[i].get());
    // And the rate badge, on every frame shown - nothing exports these.
    BadgeInPlace(h.list, slot->real.get());
    for (unsigned i = 0; i < g_fg_count; ++i) BadgeInPlace(h.list, slot->interpolated[i].get());
    ExportFgSource(v, bypass);
    const UINT64 fg_fence = EndCommands();
    if (!ProfileWait(PS_FG, fg_fence, 30000, "fg-evaluate"))
    { g_fg.failed = true; CloseFgResources(); return false; }
    g_fg_present_fence = fg_fence;
    FgDump(color, state, 0);
    for (unsigned i = 0; i < g_fg_count; ++i) FgDump(slot->interpolated[i].get(), D3D12_RESOURCE_STATE_COPY_SOURCE, i + 1);
    SpoutBridgeSend();
    {
        std::lock_guard<std::mutex> lock(g_fg.mutex);
        slot->sequence = ++g_fg.sequence;
        slot->count = g_fg_count;
        slot->interpolate = interpolate && allow_interpolation;
        slot->interval = std::clamp(interval, 0.004, 0.05);
        slot->state = 2;
    }
    g_fg.history = true;
    g_fg.wake.notify_one();
    return true;
}

// Explicit UI settings ride in each frame header, so toggling does not restart NR.
static void ConfigureFgFrame(uint32_t flags)
{
    if (!(flags & 0x800)) return; // older clients may still use NS_FRAMEGEN
    const int enabled = (flags & 0x100) != 0;
    // The multiplier minus two in bits 9-10, its third bit in bit 15 (#163).
    const unsigned step = ((flags >> 9) & 3u) | ((flags & 0x8000u) ? 4u : 0u);
    const unsigned requested = std::min(kFgMaxGenerated, step + 1u);
    // A step the runtime already refused is not offered again in this FG
    // session: the request is clamped to the last one that worked. Switching
    // FG off and on arms the user's choice again, so a newer stock provider
    // (BYO folder) or a driver update can lift the ceiling without a restart.
    const unsigned count = std::min(requested, g_fg_count_limit);
    if (g_fg_ui_enabled == enabled && g_fg_count == count) return;
    CloseFgResources();
    // A fresh switch-on is a fresh attempt: the ceiling goes back to the 5x
    // maximum and the user's own choice is asked for again. A step this card
    // refused earlier is worth exactly one retry after a runtime or driver
    // update, and those do not restart the program.
    if (enabled && g_fg_ui_enabled == 0) g_fg_count_limit = kFgMaxGenerated;
    g_fg_retry = false;
    g_fg_ui_enabled = enabled;
    g_fg_count = count;
    g_fg.failed = false;
    Log("[fg] UI: %s, %ux%s", enabled ? "on" : "off", count + 1,
        count < requested ? " (capped by the runtime ceiling)" : "");
}

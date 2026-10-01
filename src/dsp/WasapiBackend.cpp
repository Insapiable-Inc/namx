#include "WasapiBackend.h"

#include <algorithm>
#include <sstream>

#include "Log.h"

#ifdef _WIN32

// clang-format off
// Win32 headers are order-sensitive: <windows.h> must come before the rest.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>

#include <ks.h>
#include <ksmedia.h>  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
// clang-format on

#include <immintrin.h> // _MM_SET_FLUSH_ZERO_MODE / _MM_SET_DENORMALS_ZERO_MODE

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "avrt.lib")

namespace
{

constexpr int K_STOP_EVENT_INDEX = 0;
constexpr int K_CAPTURE_EVENT_INDEX = 1;
constexpr int K_RENDER_EVENT_INDEX = 2;

constexpr REFERENCE_TIME K_REF_TIMES_PER_SEC = 10000000LL; // 100-ns units

// CoTaskMemFree helper — WAVEFORMATEX returned by GetMixFormat is
// COM-allocated and must be freed by the caller.
struct CoMemFree
{
    void operator()(void* p) const {
        if (p) {
            CoTaskMemFree(p);
        }
    }
};

// Inspect a WAVEFORMATEX and return true iff it's 32-bit float PCM.
bool isFloat32(const WAVEFORMATEX* wfx) {
    if (!wfx) {
        return false;
    }
    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wfx->wBitsPerSample == 32) {
        return true;
    }
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wfx->wBitsPerSample == 32) {
        auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return false;
}

std::string friendly_device_name(IMMDevice* dev) {
    if (!dev) {
        return {};
    }
    IPropertyStore* props = nullptr;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &props)) || !props) {
        return {};
    }

    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::string name;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR &&
        pv.pwszVal) {
        // Narrow the wide string for the status pill; ASCII-fold non-ASCII.
        for (const wchar_t* p = pv.pwszVal; *p; ++p) {
            name += (*p < 128) ? static_cast<char>(*p) : '?';
        }
    }
    PropVariantClear(&pv);
    props->Release();
    return name;
}

std::string wide_to_utf8(LPCWSTR wstr) {
    if (!wstr) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) {
        return {};
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

std::vector<IAudioBackend::DeviceInfo> enumerate_wasapi_devices(EDataFlow direction) {
    std::vector<IAudioBackend::DeviceInfo> result;
    result.push_back({"default", "Default"});

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInit = (hr == S_OK || hr == S_FALSE);
    // RPC_E_CHANGED_MODE means COM already initialized — fine.

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        if (comInit) {
            CoUninitialize();
        }
        return result;
    }

    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(direction, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr) && collection) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* dev = nullptr;
            if (SUCCEEDED(collection->Item(i, &dev)) && dev) {
                IAudioBackend::DeviceInfo info;
                LPWSTR idStr = nullptr;
                if (SUCCEEDED(dev->GetId(&idStr)) && idStr) {
                    info.id = wide_to_utf8(idStr);
                    CoTaskMemFree(idStr);
                }
                info.name = friendly_device_name(dev);
                if (!info.name.empty() && !info.id.empty()) {
                    result.push_back(info);
                }
                dev->Release();
            }
        }
        collection->Release();
    }
    enumerator->Release();
    if (comInit) {
        CoUninitialize();
    }
    return result;
}

} // namespace

#endif // _WIN32

// ---------------------------------------------------------------------------
// Factory (Windows side; the Linux factory lives in JackBackend.cpp).
// ---------------------------------------------------------------------------

std::unique_ptr<IAudioBackend> IAudioBackend::create() {
    return std::make_unique<WasapiBackend>();
}

WasapiBackend::WasapiBackend() = default;

WasapiBackend::~WasapiBackend() {
    stop();
}

// ---------------------------------------------------------------------------
// status_string — visible regardless of platform so the Linux build (where this
// file is not compiled) is unaffected. The body is conditionally compiled.
// ---------------------------------------------------------------------------

std::string WasapiBackend::status_string() const {
#ifdef _WIN32
    if (!m_running.load()) {
        return "WASAPI  \xe2\x97\x8f  not started";
    }
    std::ostringstream o;
    o << "WASAPI  \xe2\x97\x8f  ";
    if (!m_device_name.empty()) {
        o << m_device_name << "  \xe2\x97\x8f  ";
    }
    o << static_cast<int>(m_sample_rate) << " Hz  \xe2\x97\x8f  stereo out";
    return o.str();
#else
    return "WASAPI  \xe2\x97\x8f  not supported on this platform";
#endif
}

// ---------------------------------------------------------------------------
// Device enumeration (works before start() is called)
// ---------------------------------------------------------------------------

std::vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerate_input_devices() const {
#ifdef _WIN32
    return enumerate_wasapi_devices(eCapture);
#else
    return {};
#endif
}

std::vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerate_output_devices() const {
#ifdef _WIN32
    return enumerate_wasapi_devices(eRender);
#else
    return {};
#endif
}

#ifdef _WIN32

// ---------------------------------------------------------------------------
// Device selection helper
// ---------------------------------------------------------------------------

IMMDevice* WasapiBackend::open_device_by_id(const std::string& deviceId) const {
    if (deviceId.empty() || deviceId == "default") {
        return nullptr; // Caller falls back to GetDefaultAudioEndpoint
    }

    std::wstring wId = utf8_to_wide(deviceId);
    IMMDevice* dev = nullptr;
    HRESULT hr = m_enumerator->GetDevice(wId.c_str(), &dev);
    if (FAILED(hr) || !dev) {
        qCWarning(lc_audio, "[WasapiBackend] GetDevice failed for id=%s: 0x%08lx", deviceId.c_str(),
                  static_cast<unsigned long>(hr));
        return nullptr;
    }
    return dev;
}

// ---------------------------------------------------------------------------
// start / stop / start_with_devices
// ---------------------------------------------------------------------------

bool WasapiBackend::start(const std::string& client_name, Callback* cb) {
    return start_with_devices(client_name, cb, "default", "default");
}

bool WasapiBackend::start_with_devices(const std::string& /*clientName*/, Callback* cb,
                                       const std::string& input_device_id,
                                       const std::string& output_device_id) {
    if (m_running.load()) {
        return true;
    }
    m_callback = cb;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        qCWarning(lc_audio, "[WasapiBackend] CoInitializeEx failed: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }
    m_com_initialized = (hr == S_OK || hr == S_FALSE);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&m_enumerator));
    if (FAILED(hr) || !m_enumerator) {
        qCWarning(lc_audio, "[WasapiBackend] MMDeviceEnumerator failed: 0x%08lx",
                  static_cast<unsigned long>(hr));
        teardown();
        return false;
    }

    // --- Select capture device ---
    m_capture_device = open_device_by_id(input_device_id);
    if (!m_capture_device) {
        // Fall back to default multimedia capture device
        hr = m_enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &m_capture_device);
        if (FAILED(hr) || !m_capture_device) {
            qCWarning(lc_audio, "[WasapiBackend] No default capture device: 0x%08lx",
                      static_cast<unsigned long>(hr));
            teardown();
            return false;
        }
    }

    // --- Select render device ---
    m_render_device = open_device_by_id(output_device_id);
    if (!m_render_device) {
        hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &m_render_device);
        if (FAILED(hr) || !m_render_device) {
            qCWarning(lc_audio, "[WasapiBackend] No default render device: 0x%08lx",
                      static_cast<unsigned long>(hr));
            teardown();
            return false;
        }
    }

    qCInfo(lc_audio, "[WasapiBackend] Capture device: %s",
           friendly_device_name(m_capture_device).c_str());
    qCInfo(lc_audio, "[WasapiBackend] Render device: %s",
           friendly_device_name(m_render_device).c_str());

    if (!init_capture() || !init_render()) {
        teardown();
        return false;
    }

    // Capture and render must share a sample rate in shared mode (both use the
    // engine mix rate). If they ever diverge we bail rather than resample.
    if (static_cast<int>(m_sample_rate) <= 0) {
        qCWarning(lc_audio, "[WasapiBackend] Invalid sample rate after Initialize");
        teardown();
        return false;
    }

    m_device_name = friendly_device_name(m_render_device);

    m_stop_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_capture_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_render_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_stop_event || !m_capture_event || !m_render_event) {
        qCWarning(lc_audio, "[WasapiBackend] CreateEventW failed");
        teardown();
        return false;
    }

    if (FAILED(m_capture_client->SetEventHandle(m_capture_event)) ||
        FAILED(m_render_client->SetEventHandle(m_render_event))) {
        qCWarning(lc_audio, "[WasapiBackend] SetEventHandle failed");
        teardown();
        return false;
    }

    if (m_callback) {
        m_callback->on_buffer_size_changed(m_buffer_size);
    }

    // Pre-fill render buffer with silence so the first audio-thread cycle has
    // something to push without waiting for input to arrive.
    {
        BYTE* buf = nullptr;
        UINT32 frames = 0;
        if (SUCCEEDED(m_render_client->GetBufferSize(&frames)) &&
            SUCCEEDED(m_render->GetBuffer(frames, &buf)) && buf) {
            const size_t bytes = static_cast<size_t>(frames) * m_render_channels * sizeof(float);
            std::fill_n(buf, bytes, 0);
            m_render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
        }
    }

    if (FAILED(m_capture_client->Start()) || FAILED(m_render_client->Start())) {
        qCWarning(lc_audio, "[WasapiBackend] IAudioClient::Start failed");
        teardown();
        return false;
    }

    m_running.store(true);
    m_audio_thread = std::thread(&WasapiBackend::audio_thread_main, this);
    qCInfo(lc_audio, "[WasapiBackend] WASAPI started at %.0f Hz, buffer %d frames", m_sample_rate,
           static_cast<int>(m_buffer_size));
    return true;
}

void WasapiBackend::stop() {
    if (!m_running.load() && !m_audio_thread.joinable()) {
        teardown();
        return;
    }

    m_running.store(false);
    if (m_stop_event) {
        SetEvent(m_stop_event);
    }

    if (m_audio_thread.joinable()) {
        m_audio_thread.join();
    }

    if (m_capture_client) {
        m_capture_client->Stop();
    }
    if (m_render_client) {
        m_render_client->Stop();
    }

    teardown();
    m_callback = nullptr;
}

// ---------------------------------------------------------------------------
// Per-side init helpers
// ---------------------------------------------------------------------------

bool WasapiBackend::init_capture() {
    HRESULT hr = m_capture_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                            reinterpret_cast<void**>(&m_capture_client));
    if (FAILED(hr) || !m_capture_client) {
        qCWarning(lc_audio, "[WasapiBackend] Activate capture: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    WAVEFORMATEX* wfxRaw = nullptr;
    if (FAILED(m_capture_client->GetMixFormat(&wfxRaw)) || !wfxRaw) {
        qCWarning(lc_audio, "[WasapiBackend] Capture GetMixFormat failed");
        return false;
    }
    std::unique_ptr<WAVEFORMATEX, CoMemFree> wfx{wfxRaw};

    if (!isFloat32(wfx.get())) {
        qCWarning(lc_audio,
                  "[WasapiBackend] Capture mix format not float32 (tag=%u, bits=%u). "
                  "PCM conversion not yet implemented.",
                  static_cast<unsigned>(wfx->wFormatTag),
                  static_cast<unsigned>(wfx->wBitsPerSample));
        return false;
    }

    m_sample_rate = wfx->nSamplesPerSec;
    m_capture_channels = wfx->nChannels;

    hr = m_capture_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                      0, // hnsBufferDuration — system default for shared mode
                                      0, // periodicity must be 0 in shared mode
                                      wfx.get(), nullptr);
    if (FAILED(hr)) {
        qCWarning(lc_audio, "[WasapiBackend] Capture Initialize: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    UINT32 bufFrames = 0;
    if (FAILED(m_capture_client->GetBufferSize(&bufFrames))) {
        qCWarning(lc_audio, "[WasapiBackend] Capture GetBufferSize failed");
        return false;
    }
    m_buffer_size = static_cast<int>(bufFrames);

    if (FAILED(m_capture_client->GetService(__uuidof(IAudioCaptureClient),
                                            reinterpret_cast<void**>(&m_capture)))) {
        qCWarning(lc_audio, "[WasapiBackend] GetService IAudioCaptureClient failed");
        return false;
    }
    return true;
}

bool WasapiBackend::init_render() {
    HRESULT hr = m_render_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void**>(&m_render_client));
    if (FAILED(hr) || !m_render_client) {
        qCWarning(lc_audio, "[WasapiBackend] Activate render: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    WAVEFORMATEX* wfxRaw = nullptr;
    if (FAILED(m_render_client->GetMixFormat(&wfxRaw)) || !wfxRaw) {
        qCWarning(lc_audio, "[WasapiBackend] Render GetMixFormat failed");
        return false;
    }
    std::unique_ptr<WAVEFORMATEX, CoMemFree> wfx{wfxRaw};

    if (!isFloat32(wfx.get())) {
        qCWarning(lc_audio,
                  "[WasapiBackend] Render mix format not float32 (tag=%u, bits=%u). "
                  "PCM conversion not yet implemented.",
                  static_cast<unsigned>(wfx->wFormatTag),
                  static_cast<unsigned>(wfx->wBitsPerSample));
        return false;
    }

    if (static_cast<double>(wfx->nSamplesPerSec) != m_sample_rate) {
        qCWarning(lc_audio,
                  "[WasapiBackend] Capture/render sample-rate mismatch (%.0f vs %lu). "
                  "Resampling not yet implemented.",
                  m_sample_rate, static_cast<unsigned long>(wfx->nSamplesPerSec));
        return false;
    }
    m_render_channels = wfx->nChannels;

    hr = m_render_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0,
                                     0, wfx.get(), nullptr);
    if (FAILED(hr)) {
        qCWarning(lc_audio, "[WasapiBackend] Render Initialize: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    if (FAILED(m_render_client->GetService(__uuidof(IAudioRenderClient),
                                           reinterpret_cast<void**>(&m_render)))) {
        qCWarning(lc_audio, "[WasapiBackend] GetService IAudioRenderClient failed");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

void WasapiBackend::audio_thread_main() {
    // Pro Audio thread characteristics — bumps priority, registers with the
    // scheduler so we get a slice every audio quantum.
    DWORD taskIndex = 0;
    HANDLE avrtHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    // Flush denormals to zero on this thread. Reverb/delay feedback tails and
    // NAM recurrent state decay into the denormal range, where each float op
    // costs 10-100x and causes periodic CPU spikes. MXCSR is per-thread and
    // this thread is ours alone, so once at startup suffices.
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

    // Scratch buffers — reused across cycles, sized to whatever the device gave us.
    std::vector<float> mono_in(m_buffer_size, 0.0f);
    std::vector<float> out_l(m_buffer_size, 0.0f);
    std::vector<float> out_r(m_buffer_size, 0.0f);

    // Pending-output FIFO. Capture and render run on their own periods, so a
    // processed block does not always fit the render endpoint right away.
    // Anything that does not fit waits here and drains on the next cycle
    // instead of being discarded (which was an audible gap). Ring buffer of
    // frames, oldest at fifoHead. Capacity: the whole render buffer plus a
    // few capture blocks; if render stalls beyond that the oldest frames are
    // dropped — a discontinuity, but a bounded one.
    UINT32 renderBufSize = 0;
    if (FAILED(m_render_client->GetBufferSize(&renderBufSize))) {
        renderBufSize = static_cast<UINT32>(m_buffer_size);
    }
    std::vector<float> fifoL, fifoR;
    size_t fifoCap = 0, fifoHead = 0, fifoCount = 0;
    auto resize_fifo = [&](size_t blockFrames) {
        fifoCap = static_cast<size_t>(renderBufSize) + 4 * blockFrames;
        fifoL.assign(fifoCap, 0.0f);
        fifoR.assign(fifoCap, 0.0f);
        fifoHead = 0;
        fifoCount = 0;
    };
    resize_fifo(static_cast<size_t>(m_buffer_size));

    auto fifo_push = [&](const float* l, const float* r, size_t n) {
        if (n > fifoCap) {
            l += n - fifoCap;
            r += n - fifoCap;
            n = fifoCap;
        }
        // Make room by dropping the oldest frames if the render side stalled.
        const size_t overflow = (fifoCount + n > fifoCap) ? fifoCount + n - fifoCap : 0;
        fifoHead = (fifoHead + overflow) % fifoCap;
        fifoCount -= overflow;
        size_t tail = (fifoHead + fifoCount) % fifoCap;
        for (size_t i = 0; i < n; ++i) {
            fifoL[tail] = l[i];
            fifoR[tail] = r[i];
            if (++tail == fifoCap) {
                tail = 0;
            }
        }
        fifoCount += n;
    };

    // Write as many pending frames as the render endpoint has room for.
    auto drain_to_render = [&]() {
        if (fifoCount == 0) {
            return;
        }
        UINT32 padding = 0;
        if (FAILED(m_render_client->GetCurrentPadding(&padding))) {
            return;
        }
        const UINT32 available = renderBufSize > padding ? renderBufSize - padding : 0;
        const UINT32 toWrite = static_cast<UINT32>(std::min<size_t>(available, fifoCount));
        if (toWrite == 0) {
            return;
        }

        BYTE* dst = nullptr;
        if (FAILED(m_render->GetBuffer(toWrite, &dst)) || !dst) {
            return;
        }

        float* fdst = reinterpret_cast<float*>(dst);
        const int rc = m_render_channels;
        size_t src = fifoHead;
        for (UINT32 i = 0; i < toWrite; ++i) {
            const float l = fifoL[src], r = fifoR[src];
            if (++src == fifoCap) {
                src = 0;
            }
            if (rc == 2) {
                fdst[i * 2 + 0] = l;
                fdst[i * 2 + 1] = r;
            } else if (rc == 1) {
                fdst[i] = 0.5f * (l + r);
            } else {
                // 5.1/7.1: fill L,R, zero the rest. Lazy but valid.
                for (int c = 0; c < rc; ++c) {
                    fdst[i * rc + c] = (c == 0) ? l : (c == 1) ? r : 0.0f;
                }
            }
        }
        m_render->ReleaseBuffer(toWrite, 0);
        fifoHead = src;
        fifoCount -= toWrite;
    };

    HANDLE waitHandles[3] = {m_stop_event, m_capture_event, m_render_event};

    while (m_running.load()) {
        // Wake on capture data (the normal case) or on render space freeing
        // up while output is still pending.
        const DWORD nHandles = fifoCount > 0 ? 3 : 2;
        DWORD wait = WaitForMultipleObjects(nHandles, waitHandles, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 + K_STOP_EVENT_INDEX) {
            break;
        }
        if (wait == WAIT_OBJECT_0 + K_RENDER_EVENT_INDEX) {
            drain_to_render();
            continue;
        }
        if (wait != WAIT_OBJECT_0 + K_CAPTURE_EVENT_INDEX) {
            continue; // timeout or error → retry
        }

        // Drain all available capture packets this cycle (usually one).
        UINT32 packetFrames = 0;
        while (SUCCEEDED(m_capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(m_capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }

            if (frames > mono_in.size()) {
                // Rare mid-stream growth (device period change). This runs on
                // the audio thread itself, sequentially before on_audio — so
                // there is no concurrent process callback, but the resize and
                // model re-Reset it triggers are not RT-safe: one audible
                // glitch beats a buffer overrun.
                mono_in.assign(frames, 0.0f);
                out_l.assign(frames, 0.0f);
                out_r.assign(frames, 0.0f);
                resize_fifo(frames);
                if (m_callback) {
                    m_callback->on_buffer_size_changed(static_cast<int>(frames));
                }
            }

            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                m_xruns.fetch_add(1, std::memory_order_relaxed);
            }

            // Down-mix capture to mono float.
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill_n(mono_in.data(), frames, 0.0f);
            } else {
                const float* src = reinterpret_cast<const float*>(data);
                const int ch = m_capture_channels;
                if (ch == 1) {
                    std::copy_n(src, frames, mono_in.data());
                } else {
                    const float invCh = 1.0f / static_cast<float>(ch);
                    for (UINT32 i = 0; i < frames; ++i) {
                        float s = 0.0f;
                        for (int c = 0; c < ch; ++c) {
                            s += src[i * ch + c];
                        }
                        mono_in[i] = s * invCh;
                    }
                }
            }
            m_capture->ReleaseBuffer(frames);

            if (m_callback) {
                m_callback->on_audio(mono_in.data(), out_l.data(), out_r.data(),
                                     static_cast<int>(frames));
            }

            // Queue the whole processed block, then push whatever fits. The
            // remainder stays pending and drains on the next capture or
            // render event — nothing is discarded.
            fifo_push(out_l.data(), out_r.data(), frames);
            drain_to_render();
        }
    }

    if (avrtHandle) {
        AvRevertMmThreadCharacteristics(avrtHandle);
    }
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void WasapiBackend::teardown() {
    if (m_capture) {
        m_capture->Release();
        m_capture = nullptr;
    }
    if (m_render) {
        m_render->Release();
        m_render = nullptr;
    }
    if (m_capture_client) {
        m_capture_client->Release();
        m_capture_client = nullptr;
    }
    if (m_render_client) {
        m_render_client->Release();
        m_render_client = nullptr;
    }
    if (m_capture_device) {
        m_capture_device->Release();
        m_capture_device = nullptr;
    }
    if (m_render_device) {
        m_render_device->Release();
        m_render_device = nullptr;
    }
    if (m_enumerator) {
        m_enumerator->Release();
        m_enumerator = nullptr;
    }

    if (m_capture_event) {
        CloseHandle(m_capture_event);
        m_capture_event = nullptr;
    }
    if (m_render_event) {
        CloseHandle(m_render_event);
        m_render_event = nullptr;
    }
    if (m_stop_event) {
        CloseHandle(m_stop_event);
        m_stop_event = nullptr;
    }

    if (m_com_initialized) {
        CoUninitialize();
        m_com_initialized = false;
    }
}

#else // !_WIN32 — stub bodies so the file builds (it shouldn't on Linux, but JIC).

bool WasapiBackend::start(const std::string&, Callback*) {
    return false;
}
void WasapiBackend::stop() {
}
std::vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerate_input_devices() const {
    return {};
}
std::vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerate_output_devices() const {
    return {};
}
bool WasapiBackend::start_with_devices(const std::string&, Callback*, const std::string&,
                                       const std::string&) {
    return false;
}

#endif // _WIN32

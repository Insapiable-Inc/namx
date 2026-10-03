#include "WasapiBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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

namespace npl
{

constexpr int STOP_EVENT_INDEX = 0;
constexpr int CAPTURE_EVENT_INDEX = 1;
constexpr int RENDER_EVENT_INDEX = 2;

constexpr REFERENCE_TIME REF_TIMES_PER_SEC = 10000000LL; // 100-ns units

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
static bool is_float32(const WAVEFORMATEX* wfx) {
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

using SampleType = WasapiBackend::SampleType;

// Exclusive-mode format candidates, best first: the container size and how many of
// its bits carry the sample.
struct SampleLayout
{
    SampleType type;
    WORD container_bits;
    WORD valid_bits;
};
constexpr SampleLayout EXCLUSIVE_LAYOUTS[] = {
    {SampleType::FLOAT32, 32, 32}, {SampleType::INT32, 32, 32}, {SampleType::INT32, 32, 24},
    {SampleType::INT24, 24, 24},   {SampleType::INT16, 16, 16},
};

static int bytes_of(SampleType t) {
    switch (t) {
        case SampleType::INT24:
            return 3;
        case SampleType::INT16:
            return 2;
        default:
            return 4;
    }
}

// A WAVEFORMATEXTENSIBLE for `layout`, with the channel count and speaker mask of
// the device's own mix format.
static WAVEFORMATEXTENSIBLE make_format(const SampleLayout& layout, const WAVEFORMATEX* mix,
                                        DWORD rate) {
    WAVEFORMATEXTENSIBLE f{};
    f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.Format.nChannels = mix->nChannels;
    f.Format.nSamplesPerSec = rate;
    f.Format.wBitsPerSample = layout.container_bits;
    f.Format.nBlockAlign = static_cast<WORD>(mix->nChannels * layout.container_bits / 8);
    f.Format.nAvgBytesPerSec = rate * f.Format.nBlockAlign;
    f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f.Samples.wValidBitsPerSample = layout.valid_bits;
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        f.dwChannelMask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask;
    } else {
        f.dwChannelMask = mix->nChannels == 1   ? SPEAKER_FRONT_CENTER
                          : mix->nChannels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
                                                : 0;
    }
    f.SubFormat = layout.type == SampleType::FLOAT32 ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
                                                     : KSDATAFORMAT_SUBTYPE_PCM;
    return f;
}

// One sample at `p` as float (-1…1). INT32 also reads 24-in-32: the sample is
// left-justified, so the 32-bit scale is right for both.
static float read_sample(const BYTE* p, SampleType t) {
    switch (t) {
        case SampleType::FLOAT32: {
            float v;
            std::memcpy(&v, p, 4);
            return v;
        }
        case SampleType::INT32: {
            std::int32_t v;
            std::memcpy(&v, p, 4);
            return static_cast<float>(static_cast<double>(v) / 2147483648.0);
        }
        case SampleType::INT24: {
            const std::int32_t v =
                static_cast<std::int32_t>(static_cast<std::uint32_t>(p[0]) << 8 |
                                          static_cast<std::uint32_t>(p[1]) << 16 |
                                          static_cast<std::uint32_t>(p[2]) << 24) >>
                8;
            return static_cast<float>(v) / 8388608.0f;
        }
        case SampleType::INT16: {
            std::int16_t v;
            std::memcpy(&v, p, 2);
            return static_cast<float>(v) / 32768.0f;
        }
    }
    return 0.0f;
}

// Writes `v` (clamped to -1…1) at `p` in layout `t`.
static void write_sample(BYTE* p, SampleType t, float v) {
    const double x = std::clamp(static_cast<double>(v), -1.0, 1.0);
    switch (t) {
        case SampleType::FLOAT32: {
            const float f = static_cast<float>(x);
            std::memcpy(p, &f, 4);
            return;
        }
        case SampleType::INT32: {
            const std::int32_t i = static_cast<std::int32_t>(
                std::clamp(x * 2147483648.0, -2147483648.0, 2147483647.0));
            std::memcpy(p, &i, 4);
            return;
        }
        case SampleType::INT24: {
            const std::int32_t i = static_cast<std::int32_t>(std::lround(x * 8388607.0));
            p[0] = static_cast<BYTE>(i & 0xFF);
            p[1] = static_cast<BYTE>((i >> 8) & 0xFF);
            p[2] = static_cast<BYTE>((i >> 16) & 0xFF);
            return;
        }
        case SampleType::INT16: {
            const std::int16_t i = static_cast<std::int16_t>(std::lround(x * 32767.0));
            std::memcpy(p, &i, 2);
            return;
        }
    }
}

static string friendly_device_name(IMMDevice* dev) {
    if (!dev) {
        return {};
    }
    IPropertyStore* props = nullptr;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &props)) || !props) {
        return {};
    }

    PROPVARIANT pv;
    PropVariantInit(&pv);
    string name;
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

static string wide_to_utf8(LPCWSTR wstr) {
    if (!wstr) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

static std::wstring utf8_to_wide(const string& str) {
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

static vector<IAudioBackend::DeviceInfo> enumerate_wasapi_devices(EDataFlow direction) {
    vector<IAudioBackend::DeviceInfo> result;
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

} // namespace npl

#endif // _WIN32

namespace npl
{

unique_ptr<IAudioBackend> IAudioBackend::create() {
    return make_unique<WasapiBackend>();
}

WasapiBackend::WasapiBackend() = default;

WasapiBackend::~WasapiBackend() {
    stop();
}

// status_string — visible regardless of platform so the Linux build (where this
// file is not compiled) is unaffected. The body is conditionally compiled.

string WasapiBackend::status_string() const {
#ifdef _WIN32
    if (!running.load()) {
        return "WASAPI  \xe2\x97\x8f  not started";
    }
    std::ostringstream o;
    o << (exclusive ? "WASAPI exclusive  \xe2\x97\x8f  " : "WASAPI shared  \xe2\x97\x8f  ");
    if (!device_name.empty()) {
        o << device_name << "  \xe2\x97\x8f  ";
    }
    o << static_cast<int>(sample_rate) << " Hz  \xe2\x97\x8f  stereo out";
    return o.str();
#else
    return "WASAPI  \xe2\x97\x8f  not supported on this platform";
#endif
}

int WasapiBackend::io_latency_frames() const {
    // Exclusive: one capture period in, one render buffer out (the driver's own
    // latency on top isn't known). Shared mode adds the system mixer: unknown.
    return exclusive ? buffer_size + render_buffer_frames : -1;
}

vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerate_input_devices() const {
#ifdef _WIN32
    return enumerate_wasapi_devices(eCapture);
#else
    return {};
#endif
}

vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerate_output_devices() const {
#ifdef _WIN32
    return enumerate_wasapi_devices(eRender);
#else
    return {};
#endif
}

#ifdef _WIN32

IMMDevice* WasapiBackend::open_device_by_id(const string& deviceId) const {
    if (deviceId.empty() || deviceId == "default") {
        return nullptr; // Caller falls back to GetDefaultAudioEndpoint
    }

    std::wstring wId = utf8_to_wide(deviceId);
    IMMDevice* dev = nullptr;
    HRESULT hr = enumerator->GetDevice(wId.c_str(), &dev);
    if (FAILED(hr) || !dev) {
        qCWarning(lc_audio, "[WasapiBackend] GetDevice failed for id=%s: 0x%08lx", deviceId.c_str(),
                  static_cast<unsigned long>(hr));
        return nullptr;
    }
    return dev;
}

bool WasapiBackend::start(const string& client_name, Callback* cb) {
    return start_with_devices(client_name, cb, "default", "default");
}

bool WasapiBackend::start_with_devices(const string& /*clientName*/, Callback* cb,
                                       const string& input_device_id,
                                       const string& output_device_id) {
    if (running.load()) {
        return true;
    }
    callback = cb;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        qCWarning(lc_audio, "[WasapiBackend] CoInitializeEx failed: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }
    com_initialized = (hr == S_OK || hr == S_FALSE);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        qCWarning(lc_audio, "[WasapiBackend] MMDeviceEnumerator failed: 0x%08lx",
                  static_cast<unsigned long>(hr));
        teardown();
        return false;
    }

    // Select capture device
    capture_device = open_device_by_id(input_device_id);
    if (!capture_device) {
        // Fall back to default multimedia capture device
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &capture_device);
        if (FAILED(hr) || !capture_device) {
            qCWarning(lc_audio, "[WasapiBackend] No default capture device: 0x%08lx",
                      static_cast<unsigned long>(hr));
            teardown();
            return false;
        }
    }

    // Select render device
    render_device = open_device_by_id(output_device_id);
    if (!render_device) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &render_device);
        if (FAILED(hr) || !render_device) {
            qCWarning(lc_audio, "[WasapiBackend] No default render device: 0x%08lx",
                      static_cast<unsigned long>(hr));
            teardown();
            return false;
        }
    }

    qCInfo(lc_audio, "[WasapiBackend] Capture device: %s",
           friendly_device_name(capture_device).c_str());
    qCInfo(lc_audio, "[WasapiBackend] Render device: %s",
           friendly_device_name(render_device).c_str());

    // Exclusive when asked for and the devices accept it; shared otherwise.
    exclusive = false;
    capture_type = render_type = SampleType::FLOAT32;
    if (exclusive_wanted) {
        exclusive = init_exclusive();
        if (!exclusive) {
            qCWarning(lc_audio, "[WasapiBackend] Exclusive mode unavailable; using shared mode");
            release_streams();
            capture_type = render_type = SampleType::FLOAT32;
        }
    }
    if (!exclusive && (!init_capture() || !init_render())) {
        teardown();
        return false;
    }

    // Capture and render must share a sample rate (shared: both use the engine
    // mix rate; exclusive: init_exclusive picks one rate for both). If they ever
    // diverge we bail rather than resample.
    if (static_cast<int>(sample_rate) <= 0) {
        qCWarning(lc_audio, "[WasapiBackend] Invalid sample rate after Initialize");
        teardown();
        return false;
    }

    device_name = friendly_device_name(render_device);

    stop_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    capture_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    render_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stop_event || !capture_event || !render_event) {
        qCWarning(lc_audio, "[WasapiBackend] CreateEventW failed");
        teardown();
        return false;
    }

    if (FAILED(capture_client->SetEventHandle(capture_event)) ||
        FAILED(render_client->SetEventHandle(render_event))) {
        qCWarning(lc_audio, "[WasapiBackend] SetEventHandle failed");
        teardown();
        return false;
    }

    if (callback) {
        callback->on_buffer_size_changed(buffer_size);
    }

    // Pre-fill render buffer with silence so the first audio-thread cycle has
    // something to push without waiting for input to arrive. (Exclusive event mode
    // requires it: the first buffer must be written before Start.)
    {
        BYTE* buf = nullptr;
        UINT32 frames = 0;
        if (SUCCEEDED(render_client->GetBufferSize(&frames)) &&
            SUCCEEDED(render->GetBuffer(frames, &buf)) && buf) {
            const size_t bytes = static_cast<size_t>(frames) *
                                 static_cast<size_t>(render_channels) *
                                 static_cast<size_t>(bytes_of(render_type));
            std::fill_n(buf, bytes, 0);
            render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
        }
    }

    if (FAILED(capture_client->Start()) || FAILED(render_client->Start())) {
        qCWarning(lc_audio, "[WasapiBackend] IAudioClient::Start failed");
        teardown();
        return false;
    }

    running.store(true);
    audio_thread = std::thread(&WasapiBackend::audio_thread_main, this);
    qCInfo(lc_audio, "[WasapiBackend] WASAPI %s started at %.0f Hz, buffer %d frames",
           exclusive ? "exclusive" : "shared", sample_rate, static_cast<int>(buffer_size));
    return true;
}

void WasapiBackend::stop() {
    if (!running.load() && !audio_thread.joinable()) {
        teardown();
        return;
    }

    running.store(false);
    if (stop_event) {
        SetEvent(stop_event);
    }

    if (audio_thread.joinable()) {
        audio_thread.join();
    }

    if (capture_client) {
        capture_client->Stop();
    }
    if (render_client) {
        render_client->Stop();
    }

    teardown();
    callback = nullptr;
}

bool WasapiBackend::init_capture() {
    HRESULT hr = capture_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                          reinterpret_cast<void**>(&capture_client));
    if (FAILED(hr) || !capture_client) {
        qCWarning(lc_audio, "[WasapiBackend] Activate capture: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    WAVEFORMATEX* wfxRaw = nullptr;
    if (FAILED(capture_client->GetMixFormat(&wfxRaw)) || !wfxRaw) {
        qCWarning(lc_audio, "[WasapiBackend] Capture GetMixFormat failed");
        return false;
    }
    unique_ptr<WAVEFORMATEX, CoMemFree> wfx{wfxRaw};

    if (!is_float32(wfx.get())) {
        qCWarning(lc_audio,
                  "[WasapiBackend] Capture mix format not float32 (tag=%u, bits=%u). "
                  "PCM conversion not yet implemented.",
                  static_cast<unsigned>(wfx->wFormatTag),
                  static_cast<unsigned>(wfx->wBitsPerSample));
        return false;
    }

    sample_rate = wfx->nSamplesPerSec;
    capture_channels = wfx->nChannels;

    hr = capture_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    0, // hnsBufferDuration — system default for shared mode
                                    0, // periodicity must be 0 in shared mode
                                    wfx.get(), nullptr);
    if (FAILED(hr)) {
        qCWarning(lc_audio, "[WasapiBackend] Capture Initialize: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    UINT32 bufFrames = 0;
    if (FAILED(capture_client->GetBufferSize(&bufFrames))) {
        qCWarning(lc_audio, "[WasapiBackend] Capture GetBufferSize failed");
        return false;
    }
    buffer_size = static_cast<int>(bufFrames);

    if (FAILED(capture_client->GetService(__uuidof(IAudioCaptureClient),
                                          reinterpret_cast<void**>(&capture)))) {
        qCWarning(lc_audio, "[WasapiBackend] GetService IAudioCaptureClient failed");
        return false;
    }
    return true;
}

bool WasapiBackend::init_render() {
    HRESULT hr = render_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                         reinterpret_cast<void**>(&render_client));
    if (FAILED(hr) || !render_client) {
        qCWarning(lc_audio, "[WasapiBackend] Activate render: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    WAVEFORMATEX* wfxRaw = nullptr;
    if (FAILED(render_client->GetMixFormat(&wfxRaw)) || !wfxRaw) {
        qCWarning(lc_audio, "[WasapiBackend] Render GetMixFormat failed");
        return false;
    }
    unique_ptr<WAVEFORMATEX, CoMemFree> wfx{wfxRaw};

    if (!is_float32(wfx.get())) {
        qCWarning(lc_audio,
                  "[WasapiBackend] Render mix format not float32 (tag=%u, bits=%u). "
                  "PCM conversion not yet implemented.",
                  static_cast<unsigned>(wfx->wFormatTag),
                  static_cast<unsigned>(wfx->wBitsPerSample));
        return false;
    }

    if (static_cast<double>(wfx->nSamplesPerSec) != sample_rate) {
        qCWarning(lc_audio,
                  "[WasapiBackend] Capture/render sample-rate mismatch (%.0f vs %lu). "
                  "Resampling not yet implemented.",
                  sample_rate, static_cast<unsigned long>(wfx->nSamplesPerSec));
        return false;
    }
    render_channels = wfx->nChannels;

    hr = render_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0,
                                   0, wfx.get(), nullptr);
    if (FAILED(hr)) {
        qCWarning(lc_audio, "[WasapiBackend] Render Initialize: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    if (FAILED(render_client->GetService(__uuidof(IAudioRenderClient),
                                         reinterpret_cast<void**>(&render)))) {
        qCWarning(lc_audio, "[WasapiBackend] GetService IAudioRenderClient failed");
        return false;
    }
    return true;
}

// Initializes `client` (activated on `dev`) for an exclusive event-driven stream of
// `format` with one period of buffer. A device that wants a different period size
// answers AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED: the documented fix is to read the size
// it would accept, release the client, activate a fresh one and initialize again.
static bool init_exclusive_stream(IMMDevice* dev, IAudioClient*& client,
                                  const WAVEFORMATEXTENSIBLE& format, REFERENCE_TIME period,
                                  UINT32& frames) {
    HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    period, period, &format.Format, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        UINT32 aligned = 0;
        if (FAILED(client->GetBufferSize(&aligned))) {
            return false;
        }
        period = static_cast<REFERENCE_TIME>(
            static_cast<double>(REF_TIMES_PER_SEC) * aligned / format.Format.nSamplesPerSec + 0.5);
        client->Release();
        client = nullptr;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void**>(&client))) ||
            !client) {
            return false;
        }
        hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                period, period, &format.Format, nullptr);
    }
    if (FAILED(hr)) {
        qCWarning(lc_audio, "[WasapiBackend] Exclusive Initialize: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }
    return SUCCEEDED(client->GetBufferSize(&frames)) && frames > 0;
}

bool WasapiBackend::init_exclusive() {
    if (FAILED(capture_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void**>(&capture_client))) ||
        !capture_client ||
        FAILED(render_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(&render_client))) ||
        !render_client) {
        return false;
    }

    // Channel counts and speaker masks come from the devices' mix formats.
    WAVEFORMATEX* capture_mix_raw = nullptr;
    WAVEFORMATEX* render_mix_raw = nullptr;
    if (FAILED(capture_client->GetMixFormat(&capture_mix_raw)) || !capture_mix_raw ||
        FAILED(render_client->GetMixFormat(&render_mix_raw)) || !render_mix_raw) {
        CoTaskMemFree(capture_mix_raw);
        CoTaskMemFree(render_mix_raw);
        return false;
    }
    const unique_ptr<WAVEFORMATEX, CoMemFree> capture_mix{capture_mix_raw};
    const unique_ptr<WAVEFORMATEX, CoMemFree> render_mix{render_mix_raw};

    // The first rate both devices take, each with its best supported layout.
    const DWORD rates[] = {render_mix->nSamplesPerSec, 48000, 44100, 96000, 88200};
    auto best_layout = [](IAudioClient* client, const WAVEFORMATEX* mix,
                          DWORD rate) -> const SampleLayout* {
        for (const SampleLayout& layout : EXCLUSIVE_LAYOUTS) {
            const WAVEFORMATEXTENSIBLE f = make_format(layout, mix, rate);
            if (client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &f.Format, nullptr) ==
                S_OK) {
                return &layout;
            }
        }
        return nullptr;
    };
    DWORD rate = 0;
    const SampleLayout* capture_layout = nullptr;
    const SampleLayout* render_layout = nullptr;
    for (const DWORD r : rates) {
        capture_layout = best_layout(capture_client, capture_mix.get(), r);
        render_layout = best_layout(render_client, render_mix.get(), r);
        if (capture_layout && render_layout) {
            rate = r;
            break;
        }
    }
    if (rate == 0) {
        qCWarning(lc_audio, "[WasapiBackend] No exclusive format both devices accept");
        return false;
    }

    // The smallest period both devices allow.
    REFERENCE_TIME default_period = 0, capture_min = 0, render_min = 0;
    if (FAILED(capture_client->GetDevicePeriod(&default_period, &capture_min)) ||
        FAILED(render_client->GetDevicePeriod(&default_period, &render_min))) {
        return false;
    }
    const REFERENCE_TIME period = std::max(capture_min, render_min);

    const WAVEFORMATEXTENSIBLE capture_format =
        make_format(*capture_layout, capture_mix.get(), rate);
    const WAVEFORMATEXTENSIBLE render_format = make_format(*render_layout, render_mix.get(), rate);
    UINT32 capture_frames = 0, render_frames = 0;
    if (!init_exclusive_stream(capture_device, capture_client, capture_format, period,
                               capture_frames) ||
        !init_exclusive_stream(render_device, render_client, render_format, period,
                               render_frames)) {
        return false;
    }
    if (FAILED(capture_client->GetService(__uuidof(IAudioCaptureClient),
                                          reinterpret_cast<void**>(&capture))) ||
        FAILED(render_client->GetService(__uuidof(IAudioRenderClient),
                                         reinterpret_cast<void**>(&render)))) {
        return false;
    }

    sample_rate = rate;
    capture_channels = capture_mix->nChannels;
    render_channels = render_mix->nChannels;
    capture_type = capture_layout->type;
    render_type = render_layout->type;
    buffer_size = static_cast<int>(capture_frames);
    render_buffer_frames = static_cast<int>(render_frames);
    qCInfo(lc_audio, "[WasapiBackend] Exclusive: %lu Hz, %u/%u-bit in, %u/%u-bit out, %u/%u frames",
           static_cast<unsigned long>(rate), capture_layout->valid_bits,
           capture_layout->container_bits, render_layout->valid_bits, render_layout->container_bits,
           capture_frames, render_frames);
    return true;
}

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
    vector<float> mono_in(buffer_size, 0.0f);
    vector<float> out_l(buffer_size, 0.0f);
    vector<float> out_r(buffer_size, 0.0f);

    // Pending-output FIFO. Capture and render run on their own periods, so a
    // processed block does not always fit the render endpoint right away.
    // Anything that does not fit waits here and drains on the next cycle
    // instead of being discarded (which was an audible gap). Ring buffer of
    // frames, oldest at fifoHead. Capacity: the whole render buffer plus a
    // few capture blocks; if render stalls beyond that the oldest frames are
    // dropped — a discontinuity, but a bounded one.
    UINT32 renderBufSize = 0;
    if (FAILED(render_client->GetBufferSize(&renderBufSize))) {
        renderBufSize = static_cast<UINT32>(buffer_size);
    }
    const int render_bytes = bytes_of(render_type);
    const int capture_bytes = bytes_of(capture_type);
    vector<float> fifoL, fifoR;
    size_t fifoCap = 0, fifoHead = 0, fifoCount = 0;
    auto resize_fifo = [&](size_t blockFrames) {
        fifoCap = static_cast<size_t>(renderBufSize) + 4 * blockFrames;
        fifoL.assign(fifoCap, 0.0f);
        fifoR.assign(fifoCap, 0.0f);
        fifoHead = 0;
        fifoCount = 0;
    };
    resize_fifo(static_cast<size_t>(buffer_size));

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

    // Moves `frames` pending frames (silence past what's pending) into `dst`, in the
    // render endpoint's channel layout and sample format.
    auto write_frames = [&](BYTE* dst, UINT32 frames) {
        const int rc = render_channels;
        const size_t from_fifo = std::min<size_t>(frames, fifoCount);
        size_t src = fifoHead;
        for (UINT32 i = 0; i < frames; ++i) {
            float l = 0.0f, r = 0.0f;
            if (i < from_fifo) {
                l = fifoL[src];
                r = fifoR[src];
                if (++src == fifoCap) {
                    src = 0;
                }
            }
            for (int c = 0; c < rc; ++c) {
                // Stereo: L, R. Mono: their mix. 5.1/7.1: L, R, the rest silent.
                const float v = rc == 1 ? 0.5f * (l + r) : c == 0 ? l : c == 1 ? r : 0.0f;
                BYTE* p = dst + (static_cast<size_t>(i) * static_cast<size_t>(rc) +
                                 static_cast<size_t>(c)) *
                                    static_cast<size_t>(render_bytes);
                if (render_type == SampleType::FLOAT32) {
                    std::memcpy(p, &v, 4);
                } else {
                    write_sample(p, render_type, v);
                }
            }
        }
        fifoHead = src;
        fifoCount -= from_fifo;
    };

    // Shared mode: write as many pending frames as the render endpoint has room for.
    auto drain_to_render = [&]() {
        if (fifoCount == 0) {
            return;
        }
        UINT32 padding = 0;
        if (FAILED(render_client->GetCurrentPadding(&padding))) {
            return;
        }
        const UINT32 available = renderBufSize > padding ? renderBufSize - padding : 0;
        const UINT32 toWrite = static_cast<UINT32>(std::min<size_t>(available, fifoCount));
        if (toWrite == 0) {
            return;
        }

        BYTE* dst = nullptr;
        if (FAILED(render->GetBuffer(toWrite, &dst)) || !dst) {
            return;
        }
        write_frames(dst, toWrite);
        render->ReleaseBuffer(toWrite, 0);
    };

    // Exclusive event mode: every render event takes exactly one whole buffer; what
    // isn't pending yet goes out as silence.
    auto render_full_buffer = [&]() {
        BYTE* dst = nullptr;
        if (FAILED(render->GetBuffer(renderBufSize, &dst)) || !dst) {
            return;
        }
        write_frames(dst, renderBufSize);
        render->ReleaseBuffer(renderBufSize, 0);
    };

    HANDLE waitHandles[3] = {stop_event, capture_event, render_event};

    while (running.load()) {
        // Wake on capture data (the normal case) or on render space freeing up
        // while output is still pending. Exclusive mode must answer every render
        // event, pending output or not.
        const DWORD nHandles = (exclusive || fifoCount > 0) ? 3 : 2;
        DWORD wait = WaitForMultipleObjects(nHandles, waitHandles, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 + STOP_EVENT_INDEX) {
            break;
        }
        if (wait == WAIT_OBJECT_0 + RENDER_EVENT_INDEX) {
            if (exclusive) {
                render_full_buffer();
            } else {
                drain_to_render();
            }
            continue;
        }
        if (wait != WAIT_OBJECT_0 + CAPTURE_EVENT_INDEX) {
            continue; // timeout or error → retry
        }

        // Drain all available capture packets this cycle (usually one).
        UINT32 packetFrames = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
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
                if (callback) {
                    callback->on_buffer_size_changed(static_cast<int>(frames));
                }
            }

            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                xruns.fetch_add(1, std::memory_order_relaxed);
            }

            // Down-mix capture to mono float.
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill_n(mono_in.data(), frames, 0.0f);
            } else if (capture_type == SampleType::FLOAT32) {
                const float* src = reinterpret_cast<const float*>(data);
                const int ch = capture_channels;
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
            } else {
                // Exclusive mode, integer samples: convert while down-mixing.
                const int ch = capture_channels;
                const float invCh = 1.0f / static_cast<float>(ch);
                for (UINT32 i = 0; i < frames; ++i) {
                    float s = 0.0f;
                    for (int c = 0; c < ch; ++c) {
                        s += read_sample(data + (static_cast<size_t>(i) * static_cast<size_t>(ch) +
                                                 static_cast<size_t>(c)) *
                                                    static_cast<size_t>(capture_bytes),
                                         capture_type);
                    }
                    mono_in[i] = s * invCh;
                }
            }
            capture->ReleaseBuffer(frames);

            if (callback) {
                callback->on_audio(mono_in.data(), out_l.data(), out_r.data(),
                                   static_cast<int>(frames));
            }

            // Queue the whole processed block, then push whatever fits. The
            // remainder stays pending and drains on the next capture or
            // render event — nothing is discarded. (Exclusive mode writes only on
            // render events, whole buffers at a time.)
            fifo_push(out_l.data(), out_r.data(), frames);
            if (!exclusive) {
                drain_to_render();
            }
        }
    }

    if (avrtHandle) {
        AvRevertMmThreadCharacteristics(avrtHandle);
    }
}

void WasapiBackend::release_streams() {
    if (capture) {
        capture->Release();
        capture = nullptr;
    }
    if (render) {
        render->Release();
        render = nullptr;
    }
    if (capture_client) {
        capture_client->Release();
        capture_client = nullptr;
    }
    if (render_client) {
        render_client->Release();
        render_client = nullptr;
    }
}

void WasapiBackend::teardown() {
    release_streams();
    if (capture_device) {
        capture_device->Release();
        capture_device = nullptr;
    }
    if (render_device) {
        render_device->Release();
        render_device = nullptr;
    }
    if (enumerator) {
        enumerator->Release();
        enumerator = nullptr;
    }

    if (capture_event) {
        CloseHandle(capture_event);
        capture_event = nullptr;
    }
    if (render_event) {
        CloseHandle(render_event);
        render_event = nullptr;
    }
    if (stop_event) {
        CloseHandle(stop_event);
        stop_event = nullptr;
    }

    if (com_initialized) {
        CoUninitialize();
        com_initialized = false;
    }
}

#else // !_WIN32 — stub bodies so the file builds (it shouldn't on Linux, but JIC).

bool WasapiBackend::start(const string&, Callback*) {
    return false;
}
void WasapiBackend::stop() {
}
vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerate_input_devices() const {
    return {};
}
vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerate_output_devices() const {
    return {};
}
bool WasapiBackend::start_with_devices(const string&, Callback*, const string&, const string&) {
    return false;
}

#endif // _WIN32

} // namespace npl

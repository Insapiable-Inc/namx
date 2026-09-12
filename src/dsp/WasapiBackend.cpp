#include "WasapiBackend.h"

#include <algorithm>
#include "Log.h"
#include <sstream>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>

#include <ks.h>
#include <ksmedia.h>  // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT

#include <immintrin.h>  // _MM_SET_FLUSH_ZERO_MODE / _MM_SET_DENORMALS_ZERO_MODE

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "avrt.lib")

namespace {

constexpr int kStopEventIndex    = 0;
constexpr int kCaptureEventIndex = 1;
constexpr int kRenderEventIndex  = 2;

constexpr REFERENCE_TIME kRefTimesPerSec = 10000000LL;  // 100-ns units

// CoTaskMemFree helper — WAVEFORMATEX returned by GetMixFormat is
// COM-allocated and must be freed by the caller.
struct CoMemFree {
    void operator()(void* p) const { if (p) CoTaskMemFree(p); }
};

// Inspect a WAVEFORMATEX and return true iff it's 32-bit float PCM.
bool isFloat32(const WAVEFORMATEX* wfx)
{
    if (!wfx) return false;
    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wfx->wBitsPerSample == 32)
        return true;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wfx->wBitsPerSample == 32) {
        auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return false;
}

std::string friendlyDeviceName(IMMDevice* dev)
{
    if (!dev) return {};
    IPropertyStore* props = nullptr;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &props)) || !props) return {};

    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::string name;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
        // Narrow the wide string for the status pill; ASCII-fold non-ASCII.
        for (const wchar_t* p = pv.pwszVal; *p; ++p)
            name += (*p < 128) ? static_cast<char>(*p) : '?';
    }
    PropVariantClear(&pv);
    props->Release();
    return name;
}

std::string wideToUtf8(LPCWSTR wstr)
{
    if (!wstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& str)
{
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, result.data(), len);
    return result;
}

std::vector<IAudioBackend::DeviceInfo> enumerateWASAPIDevices(EDataFlow direction)
{
    std::vector<IAudioBackend::DeviceInfo> result;
    result.push_back({"default", "Default"});

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comInit = (hr == S_OK || hr == S_FALSE);
    // RPC_E_CHANGED_MODE means COM already initialized — fine.

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        if (comInit) CoUninitialize();
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
                    info.id = wideToUtf8(idStr);
                    CoTaskMemFree(idStr);
                }
                info.name = friendlyDeviceName(dev);
                if (!info.name.empty() && !info.id.empty())
                    result.push_back(info);
                dev->Release();
            }
        }
        collection->Release();
    }
    enumerator->Release();
    if (comInit) CoUninitialize();
    return result;
}

} // namespace

#endif // _WIN32

// ---------------------------------------------------------------------------
// Factory (Windows side; the Linux factory lives in JackBackend.cpp).
// ---------------------------------------------------------------------------

std::unique_ptr<IAudioBackend> IAudioBackend::create()
{
    return std::make_unique<WasapiBackend>();
}

WasapiBackend::WasapiBackend() = default;

WasapiBackend::~WasapiBackend()
{
    stop();
}

// ---------------------------------------------------------------------------
// statusString — visible regardless of platform so the Linux build (where this
// file is not compiled) is unaffected. The body is conditionally compiled.
// ---------------------------------------------------------------------------

std::string WasapiBackend::statusString() const
{
#ifdef _WIN32
    if (!mRunning.load())
        return "WASAPI  \xe2\x97\x8f  not started";
    std::ostringstream o;
    o << "WASAPI  \xe2\x97\x8f  ";
    if (!mDeviceName.empty()) o << mDeviceName << "  \xe2\x97\x8f  ";
    o << static_cast<int>(mSampleRate) << " Hz  \xe2\x97\x8f  stereo out";
    return o.str();
#else
    return "WASAPI  \xe2\x97\x8f  not supported on this platform";
#endif
}

// ---------------------------------------------------------------------------
// Device enumeration (works before start() is called)
// ---------------------------------------------------------------------------

std::vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerateInputDevices() const
{
#ifdef _WIN32
    return enumerateWASAPIDevices(eCapture);
#else
    return {};
#endif
}

std::vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerateOutputDevices() const
{
#ifdef _WIN32
    return enumerateWASAPIDevices(eRender);
#else
    return {};
#endif
}

#ifdef _WIN32

// ---------------------------------------------------------------------------
// Device selection helper
// ---------------------------------------------------------------------------

IMMDevice* WasapiBackend::openDeviceById(const std::string& deviceId) const
{
    if (deviceId.empty() || deviceId == "default")
        return nullptr;  // Caller falls back to GetDefaultAudioEndpoint

    std::wstring wId = utf8ToWide(deviceId);
    IMMDevice* dev = nullptr;
    HRESULT hr = mEnumerator->GetDevice(wId.c_str(), &dev);
    if (FAILED(hr) || !dev) {
        qCWarning(lcAudio, "[WasapiBackend] GetDevice failed for id=%s: 0x%08lx",
                  deviceId.c_str(), static_cast<unsigned long>(hr));
        return nullptr;
    }
    return dev;
}

// ---------------------------------------------------------------------------
// start / stop / startWithDevices
// ---------------------------------------------------------------------------

bool WasapiBackend::start(const std::string& clientName, Callback* cb)
{
    return startWithDevices(clientName, cb, "default", "default");
}

bool WasapiBackend::startWithDevices(const std::string& /*clientName*/, Callback* cb,
                                     const std::string& inputDeviceId,
                                     const std::string& outputDeviceId)
{
    if (mRunning.load()) return true;
    mCallback = cb;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        qCWarning(lcAudio, "[WasapiBackend] CoInitializeEx failed: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }
    mComInitialized = (hr == S_OK || hr == S_FALSE);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&mEnumerator));
    if (FAILED(hr) || !mEnumerator) {
        qCWarning(lcAudio, "[WasapiBackend] MMDeviceEnumerator failed: 0x%08lx",
                  static_cast<unsigned long>(hr));
        teardown();
        return false;
    }

    // --- Select capture device ---
    mCaptureDevice = openDeviceById(inputDeviceId);
    if (!mCaptureDevice) {
        // Fall back to default multimedia capture device
        hr = mEnumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &mCaptureDevice);
        if (FAILED(hr) || !mCaptureDevice) {
            qCWarning(lcAudio, "[WasapiBackend] No default capture device: 0x%08lx",
                      static_cast<unsigned long>(hr));
            teardown();
            return false;
        }
    }

    // --- Select render device ---
    mRenderDevice = openDeviceById(outputDeviceId);
    if (!mRenderDevice) {
        hr = mEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &mRenderDevice);
        if (FAILED(hr) || !mRenderDevice) {
            qCWarning(lcAudio, "[WasapiBackend] No default render device: 0x%08lx",
                      static_cast<unsigned long>(hr));
            teardown();
            return false;
        }
    }

    qCInfo(lcAudio, "[WasapiBackend] Capture device: %s", friendlyDeviceName(mCaptureDevice).c_str());
    qCInfo(lcAudio, "[WasapiBackend] Render device: %s", friendlyDeviceName(mRenderDevice).c_str());

    if (!initCapture() || !initRender()) {
        teardown();
        return false;
    }

    // Capture and render must share a sample rate in shared mode (both use the
    // engine mix rate). If they ever diverge we bail rather than resample.
    if (static_cast<int>(mSampleRate) <= 0) {
        qCWarning(lcAudio, "[WasapiBackend] Invalid sample rate after Initialize");
        teardown();
        return false;
    }

    mDeviceName = friendlyDeviceName(mRenderDevice);

    mStopEvent    = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    mCaptureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    mRenderEvent  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!mStopEvent || !mCaptureEvent || !mRenderEvent) {
        qCWarning(lcAudio, "[WasapiBackend] CreateEventW failed");
        teardown();
        return false;
    }

    if (FAILED(mCaptureClient->SetEventHandle(mCaptureEvent)) ||
        FAILED(mRenderClient ->SetEventHandle(mRenderEvent)))
    {
        qCWarning(lcAudio, "[WasapiBackend] SetEventHandle failed");
        teardown();
        return false;
    }

    if (mCallback) mCallback->onBufferSizeChanged(mBufferSize);

    // Pre-fill render buffer with silence so the first audio-thread cycle has
    // something to push without waiting for input to arrive.
    {
        BYTE* buf = nullptr;
        UINT32 frames = 0;
        if (SUCCEEDED(mRenderClient->GetBufferSize(&frames)) &&
            SUCCEEDED(mRender->GetBuffer(frames, &buf)) && buf)
        {
            const size_t bytes = static_cast<size_t>(frames) * mRenderChannels * sizeof(float);
            std::fill_n(buf, bytes, 0);
            mRender->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
        }
    }

    if (FAILED(mCaptureClient->Start()) || FAILED(mRenderClient->Start())) {
        qCWarning(lcAudio, "[WasapiBackend] IAudioClient::Start failed");
        teardown();
        return false;
    }

    mRunning.store(true);
    mAudioThread = std::thread(&WasapiBackend::audioThreadMain, this);
    qCInfo(lcAudio, "[WasapiBackend] WASAPI started at %.0f Hz, buffer %d frames",
           mSampleRate, static_cast<int>(mBufferSize));
    return true;
}

void WasapiBackend::stop()
{
    if (!mRunning.load() && !mAudioThread.joinable()) {
        teardown();
        return;
    }

    mRunning.store(false);
    if (mStopEvent) SetEvent(mStopEvent);

    if (mAudioThread.joinable())
        mAudioThread.join();

    if (mCaptureClient) mCaptureClient->Stop();
    if (mRenderClient)  mRenderClient->Stop();

    teardown();
    mCallback = nullptr;
}

// ---------------------------------------------------------------------------
// Per-side init helpers
// ---------------------------------------------------------------------------

bool WasapiBackend::initCapture()
{
    HRESULT hr = mCaptureDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                          reinterpret_cast<void**>(&mCaptureClient));
    if (FAILED(hr) || !mCaptureClient) {
        qCWarning(lcAudio, "[WasapiBackend] Activate capture: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    WAVEFORMATEX* wfxRaw = nullptr;
    if (FAILED(mCaptureClient->GetMixFormat(&wfxRaw)) || !wfxRaw) {
        qCWarning(lcAudio, "[WasapiBackend] Capture GetMixFormat failed");
        return false;
    }
    std::unique_ptr<WAVEFORMATEX, CoMemFree> wfx{wfxRaw};

    if (!isFloat32(wfx.get())) {
        qCWarning(lcAudio,
                  "[WasapiBackend] Capture mix format not float32 (tag=%u, bits=%u). "
                  "PCM conversion not yet implemented.",
                  static_cast<unsigned>(wfx->wFormatTag), static_cast<unsigned>(wfx->wBitsPerSample));
        return false;
    }

    mSampleRate      = wfx->nSamplesPerSec;
    mCaptureChannels = wfx->nChannels;

    hr = mCaptureClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        0,        // hnsBufferDuration — system default for shared mode
        0,        // periodicity must be 0 in shared mode
        wfx.get(),
        nullptr);
    if (FAILED(hr)) {
        qCWarning(lcAudio, "[WasapiBackend] Capture Initialize: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    UINT32 bufFrames = 0;
    if (FAILED(mCaptureClient->GetBufferSize(&bufFrames))) {
        qCWarning(lcAudio, "[WasapiBackend] Capture GetBufferSize failed");
        return false;
    }
    mBufferSize = static_cast<int>(bufFrames);

    if (FAILED(mCaptureClient->GetService(__uuidof(IAudioCaptureClient),
                                          reinterpret_cast<void**>(&mCapture))))
    {
        qCWarning(lcAudio, "[WasapiBackend] GetService IAudioCaptureClient failed");
        return false;
    }
    return true;
}

bool WasapiBackend::initRender()
{
    HRESULT hr = mRenderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                         reinterpret_cast<void**>(&mRenderClient));
    if (FAILED(hr) || !mRenderClient) {
        qCWarning(lcAudio, "[WasapiBackend] Activate render: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    WAVEFORMATEX* wfxRaw = nullptr;
    if (FAILED(mRenderClient->GetMixFormat(&wfxRaw)) || !wfxRaw) {
        qCWarning(lcAudio, "[WasapiBackend] Render GetMixFormat failed");
        return false;
    }
    std::unique_ptr<WAVEFORMATEX, CoMemFree> wfx{wfxRaw};

    if (!isFloat32(wfx.get())) {
        qCWarning(lcAudio,
                  "[WasapiBackend] Render mix format not float32 (tag=%u, bits=%u). "
                  "PCM conversion not yet implemented.",
                  static_cast<unsigned>(wfx->wFormatTag), static_cast<unsigned>(wfx->wBitsPerSample));
        return false;
    }

    if (static_cast<double>(wfx->nSamplesPerSec) != mSampleRate) {
        qCWarning(lcAudio,
                  "[WasapiBackend] Capture/render sample-rate mismatch (%.0f vs %lu). "
                  "Resampling not yet implemented.",
                  mSampleRate, static_cast<unsigned long>(wfx->nSamplesPerSec));
        return false;
    }
    mRenderChannels = wfx->nChannels;

    hr = mRenderClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        0, 0,
        wfx.get(),
        nullptr);
    if (FAILED(hr)) {
        qCWarning(lcAudio, "[WasapiBackend] Render Initialize: 0x%08lx",
                  static_cast<unsigned long>(hr));
        return false;
    }

    if (FAILED(mRenderClient->GetService(__uuidof(IAudioRenderClient),
                                         reinterpret_cast<void**>(&mRender))))
    {
        qCWarning(lcAudio, "[WasapiBackend] GetService IAudioRenderClient failed");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

void WasapiBackend::audioThreadMain()
{
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
    std::vector<float> monoIn(mBufferSize, 0.0f);
    std::vector<float> outL  (mBufferSize, 0.0f);
    std::vector<float> outR  (mBufferSize, 0.0f);

    // Pending-output FIFO. Capture and render run on their own periods, so a
    // processed block does not always fit the render endpoint right away.
    // Anything that does not fit waits here and drains on the next cycle
    // instead of being discarded (which was an audible gap). Ring buffer of
    // frames, oldest at fifoHead. Capacity: the whole render buffer plus a
    // few capture blocks; if render stalls beyond that the oldest frames are
    // dropped — a discontinuity, but a bounded one.
    UINT32 renderBufSize = 0;
    if (FAILED(mRenderClient->GetBufferSize(&renderBufSize)))
        renderBufSize = static_cast<UINT32>(mBufferSize);
    std::vector<float> fifoL, fifoR;
    size_t fifoCap = 0, fifoHead = 0, fifoCount = 0;
    auto resizeFifo = [&](size_t blockFrames) {
        fifoCap   = static_cast<size_t>(renderBufSize) + 4 * blockFrames;
        fifoL.assign(fifoCap, 0.0f);
        fifoR.assign(fifoCap, 0.0f);
        fifoHead  = 0;
        fifoCount = 0;
    };
    resizeFifo(static_cast<size_t>(mBufferSize));

    auto fifoPush = [&](const float* l, const float* r, size_t n) {
        if (n > fifoCap) { l += n - fifoCap; r += n - fifoCap; n = fifoCap; }
        // Make room by dropping the oldest frames if the render side stalled.
        const size_t overflow = (fifoCount + n > fifoCap) ? fifoCount + n - fifoCap : 0;
        fifoHead   = (fifoHead + overflow) % fifoCap;
        fifoCount -= overflow;
        size_t tail = (fifoHead + fifoCount) % fifoCap;
        for (size_t i = 0; i < n; ++i) {
            fifoL[tail] = l[i];
            fifoR[tail] = r[i];
            if (++tail == fifoCap) tail = 0;
        }
        fifoCount += n;
    };

    // Write as many pending frames as the render endpoint has room for.
    auto drainToRender = [&]() {
        if (fifoCount == 0) return;
        UINT32 padding = 0;
        if (FAILED(mRenderClient->GetCurrentPadding(&padding))) return;
        const UINT32 available = renderBufSize > padding ? renderBufSize - padding : 0;
        const UINT32 toWrite   = static_cast<UINT32>(std::min<size_t>(available, fifoCount));
        if (toWrite == 0) return;

        BYTE* dst = nullptr;
        if (FAILED(mRender->GetBuffer(toWrite, &dst)) || !dst) return;

        float*    fdst = reinterpret_cast<float*>(dst);
        const int rc   = mRenderChannels;
        size_t    src  = fifoHead;
        for (UINT32 i = 0; i < toWrite; ++i) {
            const float l = fifoL[src], r = fifoR[src];
            if (++src == fifoCap) src = 0;
            if (rc == 2) {
                fdst[i*2 + 0] = l;
                fdst[i*2 + 1] = r;
            } else if (rc == 1) {
                fdst[i] = 0.5f * (l + r);
            } else {
                // 5.1/7.1: fill L,R, zero the rest. Lazy but valid.
                for (int c = 0; c < rc; ++c)
                    fdst[i*rc + c] = (c == 0) ? l : (c == 1) ? r : 0.0f;
            }
        }
        mRender->ReleaseBuffer(toWrite, 0);
        fifoHead   = src;
        fifoCount -= toWrite;
    };

    HANDLE waitHandles[3] = { mStopEvent, mCaptureEvent, mRenderEvent };

    while (mRunning.load())
    {
        // Wake on capture data (the normal case) or on render space freeing
        // up while output is still pending.
        const DWORD nHandles = fifoCount > 0 ? 3 : 2;
        DWORD wait = WaitForMultipleObjects(nHandles, waitHandles, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 + kStopEventIndex) break;
        if (wait == WAIT_OBJECT_0 + kRenderEventIndex) { drainToRender(); continue; }
        if (wait != WAIT_OBJECT_0 + kCaptureEventIndex) continue; // timeout or error → retry

        // Drain all available capture packets this cycle (usually one).
        UINT32 packetFrames = 0;
        while (SUCCEEDED(mCapture->GetNextPacketSize(&packetFrames)) && packetFrames > 0)
        {
            BYTE*  data   = nullptr;
            UINT32 frames = 0;
            DWORD  flags  = 0;
            if (FAILED(mCapture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;

            if (frames > monoIn.size()) {
                // Rare mid-stream growth (device period change). This runs on
                // the audio thread itself, sequentially before onAudio — so
                // there is no concurrent process callback, but the resize and
                // model re-Reset it triggers are not RT-safe: one audible
                // glitch beats a buffer overrun.
                monoIn.assign(frames, 0.0f);
                outL  .assign(frames, 0.0f);
                outR  .assign(frames, 0.0f);
                resizeFifo(frames);
                if (mCallback) mCallback->onBufferSizeChanged(static_cast<int>(frames));
            }

            // Down-mix capture to mono float.
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill_n(monoIn.data(), frames, 0.0f);
            } else {
                const float* src = reinterpret_cast<const float*>(data);
                const int ch = mCaptureChannels;
                if (ch == 1) {
                    std::copy_n(src, frames, monoIn.data());
                } else {
                    const float invCh = 1.0f / static_cast<float>(ch);
                    for (UINT32 i = 0; i < frames; ++i) {
                        float s = 0.0f;
                        for (int c = 0; c < ch; ++c) s += src[i * ch + c];
                        monoIn[i] = s * invCh;
                    }
                }
            }
            mCapture->ReleaseBuffer(frames);

            if (mCallback) mCallback->onAudio(monoIn.data(), outL.data(), outR.data(),
                                              static_cast<int>(frames));

            // Queue the whole processed block, then push whatever fits. The
            // remainder stays pending and drains on the next capture or
            // render event — nothing is discarded.
            fifoPush(outL.data(), outR.data(), frames);
            drainToRender();
        }
    }

    if (avrtHandle) AvRevertMmThreadCharacteristics(avrtHandle);
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void WasapiBackend::teardown()
{
    if (mCapture)        { mCapture->Release();        mCapture = nullptr; }
    if (mRender)         { mRender->Release();         mRender = nullptr; }
    if (mCaptureClient)  { mCaptureClient->Release();  mCaptureClient = nullptr; }
    if (mRenderClient)   { mRenderClient->Release();   mRenderClient = nullptr; }
    if (mCaptureDevice)  { mCaptureDevice->Release();  mCaptureDevice = nullptr; }
    if (mRenderDevice)   { mRenderDevice->Release();   mRenderDevice = nullptr; }
    if (mEnumerator)     { mEnumerator->Release();     mEnumerator = nullptr; }

    if (mCaptureEvent)   { CloseHandle(mCaptureEvent); mCaptureEvent = nullptr; }
    if (mRenderEvent)    { CloseHandle(mRenderEvent);  mRenderEvent = nullptr; }
    if (mStopEvent)      { CloseHandle(mStopEvent);    mStopEvent = nullptr; }

    if (mComInitialized) {
        CoUninitialize();
        mComInitialized = false;
    }
}

#else  // !_WIN32 — stub bodies so the file builds (it shouldn't on Linux, but JIC).

bool WasapiBackend::start(const std::string&, Callback*) { return false; }
void WasapiBackend::stop() {}
std::vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerateInputDevices()  const { return {}; }
std::vector<IAudioBackend::DeviceInfo> WasapiBackend::enumerateOutputDevices() const { return {}; }
bool WasapiBackend::startWithDevices(const std::string&, Callback*,
                                     const std::string&, const std::string&) { return false; }

#endif // _WIN32
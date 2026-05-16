#include "WasapiBackend.h"

#include <algorithm>
#include <iostream>
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

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "avrt.lib")

namespace {

constexpr int kStopEventIndex    = 0;
constexpr int kCaptureEventIndex = 1;

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
        return "WASAPI  ●  not started";
    std::ostringstream o;
    o << "WASAPI  ●  ";
    if (!mDeviceName.empty()) o << mDeviceName << "  ●  ";
    o << static_cast<int>(mSampleRate) << " Hz  ●  stereo out";
    return o.str();
#else
    return "WASAPI  ●  not supported on this platform";
#endif
}

#ifdef _WIN32

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

bool WasapiBackend::start(const std::string& /*clientName*/, Callback* cb)
{
    if (mRunning.load()) return true;
    mCallback = cb;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr != S_OK && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        std::cerr << "[WasapiBackend] CoInitializeEx failed: 0x" << std::hex << hr << "\n";
        return false;
    }
    mComInitialized = (hr == S_OK || hr == S_FALSE);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&mEnumerator));
    if (FAILED(hr) || !mEnumerator) {
        std::cerr << "[WasapiBackend] MMDeviceEnumerator failed: 0x" << std::hex << hr << "\n";
        teardown();
        return false;
    }

    // Default capture device for "communications" — biased toward mic/line-in.
    hr = mEnumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &mCaptureDevice);
    if (FAILED(hr) || !mCaptureDevice) {
        std::cerr << "[WasapiBackend] No default capture device: 0x" << std::hex << hr << "\n";
        teardown();
        return false;
    }

    hr = mEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &mRenderDevice);
    if (FAILED(hr) || !mRenderDevice) {
        std::cerr << "[WasapiBackend] No default render device: 0x" << std::hex << hr << "\n";
        teardown();
        return false;
    }

    if (!initCapture() || !initRender()) {
        teardown();
        return false;
    }

    // Capture and render must share a sample rate in shared mode (both use the
    // engine mix rate). If they ever diverge we bail rather than resample.
    if (static_cast<int>(mSampleRate) <= 0) {
        std::cerr << "[WasapiBackend] Invalid sample rate after Initialize\n";
        teardown();
        return false;
    }

    mDeviceName = friendlyDeviceName(mRenderDevice);

    mStopEvent    = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    mCaptureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    mRenderEvent  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!mStopEvent || !mCaptureEvent || !mRenderEvent) {
        std::cerr << "[WasapiBackend] CreateEventW failed\n";
        teardown();
        return false;
    }

    if (FAILED(mCaptureClient->SetEventHandle(mCaptureEvent)) ||
        FAILED(mRenderClient ->SetEventHandle(mRenderEvent)))
    {
        std::cerr << "[WasapiBackend] SetEventHandle failed\n";
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
        std::cerr << "[WasapiBackend] IAudioClient::Start failed\n";
        teardown();
        return false;
    }

    mRunning.store(true);
    mAudioThread = std::thread(&WasapiBackend::audioThreadMain, this);
    std::cout << "[WasapiBackend] WASAPI started at " << mSampleRate
              << " Hz, buffer " << mBufferSize << " frames\n";
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
        std::cerr << "[WasapiBackend] Activate capture: 0x" << std::hex << hr << "\n";
        return false;
    }

    WAVEFORMATEX* wfxRaw = nullptr;
    if (FAILED(mCaptureClient->GetMixFormat(&wfxRaw)) || !wfxRaw) {
        std::cerr << "[WasapiBackend] Capture GetMixFormat failed\n";
        return false;
    }
    std::unique_ptr<WAVEFORMATEX, CoMemFree> wfx{wfxRaw};

    if (!isFloat32(wfx.get())) {
        std::cerr << "[WasapiBackend] Capture mix format not float32 (tag="
                  << wfx->wFormatTag << ", bits=" << wfx->wBitsPerSample
                  << "). PCM conversion not yet implemented.\n";
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
        std::cerr << "[WasapiBackend] Capture Initialize: 0x" << std::hex << hr << "\n";
        return false;
    }

    UINT32 bufFrames = 0;
    if (FAILED(mCaptureClient->GetBufferSize(&bufFrames))) {
        std::cerr << "[WasapiBackend] Capture GetBufferSize failed\n";
        return false;
    }
    mBufferSize = static_cast<int>(bufFrames);

    if (FAILED(mCaptureClient->GetService(__uuidof(IAudioCaptureClient),
                                          reinterpret_cast<void**>(&mCapture))))
    {
        std::cerr << "[WasapiBackend] GetService IAudioCaptureClient failed\n";
        return false;
    }
    return true;
}

bool WasapiBackend::initRender()
{
    HRESULT hr = mRenderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                         reinterpret_cast<void**>(&mRenderClient));
    if (FAILED(hr) || !mRenderClient) {
        std::cerr << "[WasapiBackend] Activate render: 0x" << std::hex << hr << "\n";
        return false;
    }

    WAVEFORMATEX* wfxRaw = nullptr;
    if (FAILED(mRenderClient->GetMixFormat(&wfxRaw)) || !wfxRaw) {
        std::cerr << "[WasapiBackend] Render GetMixFormat failed\n";
        return false;
    }
    std::unique_ptr<WAVEFORMATEX, CoMemFree> wfx{wfxRaw};

    if (!isFloat32(wfx.get())) {
        std::cerr << "[WasapiBackend] Render mix format not float32 (tag="
                  << wfx->wFormatTag << ", bits=" << wfx->wBitsPerSample
                  << "). PCM conversion not yet implemented.\n";
        return false;
    }

    if (static_cast<double>(wfx->nSamplesPerSec) != mSampleRate) {
        std::cerr << "[WasapiBackend] Capture/render sample-rate mismatch ("
                  << mSampleRate << " vs " << wfx->nSamplesPerSec
                  << "). Resampling not yet implemented.\n";
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
        std::cerr << "[WasapiBackend] Render Initialize: 0x" << std::hex << hr << "\n";
        return false;
    }

    if (FAILED(mRenderClient->GetService(__uuidof(IAudioRenderClient),
                                         reinterpret_cast<void**>(&mRender))))
    {
        std::cerr << "[WasapiBackend] GetService IAudioRenderClient failed\n";
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

    // Scratch buffers — reused across cycles, sized to whatever the device gave us.
    std::vector<float> monoIn(mBufferSize, 0.0f);
    std::vector<float> outL  (mBufferSize, 0.0f);
    std::vector<float> outR  (mBufferSize, 0.0f);

    HANDLE waitHandles[2] = { mStopEvent, mCaptureEvent };

    while (mRunning.load())
    {
        DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, 2000);
        if (wait == WAIT_OBJECT_0 + kStopEventIndex) break;
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
                monoIn.assign(frames, 0.0f);
                outL  .assign(frames, 0.0f);
                outR  .assign(frames, 0.0f);
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

            // Push to render. Wait briefly for space if the buffer is full.
            UINT32 renderBufSize = 0;
            if (FAILED(mRenderClient->GetBufferSize(&renderBufSize))) continue;

            UINT32 padding = 0;
            mRenderClient->GetCurrentPadding(&padding);
            UINT32 available = renderBufSize > padding ? renderBufSize - padding : 0;
            UINT32 toWrite   = (available < frames) ? available : frames;
            if (toWrite == 0) {
                // Render side full — wait one event tick, then retry once.
                WaitForSingleObject(mRenderEvent, 10);
                mRenderClient->GetCurrentPadding(&padding);
                available = renderBufSize > padding ? renderBufSize - padding : 0;
                toWrite   = (available < frames) ? available : frames;
                if (toWrite == 0) continue;
            }

            BYTE* dst = nullptr;
            if (SUCCEEDED(mRender->GetBuffer(toWrite, &dst)) && dst)
            {
                float* fdst = reinterpret_cast<float*>(dst);
                const int rc = mRenderChannels;
                if (rc == 2) {
                    for (UINT32 i = 0; i < toWrite; ++i) {
                        fdst[i*2 + 0] = outL[i];
                        fdst[i*2 + 1] = outR[i];
                    }
                } else if (rc == 1) {
                    for (UINT32 i = 0; i < toWrite; ++i)
                        fdst[i] = 0.5f * (outL[i] + outR[i]);
                } else {
                    // 5.1/7.1: fill L,R, zero the rest. Lazy but valid.
                    for (UINT32 i = 0; i < toWrite; ++i) {
                        for (int c = 0; c < rc; ++c)
                            fdst[i*rc + c] = (c == 0) ? outL[i] : (c == 1) ? outR[i] : 0.0f;
                    }
                }
                mRender->ReleaseBuffer(toWrite, 0);
            }
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

#endif // _WIN32

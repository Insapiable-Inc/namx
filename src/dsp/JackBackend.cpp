#include "JackBackend.h"

#include <sstream>

#include "Denormals.h"
#include "Log.h"

std::unique_ptr<IAudioBackend> IAudioBackend::create() {
    return std::make_unique<JackBackend>();
}

JackBackend::JackBackend() = default;

JackBackend::~JackBackend() {
    stop();
}

bool JackBackend::start(const std::string& clientName, Callback* cb) {
    if (mRunning.load()) {
        return true;
    }

    mCallback = cb;

    jack_status_t status;
    mClient = jack_client_open(clientName.c_str(), JackNullOption, &status);
    if (!mClient) {
        qCWarning(lcAudio, "[JackBackend] jack_client_open failed, status=0x%x",
                  static_cast<unsigned>(status));
        return false;
    }

    mSampleRate = static_cast<double>(jack_get_sample_rate(mClient));
    mBufferSize = static_cast<int>(jack_get_buffer_size(mClient));

    jack_set_process_callback(mClient, jackProcessCallback, this);
    jack_set_buffer_size_callback(mClient, jackBufferSizeCallback, this);
    jack_on_shutdown(mClient, jackShutdownCallback, this);
    jack_set_xrun_callback(mClient, jackXrunCallback, this);
    mXruns.store(0);

    mPortIn = jack_port_register(mClient, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    mPortOutL =
        jack_port_register(mClient, "output_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    mPortOutR =
        jack_port_register(mClient, "output_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    if (!mPortIn || !mPortOutL || !mPortOutR) {
        qCWarning(lcAudio, "[JackBackend] Failed to register JACK ports");
        jack_client_close(mClient);
        mClient = nullptr;
        return false;
    }

    // Initial buffer-size notification so the callback resizes its work buffers
    // before any process callback fires.
    if (mCallback) {
        mCallback->onBufferSizeChanged(mBufferSize);
    }

    if (jack_activate(mClient) != 0) {
        qCWarning(lcAudio, "[JackBackend] jack_activate failed");
        jack_client_close(mClient);
        mClient = nullptr;
        return false;
    }

    mRunning.store(true);
    qCInfo(lcAudio, "[JackBackend] JACK started at %.0f Hz (stereo out)", mSampleRate);
    return true;
}

void JackBackend::stop() {
    if (!mRunning.load()) {
        return;
    }
    mRunning.store(false);
    if (mClient) {
        jack_deactivate(mClient);
        jack_client_close(mClient);
        mClient = nullptr;
    }
    mCallback = nullptr;
}

int JackBackend::jackXrunCallback(void* arg) {
    static_cast<JackBackend*>(arg)->mXruns.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

int JackBackend::ioLatencyFrames() const {
    if (!mClient || !mPortIn || !mPortOutL) {
        return -1;
    }
    // Capture latency of our input port: physical input → here; playback
    // latency of our output: here → physical output. Unconnected ports
    // report 0 — no meaningful round trip then.
    jack_latency_range_t in{}, out{};
    jack_port_get_latency_range(mPortIn, JackCaptureLatency, &in);
    jack_port_get_latency_range(mPortOutL, JackPlaybackLatency, &out);
    if (in.max == 0 && out.max == 0) {
        return -1;
    }
    return static_cast<int>(in.max + out.max);
}

std::string JackBackend::statusString() const {
    std::ostringstream o;
    o << "JACK  ●  " << static_cast<int>(mSampleRate) << " Hz  ●  stereo out";
    return o.str();
}

std::vector<IAudioBackend::DeviceInfo> JackBackend::enumerateInputDevices() const {
    return {{"default", "JACK Server"}};
}

std::vector<IAudioBackend::DeviceInfo> JackBackend::enumerateOutputDevices() const {
    return {{"default", "JACK Server"}};
}

// ---- Port connection persistence ------------------------------------

IAudioBackend::PortConnections JackBackend::currentConnections() const {
    PortConnections out;
    if (!mClient) {
        return out;
    }

    auto getConns = [](jack_port_t* p) {
        std::vector<std::string> result;
        if (!p) {
            return result;
        }
        const char** conns = jack_port_get_connections(p);
        if (!conns) {
            return result;
        }
        for (int i = 0; conns[i]; ++i) {
            result.emplace_back(conns[i]);
        }
        jack_free(conns);
        return result;
    };

    out.inputSources = getConns(mPortIn);
    out.outputLDests = getConns(mPortOutL);
    out.outputRDests = getConns(mPortOutR);
    return out;
}

void JackBackend::applyConnections(const PortConnections& c) {
    if (!mClient) {
        return;
    }

    auto disconnectAll = [this](jack_port_t* p) {
        if (!p) {
            return;
        }
        const char** conns = jack_port_get_connections(p);
        if (!conns) {
            return;
        }
        const char* myName = jack_port_name(p);
        const bool isOut = (jack_port_flags(p) & JackPortIsOutput) != 0;
        for (int i = 0; conns[i]; ++i) {
            if (isOut) {
                jack_disconnect(mClient, myName, conns[i]);
            } else {
                jack_disconnect(mClient, conns[i], myName);
            }
        }
        jack_free(conns);
    };
    disconnectAll(mPortIn);
    disconnectAll(mPortOutL);
    disconnectAll(mPortOutR);

    const char* inName = jack_port_name(mPortIn);
    const char* outLName = jack_port_name(mPortOutL);
    const char* outRName = jack_port_name(mPortOutR);

    for (const auto& src : c.inputSources) {
        jack_connect(mClient, src.c_str(), inName);
    }
    for (const auto& dst : c.outputLDests) {
        jack_connect(mClient, outLName, dst.c_str());
    }
    for (const auto& dst : c.outputRDests) {
        jack_connect(mClient, outRName, dst.c_str());
    }
}

// ---- JACK callbacks --------------------------------------------------

int JackBackend::jackProcessCallback(jack_nframes_t nframes, void* arg) {
    return static_cast<JackBackend*>(arg)->processAudio(nframes);
}

int JackBackend::jackBufferSizeCallback(jack_nframes_t nframes, void* arg) {
    return static_cast<JackBackend*>(arg)->onBufferSize(nframes);
}

void JackBackend::jackShutdownCallback(void* arg) {
    static_cast<JackBackend*>(arg)->mRunning.store(false);
    qCWarning(lcAudio, "[JackBackend] JACK server shut down");
}

int JackBackend::onBufferSize(jack_nframes_t nframes) {
    mBufferSize = static_cast<int>(nframes);
    if (mCallback) {
        mCallback->onBufferSizeChanged(mBufferSize);
    }
    return 0;
}

int JackBackend::processAudio(jack_nframes_t nframes) {
    // JACK may run us on any of its threads: set it every block (a few dozen cycles).
    enable_flush_to_zero();

    auto* in = static_cast<float*>(jack_port_get_buffer(mPortIn, nframes));
    auto* outL = static_cast<float*>(jack_port_get_buffer(mPortOutL, nframes));
    auto* outR = static_cast<float*>(jack_port_get_buffer(mPortOutR, nframes));

    if (mCallback) {
        mCallback->onAudio(in, outL, outR, nframes);
    }
    return 0;
}

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

bool JackBackend::start(const std::string& client_name, Callback* cb) {
    if (running.load()) {
        return true;
    }

    callback = cb;

    jack_status_t status;
    client = jack_client_open(client_name.c_str(), JackNullOption, &status);
    if (!client) {
        qCWarning(lc_audio, "[JackBackend] jack_client_open failed, status=0x%x",
                  static_cast<unsigned>(status));
        return false;
    }

    sample_rate = static_cast<double>(jack_get_sample_rate(client));
    buffer_size = static_cast<int>(jack_get_buffer_size(client));

    jack_set_process_callback(client, jack_process_callback, this);
    jack_set_buffer_size_callback(client, jack_buffer_size_callback, this);
    jack_on_shutdown(client, jack_shutdown_callback, this);
    jack_set_xrun_callback(client, jack_xrun_callback, this);
    xruns.store(0);

    port_in = jack_port_register(client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    port_out_l =
        jack_port_register(client, "output_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    port_out_r =
        jack_port_register(client, "output_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    if (!port_in || !port_out_l || !port_out_r) {
        qCWarning(lc_audio, "[JackBackend] Failed to register JACK ports");
        jack_client_close(client);
        client = nullptr;
        return false;
    }

    // Initial buffer-size notification so the callback resizes its work buffers
    // before any process callback fires.
    if (callback) {
        callback->on_buffer_size_changed(buffer_size);
    }

    if (jack_activate(client) != 0) {
        qCWarning(lc_audio, "[JackBackend] jack_activate failed");
        jack_client_close(client);
        client = nullptr;
        return false;
    }

    running.store(true);
    qCInfo(lc_audio, "[JackBackend] JACK started at %.0f Hz (stereo out)", sample_rate);
    return true;
}

void JackBackend::stop() {
    if (!running.load()) {
        return;
    }
    running.store(false);
    if (client) {
        jack_deactivate(client);
        jack_client_close(client);
        client = nullptr;
    }
    callback = nullptr;
}

int JackBackend::jack_xrun_callback(void* arg) {
    static_cast<JackBackend*>(arg)->xruns.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

int JackBackend::io_latency_frames() const {
    if (!client || !port_in || !port_out_l) {
        return -1;
    }
    // Capture latency of our input port: physical input → here; playback
    // latency of our output: here → physical output. Unconnected ports
    // report 0 — no meaningful round trip then.
    jack_latency_range_t in{}, out{};
    jack_port_get_latency_range(port_in, JackCaptureLatency, &in);
    jack_port_get_latency_range(port_out_l, JackPlaybackLatency, &out);
    if (in.max == 0 && out.max == 0) {
        return -1;
    }
    return static_cast<int>(in.max + out.max);
}

std::string JackBackend::status_string() const {
    std::ostringstream o;
    o << "JACK  ●  " << static_cast<int>(sample_rate) << " Hz  ●  stereo out";
    return o.str();
}

std::vector<IAudioBackend::DeviceInfo> JackBackend::enumerate_input_devices() const {
    return {{"default", "JACK Server"}};
}

std::vector<IAudioBackend::DeviceInfo> JackBackend::enumerate_output_devices() const {
    return {{"default", "JACK Server"}};
}

// ---- Port connection persistence ------------------------------------

IAudioBackend::PortConnections JackBackend::current_connections() const {
    PortConnections out;
    if (!client) {
        return out;
    }

    auto get_conns = [](jack_port_t* p) {
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

    out.input_sources = get_conns(port_in);
    out.output_l_dests = get_conns(port_out_l);
    out.output_r_dests = get_conns(port_out_r);
    return out;
}

void JackBackend::apply_connections(const PortConnections& c) {
    if (!client) {
        return;
    }

    auto disconnect_all = [this](jack_port_t* p) {
        if (!p) {
            return;
        }
        const char** conns = jack_port_get_connections(p);
        if (!conns) {
            return;
        }
        const char* my_name = jack_port_name(p);
        const bool is_out = (jack_port_flags(p) & JackPortIsOutput) != 0;
        for (int i = 0; conns[i]; ++i) {
            if (is_out) {
                jack_disconnect(client, my_name, conns[i]);
            } else {
                jack_disconnect(client, conns[i], my_name);
            }
        }
        jack_free(conns);
    };
    disconnect_all(port_in);
    disconnect_all(port_out_l);
    disconnect_all(port_out_r);

    const char* in_name = jack_port_name(port_in);
    const char* out_l_name = jack_port_name(port_out_l);
    const char* out_r_name = jack_port_name(port_out_r);

    for (const auto& src : c.input_sources) {
        jack_connect(client, src.c_str(), in_name);
    }
    for (const auto& dst : c.output_l_dests) {
        jack_connect(client, out_l_name, dst.c_str());
    }
    for (const auto& dst : c.output_r_dests) {
        jack_connect(client, out_r_name, dst.c_str());
    }
}

// ---- JACK callbacks --------------------------------------------------

int JackBackend::jack_process_callback(jack_nframes_t nframes, void* arg) {
    return static_cast<JackBackend*>(arg)->process_audio(nframes);
}

int JackBackend::jack_buffer_size_callback(jack_nframes_t nframes, void* arg) {
    return static_cast<JackBackend*>(arg)->on_buffer_size(nframes);
}

void JackBackend::jack_shutdown_callback(void* arg) {
    static_cast<JackBackend*>(arg)->running.store(false);
    qCWarning(lc_audio, "[JackBackend] JACK server shut down");
}

int JackBackend::on_buffer_size(jack_nframes_t nframes) {
    buffer_size = static_cast<int>(nframes);
    if (callback) {
        callback->on_buffer_size_changed(buffer_size);
    }
    return 0;
}

int JackBackend::process_audio(jack_nframes_t nframes) {
    // JACK may run us on any of its threads: set it every block (a few dozen cycles).
    enable_flush_to_zero();

    auto* in = static_cast<float*>(jack_port_get_buffer(port_in, nframes));
    auto* out_l = static_cast<float*>(jack_port_get_buffer(port_out_l, nframes));
    auto* out_r = static_cast<float*>(jack_port_get_buffer(port_out_r, nframes));

    if (callback) {
        callback->on_audio(in, out_l, out_r, nframes);
    }
    return 0;
}

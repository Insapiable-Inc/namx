#include "AsioBackend.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "Denormals.h"
#include "Log.h"

// Steinberg's ASIO SDK (external/asiosdk, GPLv3). Its driver interface is a COM one, and
// the build's WIN32_LEAN_AND_MEAN leaves <objbase.h> out of <windows.h>.
// The SDK's headers don't include what they need: this order is theirs.
// clang-format off
#include <windows.h>
#include <objbase.h>
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"
// clang-format on

// The SDK's host helpers (asiodrivers.cpp): the driver list, and loading one by name.
extern AsioDrivers* asioDrivers;
bool loadAsioDriver(char* name);

namespace namx
{

namespace
{

constexpr const char* ID_PREFIX = "asio:";
constexpr long MAX_DRIVERS = 32;
constexpr size_t DRIVER_NAME_SIZE = 32; // the SDK's driver and channel names

AsioDrivers& driver_list() {
    if (!asioDrivers) {
        asioDrivers = new AsioDrivers(); // the SDK's own global, as loadAsioDriver makes it
    }
    return *asioDrivers;
}

vector<string> installed_drivers() {
    char storage[MAX_DRIVERS][DRIVER_NAME_SIZE] = {};
    char* names[MAX_DRIVERS];
    for (long i = 0; i < MAX_DRIVERS; ++i) {
        names[i] = storage[i];
    }
    const long count = driver_list().getDriverNames(names, MAX_DRIVERS);
    vector<string> out;
    for (long i = 0; i < count; ++i) {
        out.emplace_back(names[i]);
    }
    return out;
}

string make_id(const string& driver, int channel) {
    return ID_PREFIX + driver + "#" + std::to_string(channel);
}

// "asio:<driver>#<channel>" → driver and channel; anything else → {"", 0}.
std::pair<string, int> parse_id(const string& id) {
    const size_t hash = id.rfind('#');
    if (id.rfind(ID_PREFIX, 0) != 0 || hash == string::npos || hash < std::strlen(ID_PREFIX)) {
        return {};
    }
    const string channel = id.substr(hash + 1);
    if (channel.empty() || channel.size() > 4 ||
        !std::all_of(channel.begin(), channel.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        return {};
    }
    return {id.substr(std::strlen(ID_PREFIX), hash - std::strlen(ID_PREFIX)), std::stoi(channel)};
}

AsioBackend::SampleType sample_type(ASIOSampleType t) {
    switch (t) {
        case ASIOSTInt16LSB:
            return AsioBackend::SampleType::INT16;
        case ASIOSTInt24LSB:
            return AsioBackend::SampleType::INT24;
        case ASIOSTInt32LSB:
            return AsioBackend::SampleType::INT32;
        case ASIOSTFloat32LSB:
            return AsioBackend::SampleType::FLOAT32;
        case ASIOSTFloat64LSB:
            return AsioBackend::SampleType::FLOAT64;
        case ASIOSTInt32LSB16:
            return AsioBackend::SampleType::INT32_16;
        case ASIOSTInt32LSB18:
            return AsioBackend::SampleType::INT32_18;
        case ASIOSTInt32LSB20:
            return AsioBackend::SampleType::INT32_20;
        case ASIOSTInt32LSB24:
            return AsioBackend::SampleType::INT32_24;
        default:
            return AsioBackend::SampleType::UNSUPPORTED; // big-endian, DSD
    }
}

// Full scale of a right-justified integer of `bits` bits.
constexpr float scale_of(int bits) {
    return static_cast<float>(1u << (bits - 1));
}

int32_t to_int(float v, int bits) {
    const float full = scale_of(bits);
    const float clamped = std::clamp(v, -1.0f, 1.0f - 1.0f / full);
    return static_cast<int32_t>(std::lrint(clamped * full));
}

void read_samples(const void* src, AsioBackend::SampleType t, float* dst, int n) {
    using T = AsioBackend::SampleType;
    const auto* bytes = static_cast<const uint8_t*>(src);
    for (int i = 0; i < n; ++i) {
        switch (t) {
            case T::INT16: {
                int16_t v;
                std::memcpy(&v, bytes + 2 * i, 2);
                dst[i] = static_cast<float>(v) / scale_of(16);
                break;
            }
            case T::INT24: {
                const uint8_t* p = bytes + 3 * i;
                const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(p[0]) << 8 |
                                                       static_cast<uint32_t>(p[1]) << 16 |
                                                       static_cast<uint32_t>(p[2]) << 24) >>
                                  8;
                dst[i] = static_cast<float>(v) / scale_of(24);
                break;
            }
            case T::FLOAT32:
                std::memcpy(&dst[i], bytes + 4 * i, 4);
                break;
            case T::FLOAT64: {
                double v;
                std::memcpy(&v, bytes + 8 * i, 8);
                dst[i] = static_cast<float>(v);
                break;
            }
            default: { // 32-bit containers: full width or right-justified
                int32_t v;
                std::memcpy(&v, bytes + 4 * i, 4);
                const int bits = t == T::INT32_16   ? 16
                                 : t == T::INT32_18 ? 18
                                 : t == T::INT32_20 ? 20
                                 : t == T::INT32_24 ? 24
                                                    : 32;
                dst[i] = static_cast<float>(static_cast<double>(v) / scale_of(bits));
                break;
            }
        }
    }
}

void write_samples(const float* src, AsioBackend::SampleType t, void* dst, int n) {
    using T = AsioBackend::SampleType;
    auto* bytes = static_cast<uint8_t*>(dst);
    for (int i = 0; i < n; ++i) {
        switch (t) {
            case T::INT16: {
                const auto v = static_cast<int16_t>(to_int(src[i], 16));
                std::memcpy(bytes + 2 * i, &v, 2);
                break;
            }
            case T::INT24: {
                const auto v = static_cast<uint32_t>(to_int(src[i], 24));
                uint8_t* p = bytes + 3 * i;
                p[0] = static_cast<uint8_t>(v);
                p[1] = static_cast<uint8_t>(v >> 8);
                p[2] = static_cast<uint8_t>(v >> 16);
                break;
            }
            case T::FLOAT32:
                std::memcpy(bytes + 4 * i, &src[i], 4);
                break;
            case T::FLOAT64: {
                const double v = src[i];
                std::memcpy(bytes + 8 * i, &v, 8);
                break;
            }
            default: {
                const int bits = t == T::INT32_16   ? 16
                                 : t == T::INT32_18 ? 18
                                 : t == T::INT32_20 ? 20
                                 : t == T::INT32_24 ? 24
                                                    : 32;
                const int32_t v = bits == 32 ? static_cast<int32_t>(std::lrint(
                                                   std::clamp(static_cast<double>(src[i]), -1.0,
                                                              1.0 - 1.0 / 2147483648.0) *
                                                   2147483648.0))
                                             : to_int(src[i], bits);
                std::memcpy(bytes + 4 * i, &v, 4);
                break;
            }
        }
    }
}

ASIOTime* buffer_switch_time_info(ASIOTime* /*params*/, long index, ASIOBool direct_process) {
    AsioBackend::buffer_switch(index, direct_process);
    return nullptr;
}

// The driver keeps a pointer to these for as long as its buffers exist.
ASIOCallbacks callbacks{&AsioBackend::buffer_switch, &AsioBackend::sample_rate_changed,
                        &AsioBackend::message, &buffer_switch_time_info};

} // namespace

atomic<AsioBackend*> AsioBackend::current{nullptr};

AsioBackend::AsioBackend() = default;

AsioBackend::~AsioBackend() {
    stop();
}

string AsioBackend::status_string() const {
    if (!running.load()) {
        return "ASIO  \xe2\x97\x8f  not started";
    }
    return "ASIO  \xe2\x97\x8f  " + driver_name + "  \xe2\x97\x8f  " +
           std::to_string(static_cast<int>(std::lround(sample_rate))) + " Hz";
}

// Devices

vector<IAudioBackend::DeviceInfo> AsioBackend::devices(bool input) const {
    vector<DeviceInfo> out;
    // The loaded driver's channels: running here, or in the engine's live backend.
    AsioBackend* live = current.load();
    const AsioBackend* source = loaded ? this : live;
    if (source) {
        const vector<Channel>& channels = input ? source->inputs : source->outputs;
        const size_t step = input ? 1 : 2;
        for (size_t c = 0; c < channels.size(); c += step) {
            string name = source->driver_name + " \xe2\x80\x94 " + channels[c].name;
            if (!input && c + 1 < channels.size()) {
                name += " / " + channels[c + 1].name;
            }
            out.push_back({make_id(source->driver_name, static_cast<int>(c)), name});
        }
    }
    for (const string& driver : installed_drivers()) {
        if (!source || driver != source->driver_name) {
            out.push_back({make_id(driver, 0), driver});
        }
    }
    return out;
}

vector<IAudioBackend::DeviceInfo> AsioBackend::enumerate_input_devices() const {
    return devices(true);
}

vector<IAudioBackend::DeviceInfo> AsioBackend::enumerate_output_devices() const {
    return devices(false);
}

// Driver loading

bool AsioBackend::load(const string& driver) {
    char name[DRIVER_NAME_SIZE] = {};
    driver.copy(name, sizeof(name) - 1);
    driver_list();
    if (!loadAsioDriver(name)) {
        qCWarning(lc_audio, "[AsioBackend] Couldn't load the ASIO driver '%s'", driver.c_str());
        return false;
    }
    ASIODriverInfo info{};
    info.asioVersion = 2;
    info.sysRef = GetDesktopWindow();
    if (ASIOInit(&info) != ASE_OK) {
        qCWarning(lc_audio, "[AsioBackend] '%s' didn't start: %s", driver.c_str(),
                  info.errorMessage);
        asioDrivers->removeCurrentDriver();
        return false;
    }
    loaded = true;
    driver_name = driver;

    long input_count = 0;
    long output_count = 0;
    ASIOGetChannels(&input_count, &output_count);
    inputs.clear();
    outputs.clear();
    for (long is_input = 1; is_input >= 0; --is_input) {
        const long count = is_input ? input_count : output_count;
        for (long c = 0; c < count; ++c) {
            ASIOChannelInfo ci{};
            ci.channel = c;
            ci.isInput = is_input;
            const bool named = ASIOGetChannelInfo(&ci) == ASE_OK && ci.name[0] != '\0';
            Channel channel{named ? string(ci.name)
                                  : (is_input ? "Input " : "Output ") + std::to_string(c + 1),
                            is_input != 0};
            (is_input ? inputs : outputs).push_back(channel);
        }
    }
    return true;
}

void AsioBackend::unload() {
    if (loaded) {
        ASIOExit(); // releases the driver (the SDK's removeCurrentDriver)
        loaded = false;
    }
}

// Start / stop

bool AsioBackend::start(const string& client_name, Callback* cb) {
    return start_with_devices(client_name, cb, {}, {});
}

bool AsioBackend::start_with_devices(const string& /*client_name*/, Callback* cb,
                                     const string& input_device_id,
                                     const string& output_device_id) {
    stop();
    if (current.load()) {
        qCWarning(lc_audio, "[AsioBackend] Another ASIO stream is running");
        return false;
    }
    const auto [in_driver, in_channel] = parse_id(input_device_id);
    const auto [out_driver, out_channel] = parse_id(output_device_id);
    string driver = !out_driver.empty() ? out_driver : in_driver;
    if (driver.empty()) {
        const vector<string> installed = installed_drivers();
        if (installed.empty()) {
            qCWarning(lc_audio, "[AsioBackend] No ASIO driver is installed");
            return false;
        }
        driver = installed.front();
    }
    if (!load(driver)) {
        return false;
    }
    if (inputs.empty() || outputs.empty()) {
        qCWarning(lc_audio, "[AsioBackend] '%s' has no input or no output", driver.c_str());
        unload();
        return false;
    }
    // An input on another driver (or a channel it lacks) falls back to the first.
    input_channel =
        in_driver == driver && in_channel < static_cast<int>(inputs.size()) ? in_channel : 0;
    output_channel =
        out_driver == driver && out_channel < static_cast<int>(outputs.size()) ? out_channel : 0;
    stereo_out = output_channel + 1 < static_cast<int>(outputs.size());

    long min_size = 0;
    long max_size = 0;
    long preferred = 0;
    long granularity = 0;
    if (ASIOGetBufferSize(&min_size, &max_size, &preferred, &granularity) != ASE_OK ||
        preferred <= 0) {
        qCWarning(lc_audio, "[AsioBackend] '%s' reports no buffer size", driver.c_str());
        unload();
        return false;
    }
    buffer_size = static_cast<int>(preferred);
    ASIOSampleRate rate = 0.0;
    if (ASIOGetSampleRate(&rate) != ASE_OK || rate <= 0.0) {
        ASIOSetSampleRate(48000.0); // a driver without a current rate: ask for 48 kHz
        if (ASIOGetSampleRate(&rate) != ASE_OK || rate <= 0.0) {
            qCWarning(lc_audio, "[AsioBackend] '%s' reports no sample rate", driver.c_str());
            unload();
            return false;
        }
    }
    sample_rate = rate;

    ASIOBufferInfo infos[3]{};
    infos[0] = {ASIOTrue, input_channel, {nullptr, nullptr}};
    infos[1] = {ASIOFalse, output_channel, {nullptr, nullptr}};
    infos[2] = {ASIOFalse, output_channel + 1, {nullptr, nullptr}};
    const long count = stereo_out ? 3 : 2;
    current.store(this); // the driver may call back while it creates its buffers
    if (ASIOCreateBuffers(infos, count, preferred, &callbacks) != ASE_OK) {
        qCWarning(lc_audio, "[AsioBackend] '%s' couldn't create its buffers", driver.c_str());
        current.store(nullptr);
        unload();
        return false;
    }
    ASIOChannelInfo in_info{};
    in_info.channel = input_channel;
    in_info.isInput = ASIOTrue;
    ASIOChannelInfo out_info{};
    out_info.channel = output_channel;
    out_info.isInput = ASIOFalse;
    ASIOGetChannelInfo(&in_info);
    ASIOGetChannelInfo(&out_info);
    input_type = sample_type(in_info.type);
    output_type = sample_type(out_info.type);
    if (input_type == SampleType::UNSUPPORTED || output_type == SampleType::UNSUPPORTED) {
        qCWarning(lc_audio, "[AsioBackend] '%s' uses a sample format NAM-X can't convert (%ld/%ld)",
                  driver.c_str(), static_cast<long>(in_info.type),
                  static_cast<long>(out_info.type));
        ASIODisposeBuffers();
        current.store(nullptr);
        unload();
        return false;
    }
    for (long b = 0; b < count; ++b) {
        buffers[b][0] = infos[b].buffers[0];
        buffers[b][1] = infos[b].buffers[1];
    }
    long input_latency = 0;
    long output_latency = 0;
    latency_frames = ASIOGetLatencies(&input_latency, &output_latency) == ASE_OK
                         ? static_cast<int>(input_latency + output_latency)
                         : -1;
    post_output = ASIOOutputReady() == ASE_OK;

    // Everything the callback touches, sized before the stream runs.
    in_buffer.assign(static_cast<size_t>(buffer_size), 0.0f);
    left_buffer.assign(static_cast<size_t>(buffer_size), 0.0f);
    right_buffer.assign(static_cast<size_t>(buffer_size), 0.0f);
    callback = cb;
    if (callback) {
        callback->on_buffer_size_changed(buffer_size);
    }
    running.store(true);
    if (ASIOStart() != ASE_OK) {
        qCWarning(lc_audio, "[AsioBackend] '%s' didn't start its stream", driver.c_str());
        running.store(false);
        ASIODisposeBuffers();
        current.store(nullptr);
        unload();
        return false;
    }
    return true;
}

void AsioBackend::stop() {
    if (!loaded) {
        return;
    }
    if (running.exchange(false)) {
        ASIOStop();
        ASIODisposeBuffers();
    }
    if (current.load() == this) {
        current.store(nullptr);
    }
    unload();
}

void AsioBackend::open_control_panel() {
    if (running.load()) {
        ASIOControlPanel();
    }
}

// The driver's thread

void AsioBackend::process(long index) {
    enable_flush_to_zero();
    const int n = buffer_size;
    const long half = index & 1;
    read_samples(buffers[0][half], input_type, in_buffer.data(), n);
    if (callback) {
        callback->on_audio(in_buffer.data(), left_buffer.data(), right_buffer.data(),
                           static_cast<size_t>(n));
    } else {
        std::fill(left_buffer.begin(), left_buffer.end(), 0.0f);
        std::fill(right_buffer.begin(), right_buffer.end(), 0.0f);
    }
    write_samples(left_buffer.data(), output_type, buffers[1][half], n);
    if (stereo_out) {
        write_samples(right_buffer.data(), output_type, buffers[2][half], n);
    }
    if (post_output) {
        ASIOOutputReady();
    }
}

void AsioBackend::buffer_switch(long index, long /*direct_process*/) {
    AsioBackend* self = current.load(std::memory_order_acquire);
    if (self && self->running.load(std::memory_order_acquire)) {
        self->process(index);
    }
}

void AsioBackend::sample_rate_changed(double /*rate*/) {
    if (AsioBackend* self = current.load()) {
        self->restart_requested.store(true);
    }
}

long AsioBackend::message(long selector, long value, void* /*message*/, double* /*opt*/) {
    AsioBackend* self = current.load();
    switch (selector) {
        case kAsioSelectorSupported:
            return value == kAsioResetRequest || value == kAsioEngineVersion ||
                           value == kAsioResyncRequest || value == kAsioLatenciesChanged ||
                           value == kAsioSupportsTimeInfo || value == kAsioOverload
                       ? 1L
                       : 0L;
        case kAsioEngineVersion:
            return 2L;
        case kAsioResetRequest: // its buffer size or rate changed in its own settings
            if (self) {
                self->restart_requested.store(true);
            }
            return 1L;
        case kAsioResyncRequest: // it lost sync: samples were dropped
        case kAsioOverload:
            if (self) {
                self->xruns.fetch_add(1, std::memory_order_relaxed);
            }
            return 1L;
        case kAsioLatenciesChanged:
            return 1L;
        case kAsioSupportsTimeInfo:
            return 1L;
        default:
            return 0L;
    }
}

} // namespace namx

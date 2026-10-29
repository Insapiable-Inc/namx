#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "Common.h"
#include "IAudioBackend.h"

namespace namx
{

// ASIO implementation of IAudioBackend (Windows-only), on Steinberg's ASIO SDK used under
// its GPLv3 license (external/asiosdk, VENDORED.md).
//
// One ASIO driver drives input and output together: the device IDs are
// "asio:<driver>#<channel>" — an input channel, or the first of an output pair — and the
// stream runs on the output's driver (an input on another driver falls back to that
// driver's first channel). Channels are known only for the loaded driver, so the device
// lists show its channels and one entry per other installed driver. The buffer size and
// often the sample rate are set in the driver's own window (open_control_panel); when
// they change there, the driver asks for a reset (take_restart_request).
//
// Threading: the driver is loaded, started, stopped and released on the GUI thread (its
// COM apartment); the driver's own thread calls back with each buffer half. The SDK keeps
// the loaded driver in globals and its callbacks carry no user pointer, so only one
// AsioBackend can run at a time (`current`).
class AsioBackend : public IAudioBackend
{
public:
    AsioBackend();
    ~AsioBackend() override;

    bool start(const string& client_name, Callback* cb) override;
    bool start_with_devices(const string& client_name, Callback* cb, const string& input_device_id,
                            const string& output_device_id) override;
    void stop() override;

    bool is_running() const override {
        return running.load();
    }
    double get_sample_rate() const override {
        return sample_rate;
    }
    int get_buffer_size() const override {
        return buffer_size;
    }
    string status_string() const override;

    // Drops the driver reports (kAsioResyncRequest, kAsioOverload).
    int xrun_count() const override {
        return xruns.load(std::memory_order_relaxed);
    }
    void reset_xruns() override {
        xruns.store(0, std::memory_order_relaxed);
    }
    int io_latency_frames() const override {
        return latency_frames;
    }

    vector<DeviceInfo> enumerate_input_devices() const override;
    vector<DeviceInfo> enumerate_output_devices() const override;

    bool has_control_panel() const override {
        return running.load();
    }
    void open_control_panel() override;
    bool take_restart_request() override {
        return restart_requested.exchange(false);
    }

    // The driver's callbacks (AsioBackend.cpp's ASIOCallbacks), on its thread: a buffer
    // half to fill, a new sample rate, a message.
    static void buffer_switch(long index, long direct_process);
    static void sample_rate_changed(double rate);
    static long message(long selector, long value, void* message, double* opt);

    // The ASIO sample formats this backend converts (little-endian only; Windows).
    enum class SampleType
    {
        UNSUPPORTED,
        INT16,
        INT24, // packed, 3 bytes
        INT32,
        FLOAT32,
        FLOAT64,
        INT32_16, // 32-bit containers holding 16 / 18 / 20 / 24 bits, right-justified
        INT32_18,
        INT32_20,
        INT32_24,
    };

private:
    struct Channel
    {
        string name;
        bool is_input = false;
    };

    bool load(const string& driver); // load + ASIOInit; on failure nothing stays loaded
    void unload();
    vector<DeviceInfo> devices(bool input) const;

    void process(long index);

    static atomic<AsioBackend*> current; // the running instance, for the callbacks

    Callback* callback = nullptr;
    atomic<bool> running{false};
    atomic<bool> restart_requested{false};
    atomic<int> xruns{0};
    bool loaded = false; // this instance holds the SDK's loaded driver
    string driver_name;
    vector<Channel> inputs, outputs; // the loaded driver's channels
    int input_channel = 0;
    int output_channel = 0; // the pair's first; the second is output_channel + 1, if any
    bool stereo_out = false;
    double sample_rate = 48000.0;
    int buffer_size = 0;
    int latency_frames = -1;
    bool post_output = false; // the driver wants ASIOOutputReady after each buffer
    SampleType input_type = SampleType::UNSUPPORTED;
    SampleType output_type = SampleType::UNSUPPORTED;
    void* buffers[3][2] = {}; // input, output L, output R × the two buffer halves
    vector<float> in_buffer, left_buffer, right_buffer;
};

} // namespace namx

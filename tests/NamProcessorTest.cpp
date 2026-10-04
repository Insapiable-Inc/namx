// Headless test of the model loader's boundaries (audit 2026-10-04 items 01 and 02):
// a model loaded before the backend has reported a block size, a model whose training
// rate sits far from the engine's, and a model that states an absurd rate. The first
// two used to drive the resampler's warm-up past its buffers; the third was accepted.
//
// The models are one-tap Linear models written to a QTemporaryDir this test creates.

#include "NamProcessor.h"

#include <cmath>
#include <cstdio>
#include <string>

#include <QFile>
#include <QTemporaryDir>

#include "Common.h"

namespace namx
{

static int failures = 0;
#define CHECK(cond, msg) \
    do { \
        if (cond) \
            std::printf("  ok   %s\n", msg); \
        else { \
            std::printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++failures; \
        } \
    } while (0)

// A valid model: unity gain, one tap, trained at `rate` (a string so the test can
// write rates nlohmann parses but no model would state).
static string write_model(const QTemporaryDir& dir, const char* name, const string& rate) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        const string json = "{\"version\":\"0.5.4\",\"architecture\":\"Linear\","
                            "\"config\":{\"receptive_field\":1,\"bias\":false},"
                            "\"weights\":[1.0],\"sample_rate\":" +
                            rate + "}";
        f.write(json.c_str(), static_cast<qint64>(json.size()));
    }
    return path.toStdString();
}

// Runs `blocks` blocks of a 0.5 step through the processor; true when every output
// sample is finite and the last block is not silent (the model passes audio through).
static bool runs(NamProcessor& nam, size_t frames, int blocks) {
    vector<float> l(frames), r(frames);
    bool finite = true;
    float last_peak = 0.0f;
    for (int b = 0; b < blocks; ++b) {
        std::fill(l.begin(), l.end(), 0.5f);
        std::fill(r.begin(), r.end(), 0.0f);
        nam.process(l.data(), r.data(), frames, false);
        last_peak = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            last_peak = std::max(last_peak, std::abs(l[i]));
        }
    }
    return finite && last_peak > 0.1f;
}

static int run() {
    QTemporaryDir tmp;
    const string model_48k = write_model(tmp, "48k.nam", "48000");
    const string model_8k = write_model(tmp, "8k.nam", "8000");
    const string model_10hz = write_model(tmp, "10hz.nam", "10");
    const string model_huge = write_model(tmp, "huge.nam", "1e300");

    std::printf("before audio\n");
    {
        // No sample rate, no block size: the engine's state while the backend is down.
        NamProcessor nam;
        nam.set_enabled(true);
        CHECK(nam.load_model(model_48k), "a 48 kHz model loads with no block size yet");
        CHECK(nam.is_model_loaded() && nam.get_model_path() == model_48k, "and is the model");
        nam.set_sample_rate(44100.0);
        nam.prepare_buffer(128);
        CHECK(nam.get_resampled_model_rate() == 48000.0, "resampled once audio is up");
        CHECK(runs(nam, 128, 40), "and processes blocks");
        nam.reclaim_retired(true);
    }

    std::printf("rates\n");
    {
        NamProcessor nam;
        nam.set_enabled(true);
        nam.set_sample_rate(44100.0);
        nam.prepare_buffer(16);
        CHECK(nam.load_model(model_8k), "an 8 kHz model loads into 16-frame blocks");
        CHECK(nam.get_resampled_model_rate() == 8000.0, "and is resampled");
        CHECK(nam.latency_ms() > 0.0, "with a reported latency");
        CHECK(runs(nam, 16, 400), "and processes blocks");

        nam.prepare_buffer(128);
        CHECK(!nam.load_model(model_10hz), "a 10 Hz model is rejected");
        CHECK(nam.get_last_error().find("sample rate") != string::npos, "with a reason");
        CHECK(nam.get_model_path() == model_8k, "and the loaded model stays");
        CHECK(!nam.load_model(model_huge), "a 1e300 Hz model is rejected");
        CHECK(nam.get_model_path() == model_8k && runs(nam, 128, 40),
              "and the loaded model still runs");
        nam.reclaim_retired(true);
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

} // namespace namx

int main() {
    return namx::run();
}

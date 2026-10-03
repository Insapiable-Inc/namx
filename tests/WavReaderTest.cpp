// Headless test of the IR WAV reader (code review items 01, 10, 11): every
// supported encoding decodes to the right values; a stray byte after the last sample
// is ignored, not read; and damaged files — a chunk that runs past the end of the
// file, stereo, NaN — are errors instead of out-of-bounds reads or writes.
//
// Every file is built byte by byte below (no random input); the one file written to
// disk goes into a QTemporaryDir this test creates.

#include "WavReader.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

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

using Bytes = vector<std::byte>;

static void put(Bytes& b, std::uint64_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        b.push_back(static_cast<std::byte>(v >> (8 * i) & 0xFF));
    }
}

static void put_tag(Bytes& b, const char* tag) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<std::byte>(tag[i]));
    }
}

static void chunk(Bytes& b, const char* tag, const Bytes& body, std::uint32_t declared) {
    put_tag(b, tag);
    put(b, declared, 4);
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() % 2 == 1) {
        b.push_back(std::byte{0}); // pad to a word boundary
    }
}

struct Spec
{
    std::uint16_t format = 1; // 1 PCM, 3 float, 0xFFFE extensible
    std::uint16_t sub_format = 1; // for extensible
    std::uint16_t channels = 1;
    std::uint32_t rate = 48000;
    std::uint16_t bits = 16;
};

// RIFF/WAVE with a fmt chunk, optional extra chunk, then `data`; `declared` lets a
// test lie about the data length.
static Bytes make_wav(const Spec& s, const Bytes& data, std::int64_t declared = -1,
                      const Bytes* extra = nullptr) {
    Bytes fmt;
    const std::uint16_t align = static_cast<std::uint16_t>(s.channels * s.bits / 8);
    put(fmt, s.format, 2);
    put(fmt, s.channels, 2);
    put(fmt, s.rate, 4);
    put(fmt, std::uint64_t{s.rate} * align, 4);
    put(fmt, align, 2);
    put(fmt, s.bits, 2);
    if (s.format == 0xFFFE) {
        put(fmt, 22, 2); // cbSize
        put(fmt, s.bits, 2); // valid bits
        put(fmt, 4, 4); // channel mask
        put(fmt, s.sub_format, 2); // GUID: format code first…
        for (int i = 0; i < 14; ++i) {
            fmt.push_back(std::byte{0}); // …then the fixed GUID tail (not checked)
        }
    }
    Bytes body;
    put_tag(body, "WAVE");
    chunk(body, "fmt ", fmt, static_cast<std::uint32_t>(fmt.size()));
    if (extra) {
        chunk(body, "LIST", *extra, static_cast<std::uint32_t>(extra->size()));
    }
    chunk(body, "data", data,
          static_cast<std::uint32_t>(declared >= 0 ? declared
                                                   : static_cast<std::int64_t>(data.size())));
    Bytes out;
    put_tag(out, "RIFF");
    put(out, body.size(), 4);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static bool near(float a, float b) {
    return std::abs(a - b) < 1e-6f;
}

static bool is_error(const expected<wav::MonoAudio, string>& r, const char* contains) {
    return !r && r.error().find(contains) != string::npos;
}

int run() {
    std::printf("encodings\n");
    {
        Bytes d;
        put(d, 16384, 2); // +0.5
        put(d, 0x8000, 2); // -32768 → -1
        const auto r = wav::parse_mono(make_wav({}, d));
        CHECK(r && r->samples.size() == 2 && near(r->samples[0], 0.5f) &&
                  near(r->samples[1], -1.0f) && r->sample_rate == 48000.0,
              "PCM 16-bit: +0.5, -1, 48 kHz");
    }
    {
        Bytes d;
        put(d, 0x400000, 3); // +0.5
        put(d, 0x800000, 3); // -1
        const auto r = wav::parse_mono(make_wav({.bits = 24}, d));
        CHECK(r && r->samples.size() == 2 && near(r->samples[0], 0.5f) &&
                  near(r->samples[1], -1.0f),
              "PCM 24-bit: +0.5, -1 (sign extension)");
    }
    {
        Bytes d;
        put(d, 1073741824, 4); // +0.5
        put(d, 0x80000000u, 4); // INT32_MIN → -1
        const auto r = wav::parse_mono(make_wav({.bits = 32}, d));
        CHECK(r && near(r->samples[0], 0.5f) && near(r->samples[1], -1.0f),
              "PCM 32-bit keeps its polarity: 2^30 → +0.5 (review item 11)");
    }
    {
        Bytes d;
        put(d, std::bit_cast<std::uint32_t>(0.25f), 4);
        const auto r = wav::parse_mono(make_wav({.format = 3, .bits = 32}, d));
        CHECK(r && near(r->samples[0], 0.25f), "float 32-bit");
    }
    {
        Bytes d;
        put(d, std::bit_cast<std::uint64_t>(-0.125), 8);
        const auto r = wav::parse_mono(make_wav({.format = 3, .bits = 64}, d));
        CHECK(r && near(r->samples[0], -0.125f), "float 64-bit");
    }
    {
        Bytes d;
        put(d, 16384, 2);
        const auto r = wav::parse_mono(make_wav({.format = 0xFFFE, .sub_format = 1}, d));
        CHECK(r && near(r->samples[0], 0.5f), "WAVE_FORMAT_EXTENSIBLE (PCM sub-format)");
    }
    {
        Bytes d;
        put(d, 16384, 2);
        const Bytes odd{std::byte{'x'}, std::byte{'y'}, std::byte{'z'}}; // padded chunk
        const auto r = wav::parse_mono(make_wav({}, d, -1, &odd));
        CHECK(r && r->samples.size() == 1 && near(r->samples[0], 0.5f),
              "an odd-sized chunk before the audio is skipped with its pad byte");
    }

    std::printf("damaged files\n");
    {
        Bytes d;
        put(d, 16384, 2);
        d.push_back(std::byte{0x7F}); // 1.5 samples of 16-bit audio
        const auto r = wav::parse_mono(make_wav({}, d));
        CHECK(r && r->samples.size() == 1 && near(r->samples[0], 0.5f),
              "3-byte data chunk at 16 bits: the stray byte is ignored, never read "
              "(review item 01)");
        Bytes one(1, std::byte{0x7F});
        CHECK(is_error(wav::parse_mono(make_wav({}, one)), "no samples"),
              "less than one sample: an error");
    }
    {
        Bytes d;
        put(d, 16384, 2);
        CHECK(is_error(wav::parse_mono(make_wav({}, d, 4)), "truncated"),
              "data declares 4 bytes but holds 2: truncation error (review item 10)");
    }
    {
        Bytes d;
        put(d, 1, 4);
        CHECK(is_error(wav::parse_mono(make_wav({.channels = 2}, d)), "not mono"),
              "stereo: refused with the mono message");
    }
    {
        Bytes d;
        put(d, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()), 4);
        CHECK(is_error(wav::parse_mono(make_wav({.format = 3, .bits = 32}, d)), "invalid"),
              "a NaN sample is refused");
    }
    {
        Bytes d;
        put(d, 1, 2);
        CHECK(is_error(wav::parse_mono(make_wav({.bits = 8}, d)), "sample size"),
              "8-bit PCM: unsupported sample size");
    }
    {
        Bytes junk;
        put_tag(junk, "RIFX");
        CHECK(is_error(wav::parse_mono(junk), "Not a WAV"), "not RIFF/WAVE");
        CHECK(is_error(wav::parse_mono({}), "Not a WAV"), "empty input");
    }
    {
        Bytes header = make_wav({}, {});
        header.resize(header.size() - 8); // drop the (empty) data chunk
        CHECK(is_error(wav::parse_mono(header), "no audio"), "no data chunk");
    }

    std::printf("file\n");
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath("ir.wav");
        Bytes d;
        put(d, 16384, 2);
        const Bytes file = make_wav({.rate = 44100}, d);
        QFile f(path);
        const bool written =
            f.open(QIODevice::WriteOnly) &&
            f.write(reinterpret_cast<const char*>(file.data()), static_cast<qint64>(file.size())) ==
                static_cast<qint64>(file.size());
        f.close();
        const auto r = wav::read_mono(path.toStdString());
        CHECK(written && r && r->sample_rate == 44100.0 && near(r->samples[0], 0.5f),
              "read_mono reads a file from disk");
        CHECK(!wav::read_mono(tmp.filePath("missing.wav").toStdString()),
              "a missing file is an error");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

} // namespace namx

int main() {
    return namx::run();
}

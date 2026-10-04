// Headless test of the library scan's file descriptions (audit 2026-10-04 item 02): a
// 4-bit (ADPCM) WAV, whose frame size rounds to zero bytes, used to divide by zero on
// the scan thread and take the app down; it only had to sit in a library folder.
//
// The files are built byte by byte below and written to a QTemporaryDir this test
// creates.

#include "LibraryIndex.h"

#include <cstdint>
#include <cstdio>

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

static void put(QByteArray& b, std::uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        b.push_back(static_cast<char>(v >> (8 * i) & 0xFF));
    }
}

// RIFF/WAVE with one fmt chunk and `data_bytes` of data.
static QString write_wav(const QTemporaryDir& dir, const char* name, std::uint16_t format,
                         std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                         std::uint32_t data_bytes) {
    QByteArray b;
    b.append("RIFF");
    put(b, 4 + 24 + 8 + data_bytes, 4);
    b.append("WAVE");
    b.append("fmt ");
    put(b, 16, 4);
    put(b, format, 2);
    put(b, channels, 2);
    put(b, rate, 4);
    const std::uint32_t frame_bytes = static_cast<std::uint32_t>(channels) * bits / 8;
    put(b, rate * frame_bytes, 4);
    put(b, frame_bytes, 2);
    put(b, bits, 2);
    b.append("data");
    put(b, data_bytes, 4);
    b.append(QByteArray(static_cast<qsizetype>(data_bytes), '\0'));
    const QString path = dir.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(b);
    }
    return path;
}

static int run() {
    QTemporaryDir tmp;

    std::printf("IR descriptions\n");
    {
        const QString pcm = write_wav(tmp, "pcm.wav", 1, 1, 48000, 16, 96000);
        const LibraryEntry e = LibraryIndex::describe(pcm);
        CHECK(e.summary.contains("mono") && e.summary.contains("1000 ms"),
              "a 16-bit mono second reads as mono, 1000 ms");

        const QString adpcm = write_wav(tmp, "adpcm.wav", 0x11, 1, 48000, 4, 4096);
        const LibraryEntry a = LibraryIndex::describe(adpcm);
        CHECK(a.summary.contains("mono") && !a.summary.contains("ms"),
              "a 4-bit file gets no length line (and no division by zero)");

        const QString odd = write_wav(tmp, "odd.wav", 1, 0xFFFF, 48000, 0xFFFF, 4096);
        const LibraryEntry o = LibraryIndex::describe(odd);
        CHECK(o.summary.contains("48"), "a damaged header is described without a length");
    }

    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}

} // namespace namx

int main() {
    return namx::run();
}

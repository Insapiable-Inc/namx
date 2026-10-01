#pragma once

// Qt logging category for the audio/DSP layer — the one permitted Qt
// dependency in dsp/ .cpp files (headers stay Qt-free). std::cerr is
// invisible on Windows GUI builds (WIN32_EXECUTABLE); qCWarning/qCInfo go
// to stderr on Linux and OutputDebugString on Windows, and can be
// controlled at runtime, e.g. QT_LOGGING_RULES="nam.audio.info=false".
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lc_audio)

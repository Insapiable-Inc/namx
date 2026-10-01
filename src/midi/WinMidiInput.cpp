// WinMM MIDI input (Windows). Compiled instead of AlsaMidiInput.cpp.
//
// WinMM has no hot-plug notification, so the device list is polled every
// two seconds; a device that appears is opened if enabled.

#include <QMetaObject>
#include <QTimer>

#include "Log.h"
#include "MidiInput.h"

// clang-format off
// Win32 headers are order-sensitive: <windows.h> must come before <mmsystem.h>.
#include <windows.h>
#include <mmsystem.h>
// clang-format on

#include <map>

#include "Common.h"

namespace npl
{

class WinMidiInput final : public MidiInput
{
public:
    explicit WinMidiInput(QObject* parent)
        : MidiInput(parent) {
    }
    ~WinMidiInput() override {
        for (auto& [name, h] : open) {
            close_device(h);
        }
    }

    bool start() override {
        if (started) {
            return true;
        }
        started = true;
        rescan();
        sync_connections();
        auto* poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [this] {
            const auto before = present;
            rescan();
            if (before != present) {
                sync_connections();
                emit devices_changed();
            }
        });
        poll->start(2000);
        return true;
    }

    QString backend_name() const override {
        return "WinMM";
    }

    QStringList devices() const override {
        QStringList out;
        for (const auto& [name, id] : present) {
            out << name;
        }
        return out;
    }

protected:
    void sync_connections() override {
        for (auto it = open.begin(); it != open.end();) {
            if (!present.count(it->first) || !is_enabled(it->first)) {
                close_device(it->second);
                it = open.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& [name, id] : present) {
            if (!is_enabled(name) || open.count(name)) {
                continue;
            }
            HMIDIIN h = nullptr;
            if (midiInOpen(&h, id, reinterpret_cast<DWORD_PTR>(&WinMidiInput::callback),
                           reinterpret_cast<DWORD_PTR>(this),
                           CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
                midiInStart(h);
                open[name] = h;
            } else {
                qCWarning(lc_audio, "[MIDI] cannot open %s", qPrintable(name));
            }
        }
    }

private:
    static void CALLBACK callback(HMIDIIN, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR) {
        if (msg != MIM_DATA) {
            return;
        }
        auto* self = reinterpret_cast<WinMidiInput*>(inst);
        const MidiMessage m = MidiMessage::from_bytes(
            static_cast<unsigned char>(p1 & 0xFF), static_cast<unsigned char>((p1 >> 8) & 0xFF),
            static_cast<unsigned char>((p1 >> 16) & 0xFF));
        if (m.type != MidiMessage::NONE) {
            QMetaObject::invokeMethod(
                self,
                [self, m] {
                    emit self->message_received(m);
                },
                Qt::QueuedConnection);
        }
    }

    static void close_device(HMIDIIN h) {
        midiInStop(h);
        midiInReset(h);
        midiInClose(h);
    }

    void rescan() {
        std::map<QString, UINT> found;
        const UINT n = midiInGetNumDevs();
        for (UINT i = 0; i < n; ++i) {
            MIDIINCAPSW caps{};
            if (midiInGetDevCapsW(i, &caps, sizeof caps) != MMSYSERR_NOERROR) {
                continue;
            }
            QString name = QString::fromWCharArray(caps.szPname);
            // Identical names (two of the same interface): number them.
            for (int k = 2; found.count(name); ++k) {
                name = QString::fromWCharArray(caps.szPname) + QString(" (%1)").arg(k);
            }
            found[name] = i;
        }
        // A device id can shift when another one is unplugged: reopen those.
        for (auto it = open.begin(); it != open.end();) {
            const auto f = found.find(it->first);
            const auto p = present.find(it->first);
            if (f != found.end() && p != present.end() && f->second != p->second) {
                close_device(it->second);
                it = open.erase(it);
            } else {
                ++it;
            }
        }
        present = std::move(found);
    }

    bool started = false;
    std::map<QString, UINT> present;
    std::map<QString, HMIDIIN> open;
};

MidiInput* MidiInput::create(QObject* parent) {
    return new WinMidiInput(parent);
}

} // namespace npl

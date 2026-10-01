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

namespace
{

class WinMidiInput final : public MidiInput
{
public:
    explicit WinMidiInput(QObject* parent)
        : MidiInput(parent) {
    }
    ~WinMidiInput() override {
        for (auto& [name, h] : mOpen) {
            closeDevice(h);
        }
    }

    bool start() override {
        if (mStarted) {
            return true;
        }
        mStarted = true;
        rescan();
        syncConnections();
        auto* poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [this] {
            const auto before = mPresent;
            rescan();
            if (before != mPresent) {
                syncConnections();
                emit devicesChanged();
            }
        });
        poll->start(2000);
        return true;
    }

    QString backendName() const override {
        return "WinMM";
    }

    QStringList devices() const override {
        QStringList out;
        for (const auto& [name, id] : mPresent) {
            out << name;
        }
        return out;
    }

protected:
    void syncConnections() override {
        for (auto it = mOpen.begin(); it != mOpen.end();) {
            if (!mPresent.count(it->first) || !isEnabled(it->first)) {
                closeDevice(it->second);
                it = mOpen.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& [name, id] : mPresent) {
            if (!isEnabled(name) || mOpen.count(name)) {
                continue;
            }
            HMIDIIN h = nullptr;
            if (midiInOpen(&h, id, reinterpret_cast<DWORD_PTR>(&WinMidiInput::callback),
                           reinterpret_cast<DWORD_PTR>(this),
                           CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
                midiInStart(h);
                mOpen[name] = h;
            } else {
                qCWarning(lcAudio, "[MIDI] cannot open %s", qPrintable(name));
            }
        }
    }

private:
    static void CALLBACK callback(HMIDIIN, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR) {
        if (msg != MIM_DATA) {
            return;
        }
        auto* self = reinterpret_cast<WinMidiInput*>(inst);
        const MidiMessage m = MidiMessage::fromBytes(static_cast<unsigned char>(p1 & 0xFF),
                                                     static_cast<unsigned char>((p1 >> 8) & 0xFF),
                                                     static_cast<unsigned char>((p1 >> 16) & 0xFF));
        if (m.type != MidiMessage::None) {
            QMetaObject::invokeMethod(
                self,
                [self, m] {
                    emit self->messageReceived(m);
                },
                Qt::QueuedConnection);
        }
    }

    static void closeDevice(HMIDIIN h) {
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
        for (auto it = mOpen.begin(); it != mOpen.end();) {
            const auto f = found.find(it->first);
            const auto p = mPresent.find(it->first);
            if (f != found.end() && p != mPresent.end() && f->second != p->second) {
                closeDevice(it->second);
                it = mOpen.erase(it);
            } else {
                ++it;
            }
        }
        mPresent = std::move(found);
    }

    bool mStarted = false;
    std::map<QString, UINT> mPresent;
    std::map<QString, HMIDIIN> mOpen;
};

} // namespace

std::unique_ptr<MidiInput> MidiInput::create(QObject* parent) {
    return std::make_unique<WinMidiInput>(parent);
}

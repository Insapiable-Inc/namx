// ALSA sequencer MIDI input (Linux). Compiled instead of WinMidiInput.cpp.

#include "MidiInput.h"

#include "Log.h"

#include <QMetaObject>
#include <QStringList>

#include <alsa/asoundlib.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace {

class AlsaMidiInput final : public MidiInput
{
public:
    explicit AlsaMidiInput(QObject* parent) : MidiInput(parent) {}
    ~AlsaMidiInput() override { shutdown(); }

    bool start() override
    {
        if (mSeq) return true;
        if (snd_seq_open(&mSeq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0)
        {
            qCWarning(lcAudio, "[MIDI] ALSA sequencer unavailable — MIDI disabled");
            mSeq = nullptr;
            return false;
        }
        snd_seq_set_client_name(mSeq, "NAM Player");
        mClient = snd_seq_client_id(mSeq);
        mPort = snd_seq_create_simple_port(mSeq, "MIDI In",
                                           SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                           SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (mPort < 0)
        {
            qCWarning(lcAudio, "[MIDI] cannot create the input port");
            snd_seq_close(mSeq);
            mSeq = nullptr;
            return false;
        }
        // Hot-plug: the system announce port reports clients/ports coming and going.
        snd_seq_connect_from(mSeq, mPort, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

        rescan();
        syncConnections();

        mWake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        mRunning = true;
        mThread = std::thread([this] { run(); });
        return true;
    }

    QString backendName() const override { return "ALSA"; }

    QStringList devices() const override
    {
        std::lock_guard<std::mutex> lock(mMutex);
        QStringList out;
        for (const auto& [name, addr] : mPresent) out << name;
        return out; // std::map: already sorted
    }

protected:
    void syncConnections() override
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mSeq) return;
        // Disconnect what is gone or disabled, connect what is enabled.
        for (auto it = mConnected.begin(); it != mConnected.end();)
        {
            const auto p = mPresent.find(it->first);
            if (p == mPresent.end() || !isEnabled(it->first) || p->second != it->second)
            {
                snd_seq_disconnect_from(mSeq, mPort, it->second.client, it->second.port);
                it = mConnected.erase(it);
            }
            else
                ++it;
        }
        for (const auto& [name, addr] : mPresent)
            if (isEnabled(name) && !mConnected.count(name)
                && snd_seq_connect_from(mSeq, mPort, addr.client, addr.port) == 0)
                mConnected[name] = addr;
    }

private:
    struct Addr
    {
        int client = 0, port = 0;
        bool operator!=(const Addr& o) const { return client != o.client || port != o.port; }
    };

    void shutdown()
    {
        if (!mSeq) return;
        mRunning = false;
        const uint64_t one = 1;
        if (mWake >= 0) (void)!write(mWake, &one, sizeof one);
        if (mThread.joinable()) mThread.join();
        if (mWake >= 0) close(mWake);
        snd_seq_close(mSeq);
        mSeq = nullptr;
    }

    // Readable, subscribable ports of other clients (GUI thread).
    void rescan()
    {
        std::map<QString, Addr> found;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            snd_seq_client_info_t* cinfo;
            snd_seq_port_info_t*   pinfo;
            snd_seq_client_info_alloca(&cinfo);
            snd_seq_port_info_alloca(&pinfo);
            snd_seq_client_info_set_client(cinfo, -1);
            while (snd_seq_query_next_client(mSeq, cinfo) >= 0)
            {
                const int client = snd_seq_client_info_get_client(cinfo);
                if (client == SND_SEQ_CLIENT_SYSTEM || client == mClient) continue;
                const QString clientName = QString::fromUtf8(snd_seq_client_info_get_name(cinfo));
                snd_seq_port_info_set_client(pinfo, client);
                snd_seq_port_info_set_port(pinfo, -1);
                while (snd_seq_query_next_port(mSeq, pinfo) >= 0)
                {
                    const unsigned caps = snd_seq_port_info_get_capability(pinfo);
                    const unsigned need = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
                    if ((caps & need) != need || (caps & SND_SEQ_PORT_CAP_NO_EXPORT)) continue;
                    const QString portName = QString::fromUtf8(snd_seq_port_info_get_name(pinfo));
                    // "Device: Port" unless the port just repeats the client name.
                    const QString name = portName.startsWith(clientName) ? portName
                                                                         : clientName + ": " + portName;
                    found[name] = {client, snd_seq_port_info_get_port(pinfo)};
                }
            }
            mPresent = std::move(found);
        }
    }

    void run()
    {
        std::vector<pollfd> fds;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            const int n = snd_seq_poll_descriptors_count(mSeq, POLLIN);
            fds.resize(n + 1);
            snd_seq_poll_descriptors(mSeq, fds.data(), n, POLLIN);
        }
        fds.back() = {mWake, POLLIN, 0};

        while (mRunning)
        {
            if (poll(fds.data(), fds.size(), -1) < 0) continue;
            if (!mRunning) break;
            bool topology = false;
            for (;;)
            {
                snd_seq_event_t* ev = nullptr;
                int r;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    r = snd_seq_event_input(mSeq, &ev);
                }
                if (r < 0 || !ev) break; // -EAGAIN: drained
                MidiMessage m;
                switch (ev->type)
                {
                case SND_SEQ_EVENT_CONTROLLER:
                    m.type = MidiMessage::ControlChange;
                    m.channel = ev->data.control.channel;
                    m.number  = static_cast<int>(ev->data.control.param) & 0x7F;
                    m.value   = ev->data.control.value & 0x7F;
                    break;
                case SND_SEQ_EVENT_NOTEON:
                case SND_SEQ_EVENT_NOTEOFF:
                    m.type = ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity > 0
                                 ? MidiMessage::NoteOn : MidiMessage::NoteOff;
                    m.channel = ev->data.note.channel;
                    m.number  = ev->data.note.note;
                    m.value   = ev->data.note.velocity;
                    break;
                case SND_SEQ_EVENT_PGMCHANGE:
                    m.type = MidiMessage::ProgramChange;
                    m.channel = ev->data.control.channel;
                    m.number  = ev->data.control.value & 0x7F;
                    break;
                case SND_SEQ_EVENT_CLOCK:    m.type = MidiMessage::Clock;    break;
                case SND_SEQ_EVENT_START:    m.type = MidiMessage::Start;    break;
                case SND_SEQ_EVENT_STOP:     m.type = MidiMessage::Stop;     break;
                case SND_SEQ_EVENT_CONTINUE: m.type = MidiMessage::Continue; break;
                case SND_SEQ_EVENT_CLIENT_START:
                case SND_SEQ_EVENT_CLIENT_EXIT:
                case SND_SEQ_EVENT_PORT_START:
                case SND_SEQ_EVENT_PORT_EXIT:
                case SND_SEQ_EVENT_PORT_CHANGE:
                    topology = true;
                    break;
                default: break;
                }
                if (m.type != MidiMessage::None)
                    QMetaObject::invokeMethod(this, [this, m] { emit messageReceived(m); }, Qt::QueuedConnection);
            }
            if (topology)
            {
                // Rescan + reconnect on the GUI thread, where the enabled set
                // lives; seq calls stay serialized by the mutex.
                QMetaObject::invokeMethod(this, [this] {
                    rescan();
                    syncConnections();
                    emit devicesChanged();
                }, Qt::QueuedConnection);
            }
        }
    }

    snd_seq_t*  mSeq    = nullptr;
    int         mClient = -1;
    int         mPort   = -1;
    int         mWake   = -1;
    std::thread mThread;
    std::atomic<bool> mRunning{false};
    mutable std::mutex mMutex; // guards mSeq calls and the maps

    std::map<QString, Addr> mPresent;   // name → address, current
    std::map<QString, Addr> mConnected; // what we subscribed
};

} // namespace

std::unique_ptr<MidiInput> MidiInput::create(QObject* parent)
{
    return std::make_unique<AlsaMidiInput>(parent);
}

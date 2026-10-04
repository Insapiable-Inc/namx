// ALSA sequencer MIDI input (Linux). Compiled instead of WinMidiInput.cpp.

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <QMetaObject>
#include <QStringList>
#include <alsa/asoundlib.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "Common.h"
#include "Log.h"
#include "MidiInput.h"

namespace namx
{

class AlsaMidiInput final : public MidiInput
{
public:
    explicit AlsaMidiInput(QObject* parent)
        : MidiInput(parent) {
    }
    ~AlsaMidiInput() override {
        shutdown();
    }

    bool start() override {
        if (seq) {
            return true;
        }
        if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
            qCWarning(lc_audio, "[MIDI] ALSA sequencer unavailable — MIDI disabled");
            seq = nullptr;
            return false;
        }
        snd_seq_set_client_name(seq, "NAM-X");
        client = snd_seq_client_id(seq);
        port = snd_seq_create_simple_port(
            seq, "MIDI In", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (port < 0) {
            qCWarning(lc_audio, "[MIDI] cannot create the input port");
            snd_seq_close(seq);
            seq = nullptr;
            return false;
        }
        // Hot-plug: the system announce port reports clients/ports coming and going.
        snd_seq_connect_from(seq, port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

        rescan();
        sync_connections();

        wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        running = true;
        thread = std::thread([this] {
            run();
        });
        return true;
    }

    QString backend_name() const override {
        return "ALSA";
    }

    QStringList devices() const override {
        std::lock_guard<std::mutex> lock(mutex);
        QStringList out;
        for (const auto& [name, addr] : present) {
            out << name;
        }
        return out; // std::map: already sorted
    }

protected:
    void sync_connections() override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!seq) {
            return;
        }
        // Disconnect what is gone or disabled, connect what is enabled.
        for (auto it = connected.begin(); it != connected.end();) {
            const auto p = present.find(it->first);
            if (p == present.end() || !is_enabled(it->first) || p->second != it->second) {
                snd_seq_disconnect_from(seq, port, it->second.client, it->second.port);
                it = connected.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& [name, addr] : present) {
            if (is_enabled(name) && !connected.count(name) &&
                snd_seq_connect_from(seq, port, addr.client, addr.port) == 0) {
                connected[name] = addr;
            }
        }
    }

private:
    struct Addr
    {
        int client = 0, port = 0;
        bool operator!=(const Addr& o) const {
            return client != o.client || port != o.port;
        }
    };

    void shutdown() {
        if (!seq) {
            return;
        }
        running = false;
        const uint64_t one = 1;
        if (wake >= 0) {
            (void)!write(wake, &one, sizeof one);
        }
        if (thread.joinable()) {
            thread.join();
        }
        if (wake >= 0) {
            close(wake);
        }
        snd_seq_close(seq);
        seq = nullptr;
    }

    // Readable, subscribable ports of other clients (GUI thread).
    // GUI thread, after a hot-plug: rescan, reconnect and tell listeners.
    void refresh_devices() {
        rescan();
        sync_connections();
        emit devices_changed();
    }

    void rescan() {
        std::map<QString, Addr> found;
        {
            std::lock_guard<std::mutex> lock(mutex);
            snd_seq_client_info_t* cinfo;
            snd_seq_port_info_t* pinfo;
            snd_seq_client_info_alloca(&cinfo);
            snd_seq_port_info_alloca(&pinfo);
            snd_seq_client_info_set_client(cinfo, -1);
            while (snd_seq_query_next_client(seq, cinfo) >= 0) {
                const int client_id = snd_seq_client_info_get_client(cinfo);
                if (client_id == SND_SEQ_CLIENT_SYSTEM || client_id == client) {
                    continue;
                }
                const QString client_name = QString::fromUtf8(snd_seq_client_info_get_name(cinfo));
                snd_seq_port_info_set_client(pinfo, client_id);
                snd_seq_port_info_set_port(pinfo, -1);
                while (snd_seq_query_next_port(seq, pinfo) >= 0) {
                    const unsigned caps = snd_seq_port_info_get_capability(pinfo);
                    const unsigned need = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
                    if ((caps & need) != need || (caps & SND_SEQ_PORT_CAP_NO_EXPORT)) {
                        continue;
                    }
                    const QString port_name = QString::fromUtf8(snd_seq_port_info_get_name(pinfo));
                    // "Device: Port" unless the port just repeats the client name.
                    const QString name = port_name.startsWith(client_name)
                                             ? port_name
                                             : client_name + ": " + port_name;
                    found[name] = {client_id, snd_seq_port_info_get_port(pinfo)};
                }
            }
            present = std::move(found);
        }
    }

    void run() {
        vector<pollfd> fds;
        {
            std::lock_guard<std::mutex> lock(mutex);
            // ALSA returns the count as int (never negative) but takes it back as unsigned.
            const unsigned int n =
                static_cast<unsigned int>(snd_seq_poll_descriptors_count(seq, POLLIN));
            fds.resize(n + 1);
            snd_seq_poll_descriptors(seq, fds.data(), n, POLLIN);
        }
        fds.back() = {wake, POLLIN, 0};

        while (running) {
            if (poll(fds.data(), fds.size(), -1) < 0) {
                continue;
            }
            if (!running) {
                break;
            }
            bool topology = false;
            for (;;) {
                snd_seq_event_t* ev = nullptr;
                int r;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    r = snd_seq_event_input(seq, &ev);
                }
                if (r < 0 || !ev) {
                    break; // -EAGAIN: drained
                }
                MidiMessage m;
                m.received_us = MidiMessage::now_us();
                switch (ev->type) {
                    case SND_SEQ_EVENT_CONTROLLER:
                        m.type = MidiMessage::CONTROL_CHANGE;
                        m.channel = ev->data.control.channel;
                        m.number = static_cast<int>(ev->data.control.param) & 0x7F;
                        m.value = ev->data.control.value & 0x7F;
                        break;
                    case SND_SEQ_EVENT_NOTEON:
                    case SND_SEQ_EVENT_NOTEOFF:
                        m.type = ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity > 0
                                     ? MidiMessage::NOTE_ON
                                     : MidiMessage::NOTE_OFF;
                        m.channel = ev->data.note.channel;
                        m.number = ev->data.note.note;
                        m.value = ev->data.note.velocity;
                        break;
                    case SND_SEQ_EVENT_PGMCHANGE:
                        m.type = MidiMessage::PROGRAM_CHANGE;
                        m.channel = ev->data.control.channel;
                        m.number = ev->data.control.value & 0x7F;
                        break;
                    case SND_SEQ_EVENT_CLOCK:
                        m.type = MidiMessage::CLOCK;
                        break;
                    case SND_SEQ_EVENT_START:
                        m.type = MidiMessage::START;
                        break;
                    case SND_SEQ_EVENT_STOP:
                        m.type = MidiMessage::STOP;
                        break;
                    case SND_SEQ_EVENT_CONTINUE:
                        m.type = MidiMessage::CONTINUE;
                        break;
                    case SND_SEQ_EVENT_CLIENT_START:
                    case SND_SEQ_EVENT_CLIENT_EXIT:
                    case SND_SEQ_EVENT_PORT_START:
                    case SND_SEQ_EVENT_PORT_EXIT:
                    case SND_SEQ_EVENT_PORT_CHANGE:
                        topology = true;
                        break;
                    default:
                        break;
                }
                if (m.type != MidiMessage::NONE) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, m] {
                            emit message_received(m);
                        },
                        Qt::QueuedConnection);
                }
            }
            if (topology) {
                // Rescan + reconnect on the GUI thread, where the enabled set
                // lives; seq calls stay serialized by the mutex.
                QMetaObject::invokeMethod(this, &AlsaMidiInput::refresh_devices,
                                          Qt::QueuedConnection);
            }
        }
    }

    snd_seq_t* seq = nullptr;
    int client = -1;
    int port = -1;
    int wake = -1;
    std::thread thread;
    atomic<bool> running{false};
    mutable std::mutex mutex; // guards seq calls and the maps

    std::map<QString, Addr> present; // name → address, current
    std::map<QString, Addr> connected; // what we subscribed
};

MidiInput* MidiInput::create(QObject* parent) {
    return new AlsaMidiInput(parent);
}

} // namespace namx

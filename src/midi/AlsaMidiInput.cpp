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

#include "Log.h"
#include "MidiInput.h"

namespace
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
        if (m_seq) {
            return true;
        }
        if (snd_seq_open(&m_seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
            qCWarning(lc_audio, "[MIDI] ALSA sequencer unavailable — MIDI disabled");
            m_seq = nullptr;
            return false;
        }
        snd_seq_set_client_name(m_seq, "NAM Player");
        m_client = snd_seq_client_id(m_seq);
        m_port = snd_seq_create_simple_port(
            m_seq, "MIDI In", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (m_port < 0) {
            qCWarning(lc_audio, "[MIDI] cannot create the input port");
            snd_seq_close(m_seq);
            m_seq = nullptr;
            return false;
        }
        // Hot-plug: the system announce port reports clients/ports coming and going.
        snd_seq_connect_from(m_seq, m_port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

        rescan();
        sync_connections();

        m_wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        m_running = true;
        m_thread = std::thread([this] {
            run();
        });
        return true;
    }

    QString backend_name() const override {
        return "ALSA";
    }

    QStringList devices() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        QStringList out;
        for (const auto& [name, addr] : m_present) {
            out << name;
        }
        return out; // std::map: already sorted
    }

protected:
    void sync_connections() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_seq) {
            return;
        }
        // Disconnect what is gone or disabled, connect what is enabled.
        for (auto it = m_connected.begin(); it != m_connected.end();) {
            const auto p = m_present.find(it->first);
            if (p == m_present.end() || !is_enabled(it->first) || p->second != it->second) {
                snd_seq_disconnect_from(m_seq, m_port, it->second.client, it->second.port);
                it = m_connected.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& [name, addr] : m_present) {
            if (is_enabled(name) && !m_connected.count(name) &&
                snd_seq_connect_from(m_seq, m_port, addr.client, addr.port) == 0) {
                m_connected[name] = addr;
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
        if (!m_seq) {
            return;
        }
        m_running = false;
        const uint64_t one = 1;
        if (m_wake >= 0) {
            (void)!write(m_wake, &one, sizeof one);
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_wake >= 0) {
            close(m_wake);
        }
        snd_seq_close(m_seq);
        m_seq = nullptr;
    }

    // Readable, subscribable ports of other clients (GUI thread).
    void rescan() {
        std::map<QString, Addr> found;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            snd_seq_client_info_t* cinfo;
            snd_seq_port_info_t* pinfo;
            snd_seq_client_info_alloca(&cinfo);
            snd_seq_port_info_alloca(&pinfo);
            snd_seq_client_info_set_client(cinfo, -1);
            while (snd_seq_query_next_client(m_seq, cinfo) >= 0) {
                const int client = snd_seq_client_info_get_client(cinfo);
                if (client == SND_SEQ_CLIENT_SYSTEM || client == m_client) {
                    continue;
                }
                const QString client_name = QString::fromUtf8(snd_seq_client_info_get_name(cinfo));
                snd_seq_port_info_set_client(pinfo, client);
                snd_seq_port_info_set_port(pinfo, -1);
                while (snd_seq_query_next_port(m_seq, pinfo) >= 0) {
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
                    found[name] = {client, snd_seq_port_info_get_port(pinfo)};
                }
            }
            m_present = std::move(found);
        }
    }

    void run() {
        std::vector<pollfd> fds;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // ALSA returns the count as int (never negative) but takes it back as unsigned.
            const auto n = static_cast<unsigned int>(snd_seq_poll_descriptors_count(m_seq, POLLIN));
            fds.resize(n + 1);
            snd_seq_poll_descriptors(m_seq, fds.data(), n, POLLIN);
        }
        fds.back() = {m_wake, POLLIN, 0};

        while (m_running) {
            if (poll(fds.data(), fds.size(), -1) < 0) {
                continue;
            }
            if (!m_running) {
                break;
            }
            bool topology = false;
            for (;;) {
                snd_seq_event_t* ev = nullptr;
                int r;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    r = snd_seq_event_input(m_seq, &ev);
                }
                if (r < 0 || !ev) {
                    break; // -EAGAIN: drained
                }
                MidiMessage m;
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
                QMetaObject::invokeMethod(
                    this,
                    [this] {
                        rescan();
                        sync_connections();
                        emit devices_changed();
                    },
                    Qt::QueuedConnection);
            }
        }
    }

    snd_seq_t* m_seq = nullptr;
    int m_client = -1;
    int m_port = -1;
    int m_wake = -1;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    mutable std::mutex m_mutex; // guards mSeq calls and the maps

    std::map<QString, Addr> m_present; // name → address, current
    std::map<QString, Addr> m_connected; // what we subscribed
};

} // namespace

std::unique_ptr<MidiInput> MidiInput::create(QObject* parent) {
    return std::make_unique<AlsaMidiInput>(parent);
}

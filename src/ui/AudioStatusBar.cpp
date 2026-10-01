#include "AudioStatusBar.h"

#include <algorithm>
#include <functional>

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>

#include "AudioEngine.h"
#include "Theme.h"

namespace
{

// Label that fires a callback on left click; used for the interactive items
// (backend reconnect, device pickers). No Q_OBJECT — plain std::function.
class ClickableLabel : public QLabel
{
public:
    std::function<void()> on_click;

    explicit ClickableLabel(QWidget* parent = nullptr)
        : QLabel(parent) {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && on_click) {
            on_click();
        } else {
            QLabel::mousePressEvent(e);
        }
    }
};

QLabel* make_item(QHBoxLayout* lay, const char* object_name) {
    auto* l = new QLabel;
    l->setObjectName(object_name);
    l->setTextFormat(Qt::RichText);
    lay->addWidget(l);
    return l;
}

ClickableLabel* make_action(QHBoxLayout* lay, std::function<void()> on_click) {
    auto* l = new ClickableLabel;
    l->setObjectName("statusAction");
    l->setTextFormat(Qt::RichText);
    l->on_click = std::move(on_click);
    lay->addWidget(l);
    return l;
}

QString dot(const QColor& c) {
    return QString("<span style=\"color:%1\">●</span> ").arg(c.name());
}

// Resolve a device id against the enumerated list: the matching name, the
// first device as fallback, or "Default" when the backend enumerates nothing
// (JACK — the server is the only "device").
QString device_name(const std::vector<IAudioBackend::DeviceInfo>& devs, const QString& id) {
    for (const auto& d : devs) {
        if (QString::fromStdString(d.id) == id) {
            return QString::fromStdString(d.name);
        }
    }
    if (!devs.empty()) {
        return QString::fromStdString(devs.front().name);
    }
    return "Default";
}

} // namespace

AudioStatusBar::AudioStatusBar(AudioEngine& engine, QWidget* parent)
    : QWidget(parent)
    , m_engine(engine) {
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 4, 0);
    lay->setSpacing(2);

    m_lbl_backend = make_action(lay, [this] {
        if (!m_connected) {
            emit reconnect_requested();
        }
    });
    m_lbl_rate = make_item(lay, "statusItem");
    m_lbl_buffer = make_item(lay, "statusItem");
    m_lbl_latency = make_item(lay, "statusItem");
    m_lbl_load = make_action(lay, [this] {
        reset_counters();
    });
    m_lbl_xruns = make_action(lay, [this] {
        reset_counters();
    });
    m_lbl_input = make_action(lay, [this] {
        pick_device(true);
    });
    m_lbl_output = make_action(lay, [this] {
        pick_device(false);
    });
}

void AudioStatusBar::refresh() {
    QSettings s;
    m_input_id = s.value("audio/inputDeviceId").toString();
    m_output_id = s.value("audio/outputDeviceId").toString();

    m_connected = m_engine.is_running();
    const QString status = QString::fromStdString(m_engine.backend_status());
    // First token of the backend status ("JACK ● 48000 Hz ● …" → "JACK").
    const QString backend = status.section(' ', 0, 0);
    m_lbl_backend->setText(dot(Theme::meter_low()) + (backend.isEmpty() ? "Audio" : backend));
    m_lbl_backend->setToolTip(status);
    static_cast<ClickableLabel*>(m_lbl_backend)->setCursor(Qt::ArrowCursor);

    set_device_labels();
    update_metrics();
}

void AudioStatusBar::set_error(const QString& text) {
    m_connected = false;
    m_lbl_backend->setText(dot(Theme::meter_high()) + text.toHtmlEscaped());
    m_lbl_backend->setToolTip("Click to reconnect");
    static_cast<ClickableLabel*>(m_lbl_backend)->setCursor(Qt::PointingHandCursor);

    // Keep the device pickers usable even when audio never started — picking
    // a device is often exactly how the user fixes the connection.
    QSettings s;
    m_input_id = s.value("audio/inputDeviceId").toString();
    m_output_id = s.value("audio/outputDeviceId").toString();
    set_device_labels();
    update_metrics();
}

void AudioStatusBar::update_metrics() {
    QString rate, buffer, latency, latency_tip;
    if (m_connected) {
        const double sr = m_engine.sample_rate();
        const int bs = m_engine.buffer_size();
        rate = QString("%1 Hz").arg(qRound(sr));
        buffer = QString("%1 frames").arg(bs);
        if (sr > 0.0 && bs > 0) {
            const QString one = QString("%1 ms").arg(bs / sr * 1000.0, 0, 'f', 1);
            const int io = m_engine.io_latency_frames();
            if (io > 0) {
                latency = QString("Round trip %1 ms").arg(io / sr * 1000.0, 0, 'f', 1);
                latency_tip = QString("Input → output latency reported by the audio server "
                                      "(%1 frames). One buffer is %2.")
                                  .arg(io)
                                  .arg(one);
            } else {
                latency = one;
                latency_tip = "One buffer of latency (buffer size ÷ sample rate); "
                              "the server doesn't report the full round trip";
            }
        }
    }
    if (m_lbl_rate->text() != rate) {
        m_lbl_rate->setText(rate);
    }
    if (m_lbl_buffer->text() != buffer) {
        m_lbl_buffer->setText(buffer);
    }
    if (m_lbl_latency->text() != latency) {
        m_lbl_latency->setText(latency);
    }
    m_lbl_latency->setToolTip(latency_tip);

    update_dsp_load();
    update_xruns();
}

// Level → status color: calm, warning, trouble. Theme meter colors so the
// status bar speaks the same language as the VU meters.
static QString load_span(const QString& text, float frac) {
    const QColor c = frac >= 0.9f   ? Theme::meter_high()
                     : frac >= 0.7f ? Theme::meter_mid()
                                    : Theme::dim_text();
    return QString("<span style=\"color:%1\">%2</span>").arg(c.name(), text);
}

void AudioStatusBar::update_dsp_load() {
    const float peak = m_engine.take_dsp_load_peak();
    if (!m_connected) {
        m_lbl_load->hide();
        m_load_window = 0.0f;
        m_load_ticks = 0;
        return;
    }
    m_lbl_load->show();
    m_load_window = std::max(m_load_window, peak);
    m_load_max = std::max(m_load_max, peak);
    if (++m_load_ticks < 10) {
        return; // 10 × 50 ms metrics ticks
    }

    m_lbl_load->setText(
        load_span(QString("DSP %1%").arg(qRound(m_load_window * 100.0f)), m_load_window));
    m_lbl_load->setToolTip(
        QString("Audio-thread time per buffer as a share of the buffer's duration "
                "(highest in the last half second).\n"
                "Highest since reset: %1%. Over 100% means dropouts.\n"
                "Click to reset this and the xrun counter.")
            .arg(qRound(m_load_max * 100.0f)));
    m_load_window = 0.0f;
    m_load_ticks = 0;
}

void AudioStatusBar::update_xruns() {
    const int x = m_connected ? m_engine.xrun_count() : -1;
    if (x == m_shown_xruns) {
        return;
    }
    m_shown_xruns = x;
    m_lbl_xruns->setVisible(x >= 0);
    if (x < 0) {
        return;
    }
    m_lbl_xruns->setText(
        load_span(x == 1 ? QString("1 xrun") : QString("%1 xruns").arg(x), x > 0 ? 0.8f : 0.0f));
    m_lbl_xruns->setToolTip(
        "Buffer over/underruns (audible dropouts) since start or the last reset.\n"
        "Click to reset.");
}

void AudioStatusBar::reset_counters() {
    m_engine.reset_xruns();
    m_load_max = 0.0f;
    update_xruns();
}

void AudioStatusBar::set_device_labels() {
    const auto ins = m_engine.enumerate_input_devices();
    const auto outs = m_engine.enumerate_output_devices();

    auto elide = [](QString s) {
        return s.size() > 28 ? s.left(27) + "…" : s;
    };
    const QString in_name = device_name(ins, m_input_id);
    const QString out_name = device_name(outs, m_output_id);
    m_lbl_input->setText("In: " + elide(in_name).toHtmlEscaped());
    m_lbl_input->setToolTip("Input device (click to change)\n" + in_name);
    m_lbl_output->setText("Out: " + elide(out_name).toHtmlEscaped());
    m_lbl_output->setToolTip("Output device (click to change)\n" + out_name);
}

void AudioStatusBar::pick_device(bool input) {
    const auto devs =
        input ? m_engine.enumerate_input_devices() : m_engine.enumerate_output_devices();
    QString& current = input ? m_input_id : m_output_id;
    QLabel* anchor = input ? m_lbl_input : m_lbl_output;

    QMenu menu(this);
    if (devs.empty()) {
        menu.addAction("Default")->setEnabled(false);
    }
    for (const auto& d : devs) {
        const QString id = QString::fromStdString(d.id);
        const QString name = QString::fromStdString(d.name);
        QAction* act = menu.addAction(name);
        act->setCheckable(true);
        act->setChecked(id == current || (current.isEmpty() && &d == &devs.front()));
        act->setData(id);
    }

    // Pop up above the item, VS Code style.
    QAction* picked = menu.exec(anchor->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    if (!picked || picked->data().toString() == current) {
        return;
    }

    current = picked->data().toString();
    QSettings s;
    s.setValue(input ? "audio/inputDeviceId" : "audio/outputDeviceId", current);
    set_device_labels();
    emit device_selection_changed();
}

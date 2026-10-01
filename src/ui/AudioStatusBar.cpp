#include "AudioStatusBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>

#include <algorithm>
#include <functional>

#include "AudioEngine.h"
#include "Theme.h"

namespace {

// Label that fires a callback on left click; used for the interactive items
// (backend reconnect, device pickers). No Q_OBJECT — plain std::function.
class ClickableLabel : public QLabel
{
public:
    std::function<void()> onClick;

    explicit ClickableLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && onClick) onClick();
        else QLabel::mousePressEvent(e);
    }
};

QLabel* makeItem(QHBoxLayout* lay, const char* objectName)
{
    auto* l = new QLabel;
    l->setObjectName(objectName);
    l->setTextFormat(Qt::RichText);
    lay->addWidget(l);
    return l;
}

ClickableLabel* makeAction(QHBoxLayout* lay, std::function<void()> onClick)
{
    auto* l = new ClickableLabel;
    l->setObjectName("statusAction");
    l->setTextFormat(Qt::RichText);
    l->onClick = std::move(onClick);
    lay->addWidget(l);
    return l;
}

QString dot(const QColor& c)
{
    return QString("<span style=\"color:%1\">●</span> ").arg(c.name());
}

// Resolve a device id against the enumerated list: the matching name, the
// first device as fallback, or "Default" when the backend enumerates nothing
// (JACK — the server is the only "device").
QString deviceName(const std::vector<IAudioBackend::DeviceInfo>& devs,
                   const QString& id)
{
    for (const auto& d : devs)
        if (QString::fromStdString(d.id) == id)
            return QString::fromStdString(d.name);
    if (!devs.empty())
        return QString::fromStdString(devs.front().name);
    return "Default";
}

} // namespace

AudioStatusBar::AudioStatusBar(AudioEngine& engine, QWidget* parent)
    : QWidget(parent)
    , mEngine(engine)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 4, 0);
    lay->setSpacing(2);

    mLblBackend = makeAction(lay, [this] {
        if (!mConnected) emit reconnectRequested();
    });
    mLblRate    = makeItem(lay, "statusItem");
    mLblBuffer  = makeItem(lay, "statusItem");
    mLblLatency = makeItem(lay, "statusItem");
    mLblLoad    = makeAction(lay, [this] { resetCounters(); });
    mLblXruns   = makeAction(lay, [this] { resetCounters(); });
    mLblInput   = makeAction(lay, [this] { pickDevice(true); });
    mLblOutput  = makeAction(lay, [this] { pickDevice(false); });
}

void AudioStatusBar::refresh()
{
    QSettings s;
    mInputId  = s.value("audio/inputDeviceId").toString();
    mOutputId = s.value("audio/outputDeviceId").toString();

    mConnected = mEngine.isRunning();
    const QString status = QString::fromStdString(mEngine.backendStatus());
    // First token of the backend status ("JACK ● 48000 Hz ● …" → "JACK").
    const QString backend = status.section(' ', 0, 0);
    mLblBackend->setText(dot(Theme::meterLow())
                         + (backend.isEmpty() ? "Audio" : backend));
    mLblBackend->setToolTip(status);
    static_cast<ClickableLabel*>(mLblBackend)->setCursor(Qt::ArrowCursor);

    setDeviceLabels();
    updateMetrics();
}

void AudioStatusBar::setError(const QString& text)
{
    mConnected = false;
    mLblBackend->setText(dot(Theme::meterHigh()) + text.toHtmlEscaped());
    mLblBackend->setToolTip("Click to reconnect");
    static_cast<ClickableLabel*>(mLblBackend)->setCursor(Qt::PointingHandCursor);

    // Keep the device pickers usable even when audio never started — picking
    // a device is often exactly how the user fixes the connection.
    QSettings s;
    mInputId  = s.value("audio/inputDeviceId").toString();
    mOutputId = s.value("audio/outputDeviceId").toString();
    setDeviceLabels();
    updateMetrics();
}

void AudioStatusBar::updateMetrics()
{
    QString rate, buffer, latency, latencyTip;
    if (mConnected)
    {
        const double sr = mEngine.sampleRate();
        const int    bs = mEngine.bufferSize();
        rate   = QString("%1 Hz").arg(qRound(sr));
        buffer = QString("%1 frames").arg(bs);
        if (sr > 0.0 && bs > 0)
        {
            const QString one = QString("%1 ms").arg(bs / sr * 1000.0, 0, 'f', 1);
            const int io = mEngine.ioLatencyFrames();
            if (io > 0)
            {
                latency    = QString("Round trip %1 ms").arg(io / sr * 1000.0, 0, 'f', 1);
                latencyTip = QString("Input → output latency reported by the audio server "
                                     "(%1 frames). One buffer is %2.").arg(io).arg(one);
            }
            else
            {
                latency    = one;
                latencyTip = "One buffer of latency (buffer size ÷ sample rate); "
                             "the server doesn't report the full round trip";
            }
        }
    }
    if (mLblRate->text()    != rate)    mLblRate->setText(rate);
    if (mLblBuffer->text()  != buffer)  mLblBuffer->setText(buffer);
    if (mLblLatency->text() != latency) mLblLatency->setText(latency);
    mLblLatency->setToolTip(latencyTip);

    updateDspLoad();
    updateXruns();
}

// Level → status color: calm, warning, trouble. Theme meter colors so the
// status bar speaks the same language as the VU meters.
static QString loadSpan(const QString& text, float frac)
{
    const QColor c = frac >= 0.9f ? Theme::meterHigh()
                   : frac >= 0.7f ? Theme::meterMid()
                                  : Theme::dimText();
    return QString("<span style=\"color:%1\">%2</span>").arg(c.name(), text);
}

void AudioStatusBar::updateDspLoad()
{
    const float peak = mEngine.takeDspLoadPeak();
    if (!mConnected)
    {
        mLblLoad->hide();
        mLoadWindow = 0.0f;
        mLoadTicks  = 0;
        return;
    }
    mLblLoad->show();
    mLoadWindow = std::max(mLoadWindow, peak);
    mLoadMax    = std::max(mLoadMax, peak);
    if (++mLoadTicks < 10) return; // 10 × 50 ms metrics ticks

    mLblLoad->setText(loadSpan(QString("DSP %1%").arg(qRound(mLoadWindow * 100.0f)), mLoadWindow));
    mLblLoad->setToolTip(QString("Audio-thread time per buffer as a share of the buffer's duration "
                                 "(highest in the last half second).\n"
                                 "Highest since reset: %1%. Over 100% means dropouts.\n"
                                 "Click to reset this and the xrun counter.")
                             .arg(qRound(mLoadMax * 100.0f)));
    mLoadWindow = 0.0f;
    mLoadTicks  = 0;
}

void AudioStatusBar::updateXruns()
{
    const int x = mConnected ? mEngine.xrunCount() : -1;
    if (x == mShownXruns) return;
    mShownXruns = x;
    mLblXruns->setVisible(x >= 0);
    if (x < 0) return;
    mLblXruns->setText(loadSpan(x == 1 ? QString("1 xrun") : QString("%1 xruns").arg(x),
                                x > 0 ? 0.8f : 0.0f));
    mLblXruns->setToolTip("Buffer over/underruns (audible dropouts) since start or the last reset.\n"
                          "Click to reset.");
}

void AudioStatusBar::resetCounters()
{
    mEngine.resetXruns();
    mLoadMax = 0.0f;
    updateXruns();
}

void AudioStatusBar::setDeviceLabels()
{
    const auto ins  = mEngine.enumerateInputDevices();
    const auto outs = mEngine.enumerateOutputDevices();

    auto elide = [](QString s) {
        return s.size() > 28 ? s.left(27) + "…" : s;
    };
    const QString inName  = deviceName(ins,  mInputId);
    const QString outName = deviceName(outs, mOutputId);
    mLblInput->setText("In: " + elide(inName).toHtmlEscaped());
    mLblInput->setToolTip("Input device (click to change)\n" + inName);
    mLblOutput->setText("Out: " + elide(outName).toHtmlEscaped());
    mLblOutput->setToolTip("Output device (click to change)\n" + outName);
}

void AudioStatusBar::pickDevice(bool input)
{
    const auto devs = input ? mEngine.enumerateInputDevices()
                            : mEngine.enumerateOutputDevices();
    QString& current = input ? mInputId : mOutputId;
    QLabel*  anchor  = input ? mLblInput : mLblOutput;

    QMenu menu(this);
    if (devs.empty())
        menu.addAction("Default")->setEnabled(false);
    for (const auto& d : devs)
    {
        const QString id   = QString::fromStdString(d.id);
        const QString name = QString::fromStdString(d.name);
        QAction* act = menu.addAction(name);
        act->setCheckable(true);
        act->setChecked(id == current || (current.isEmpty() && &d == &devs.front()));
        act->setData(id);
    }

    // Pop up above the item, VS Code style.
    QAction* picked = menu.exec(anchor->mapToGlobal(
        QPoint(0, -menu.sizeHint().height())));
    if (!picked || picked->data().toString() == current)
        return;

    current = picked->data().toString();
    QSettings s;
    s.setValue(input ? "audio/inputDeviceId" : "audio/outputDeviceId", current);
    setDeviceLabels();
    emit deviceSelectionChanged();
}

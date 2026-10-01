#include "TempoBar.h"

#include "KnobWidget.h"
#include "Metronome.h"
#include "TempoSync.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QShortcut>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// BpmField
// ---------------------------------------------------------------------------

BpmField::BpmField(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("bpmField");
    setFixedSize(sizeHint());
    setCursor(Qt::SizeVerCursor);
    setAttribute(Qt::WA_Hover); // border brightens under the mouse
    setToolTip("Tempo (quarter notes per minute): drag up/down or scroll to change "
               "(Shift = fine), click to type");
}

void BpmField::setValue(double bpm)
{
    bpm = std::round(std::clamp(bpm, TempoSync::kMinBpm, TempoSync::kMaxBpm) * 10.0) / 10.0;
    if (bpm == mBpm) return;
    mBpm = bpm;
    update();
    emit valueChanged(mBpm);
}

void BpmField::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(underMouse() || mDragging ? Theme::trackHover().lighter(130) : Theme::track(), 1.0));
    p.setBrush(Theme::well());
    p.drawRoundedRect(r, 5, 5);
    if (mEdit) return; // the line edit draws the text while typing

    QFont f = font();
    f.setPointSizeF(12.0);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Theme::genericAccent());
    const QString num = std::fmod(mBpm, 1.0) == 0.0 ? QString::number(mBpm, 'f', 0)
                                                    : QString::number(mBpm, 'f', 1);
    p.drawText(r.adjusted(8, 0, -30, 0), Qt::AlignRight | Qt::AlignVCenter, num);
    f.setPointSizeF(7.5);
    f.setBold(false);
    p.setFont(f);
    p.setPen(Theme::dimText());
    p.drawText(r.adjusted(0, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter, "BPM");
}

void BpmField::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton || mEdit) return QWidget::mousePressEvent(ev);
    mPressed  = true;
    mDragging = false;
    mPressY   = ev->position().toPoint().y();
    mPressBpm = mBpm;
}

void BpmField::mouseMoveEvent(QMouseEvent* ev)
{
    if (!mPressed) return;
    const int dy = mPressY - ev->position().toPoint().y();
    if (!mDragging && std::abs(dy) < 3) return;
    mDragging = true;
    const double perPx = (ev->modifiers() & Qt::ShiftModifier) ? 0.025 : 0.25;
    setValue(mPressBpm + dy * perPx);
}

void BpmField::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton || !mPressed) return;
    mPressed = false;
    if (!mDragging) beginEdit(); // a click without a drag: type a value
    mDragging = false;
    update();
}

void BpmField::wheelEvent(QWheelEvent* ev)
{
    const int d = ev->angleDelta().y();
    if (d == 0) return;
    const double step = (ev->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
    setValue(mBpm + (d > 0 ? step : -step));
}

void BpmField::beginEdit()
{
    mEdit = new QLineEdit(QString::number(mBpm, 'f', std::fmod(mBpm, 1.0) == 0.0 ? 0 : 1), this);
    mEdit->setObjectName("bpmEdit");
    mEdit->setGeometry(rect().adjusted(2, 2, -2, -2));
    mEdit->setAlignment(Qt::AlignCenter);
    mEdit->selectAll();
    mEdit->show();
    mEdit->setFocus();
    connect(mEdit, &QLineEdit::returnPressed, this, [this] { endEdit(true); });
    connect(mEdit, &QLineEdit::editingFinished, this, [this] { endEdit(true); });
    update();
}

void BpmField::endEdit(bool commit)
{
    if (!mEdit) return;
    QLineEdit* e = mEdit;
    mEdit = nullptr;
    bool ok = false;
    const double v = e->text().replace(',', '.').toDouble(&ok);
    e->deleteLater();
    if (commit && ok) setValue(v);
    update();
}

// ---------------------------------------------------------------------------
// BeatLeds
// ---------------------------------------------------------------------------

BeatLeds::BeatLeds(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip("Beats of the bar. Click a beat to cycle its accent: strong → normal → soft → mute");
}

QSize BeatLeds::sizeHint() const
{
    return {mBeats * kW + (mBeats - 1) * kGap + 2, kH};
}

void BeatLeds::setPattern(int beats, uint32_t accents)
{
    mBeats   = std::clamp(beats, 1, Metronome::kMaxBeats);
    mAccents = accents;
    setFixedSize(sizeHint());
    update();
}

void BeatLeds::setBeat(int beat, bool countIn)
{
    if (beat == mBeat && countIn == mCountIn) return;
    mBeat    = beat;
    mCountIn = countIn;
    update();
}

void BeatLeds::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    for (int b = 0; b < mBeats; ++b)
    {
        const auto   a   = Metronome::accentOf(mAccents, b);
        const qreal  x   = 1 + b * (kW + kGap);
        const qreal  h   = a == Metronome::Strong ? kH - 2 : a == Metronome::Normal ? (kH - 2) * 0.66
                         : a == Metronome::Soft   ? (kH - 2) * 0.36 : (kH - 2) * 0.36;
        const QRectF r(x, kH - 1 - h, kW, h);
        const bool   lit = b == mBeat;
        QColor c = lit ? (mCountIn ? Theme::meterMid() : Theme::genericAccent()) : Theme::trackHover();
        if (a == Metronome::Mute)
        {
            p.setPen(QPen(lit ? c : Theme::trackHover(), 1.2));
            p.setBrush(Qt::NoBrush);
        }
        else
        {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
        }
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);
    }
}

void BeatLeds::mousePressEvent(QMouseEvent* ev)
{
    const int b = static_cast<int>(ev->position().x()) / (kW + kGap);
    if (b < 0 || b >= mBeats) return;
    // strong → normal → soft → mute → strong
    const auto a    = Metronome::accentOf(mAccents, b);
    const auto next = a == Metronome::Mute ? Metronome::Strong : Metronome::Accent(a - 1);
    mAccents = Metronome::withAccent(mAccents, b, next);
    update();
    emit accentsChanged(mAccents);
}

// ---------------------------------------------------------------------------
// TempoBar
// ---------------------------------------------------------------------------

namespace {
const char* kSoundNames[] = {"Click", "Wood", "Beep", "Cowbell"};
const char* kSubdivNames[] = {"Beats", "Eighths", "Triplets", "Sixteenths"};
const char* kSubdivShort[] = {"♩", "♪♪", "3", "♬"};
struct MeterPreset { int beats, unit; };
const MeterPreset kMeters[] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 4}, {7, 4},
                               {3, 8}, {5, 8}, {6, 8}, {7, 8}, {9, 8}, {12, 8}};
} // namespace

QToolButton* TempoBar::menuButton(const QString& tip)
{
    auto* b = new QToolButton(this);
    b->setObjectName("presetTool");
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setMenu(new QMenu(b));
    return b;
}

TempoBar::TempoBar(Metronome& metronome, QWidget* parent)
    : QFrame(parent)
    , mMetro(metronome)
{
    setObjectName("tempoBar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 2, 8, 2);
    lay->setSpacing(6);
    QSettings s;

    auto* caption = new QLabel("Tempo", this);
    caption->setObjectName("presetCaption");
    lay->addWidget(caption);

    mBpm = new BpmField(this);
    mBpm->setValue(s.value("tempo/bpm", TempoSync::kDefaultBpm).toDouble());
    mMetro.setBpm(mBpm->value());
    connect(mBpm, &BpmField::valueChanged, this, [this](double bpm) {
        QSettings().setValue("tempo/bpm", bpm);
        mMetro.setBpm(bpm);
        emit bpmChanged(bpm);
    });
    lay->addWidget(mBpm);

    mTap = new QToolButton(this);
    mTap->setObjectName("presetTool");
    mTap->setText("Tap");
    mTap->setToolTip("Tap the tempo: four taps or more average out");
    mTap->setFocusPolicy(Qt::NoFocus);
    connect(mTap, &QToolButton::pressed, this, &TempoBar::tap); // on press: tighter timing
    lay->addWidget(mTap);
    lay->addSpacing(18);

    // ---- Metronome ----
    auto* metroCaption = new QLabel("Metronome", this);
    metroCaption->setObjectName("presetCaption");
    lay->addWidget(metroCaption);

    mPlay = new QToolButton(this);
    mPlay->setObjectName("metroPlay");
    mPlay->setCheckable(true);
    mPlay->setText("▶");
    mPlay->setToolTip("Start / stop the metronome (Space)");
    mPlay->setFocusPolicy(Qt::NoFocus);
    connect(mPlay, &QToolButton::toggled, this, [this](bool on) {
        mMetro.setRunning(on);
        mPlay->setText(on ? "■" : "▶");
    });
    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    space->setContext(Qt::WindowShortcut);
    connect(space, &QShortcut::activated, mPlay, &QToolButton::toggle);
    lay->addWidget(mPlay);

    mLeds = new BeatLeds(this);
    connect(mLeds, &BeatLeds::accentsChanged, this, [this](uint32_t a) {
        mMetro.setAccents(a);
        QSettings().setValue("metronome/accents", a);
    });
    lay->addWidget(mLeds, 0, Qt::AlignVCenter);

    // Meter.
    mMeterBtn = menuButton("Time signature");
    connect(mMeterBtn->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* m = mMeterBtn->menu();
        m->clear();
        for (const auto& mp : kMeters)
        {
            QAction* a = m->addAction(QString("%1/%2").arg(mp.beats).arg(mp.unit));
            a->setCheckable(true);
            a->setChecked(mp.beats == mMetro.beats() && mp.unit == mMetro.unit());
            connect(a, &QAction::triggered, this, [this, mp] { setMeter(mp.beats, mp.unit, true); });
        }
        m->addSeparator();
        connect(m->addAction("Custom…"), &QAction::triggered, this, [this] {
            bool ok = false;
            const int beats = QInputDialog::getInt(this, "Time Signature", "Beats per bar:",
                                                   mMetro.beats(), 1, Metronome::kMaxBeats, 1, &ok);
            if (!ok) return;
            const QString unit = QInputDialog::getItem(this, "Time Signature", "Beat unit:",
                                                       {"2", "4", "8", "16"},
                                                       QStringList{"2", "4", "8", "16"}.indexOf(QString::number(mMetro.unit())),
                                                       false, &ok);
            if (ok) setMeter(beats, unit.toInt(), true);
        });
    });
    lay->addWidget(mMeterBtn);

    // Subdivision.
    mSubdivBtn = menuButton("Subdivision: extra clicks between beats");
    for (int i = 0; i < 4; ++i)
        connect(mSubdivBtn->menu()->addAction(kSubdivNames[i]), &QAction::triggered, this, [this, i] {
            mMetro.setSubdivision(i + 1);
            QSettings().setValue("metronome/subdivision", i + 1);
            updateLabels();
        });
    lay->addWidget(mSubdivBtn);

    // Sound.
    mSoundBtn = menuButton("Click sound");
    for (int i = 0; i < Metronome::kSoundCount; ++i)
        connect(mSoundBtn->menu()->addAction(kSoundNames[i]), &QAction::triggered, this, [this, i] {
            mMetro.setSound(i);
            QSettings().setValue("metronome/sound", i);
            updateLabels();
        });
    lay->addWidget(mSoundBtn);

    // Count-in.
    mCountInBtn = menuButton("Count-in bars when starting");
    {
        QMenu* m = mCountInBtn->menu();
        for (int bars : {0, 1, 2})
            connect(m->addAction(bars == 0 ? QString("No count-in") : QString("Count in %1 bar%2").arg(bars).arg(bars > 1 ? "s" : "")),
                    &QAction::triggered, this, [this, bars] {
                        mMetro.setCountInBars(bars);
                        QSettings().setValue("metronome/countIn", bars);
                        updateLabels();
                    });
        m->addSeparator();
        QAction* silent = m->addAction("Silent after count-in (lights only)");
        silent->setCheckable(true);
        silent->setChecked(s.value("metronome/silentAfter", false).toBool());
        connect(silent, &QAction::toggled, this, [this](bool on) {
            mMetro.setSilentAfterCountIn(on);
            QSettings().setValue("metronome/silentAfter", on);
            updateLabels();
        });
        mMetro.setSilentAfterCountIn(silent->isChecked());
    }
    lay->addWidget(mCountInBtn);

    // Level + pan.
    mVolume = new KnobWidget("Level", -40.0, 6.0, -6.0, "dB");
    mVolume->setScrollStep(1.0);
    mVolume->setAccent(Theme::genericAccent());
    mVolume->setValue(s.value("metronome/volume", -6.0).toDouble());
    mMetro.setVolumeDb(static_cast<float>(mVolume->value()));
    connect(mVolume, &KnobWidget::valueChanged, this, [this](double v) {
        mMetro.setVolumeDb(static_cast<float>(v));
        QSettings().setValue("metronome/volume", v);
    });
    mPan = new KnobWidget("Pan", -100.0, 100.0, 0.0, "%");
    mPan->setScrollStep(10.0);
    mPan->setAccent(Theme::genericAccent());
    mPan->setValueFormatter([](double v) {
        const int p = static_cast<int>(std::lround(v));
        return p == 0 ? QString("Center") : QString("%1 %2").arg(std::abs(p)).arg(p < 0 ? "L" : "R");
    });
    mPan->setValue(s.value("metronome/pan", 0.0).toDouble());
    mMetro.setPan(static_cast<float>(mPan->value() / 100.0));
    connect(mPan, &KnobWidget::valueChanged, this, [this](double v) {
        mMetro.setPan(static_cast<float>(v / 100.0));
        QSettings().setValue("metronome/pan", v);
    });
    lay->addWidget(mVolume);
    lay->addWidget(mPan);
    lay->addStretch(1);

    // Restore the rest.
    mMetro.setSubdivision(s.value("metronome/subdivision", 1).toInt());
    mMetro.setSound(s.value("metronome/sound", 0).toInt());
    mMetro.setCountInBars(s.value("metronome/countIn", 0).toInt());
    const int beats = s.value("metronome/beats", 4).toInt();
    const int unit  = s.value("metronome/unit", 4).toInt();
    setMeter(beats, unit, !s.contains("metronome/accents"));
    if (s.contains("metronome/accents"))
    {
        const uint32_t acc = s.value("metronome/accents").toUInt();
        mMetro.setAccents(acc);
        mLeds->setPattern(mMetro.beats(), acc);
    }
    updateLabels();
}

double TempoBar::bpm() const { return mBpm->value(); }

void TempoBar::poll()
{
    mLeds->setBeat(mMetro.isRunning() ? mMetro.currentBeat() : -1, mMetro.inCountIn());
}

void TempoBar::setMeter(int beats, int unit, bool resetAccents)
{
    mMetro.setMeter(beats, unit);
    QSettings s;
    s.setValue("metronome/beats", mMetro.beats());
    s.setValue("metronome/unit", mMetro.unit());
    if (resetAccents)
    {
        const uint32_t acc = Metronome::defaultAccents(mMetro.beats(), mMetro.unit());
        mMetro.setAccents(acc);
        s.setValue("metronome/accents", acc);
    }
    mLeds->setPattern(mMetro.beats(), mMetro.accents());
    updateLabels();
}

void TempoBar::updateLabels()
{
    QSettings s;
    mMeterBtn->setText(QString("%1/%2").arg(mMetro.beats()).arg(mMetro.unit()));
    const int sub = std::clamp(s.value("metronome/subdivision", 1).toInt(), 1, 4);
    mSubdivBtn->setText(kSubdivShort[sub - 1]);
    mSubdivBtn->setToolTip(QString("Subdivision: %1").arg(kSubdivNames[sub - 1]));
    mSoundBtn->setText(kSoundNames[std::clamp(s.value("metronome/sound", 0).toInt(), 0, Metronome::kSoundCount - 1)]);
    const int ci = s.value("metronome/countIn", 0).toInt();
    const bool silent = s.value("metronome/silentAfter", false).toBool();
    mCountInBtn->setText(ci == 0 ? QString("No count-in")
                                 : QString("Count-in %1%2").arg(ci).arg(silent ? " · silent" : ""));
}

void TempoBar::tap()
{
    if (!mTapClock.isValid()) mTapClock.start();
    const qint64 now = mTapClock.elapsed();
    // A pause longer than a 40 BPM beat (plus slack) starts a new measurement.
    if (!mTaps.isEmpty() && now - mTaps.last() > 2000) mTaps.clear();
    mTaps.append(now);
    if (mTaps.size() > 5) mTaps.removeFirst(); // average the last four intervals
    if (mTaps.size() < 2) return;
    const double avgMs = static_cast<double>(mTaps.last() - mTaps.first()) / (mTaps.size() - 1);
    if (avgMs > 0.0) mBpm->setValue(60000.0 / avgMs);
}

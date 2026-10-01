#include "TempoBar.h"

#include "TempoSync.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
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
// TempoBar
// ---------------------------------------------------------------------------

TempoBar::TempoBar(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("tempoBar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(6);

    auto* caption = new QLabel("Tempo", this);
    caption->setObjectName("presetCaption");
    lay->addWidget(caption);

    mBpm = new BpmField(this);
    mBpm->setValue(QSettings().value("tempo/bpm", TempoSync::kDefaultBpm).toDouble());
    connect(mBpm, &BpmField::valueChanged, this, [this](double bpm) {
        QSettings().setValue("tempo/bpm", bpm);
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
    lay->addStretch(1);
}

double TempoBar::bpm() const { return mBpm->value(); }

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

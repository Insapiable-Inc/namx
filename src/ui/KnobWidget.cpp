#include "KnobWidget.h"

#include <algorithm>
#include <cmath>

#include <QContextMenuEvent>
#include <QDial>
#include <QEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRadialGradient>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include "Theme.h"

// ---------------------------------------------------------------------------
// VerticalDial — QDial that maps vertical mouse drag to value changes.
// ---------------------------------------------------------------------------

class VerticalDial : public QDial
{
public:
    explicit VerticalDial(QWidget* parent = nullptr)
        : QDial(parent) {
        setNotchesVisible(false);
        setWrapping(false);
    }

    void setAccent(const QColor& accent) {
        mAccent = accent;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            mDragStartY = e->pos().y();
            mDragStartVal = value();
            e->accept();
        } else {
            QDial::mousePressEvent(e);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) {
            int dy = mDragStartY - e->pos().y(); // up → positive
            int range = maximum() - minimum();
            int newVal = mDragStartVal + (dy * range) / 200;
            setValue(qBound(minimum(), newVal, maximum()));
            e->accept();
        } else {
            QDial::mouseMoveEvent(e);
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        e->accept(); // do not call base (prevents click-to-jump)
    }

    // Let double-click fall through for the eventFilter in KnobWidget
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        QDial::mouseDoubleClickEvent(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Square area, centred.
        const qreal side = std::min(width(), height());
        const QPointF c(width() / 2.0, height() / 2.0);

        // Outer arc geometry: a thin ring on the outside of the body.
        const qreal arcInset = 2.5;
        const qreal arcWidth = 2.5;
        const QRectF arcRect(c.x() - side / 2.0 + arcInset, c.y() - side / 2.0 + arcInset,
                             side - 2 * arcInset, side - 2 * arcInset);

        // Sweep from 7 o'clock clockwise to 5 o'clock = 300° span, starting at 225°.
        const int startAngle = 225 * 16;
        const int sweepTotal = -270 * 16;

        // Faded background track.
        QPen trackPen(Theme::track(), arcWidth);
        trackPen.setCapStyle(Qt::FlatCap);
        p.setPen(trackPen);
        p.drawArc(arcRect, startAngle, sweepTotal);

        // Value arc.
        const QColor accent = mAccent;
        const double range = std::max(1, maximum() - minimum());
        const double t = (value() - minimum()) / range;
        QPen valuePen(accent, arcWidth);
        valuePen.setCapStyle(Qt::RoundCap);
        p.setPen(valuePen);
        p.drawArc(arcRect, startAngle, static_cast<int>(sweepTotal * t));

        // Knob body — neutral flat-ish disc with a subtle top→bottom gradient.
        // The effect accent lives only on the outer value ring and the tick.
        const qreal bodyInset = arcInset + 4.0;
        const QRectF bodyRect(c.x() - side / 2.0 + bodyInset, c.y() - side / 2.0 + bodyInset,
                              side - 2 * bodyInset, side - 2 * bodyInset);

        QLinearGradient body(bodyRect.topLeft(), bodyRect.bottomLeft());
        body.setColorAt(0.0, Theme::knobBodyTop());
        body.setColorAt(1.0, Theme::knobBodyBot());
        p.setBrush(body);
        p.setPen(QPen(Theme::bezel(), 1.0));
        p.drawEllipse(bodyRect);

        // Single thin highlight arc along the top crown — no bottom rim,
        // since duplicating shadow with the dark background fights itself.
        const QRectF crownRect = bodyRect.adjusted(1.5, 1.5, -1.5, -1.5);
        QPen crownPen(QColor(255, 255, 255, 26), 1.0);
        p.setPen(crownPen);
        p.setBrush(Qt::NoBrush);
        // Arc from 30° to 150° (top half).
        p.drawArc(crownRect, 30 * 16, 120 * 16);

        // Indicator tick — the effect accent, matching the value ring.
        // Radii are proportional so the tick stays sane at small dial sizes.
        const double angleDeg = 225.0 - 270.0 * t; // 7 o'clock to 5 o'clock CW
        const double angleRad = qDegreesToRadians(angleDeg);
        const double bodyR = bodyRect.width() / 2.0;
        const double r1 = bodyR - 2.0;
        const double r2 = bodyR * 0.35;
        const QPointF p1(bodyRect.center().x() + r1 * std::cos(angleRad),
                         bodyRect.center().y() - r1 * std::sin(angleRad));
        const QPointF p2(bodyRect.center().x() + r2 * std::cos(angleRad),
                         bodyRect.center().y() - r2 * std::sin(angleRad));
        QPen indicatorPen(mAccent, 2.6);
        indicatorPen.setCapStyle(Qt::RoundCap);
        p.setPen(indicatorPen);
        p.drawLine(p1, p2);
    }

private:
    QColor mAccent{Theme::genericAccent()};
    int mDragStartY{0};
    int mDragStartVal{0};
};

// ---------------------------------------------------------------------------
// KnobWidget
// ---------------------------------------------------------------------------

KnobWidget::KnobWidget(const QString& label, double minVal, double maxVal, double defaultVal,
                       const QString& unit, Form form, QWidget* parent)
    : QWidget(parent)
    , mMin(minVal)
    , mMax(maxVal)
    , mDefault(defaultVal)
    , mUnit(unit)
    , mLabel(label) {
    setAttribute(Qt::WA_StyledBackground, false);

    auto* nameLabel = new QLabel(label);
    {
        QPalette p = nameLabel->palette();
        p.setColor(QPalette::WindowText, Theme::dimText());
        nameLabel->setPalette(p);
    }

    setToolTip(QString("%1: drag or scroll to adjust, double-click to reset").arg(label));

    mDial = new VerticalDial;
    mDial->setMinimum(0);
    mDial->setMaximum(kSteps);
    mDial->setValue(valueToDial(defaultVal));

    mLabelValue = new QLabel;
    {
        QFont f = mLabelValue->font();
        f.setBold(true);
        mLabelValue->setFont(f);
    }

    if (form == Form::Stacked) {
        // Prominent stacked form (Input/Output gain): centred name over a
        // large dial over the value.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        mDial->setFixedSize(64, 64);
        nameLabel->setAlignment(Qt::AlignHCenter);
        mLabelValue->setAlignment(Qt::AlignHCenter);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(2);
        layout->addWidget(nameLabel);
        layout->addWidget(mDial, 0, Qt::AlignHCenter);
        layout->addWidget(mLabelValue);
    } else {
        // Compact row form: dial on the left, name over value on the right.
        // Keeps the dial a usable size while the whole widget stays ~46px
        // tall, so a full effect row fits in ~58px.
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedWidth(142); // uniform tiles — knob columns align across rows
        mDial->setFixedSize(40, 40);
        nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        mLabelValue->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        mLabelValue->setMinimumWidth(64); // steady width while the value changes

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 4, 8, 4);
        layout->setSpacing(7);

        auto* text = new QVBoxLayout;
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(0);
        text->addStretch();
        text->addWidget(nameLabel);
        text->addWidget(mLabelValue);
        text->addStretch();

        layout->addWidget(mDial, 0, Qt::AlignVCenter);
        layout->addLayout(text);
    }

    updateLabel();
    mDial->installEventFilter(this);
    connect(mDial, &QDial::valueChanged, this, &KnobWidget::onDialChanged);
}

void KnobWidget::setAccent(const QColor& accent) {
    mAccent = accent;
    mDial->setAccent(accent);
    updateLabel();
}

double KnobWidget::value() const {
    const double v = dialToValue(mDial->value());
    return mSnap > 0.0 ? qBound(mMin, std::round(v / mSnap) * mSnap, mMax) : v;
}

void KnobWidget::setValue(double v) {
    v = qBound(mMin, v, mMax);
    const bool blocked = mDial->blockSignals(true);
    mDial->setValue(valueToDial(v));
    mDial->blockSignals(blocked);
    updateLabel();
    // Programmatic writes must reach the engine too; the dial's own signal is
    // blocked above, so emit the (dial-quantized) value ourselves.
    mLastEmitted = value();
    emit valueChanged(mLastEmitted);
}

void KnobWidget::resetToDefault() {
    setValue(mDefault);
}

void KnobWidget::onDialChanged(int) {
    const double v = value();
    if (mSnap > 0.0 && v == mLastEmitted) {
        return; // still inside the same step
    }
    mLastEmitted = v;
    updateLabel();
    emit valueChanged(v);
}

void KnobWidget::updateLabel() {
    double v = value();
    QString txt =
        mIntDisplay ? QString::number(static_cast<int>(std::round(v))) : QString::number(v, 'f', 1);
    if (!mUnit.isEmpty()) {
        txt += " " + mUnit;
    }
    if (mFormatter) {
        txt = mFormatter(v);
    }

    QPalette p = mLabelValue->palette();
    p.setColor(QPalette::WindowText, mAccent);
    mLabelValue->setPalette(p);
    mLabelValue->setText(txt);
}

static std::function<void(KnobWidget*, QMenu*)>& menuHook() {
    static std::function<void(KnobWidget*, QMenu*)> hook;
    return hook;
}

void KnobWidget::setMenuHook(std::function<void(KnobWidget*, QMenu*)> hook) {
    menuHook() = std::move(hook);
}

void KnobWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    auto* resetAction = menu.addAction("Reset to Default");
    if (menuHook()) {
        menuHook()(this, &menu);
    }
    if (menu.exec(event->globalPos()) == resetAction) {
        resetToDefault();
    }
}

bool KnobWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == mDial && event->type() == QEvent::MouseButtonDblClick) {
        resetToDefault();
        return true;
    }
    if (obj == mDial && event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        int delta = we->angleDelta().y();
        if (delta == 0) {
            return true;
        }
        if (mSnap > 0.0) {
            setValue(value() + (delta > 0 ? mSnap : -mSnap)); // one step per notch
            return true;
        }
        int dialStep;
        if (mScrollStep > 0.0 && !mLogTaper) {
            dialStep = qMax(1, static_cast<int>(mScrollStep / (mMax - mMin) * kSteps + 0.5));
        } else {
            dialStep = kSteps / 100;
        }
        int newVal = mDial->value() + (delta > 0 ? dialStep : -dialStep);
        mDial->setValue(qBound(0, newVal, kSteps));
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

double KnobWidget::dialToValue(int d) const {
    const double t = static_cast<double>(d) / kSteps;
    if (mLogTaper) {
        return mMin * std::pow(mMax / mMin, t);
    }
    return mMin + (mMax - mMin) * t;
}

int KnobWidget::valueToDial(double v) const {
    double norm = mLogTaper ? std::log(qBound(mMin, v, mMax) / mMin) / std::log(mMax / mMin)
                            : (v - mMin) / (mMax - mMin);
    norm = qBound(0.0, norm, 1.0);
    return static_cast<int>(norm * kSteps + 0.5);
}

void KnobWidget::setLogTaper(bool on) {
    const double v = value();
    mLogTaper = on && mMin > 0.0 && mMax > mMin; // needs a positive range
    const bool blocked = mDial->blockSignals(true);
    mDial->setValue(valueToDial(v));
    mDial->blockSignals(blocked);
    updateLabel();
}

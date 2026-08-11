#include "KnobWidget.h"
#include "Theme.h"

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

#include <algorithm>

// ---------------------------------------------------------------------------
// VerticalDial — QDial that maps vertical mouse drag to value changes.
// ---------------------------------------------------------------------------

class VerticalDial : public QDial
{
public:
    explicit VerticalDial(QWidget* parent = nullptr) : QDial(parent)
    {
        setNotchesVisible(false);
        setWrapping(false);
    }

    void setAccent(const QColor& accent)
    {
        mAccent = accent;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton)
        {
            mDragStartY    = e->pos().y();
            mDragStartVal  = value();
            e->accept();
        }
        else QDial::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (e->buttons() & Qt::LeftButton)
        {
            int dy    = mDragStartY - e->pos().y(); // up → positive
            int range = maximum() - minimum();
            int newVal = mDragStartVal + (dy * range) / 200;
            setValue(qBound(minimum(), newVal, maximum()));
            e->accept();
        }
        else QDial::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        e->accept(); // do not call base (prevents click-to-jump)
    }

    // Let double-click fall through for the eventFilter in KnobWidget
    void mouseDoubleClickEvent(QMouseEvent* e) override { QDial::mouseDoubleClickEvent(e); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Square area, centred.
        const qreal side = std::min(width(), height());
        const QPointF c(width() / 2.0, height() / 2.0);

        // Outer arc geometry: a thin ring on the outside of the body.
        const qreal arcInset  = 3.5;
        const qreal arcWidth  = 2.5;
        const QRectF arcRect(c.x() - side / 2.0 + arcInset,
                             c.y() - side / 2.0 + arcInset,
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
        const qreal bodyInset = arcInset + 6.0;
        const QRectF bodyRect(c.x() - side / 2.0 + bodyInset,
                              c.y() - side / 2.0 + bodyInset,
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
        const double angleDeg = 225.0 - 270.0 * t;  // 7 o'clock to 5 o'clock CW
        const double angleRad = qDegreesToRadians(angleDeg);
        const double r1 = bodyRect.width() / 2.0 - 2.5;
        const double r2 = bodyRect.width() / 2.0 - 11.0;
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

KnobWidget::KnobWidget(const QString& label, double minVal, double maxVal,
                       double defaultVal, const QString& unit, QWidget* parent)
    : QWidget(parent)
    , mMin(minVal)
    , mMax(maxVal)
    , mDefault(defaultVal)
    , mUnit(unit)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 5, 4, 5);
    layout->setSpacing(1);

    auto* nameLabel = new QLabel(label);
    nameLabel->setAlignment(Qt::AlignHCenter);
    {
        QPalette p = nameLabel->palette();
        p.setColor(QPalette::WindowText, Theme::dimText());
        nameLabel->setPalette(p);
    }

    setToolTip(QString("%1 — drag or scroll to adjust, double-click to reset").arg(label));

    mDial = new VerticalDial;
    mDial->setMinimum(0);
    mDial->setMaximum(kSteps);
    mDial->setValue(valueToDial(defaultVal));
    mDial->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    mLabelValue = new QLabel;
    mLabelValue->setAlignment(Qt::AlignHCenter);
    {
        QFont f = mLabelValue->font();
        f.setBold(true);
        mLabelValue->setFont(f);
    }

    layout->addWidget(nameLabel);
    layout->addWidget(mDial, 1);
    layout->addWidget(mLabelValue);

    updateLabel();
    mDial->installEventFilter(this);
    connect(mDial, &QDial::valueChanged, this, &KnobWidget::onDialChanged);
}

void KnobWidget::setAccent(const QColor& accent)
{
    mAccent = accent;
    mDial->setAccent(accent);
    updateLabel();
}

void KnobWidget::paintEvent(QPaintEvent*)
{
    // Subtle recessed tile that visually groups name + dial + value into one
    // unit. Without this the value label below knob A reads as belonging to
    // knob B (the column layout is vertical and tight).
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);

    // Slightly-darker fill than the parent card.
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::well());
    p.drawRoundedRect(r, 5, 5);

    // Top hairline highlight for a hint of depth.
    QPen topRim(QColor(255, 255, 255, 12), 1.0);
    p.setPen(topRim);
    p.drawLine(QPointF(r.left() + 4, r.top() + 0.5),
               QPointF(r.right() - 4, r.top() + 0.5));
}

double KnobWidget::value() const { return dialToValue(mDial->value()); }

void KnobWidget::setValue(double v)
{
    v = qBound(mMin, v, mMax);
    const bool blocked = mDial->blockSignals(true);
    mDial->setValue(valueToDial(v));
    mDial->blockSignals(blocked);
    updateLabel();
    // Programmatic writes must reach the engine too; the dial's own signal is
    // blocked above, so emit the (dial-quantized) value ourselves.
    emit valueChanged(value());
}

void KnobWidget::resetToDefault()
{
    setValue(mDefault);
}

void KnobWidget::onDialChanged(int dialVal)
{
    updateLabel();
    emit valueChanged(dialToValue(dialVal));
}

void KnobWidget::updateLabel()
{
    double v = value();
    QString txt = mIntDisplay
        ? QString::number(static_cast<int>(std::round(v)))
        : QString::number(v, 'f', 1);
    if (!mUnit.isEmpty())
        txt += " " + mUnit;

    QPalette p = mLabelValue->palette();
    p.setColor(QPalette::WindowText, mAccent);
    mLabelValue->setPalette(p);
    mLabelValue->setText(txt);
}

void KnobWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    auto* resetAction = menu.addAction("Reset to Default");
    if (menu.exec(event->globalPos()) == resetAction)
        resetToDefault();
}

bool KnobWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == mDial && event->type() == QEvent::MouseButtonDblClick)
    {
        resetToDefault();
        return true;
    }
    if (obj == mDial && event->type() == QEvent::Wheel)
    {
        auto* we = static_cast<QWheelEvent*>(event);
        int delta = we->angleDelta().y();
        if (delta == 0) return true;
        int dialStep;
        if (mScrollStep > 0.0)
            dialStep = qMax(1, static_cast<int>(mScrollStep / (mMax - mMin) * kSteps + 0.5));
        else
            dialStep = kSteps / 100;
        int newVal = mDial->value() + (delta > 0 ? dialStep : -dialStep);
        mDial->setValue(qBound(0, newVal, kSteps));
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

double KnobWidget::dialToValue(int d) const
{
    return mMin + (mMax - mMin) * static_cast<double>(d) / kSteps;
}

int KnobWidget::valueToDial(double v) const
{
    double norm = (v - mMin) / (mMax - mMin);
    norm = qBound(0.0, norm, 1.0);
    return static_cast<int>(norm * kSteps + 0.5);
}

#include "KnobWidget.h"

#include <QContextMenuEvent>
#include <QDial>
#include <QEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// VerticalDial — QDial that maps vertical mouse drag to value changes.
// ---------------------------------------------------------------------------

class VerticalDial : public QDial
{
public:
    explicit VerticalDial(QWidget* parent = nullptr) : QDial(parent)
    {
        setNotchesVisible(true);
        setWrapping(false);
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

private:
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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(1);

    auto* nameLabel = new QLabel(label);
    nameLabel->setAlignment(Qt::AlignHCenter);

    mDial = new VerticalDial;
    mDial->setMinimum(0);
    mDial->setMaximum(kSteps);
    mDial->setValue(valueToDial(defaultVal));
    mDial->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    mLabelValue = new QLabel;
    mLabelValue->setAlignment(Qt::AlignHCenter);

    layout->addWidget(nameLabel);
    layout->addWidget(mDial, 1);
    layout->addWidget(mLabelValue);

    updateLabel();
    mDial->installEventFilter(this);
    connect(mDial, &QDial::valueChanged, this, &KnobWidget::onDialChanged);
}

double KnobWidget::value() const { return dialToValue(mDial->value()); }

void KnobWidget::setValue(double v)
{
    v = qBound(mMin, v, mMax);
    const bool blocked = mDial->blockSignals(true);
    mDial->setValue(valueToDial(v));
    mDial->blockSignals(blocked);
    updateLabel();
}

void KnobWidget::resetToDefault()
{
    setValue(mDefault);
    emit valueChanged(mDefault);
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

    // Use theme highlight color for the value text via palette (Breeze-compatible)
    QPalette p = mLabelValue->palette();
    p.setColor(QPalette::WindowText, palette().color(QPalette::Highlight));
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

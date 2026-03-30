#include "KnobWidget.h"

#include <QDial>
#include <QEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <QString>

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
    layout->setSpacing(2);

    auto* nameLabel = new QLabel(label);
    nameLabel->setAlignment(Qt::AlignHCenter);
    nameLabel->setStyleSheet("color: #aaa; font-size: 8pt;");

    mDial = new QDial;
    mDial->setMinimum(0);
    mDial->setMaximum(kSteps);
    mDial->setValue(valueToDial(defaultVal));
    mDial->setNotchesVisible(true);
    mDial->setWrapping(false);
    mDial->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    mLabelValue = new QLabel;
    mLabelValue->setAlignment(Qt::AlignHCenter);
    mLabelValue->setStyleSheet("color: #dc7828; font-size: 7pt; font-family: monospace;");

    layout->addWidget(nameLabel);
    layout->addWidget(mDial, 1);
    layout->addWidget(mLabelValue);

    // Update value display
    onDialChanged(mDial->value());

    connect(mDial, &QDial::valueChanged, this, &KnobWidget::onDialChanged);

    // Double-click to reset — install on the dial
    mDial->installEventFilter(this);
}

double KnobWidget::value() const
{
    return dialToValue(mDial->value());
}

void KnobWidget::setValue(double v)
{
    // Clamp
    if (v < mMin) v = mMin;
    if (v > mMax) v = mMax;

    // Block signals so we don't re-emit valueChanged
    const bool blocked = mDial->blockSignals(true);
    mDial->setValue(valueToDial(v));
    mDial->blockSignals(blocked);

    // Update display
    QString txt = QString::number(v, 'f', 1);
    if (!mUnit.isEmpty()) txt += " " + mUnit;
    mLabelValue->setText(txt);
}

void KnobWidget::resetToDefault()
{
    setValue(mDefault);
    emit valueChanged(mDefault);
}

void KnobWidget::onDialChanged(int dialVal)
{
    double v = dialToValue(dialVal);
    QString txt = QString::number(v, 'f', 1);
    if (!mUnit.isEmpty()) txt += " " + mUnit;
    mLabelValue->setText(txt);
    emit valueChanged(v);
}

double KnobWidget::dialToValue(int d) const
{
    return mMin + (mMax - mMin) * static_cast<double>(d) / kSteps;
}

int KnobWidget::valueToDial(double v) const
{
    double norm = (v - mMin) / (mMax - mMin);
    if (norm < 0.0) norm = 0.0;
    if (norm > 1.0) norm = 1.0;
    return static_cast<int>(norm * kSteps + 0.5);
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

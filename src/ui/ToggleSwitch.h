#pragma once

#include <QAbstractButton>
#include <QColor>

/// iOS-style toggle switch.
/// Drop-in replacement for QCheckBox: same isChecked(), setChecked(), toggled() API.
/// The "on" track color is set explicitly via setAccent (Theme::tagCard) —
/// the app stylesheet disables palette propagation, so QPalette::Highlight
/// can't carry it.
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    void setAccent(const QColor& c);

    QSize sizeHint()        const override { return {42, 22}; }
    QSize minimumSizeHint() const override { return {42, 22}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QColor mAccent;
    bool   mHovered{false};
};

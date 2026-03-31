#pragma once

#include <QAbstractButton>

/// iOS-style toggle switch.
/// Drop-in replacement for QCheckBox: same isChecked(), setChecked(), toggled() API.
/// Uses QPalette::Highlight for the "on" track color.
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    QSize sizeHint()        const override { return {42, 22}; }
    QSize minimumSizeHint() const override { return {42, 22}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    bool mHovered{false};
};

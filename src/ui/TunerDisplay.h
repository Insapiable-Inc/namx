#pragma once

#include <QString>
#include <QWidget>

/// Boss-TU-3-inspired tuner readout: a horizontal LED ladder (the center LED
/// glows green when in tune; red LEDs march from the center toward flat/left
/// or sharp/right as the pitch drifts), the note name rendered as a glowing
/// seven-segment digit with a sharp marker, and a signed cents readout.
class TunerDisplay : public QWidget
{
    Q_OBJECT
public:
    explicit TunerDisplay(QWidget* parent = nullptr);

    /// note is the full name from the tuner ("A#2" — only the letter and the
    /// sharp are shown); cents is -50..+50; valid=false blanks the display.
    void setReading(const QString& note, float cents, bool valid);

    QSize sizeHint()        const override { return {380, 48}; }
    QSize minimumSizeHint() const override { return {260, 48}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString mNote{"-"};
    float   mCents{0.0f};
    bool    mValid{false};

    static constexpr int   kLedCount    = 21;   // odd, so one LED is dead center
    static constexpr float kInTuneCents = 3.0f; // |cents| at or below: green
};

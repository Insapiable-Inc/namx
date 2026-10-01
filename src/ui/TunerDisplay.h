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
    /// sharp are shown); cents is -50..+50; hz is the detected frequency;
    /// valid=false blanks the display.
    void set_reading(const QString& new_note, float new_cents, bool new_valid, float new_hz = 0.0f);
    /// Left of the note: the detected frequency (on) or the A4 reference (off).
    void set_show_hz(bool on);
    void set_reference(int new_hz);

    QSize sizeHint() const override {
        return {380, 48};
    }
    QSize minimumSizeHint() const override {
        return {260, 48};
    }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_note{"-"};
    float m_cents{0.0f};
    bool m_valid{false};
    float m_hz{0.0f};
    bool m_show_hz{false};
    int m_reference{440};

    static constexpr int K_LED_COUNT = 21; // odd, so one LED is dead center
    static constexpr float K_IN_TUNE_CENTS = 3.0f; // |cents| at or below: green
};

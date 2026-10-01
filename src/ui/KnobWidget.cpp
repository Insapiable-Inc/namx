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

    void set_accent(const QColor& accent) {
        m_accent = accent;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_drag_start_y = e->pos().y();
            m_drag_start_val = value();
            e->accept();
        } else {
            QDial::mousePressEvent(e);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) {
            int dy = m_drag_start_y - e->pos().y(); // up → positive
            int range = maximum() - minimum();
            int new_val = m_drag_start_val + (dy * range) / 200;
            setValue(qBound(minimum(), new_val, maximum()));
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
        const qreal arc_inset = 2.5;
        const qreal arc_width = 2.5;
        const QRectF arc_rect(c.x() - side / 2.0 + arc_inset, c.y() - side / 2.0 + arc_inset,
                              side - 2 * arc_inset, side - 2 * arc_inset);

        // Sweep from 7 o'clock clockwise to 5 o'clock = 300° span, starting at 225°.
        const int start_angle = 225 * 16;
        const int sweep_total = -270 * 16;

        // Faded background track.
        QPen track_pen(Theme::track(), arc_width);
        track_pen.setCapStyle(Qt::FlatCap);
        p.setPen(track_pen);
        p.drawArc(arc_rect, start_angle, sweep_total);

        // Value arc.
        const QColor accent = m_accent;
        const double range = std::max(1, maximum() - minimum());
        const double t = (value() - minimum()) / range;
        QPen value_pen(accent, arc_width);
        value_pen.setCapStyle(Qt::RoundCap);
        p.setPen(value_pen);
        p.drawArc(arc_rect, start_angle, static_cast<int>(sweep_total * t));

        // Knob body — neutral flat-ish disc with a subtle top→bottom gradient.
        // The effect accent lives only on the outer value ring and the tick.
        const qreal body_inset = arc_inset + 4.0;
        const QRectF body_rect(c.x() - side / 2.0 + body_inset, c.y() - side / 2.0 + body_inset,
                               side - 2 * body_inset, side - 2 * body_inset);

        QLinearGradient body(body_rect.topLeft(), body_rect.bottomLeft());
        body.setColorAt(0.0, Theme::knob_body_top());
        body.setColorAt(1.0, Theme::knob_body_bot());
        p.setBrush(body);
        p.setPen(QPen(Theme::bezel(), 1.0));
        p.drawEllipse(body_rect);

        // Single thin highlight arc along the top crown — no bottom rim,
        // since duplicating shadow with the dark background fights itself.
        const QRectF crown_rect = body_rect.adjusted(1.5, 1.5, -1.5, -1.5);
        QPen crown_pen(QColor(255, 255, 255, 26), 1.0);
        p.setPen(crown_pen);
        p.setBrush(Qt::NoBrush);
        // Arc from 30° to 150° (top half).
        p.drawArc(crown_rect, 30 * 16, 120 * 16);

        // Indicator tick — the effect accent, matching the value ring.
        // Radii are proportional so the tick stays sane at small dial sizes.
        const double angle_deg = 225.0 - 270.0 * t; // 7 o'clock to 5 o'clock CW
        const double angle_rad = qDegreesToRadians(angle_deg);
        const double body_r = body_rect.width() / 2.0;
        const double r1 = body_r - 2.0;
        const double r2 = body_r * 0.35;
        const QPointF p1(body_rect.center().x() + r1 * std::cos(angle_rad),
                         body_rect.center().y() - r1 * std::sin(angle_rad));
        const QPointF p2(body_rect.center().x() + r2 * std::cos(angle_rad),
                         body_rect.center().y() - r2 * std::sin(angle_rad));
        QPen indicator_pen(m_accent, 2.6);
        indicator_pen.setCapStyle(Qt::RoundCap);
        p.setPen(indicator_pen);
        p.drawLine(p1, p2);
    }

private:
    QColor m_accent{Theme::generic_accent()};
    int m_drag_start_y{0};
    int m_drag_start_val{0};
};

// ---------------------------------------------------------------------------
// KnobWidget
// ---------------------------------------------------------------------------

KnobWidget::KnobWidget(const QString& label, double min_val, double max_val, double default_val,
                       const QString& unit, Form form, QWidget* parent)
    : QWidget(parent)
    , m_min(min_val)
    , m_max(max_val)
    , m_default(default_val)
    , m_unit(unit)
    , m_label(label) {
    setAttribute(Qt::WA_StyledBackground, false);

    auto* name_label = new QLabel(label);
    {
        QPalette p = name_label->palette();
        p.setColor(QPalette::WindowText, Theme::dim_text());
        name_label->setPalette(p);
    }

    setToolTip(QString("%1: drag or scroll to adjust, double-click to reset").arg(label));

    m_dial = new VerticalDial;
    m_dial->setMinimum(0);
    m_dial->setMaximum(K_STEPS);
    m_dial->setValue(value_to_dial(default_val));

    m_label_value = new QLabel;
    {
        QFont f = m_label_value->font();
        f.setBold(true);
        m_label_value->setFont(f);
    }

    if (form == Form::STACKED) {
        // Prominent stacked form (Input/Output gain): centred name over a
        // large dial over the value.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_dial->setFixedSize(64, 64);
        name_label->setAlignment(Qt::AlignHCenter);
        m_label_value->setAlignment(Qt::AlignHCenter);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(2);
        layout->addWidget(name_label);
        layout->addWidget(m_dial, 0, Qt::AlignHCenter);
        layout->addWidget(m_label_value);
    } else {
        // Compact row form: dial on the left, name over value on the right.
        // Keeps the dial a usable size while the whole widget stays ~46px
        // tall, so a full effect row fits in ~58px.
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedWidth(142); // uniform tiles — knob columns align across rows
        m_dial->setFixedSize(40, 40);
        name_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_label_value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_label_value->setMinimumWidth(64); // steady width while the value changes

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 4, 8, 4);
        layout->setSpacing(7);

        auto* text = new QVBoxLayout;
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(0);
        text->addStretch();
        text->addWidget(name_label);
        text->addWidget(m_label_value);
        text->addStretch();

        layout->addWidget(m_dial, 0, Qt::AlignVCenter);
        layout->addLayout(text);
    }

    update_label();
    m_dial->installEventFilter(this);
    connect(m_dial, &QDial::valueChanged, this, &KnobWidget::on_dial_changed);
}

void KnobWidget::set_accent(const QColor& accent) {
    m_accent = accent;
    m_dial->set_accent(accent);
    update_label();
}

double KnobWidget::value() const {
    const double v = dial_to_value(m_dial->value());
    return m_snap > 0.0 ? qBound(m_min, std::round(v / m_snap) * m_snap, m_max) : v;
}

void KnobWidget::set_value(double v) {
    v = qBound(m_min, v, m_max);
    const bool blocked = m_dial->blockSignals(true);
    m_dial->setValue(value_to_dial(v));
    m_dial->blockSignals(blocked);
    update_label();
    // Programmatic writes must reach the engine too; the dial's own signal is
    // blocked above, so emit the (dial-quantized) value ourselves.
    m_last_emitted = value();
    emit value_changed(m_last_emitted);
}

void KnobWidget::reset_to_default() {
    set_value(m_default);
}

void KnobWidget::on_dial_changed(int) {
    const double v = value();
    if (m_snap > 0.0 && v == m_last_emitted) {
        return; // still inside the same step
    }
    m_last_emitted = v;
    update_label();
    emit value_changed(v);
}

void KnobWidget::update_label() {
    double v = value();
    QString txt = m_int_display ? QString::number(static_cast<int>(std::round(v)))
                                : QString::number(v, 'f', 1);
    if (!m_unit.isEmpty()) {
        txt += " " + m_unit;
    }
    if (m_formatter) {
        txt = m_formatter(v);
    }

    QPalette p = m_label_value->palette();
    p.setColor(QPalette::WindowText, m_accent);
    m_label_value->setPalette(p);
    m_label_value->setText(txt);
}

static std::function<void(KnobWidget*, QMenu*)>& menu_hook() {
    static std::function<void(KnobWidget*, QMenu*)> hook;
    return hook;
}

void KnobWidget::set_menu_hook(std::function<void(KnobWidget*, QMenu*)> hook) {
    menu_hook() = std::move(hook);
}

void KnobWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    auto* reset_action = menu.addAction("Reset to Default");
    if (menu_hook()) {
        menu_hook()(this, &menu);
    }
    if (menu.exec(event->globalPos()) == reset_action) {
        reset_to_default();
    }
}

bool KnobWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_dial && event->type() == QEvent::MouseButtonDblClick) {
        reset_to_default();
        return true;
    }
    if (obj == m_dial && event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        int delta = we->angleDelta().y();
        if (delta == 0) {
            return true;
        }
        if (m_snap > 0.0) {
            set_value(value() + (delta > 0 ? m_snap : -m_snap)); // one step per notch
            return true;
        }
        int dial_step;
        if (m_scroll_step > 0.0 && !m_log_taper) {
            dial_step = qMax(1, static_cast<int>(m_scroll_step / (m_max - m_min) * K_STEPS + 0.5));
        } else {
            dial_step = K_STEPS / 100;
        }
        int new_val = m_dial->value() + (delta > 0 ? dial_step : -dial_step);
        m_dial->setValue(qBound(0, new_val, K_STEPS));
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

double KnobWidget::dial_to_value(int d) const {
    const double t = static_cast<double>(d) / K_STEPS;
    if (m_log_taper) {
        return m_min * std::pow(m_max / m_min, t);
    }
    return m_min + (m_max - m_min) * t;
}

int KnobWidget::value_to_dial(double v) const {
    double norm = m_log_taper ? std::log(qBound(m_min, v, m_max) / m_min) / std::log(m_max / m_min)
                              : (v - m_min) / (m_max - m_min);
    norm = qBound(0.0, norm, 1.0);
    return static_cast<int>(norm * K_STEPS + 0.5);
}

void KnobWidget::set_log_taper(bool on) {
    const double v = value();
    m_log_taper = on && m_min > 0.0 && m_max > m_min; // needs a positive range
    const bool blocked = m_dial->blockSignals(true);
    m_dial->setValue(value_to_dial(v));
    m_dial->blockSignals(blocked);
    update_label();
}

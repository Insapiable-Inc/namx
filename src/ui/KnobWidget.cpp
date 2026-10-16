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
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include "Common.h"
#include "Theme.h"

namespace namx
{

class VerticalDial : public QDial
{
public:
    explicit VerticalDial(QWidget* parent = nullptr)
        : QDial(parent) {
        setNotchesVisible(false);
        setWrapping(false);
    }

    void set_accent(const QColor& new_accent) {
        accent = new_accent;
        update();
    }

    // Discrete knobs (`positions` ≥ 2) draw one dot per position on the ring, the
    // current one lit, instead of a value arc: a switch, not a sweep. 0 = continuous.
    void set_positions(int n) {
        positions = n;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            drag_start_y = e->pos().y();
            drag_start_val = value();
            e->accept();
        } else {
            QDial::mousePressEvent(e);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) {
            int dy = drag_start_y - e->pos().y(); // up → positive
            int range = maximum() - minimum();
            int new_val = drag_start_val + (dy * range) / 200;
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
        const qreal arc_width = 3.0;
        const QRectF arc_rect(c.x() - side / 2.0 + arc_inset, c.y() - side / 2.0 + arc_inset,
                              side - 2 * arc_inset, side - 2 * arc_inset);

        // Sweep from 7 o'clock clockwise to 5 o'clock = 300° span, starting at 225°.
        const int start_angle = 225 * 16;
        const int sweep_total = -270 * 16;

        const double range = std::max(1, maximum() - minimum());
        double t = (value() - minimum()) / range;
        if (positions >= 2) {
            // Stepped: a dot per position along the 270° sweep, the current one lit
            // and larger; the pointer sits exactly on a position.
            const int current = static_cast<int>(std::lround(t * (positions - 1)));
            t = static_cast<double>(current) / (positions - 1);
            const double ring_r = arc_rect.width() / 2.0;
            const double dot_r = positions > 12 ? 1.3 : 2.0;
            p.setPen(Qt::NoPen);
            for (int i = 0; i < positions; ++i) {
                const double a = qDegreesToRadians(225.0 - 270.0 * i / (positions - 1));
                const QPointF dot(c.x() + ring_r * std::cos(a), c.y() - ring_r * std::sin(a));
                p.setBrush(i == current ? accent : Theme::detent());
                const double r = i == current ? dot_r + 0.9 : dot_r;
                p.drawEllipse(dot, r, r);
            }
        } else {
            // Faded background track.
            QPen track_pen(Theme::track(), arc_width);
            track_pen.setCapStyle(Qt::RoundCap);
            p.setPen(track_pen);
            p.drawArc(arc_rect, start_angle, sweep_total);

            // Value arc.
            QPen value_pen(accent, arc_width);
            value_pen.setCapStyle(Qt::RoundCap);
            p.setPen(value_pen);
            p.drawArc(arc_rect, start_angle, static_cast<int>(sweep_total * t));
        }

        // Knob body: a flat neutral disc (no gradient, highlight or bezel). The effect
        // accent lives only on the outer value ring and the tick.
        const qreal body_inset = arc_inset + 4.5;
        QRectF body_rect(c.x() - side / 2.0 + body_inset, c.y() - side / 2.0 + body_inset,
                         side - 2 * body_inset, side - 2 * body_inset);
        p.setBrush(Theme::knob_face());
        const QColor edge = Theme::knob_edge();
        if (edge.isValid()) {
            p.setPen(QPen(edge, 1.0)); // a light disc on a light card needs an edge
            body_rect.adjust(0.5, 0.5, -0.5, -0.5);
        } else {
            p.setPen(Qt::NoPen);
        }
        p.drawEllipse(body_rect);

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
        QPen indicator_pen(accent, 2.6);
        indicator_pen.setCapStyle(Qt::RoundCap);
        p.setPen(indicator_pen);
        p.drawLine(p1, p2);
    }

private:
    QColor accent{Theme::generic_accent()};
    int positions{0};
    int drag_start_y{0};
    int drag_start_val{0};
};

KnobWidget::KnobWidget(const QString& new_label, double min_val, double max_val, double default_val,
                       const QString& new_unit, Form form, QWidget* parent)
    : QWidget(parent)
    , min(min_val)
    , max(max_val)
    , default_value(default_val)
    , unit(new_unit)
    , label(new_label) {
    setAttribute(Qt::WA_StyledBackground, false);

    name_label = new QLabel(new_label);
    {
        QPalette p = name_label->palette();
        p.setColor(QPalette::WindowText, Theme::dim_text());
        name_label->setPalette(p);
    }

    setToolTip(QString("%1: drag or scroll to adjust, double-click to reset").arg(new_label));

    dial = new VerticalDial;
    dial->setMinimum(0);
    dial->setMaximum(STEPS);
    dial->setValue(value_to_dial(default_val));

    label_value = new QLabel;
    {
        QFont f = label_value->font();
        f.setBold(true);
        label_value->setFont(f);
    }

    if (form == Form::STACKED) {
        // Prominent stacked form (Input/Output gain): centred name over a
        // large dial over the value.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        dial->setFixedSize(64, 64);
        name_label->setAlignment(Qt::AlignHCenter);
        label_value->setAlignment(Qt::AlignHCenter);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(2);
        layout->addWidget(name_label);
        layout->addWidget(dial, 0, Qt::AlignHCenter);
        layout->addWidget(label_value);
    } else {
        // Compact row form: dial on the left, name over value on the right.
        // Keeps the dial a usable size while the whole widget stays ~46px
        // tall, so a full effect row fits in ~58px.
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedWidth(142); // uniform tiles — knob columns align across rows
        dial->setFixedSize(40, 40);
        name_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label_value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label_value->setMinimumWidth(64); // steady width while the value changes

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 4, 8, 4);
        layout->setSpacing(7);

        auto* text = new QVBoxLayout;
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(0);
        text->addStretch();
        text->addWidget(name_label);
        text->addWidget(label_value);
        text->addStretch();

        layout->addWidget(dial, 0, Qt::AlignVCenter);
        layout->addLayout(text);
    }

    update_label();
    dial->installEventFilter(this);
    connect(dial, &QDial::valueChanged, this, &KnobWidget::on_dial_changed);
}

void KnobWidget::set_snap(double step) {
    snap = step;
    const int positions = step > 0.0 ? static_cast<int>(std::lround((max - min) / step)) + 1 : 0;
    dial->set_positions(positions >= 2 && positions <= MAX_DRAWN_POSITIONS ? positions : 0);
    update_label();
}

void KnobWidget::set_accent(const QColor& new_accent) {
    accent = new_accent;
    dial->set_accent(new_accent);
    update_label();
}

double KnobWidget::value() const {
    const double v = dial_to_value(dial->value());
    return snap > 0.0 ? qBound(min, std::round(v / snap) * snap, max) : v;
}

void KnobWidget::set_value(double v) {
    v = qBound(min, v, max);
    const bool blocked = dial->blockSignals(true);
    dial->setValue(value_to_dial(v));
    dial->blockSignals(blocked);
    update_label();
    // Programmatic writes must reach the engine too; the dial's own signal is
    // blocked above, so emit the (dial-quantized) value ourselves.
    last_emitted = value();
    emit value_changed(last_emitted);
}

void KnobWidget::reset_to_default() {
    set_value(default_value);
}

void KnobWidget::on_dial_changed(int) {
    const double v = value();
    if (snap > 0.0 && v == last_emitted) {
        return; // still inside the same step
    }
    last_emitted = v;
    update_label();
    emit value_changed(v);
}

void KnobWidget::set_name_visible(bool on) {
    name_label->setVisible(on);
}

void KnobWidget::update_label() {
    double v = value();
    QString txt =
        int_display ? QString::number(static_cast<int>(std::round(v))) : QString::number(v, 'f', 1);
    if (!unit.isEmpty()) {
        txt += " " + unit;
    }
    if (formatter) {
        txt = formatter(v);
    }

    QPalette p = label_value->palette();
    p.setColor(QPalette::WindowText, accent);
    label_value->setPalette(p);
    label_value->setText(txt);
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
    QAction* reset_action = menu.addAction("Reset to Default");
    if (menu_hook()) {
        menu_hook()(this, &menu);
    }
    if (menu.exec(event->globalPos()) == reset_action) {
        reset_to_default();
    }
}

bool KnobWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == dial && event->type() == QEvent::MouseButtonDblClick) {
        reset_to_default();
        return true;
    }
    if (obj == dial && event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        int delta = we->angleDelta().y();
        if (delta == 0) {
            return true;
        }
        if (snap > 0.0) {
            set_value(value() + (delta > 0 ? snap : -snap)); // one step per notch
            return true;
        }
        int dial_step;
        if (scroll_step > 0.0 && !log_taper) {
            dial_step = qMax(1, static_cast<int>(scroll_step / (max - min) * STEPS + 0.5));
        } else {
            dial_step = STEPS / 100;
        }
        int new_val = dial->value() + (delta > 0 ? dial_step : -dial_step);
        dial->setValue(qBound(0, new_val, STEPS));
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

double KnobWidget::dial_to_value(int d) const {
    const double t = static_cast<double>(d) / STEPS;
    if (log_taper) {
        return min * std::pow(max / min, t);
    }
    return min + (max - min) * t;
}

int KnobWidget::value_to_dial(double v) const {
    double norm = log_taper ? std::log(qBound(min, v, max) / min) / std::log(max / min)
                            : (v - min) / (max - min);
    norm = qBound(0.0, norm, 1.0);
    return static_cast<int>(norm * STEPS + 0.5);
}

void KnobWidget::set_log_taper(bool on) {
    const double v = value();
    log_taper = on && min > 0.0 && max > min; // needs a positive range
    const bool blocked = dial->blockSignals(true);
    dial->setValue(value_to_dial(v));
    dial->blockSignals(blocked);
    update_label();
}

} // namespace namx

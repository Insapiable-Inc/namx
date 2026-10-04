#include "NumberField.h"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "Theme.h"

namespace namx
{

static constexpr qreal NUMBER_PT = 12.0;
static constexpr qreal LABEL_PT = 7.5;
static constexpr int PAD = 7; // inner margin, both sides
static constexpr int GAP = 4; // between the number and its unit

static QFont sized(QFont f, qreal pt, bool bold) {
    f.setPointSizeF(pt);
    f.setBold(bold);
    return f;
}

NumberField::NumberField(const Spec& new_spec, QWidget* parent)
    : QWidget(parent)
    , spec(new_spec)
    , current(new_spec.minimum) {
    setObjectName("numberField");
    setFixedSize(sizeHint());
    setCursor(Qt::SizeVerCursor);
    setAttribute(Qt::WA_Hover); // border brightens under the mouse
}

QSize NumberField::sizeHint() const {
    const QFontMetrics number_fm(sized(font(), NUMBER_PT, true));
    const QFontMetrics label_fm(sized(font(), LABEL_PT, false));
    const QString widest = QString::number(spec.maximum, 'f', spec.decimals);
    int w = 2 * PAD + number_fm.horizontalAdvance(widest);
    if (!spec.unit.isEmpty()) {
        w += GAP + label_fm.horizontalAdvance(spec.unit);
    }
    return {std::max(w, 72), 30};
}

void NumberField::set_value(double new_value) {
    const double scale = std::pow(10.0, spec.decimals);
    new_value = std::round(std::clamp(new_value, spec.minimum, spec.maximum) * scale) / scale;
    if (new_value == current) {
        return;
    }
    current = new_value;
    update();
    emit value_changed(current);
}

// Whole values without decimals: "120", but "120.5".
QString NumberField::number_text() const {
    return QString::number(current, 'f', std::fmod(current, 1.0) == 0.0 ? 0 : spec.decimals);
}

void NumberField::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(
        QPen(underMouse() || dragging ? Theme::track_hover().lighter(130) : Theme::track(), 1.0));
    p.setBrush(Theme::well());
    p.drawRoundedRect(r, 5, 5);
    if (edit) {
        return; // the line edit draws the text while typing
    }

    const QFont label_font = sized(font(), LABEL_PT, false);
    const QRectF inner = r.adjusted(PAD, 0, -PAD, 0);
    p.setFont(label_font);
    p.setPen(Theme::dim_text());
    qreal right = inner.right();
    if (!spec.unit.isEmpty()) {
        p.drawText(inner, Qt::AlignRight | Qt::AlignVCenter, spec.unit);
        right -= QFontMetrics(label_font).horizontalAdvance(spec.unit) + GAP;
    }
    p.setFont(sized(font(), NUMBER_PT, true));
    p.setPen(Theme::generic_accent());
    p.drawText(QRectF(inner.left(), inner.top(), right - inner.left(), inner.height()),
               Qt::AlignRight | Qt::AlignVCenter, number_text());
}

void NumberField::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton || edit) {
        return QWidget::mousePressEvent(ev);
    }
    pressed = true;
    dragging = false;
    press_y = ev->position().toPoint().y();
    press_value = current;
}

void NumberField::mouseMoveEvent(QMouseEvent* ev) {
    if (!pressed) {
        return;
    }
    const int dy = press_y - ev->position().toPoint().y();
    if (!dragging && std::abs(dy) < 3) {
        return;
    }
    dragging = true;
    const double per_px = spec.step / ((ev->modifiers() & Qt::ShiftModifier) ? 40.0 : 4.0);
    set_value(press_value + dy * per_px);
}

void NumberField::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton || !pressed) {
        return;
    }
    pressed = false;
    if (!dragging) {
        begin_edit(); // a click without a drag: type a value
    }
    dragging = false;
    update();
}

void NumberField::wheelEvent(QWheelEvent* ev) {
    const int d = ev->angleDelta().y();
    if (d == 0) {
        return;
    }
    const double step = (ev->modifiers() & Qt::ShiftModifier) ? spec.step / 10.0 : spec.step;
    set_value(current + (d > 0 ? step : -step));
}

void NumberField::begin_edit() {
    edit = new QLineEdit(number_text(), this);
    edit->setObjectName("numberEdit");
    edit->setGeometry(rect().adjusted(2, 2, -2, -2));
    edit->setAlignment(Qt::AlignCenter);
    edit->selectAll();
    edit->installEventFilter(this);
    edit->show();
    edit->setFocus();
    connect(edit, &QLineEdit::returnPressed, this, [this] {
        end_edit(true);
    });
    connect(edit, &QLineEdit::editingFinished, this, [this] {
        end_edit(true);
    });
    update();
}

bool NumberField::eventFilter(QObject* watched, QEvent* ev) {
    if (watched == edit && ev->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(ev)->key() == Qt::Key_Escape) {
        end_edit(false);
        return true;
    }
    return QWidget::eventFilter(watched, ev);
}

void NumberField::end_edit(bool commit) {
    if (!edit) {
        return;
    }
    QLineEdit* e = edit;
    edit = nullptr;
    bool ok = false;
    const double v = e->text().replace(',', '.').toDouble(&ok);
    e->deleteLater();
    if (commit && ok) {
        set_value(v);
    }
    update();
}

} // namespace namx

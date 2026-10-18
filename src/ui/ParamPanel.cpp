#include "ParamPanel.h"

#include <algorithm>
#include <cmath>

#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "GraphModel.h"
#include "KnobWidget.h"
#include "NodeCatalog.h"
#include "NodeIcons.h"
#include "ReverbProcessor.h"
#include "TempoSync.h"
#include "Theme.h"
#include "ToggleSwitch.h"
#include "VUMeter.h"

namespace namx
{

namespace
{

QString theme_id(const NodeType& t) {
    return t.id == "input" || t.id == "output" ? QString("neutral") : QString::fromStdString(t.id);
}

// Field texts carry no unit: it is in the knob's label ("Low Cut (Hz)").
QString format_hz(double hz) {
    return QString::number(qRound(hz));
}

// The unit the field's number is in, for the label: the stored one, except where the
// Format shows something else (Decay in seconds).
QString display_unit(const ParamSpec& p) {
    switch (p.format) {
        case ParamSpec::Format::DECAY:
            return "s";
        case ParamSpec::Format::SEMITONES:
            return "st";
        case ParamSpec::Format::NUMBER:
        case ParamSpec::Format::HZ:
        case ParamSpec::Format::HZ_OFF_LOW:
        case ParamSpec::Format::HZ_OFF_HIGH:
            break;
    }
    return QString::fromStdString(p.unit);
}

// The value as the parameter's Format shows it; {} = KnobWidget's own number + unit.
std::function<QString(double)> formatter_for(const ParamSpec& p) {
    switch (p.format) {
        case ParamSpec::Format::HZ_OFF_LOW: {
            const double off = p.min + 0.5;
            return [off](double v) {
                return v <= off ? QString("Off") : format_hz(v);
            };
        }
        case ParamSpec::Format::HZ_OFF_HIGH: {
            const double off = p.max - 1.0;
            return [off](double v) {
                return v >= off ? QString("Off") : format_hz(v);
            };
        }
        case ParamSpec::Format::SEMITONES:
            return [](double v) {
                const double tenths = std::round(v * 10.0) / 10.0;
                if (tenths == 0.0) {
                    return QString("0");
                }
                const bool whole = std::fmod(tenths, 1.0) == 0.0;
                return QString("%1%2")
                    .arg(tenths > 0 ? "+" : "")
                    .arg(tenths, 0, 'f', whole ? 0 : 1);
            };
        case ParamSpec::Format::DECAY:
            return [](double v) {
                const double s = ReverbProcessor::decay_seconds(static_cast<float>(v));
                return QString::number(s, 'f', s < 1.0 ? 2 : 1);
            };
        case ParamSpec::Format::HZ:
            return [](double v) {
                return format_hz(v);
            };
        case ParamSpec::Format::NUMBER:
            break;
    }
    return {};
}

// A typed value back to the parameter's own units, where its Format shows something
// else (Off, seconds); {} = KnobWidget's default (the first number, k = ×1000 Hz).
std::function<std::optional<double>(const QString&)> parser_for(const ParamSpec& p) {
    auto number = [](const QString& text) -> std::optional<double> {
        static const QRegularExpression re(R"([-+]?\d*[.,]?\d+)");
        const QRegularExpressionMatch m = re.match(text);
        bool ok = false;
        const double v = m.hasMatch() ? m.captured().replace(',', '.').toDouble(&ok) : 0.0;
        return ok ? std::optional<double>(v) : std::nullopt;
    };
    switch (p.format) {
        case ParamSpec::Format::HZ_OFF_LOW:
        case ParamSpec::Format::HZ_OFF_HIGH: {
            const double off = p.format == ParamSpec::Format::HZ_OFF_LOW ? p.min : p.max;
            return [number, off](const QString& text) -> std::optional<double> {
                if (text.contains("off", Qt::CaseInsensitive)) {
                    return off;
                }
                const auto v = number(text);
                return v && text.contains('k', Qt::CaseInsensitive) ? *v * 1000.0 : v;
            };
        }
        case ParamSpec::Format::DECAY:
            // Seconds back to the 0–100 position: decay_seconds rises with it.
            return [number, lo = p.min, hi = p.max](const QString& text) -> std::optional<double> {
                const auto secs = number(text);
                if (!secs) {
                    return std::nullopt;
                }
                double a = lo;
                double b = hi;
                for (int i = 0; i < 40; ++i) {
                    const double mid = (a + b) / 2.0;
                    (ReverbProcessor::decay_seconds(static_cast<float>(mid)) < *secs ? a : b) = mid;
                }
                return (a + b) / 2.0;
            };
        case ParamSpec::Format::NUMBER:
        case ParamSpec::Format::HZ:
        case ParamSpec::Format::SEMITONES:
            break;
    }
    return {};
}

// The node's icon in its accent box (header).
class IconBox : public QWidget
{
public:
    IconBox(QString new_type, QString new_category, QColor new_accent)
        : type(std::move(new_type))
        , category(std::move(new_category))
        , accent(new_accent) {
        setFixedSize(52, 52);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor tint = accent;
        tint.setAlpha(40);
        p.setPen(QPen(accent, 1.4));
        p.setBrush(tint);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.drawRoundedRect(r, 8, 8);
        paint_node_icon(p, r, type, category, accent);
    }

private:
    QString type;
    QString category;
    QColor accent;
};

QLabel* caption(const QString& text) {
    auto* l = new QLabel(text.toUpper());
    l->setObjectName("panelCaption");
    return l;
}

} // namespace

ParamPanel::ParamPanel(GraphModel& new_model, LibraryIndex& new_library, QWidget* parent)
    : QWidget(parent)
    , model(new_model)
    , library(new_library) {
    setObjectName("paramPanel");
    setMinimumWidth(460); // two columns of knobs
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    connect(&model, &GraphModel::node_changed, this, &ParamPanel::sync_from_model);
    connect(&library, &LibraryIndex::entries_changed, this, &ParamPanel::update_file_labels);
    show_node(0);
}

void ParamPanel::show_node(int id) {
    current_id = model.node(id) ? id : 0;
    knobs.clear();
    switches.clear();
    choices.clear();
    synced_cells.clear();
    title = subtitle = file_name = file_meta = nullptr;
    power = nullptr;
    meter = nullptr;
    if (body) {
        // Later: this may run from a signal of one of its own widgets.
        layout()->removeWidget(body);
        body->hide();
        body->deleteLater();
    }

    body = new QFrame;
    body->setObjectName("panelBody");
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(12);
    layout()->addWidget(body);

    const GraphModel::Node* n = model.node(current_id);
    if (!n) {
        auto* hint = new QLabel("Select a node to edit it.\n\nRight-click the canvas (or "
                                "double-click it) to add one; drag from a port to a port to "
                                "wire them. An output can feed any number of inputs; two "
                                "signals meet in a Merge node.");
        hint->setObjectName("panelHint");
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignCenter);
        lay->addStretch();
        lay->addWidget(hint);
        lay->addStretch();
        return;
    }

    build_header(lay);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("panelScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    content->setObjectName("panelContent");
    auto* clay = new QVBoxLayout(content);
    clay->setContentsMargins(0, 4, 0, 0);
    clay->setSpacing(14);
    if (n->type->file != NodeType::File::NONE) {
        build_file_section(clay);
    }
    build_params(clay);
    clay->addStretch();
    scroll->setWidget(content);
    lay->addWidget(scroll, 1);

    Theme::tag_card(body, theme_id(*n->type));
    sync_from_model(current_id);
}

QString ParamPanel::io_text() const {
    const GraphModel::Node* n = model.node(current_id);
    if (!n) {
        return {};
    }
    auto wired = [this](int port) {
        return std::any_of(model.get_wires().begin(), model.get_wires().end(),
                           [this, port](const GraphWire& w) {
                               return w.to_node == current_id && w.to_port == port;
                           });
    };
    switch (n->kind()) {
        case NodeKind::INPUT:
            return "Guitar In (Mono) → Mono Out";
        case NodeKind::OUTPUT:
            return wired(0) && wired(1) ? "Stereo In → Stereo Out" : "Mono In → Stereo Out (L = R)";
        case NodeKind::MERGE:
            return "Mono In A + Mono In B → Mono Out";
        case NodeKind::STEREO:
            return "Mono In → Stereo Out";
        case NodeKind::MONO:
            break;
    }
    return "Mono In → Mono Out";
}

void ParamPanel::build_header(QVBoxLayout* lay) {
    const GraphModel::Node* n = model.node(current_id);
    auto* row = new QHBoxLayout;
    row->setSpacing(14);
    row->addWidget(new IconBox(QString::fromStdString(n->type->id),
                               QString::fromStdString(n->type->category),
                               Theme::fx_accent(theme_id(*n->type))));
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    title = new QLabel;
    title->setObjectName("panelTitle");
    subtitle = new QLabel;
    subtitle->setObjectName("panelSubtitle");
    text->addWidget(title);
    text->addWidget(subtitle);
    row->addLayout(text, 1);

    const bool io = n->kind() == NodeKind::INPUT || n->kind() == NodeKind::OUTPUT;
    if (n->can_bypass()) {
        power = new QToolButton;
        power->setObjectName("powerButton");
        power->setCheckable(true);
        // Drawn, not a font glyph: centred in every font. Dim off, the accent on.
        auto pixmap = [this](const QColor& color) {
            const qreal dpr = devicePixelRatioF();
            QPixmap pm(QSize(20, 20) * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter painter(&pm);
            paint_power_icon(painter, QRectF(0, 0, 20, 20), color);
            return pm;
        };
        QIcon icon;
        icon.addPixmap(pixmap(Theme::node_text_dim()), QIcon::Normal, QIcon::Off);
        icon.addPixmap(pixmap(Theme::fx_accent(theme_id(*n->type))), QIcon::Normal, QIcon::On);
        power->setIcon(icon);
        power->setIconSize(QSize(20, 20));
        power->setToolTip("On / bypassed");
        connect(power, &QToolButton::toggled, this, [this](bool on) {
            model.set_enabled(current_id, on);
        });
        offer_learn(power, "enabled");
        row->addWidget(power, 0, Qt::AlignVCenter);
    }

    auto* more = new QToolButton;
    more->setObjectName("panelMore");
    more->setText("⋮");
    more->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(more);
    menu->addAction("Rename…", this, [this] {
        const GraphModel::Node* node = model.node(current_id);
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, "Rename Node", "Name:", QLineEdit::Normal, node->name, &ok);
        if (ok) {
            model.set_name(current_id, name);
        }
    });
    if (n->kind() == NodeKind::MONO && n->type->has_stereo) {
        menu->addAction("Make Stereo", this, [this] {
            model.set_stereo(current_id, true);
        });
    } else if (n->kind() == NodeKind::STEREO && n->type->has_mono) {
        menu->addAction("Make Mono", this, [this] {
            model.set_stereo(current_id, false);
        });
    }
    menu->addAction("Reset to Defaults", this, [this] {
        model.reset_values(current_id);
    });
    if (!io) {
        menu->addAction("Duplicate", this, [this] {
            const int copy = model.duplicate_node(current_id);
            if (copy && on_duplicate) {
                on_duplicate(copy);
            }
        });
        menu->addSeparator();
        menu->addAction("Delete", this, [this] {
            model.remove_node(current_id);
        });
    }
    more->setMenu(menu);
    row->addWidget(more, 0, Qt::AlignVCenter);
    lay->addLayout(row);

    auto* line = new QFrame;
    line->setObjectName("panelLine");
    line->setFixedHeight(1);
    lay->addWidget(line);
}

void ParamPanel::build_file_section(QVBoxLayout* lay) {
    const GraphModel::Node* n = model.node(current_id);
    const bool is_model = n->type->file == NodeType::File::MODEL;
    lay->addWidget(caption(is_model ? "Model" : "Impulse Response"));
    auto* box = new QFrame;
    box->setObjectName("panelGroup");
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(4);
    file_name = new QLabel;
    file_name->setObjectName("fxName");
    file_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    file_meta = new QLabel;
    file_meta->setObjectName("fileMeta");
    file_meta->setWordWrap(true);
    v->addWidget(file_name);
    v->addWidget(file_meta);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    auto add = [this, buttons](const QString& text, const QString& tip, auto fn) {
        auto* b = new QToolButton;
        b->setObjectName("panelTool");
        b->setText(text);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, fn);
        buttons->addWidget(b);
    };
    add("‹", "Previous file in the library", [this] {
        step_file(-1);
    });
    add("›", "Next file in the library", [this] {
        step_file(1);
    });
    add("Browse…", is_model ? "Load a .nam model" : "Load a .wav impulse response", [this] {
        browse_file();
    });
    buttons->addStretch();
    v->addLayout(buttons);
    lay->addWidget(box);
}

void ParamPanel::build_params(QVBoxLayout* lay) {
    const GraphModel::Node* n = model.node(current_id);
    const bool stereo = n->stereo();
    vector<const ParamSpec*> switch_specs;
    vector<const ParamSpec*> choice_specs;
    vector<const ParamSpec*> dials;
    for (const auto& p : n->type->params) {
        if (p.stereo_only && !stereo) {
            continue;
        }
        switch (p.kind) {
            case ParamSpec::Kind::SWITCH:
                switch_specs.push_back(&p);
                break;
            case ParamSpec::Kind::CHOICE:
                choice_specs.push_back(&p);
                break;
            case ParamSpec::Kind::KNOB:
                dials.push_back(&p);
                break;
            case ParamSpec::Kind::NOTE:
                break; // shown in its knob's place while synced
        }
    }

    if (n->kind() == NodeKind::INPUT || n->kind() == NodeKind::OUTPUT) {
        lay->addWidget(caption("Level"));
        meter = new VUMeter(Qt::Horizontal, n->kind() == NodeKind::OUTPUT ? 2 : 1);
        meter->setMinimumHeight(n->kind() == NodeKind::OUTPUT ? 36 : 20);
        lay->addWidget(meter);
    }
    if (switch_specs.empty() && choice_specs.empty() && dials.empty()) {
        return;
    }
    lay->addWidget(caption("Controls"));
    auto* box = new QFrame;
    box->setObjectName("panelGroup");
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(16, 14, 16, 16);
    v->setSpacing(16);
    if (!switch_specs.empty()) {
        auto* row = new QHBoxLayout;
        row->setSpacing(26);
        for (const ParamSpec* p : switch_specs) {
            row->addWidget(make_switch(*p));
        }
        row->addStretch();
        v->addLayout(row);
    }
    for (const ParamSpec* p : choice_specs) {
        v->addWidget(make_choice(*p));
    }
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setColumnStretch(0, 1); // two equal columns, a lone knob included
    grid->setColumnStretch(1, 1);
    grid->setVerticalSpacing(18);
    for (size_t i = 0; i < dials.size(); ++i) {
        grid->addWidget(make_dial_cell(*dials[i]), static_cast<int>(i / 2),
                        static_cast<int>(i % 2));
    }
    v->addLayout(grid);
    lay->addWidget(box);
}

QString ParamPanel::target_for(const QString& key) const {
    const GraphModel::Node* n = model.node(current_id);
    return n ? GraphModel::midi_target_id(n->name, key) : QString();
}

void ParamPanel::offer_learn(QWidget* w, const QString& key) {
    w->setProperty("midiTarget", target_for(key));
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w, &QWidget::customContextMenuRequested, this, [this, w](const QPoint& at) {
        if (!learn_menu) {
            return;
        }
        QMenu menu(this);
        learn_menu(w->property("midiTarget").toString(), &menu);
        if (!menu.isEmpty()) {
            menu.exec(w->mapToGlobal(at));
        }
    });
}

QWidget* ParamPanel::make_switch(const ParamSpec& p) {
    const QString key = QString::fromStdString(p.key);
    const QString tip = QString::fromStdString(p.tip);
    const int id = current_id;
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* sw = new ToggleSwitch;
    sw->setCheckable(true);
    sw->setToolTip(tip);
    auto* cap = new QLabel(QString::fromStdString(p.label));
    cap->setObjectName("toggleCaption");
    cap->setToolTip(tip);
    h->addWidget(sw);
    h->addWidget(cap);
    connect(sw, &ToggleSwitch::toggled, this, [this, id, key](bool on) {
        model.set_value(id, key, on ? 1.0 : 0.0);
    });
    offer_learn(sw, key);
    switches.insert(key, sw);
    return w;
}

QWidget* ParamPanel::make_choice(const ParamSpec& p) {
    const QString key = QString::fromStdString(p.key);
    const int id = current_id;
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto* cap = new QLabel(QString::fromStdString(p.label));
    cap->setObjectName("toggleCaption");
    h->addWidget(cap);
    h->addSpacing(12);
    auto* group = new QButtonGroup(w);
    QList<QToolButton*> buttons;
    for (size_t i = 0; i < p.choices.size(); ++i) {
        auto* b = new QToolButton;
        b->setObjectName("segment");
        b->setText(QString::fromStdString(p.choices[i]));
        b->setCheckable(true);
        b->setProperty("first", i == 0);
        b->setProperty("last", i + 1 == p.choices.size());
        group->addButton(b, static_cast<int>(i));
        h->addWidget(b);
        buttons << b;
    }
    h->addStretch();
    connect(group, &QButtonGroup::idClicked, this, [this, id, key](int index) {
        model.set_value(id, key, index);
    });
    offer_learn(w, key);
    choices.insert(key, buttons);
    return w;
}

KnobWidget* ParamPanel::make_knob(const ParamSpec& p) {
    const QString key = QString::fromStdString(p.key);
    const int id = current_id;
    auto* k = new KnobWidget(QString::fromStdString(p.label), p.min, p.max, p.def, display_unit(p),
                             KnobWidget::Form::PANEL);
    k->setToolTip(QString::fromStdString(p.tip));
    k->set_scroll_step(p.step);
    if (p.kind == ParamSpec::Kind::NOTE) {
        k->set_snap(1.0);
        k->set_value_formatter([](double v) {
            return TempoSync::label(v);
        });
        k->set_value_parser([](const QString& text) -> std::optional<double> {
            for (size_t i = 0; i < TempoSync::NOTES.size(); ++i) {
                if (text.compare(QLatin1String(TempoSync::NOTES[i].label), Qt::CaseInsensitive) ==
                    0) {
                    return static_cast<double>(i);
                }
            }
            return std::nullopt;
        });
    } else {
        if (p.log_taper) {
            k->set_log_taper(true);
        }
        if (const auto f = formatter_for(p)) {
            k->set_value_formatter(f);
        }
        if (const auto parse = parser_for(p)) {
            k->set_value_parser(parse);
        }
    }
    connect(k, &KnobWidget::value_changed, this, [this, id, key](double v) {
        model.set_value(id, key, v);
    });
    k->setProperty("midiTarget", target_for(key));
    knobs.insert(key, k);
    return k;
}

QWidget* ParamPanel::make_dial_cell(const ParamSpec& p) {
    KnobWidget* free_knob = make_knob(p);
    if (p.sync_note.empty()) {
        return free_knob;
    }
    // Synced: the note-value knob takes the free knob's place.
    const GraphModel::Node* n = model.node(current_id);
    const ParamSpec* note_spec = nullptr;
    for (const auto& q : n->type->params) {
        if (q.key == p.sync_note) {
            note_spec = &q;
        }
    }
    if (!note_spec) {
        return free_knob;
    }
    auto* cell = new QWidget;
    auto* stack = new QStackedLayout(cell);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(free_knob);
    stack->addWidget(make_knob(*note_spec));
    synced_cells.push_back({stack, QString::fromStdString(p.sync_switch)});
    return cell;
}

void ParamPanel::sync_from_model(int id) {
    if (id != current_id || !title) {
        return;
    }
    const GraphModel::Node* n = model.node(id);
    if (!n) {
        return;
    }
    title->setText(n->name);
    subtitle->setText(io_text());
    if (power) {
        const QSignalBlocker block(power);
        power->setChecked(n->enabled);
    }
    for (auto it = knobs.begin(); it != knobs.end(); ++it) {
        const QSignalBlocker block(it.value());
        it.value()->set_value(n->values.value(it.key()));
    }
    for (auto it = switches.begin(); it != switches.end(); ++it) {
        const QSignalBlocker block(it.value());
        it.value()->setChecked(n->values.value(it.key()) > 0.5);
    }
    for (auto it = choices.begin(); it != choices.end(); ++it) {
        const int index = static_cast<int>(std::lround(n->values.value(it.key())));
        for (int i = 0; i < it.value().size(); ++i) {
            const QSignalBlocker block(it.value()[i]);
            it.value()[i]->setChecked(i == index);
        }
    }
    for (const auto& [stack, sync_key] : synced_cells) {
        stack->setCurrentIndex(n->values.value(sync_key) > 0.5 ? 1 : 0);
    }
    // Pitch Shift: whole semitones unless Continuous.
    if (KnobWidget* semis = knobs.value("semitones")) {
        semis->set_snap(n->values.value("continuous") > 0.5 ? 0.0 : 1.0);
    }
    update_file_labels();
}

void ParamPanel::update_file_labels() {
    const GraphModel::Node* n = model.node(current_id);
    if (!n || !file_name) {
        return;
    }
    if (n->file.isEmpty()) {
        file_name->setText(n->type->file == NodeType::File::MODEL ? "No model loaded"
                                                                  : "No IR loaded");
        file_meta->setText("Browse… or step through your library with ‹ ›");
        return;
    }
    file_name->setText(QFileInfo(n->file).completeBaseName());
    const LibraryEntry* e = library.find(n->file);
    file_meta->setText(e ? e->summary : LibraryIndex::describe(n->file).summary);
    file_meta->setToolTip(Theme::plain_tooltip(n->file));
}

void ParamPanel::browse_file() {
    const GraphModel::Node* n = model.node(current_id);
    if (!n) {
        return;
    }
    const bool is_model = n->type->file == NodeType::File::MODEL;
    const QString start = n->file.isEmpty() ? QString() : QFileInfo(n->file).absolutePath();
    const QString path =
        QFileDialog::getOpenFileName(this, is_model ? "Load Model" : "Load Impulse Response", start,
                                     is_model ? "NAM models (*.nam)" : "Impulse responses (*.wav)");
    if (path.isEmpty()) {
        return;
    }
    if (auto ok = model.load_file(current_id, path); !ok) {
        QMessageBox::warning(this, "Couldn't Load the File", ok.error());
    }
}

void ParamPanel::step_file(int step) {
    const GraphModel::Node* n = model.node(current_id);
    if (!n) {
        return;
    }
    const LibraryEntry::Kind kind =
        n->type->file == NodeType::File::MODEL ? LibraryEntry::Kind::MODEL : LibraryEntry::Kind::IR;
    QStringList paths;
    for (const auto& e : library.get_entries()) {
        if (e.kind == kind) {
            paths << e.path;
        }
    }
    if (paths.isEmpty()) {
        QMessageBox::information(this, "Library Is Empty",
                                 "No files of this kind in the library folders yet (Settings → "
                                 "Library).");
        return;
    }
    const qsizetype at = paths.indexOf(n->file);
    const qsizetype size = paths.size();
    const qsizetype next = at < 0 ? (step > 0 ? 0 : size - 1) : ((at + step) % size + size) % size;
    if (auto ok = model.load_file(current_id, paths[next]); !ok) {
        QMessageBox::warning(this, "Couldn't Load the File", ok.error());
    }
}

void ParamPanel::poll_meters(AudioEngine& engine) {
    const GraphModel::Node* n = model.node(current_id);
    if (!meter || !n) {
        return;
    }
    if (n->kind() == NodeKind::INPUT) {
        meter->set_peak(engine.take_input_peak());
    } else {
        meter->set_peaks(engine.take_output_peak(0), engine.take_output_peak(1));
    }
}

} // namespace namx

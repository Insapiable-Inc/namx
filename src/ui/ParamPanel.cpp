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
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "AudioEngine.h"
#include "GraphModel.h"
#include "KnobWidget.h"
#include "NodeCatalog.h"
#include "NodeIcons.h"
#include "ReverbProcessor.h"
#include "Theme.h"
#include "ToggleSwitch.h"
#include "TunerDisplay.h"
#include "TunerProcessor.h"
#include "VUMeter.h"

namespace namx
{

namespace
{

QString theme_id(const NodeType& t) {
    return t.id == "input" || t.id == "output" ? QString("neutral") : QString::fromStdString(t.id);
}

QString format_hz(double hz) {
    return hz < 1000.0 ? QString("%1 Hz").arg(qRound(hz))
                       : QString("%1 kHz").arg(hz / 1000.0, 0, 'f', hz < 10000.0 ? 1 : 0);
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
                    return QString("0 st");
                }
                const bool whole = std::fmod(tenths, 1.0) == 0.0;
                return QString("%1%2 st")
                    .arg(tenths > 0 ? "+" : "")
                    .arg(tenths, 0, 'f', whole ? 0 : 1);
            };
        case ParamSpec::Format::DECAY:
            return [](double v) {
                const double s = ReverbProcessor::decay_seconds(static_cast<float>(v));
                return QString("%1 s").arg(s, 0, 'f', s < 1.0 ? 2 : 1);
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
    setMinimumWidth(380);
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
    title = subtitle = file_name = file_meta = nullptr;
    power = nullptr;
    tuner = nullptr;
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
    auto* content = new QWidget;
    content->setObjectName("panelContent");
    auto* clay = new QVBoxLayout(content);
    clay->setContentsMargins(0, 4, 0, 0);
    clay->setSpacing(14);
    if (n->type->file != NodeType::File::NONE) {
        build_file_section(clay);
    }
    if (n->type->id == "tuner") {
        tuner = new TunerDisplay;
        tuner->setMinimumHeight(140);
        clay->addWidget(tuner);
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
            return wired(0) && wired(1) ? "Stereo In → Stereo Out" : "Mono In → Stereo Out";
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
    if (!io) {
        power = new QToolButton;
        power->setObjectName("powerButton");
        power->setText("⏻");
        power->setCheckable(true);
        power->setToolTip("On / bypassed");
        connect(power, &QToolButton::toggled, this, [this](bool on) {
            model.set_enabled(current_id, on);
        });
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
        const GraphModel::Node* node = model.node(current_id);
        for (const auto& p : node->type->params) {
            model.set_value(current_id, QString::fromStdString(p.key), p.def);
        }
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
    vector<const ParamSpec*> toggles;
    vector<const ParamSpec*> dials;
    for (const auto& p : n->type->params) {
        if (p.stereo_only && !stereo) {
            continue;
        }
        (p.kind == ParamSpec::Kind::KNOB ? dials : toggles).push_back(&p);
    }

    if (n->kind() == NodeKind::INPUT || n->kind() == NodeKind::OUTPUT) {
        lay->addWidget(caption("Level"));
        meter = new VUMeter(Qt::Horizontal, n->kind() == NodeKind::OUTPUT ? 2 : 1);
        meter->setMinimumHeight(n->kind() == NodeKind::OUTPUT ? 36 : 20);
        lay->addWidget(meter);
    }
    if (toggles.empty() && dials.empty()) {
        return;
    }
    lay->addWidget(caption("Controls"));
    auto* box = new QFrame;
    box->setObjectName("panelGroup");
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(12);
    for (const ParamSpec* p : toggles) {
        v->addWidget(make_param(*p));
    }
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(14);
    for (size_t i = 0; i < dials.size(); ++i) {
        grid->addWidget(make_param(*dials[i]), static_cast<int>(i / 2), static_cast<int>(i % 2));
    }
    v->addLayout(grid);
    lay->addWidget(box);
}

QWidget* ParamPanel::make_param(const ParamSpec& p) {
    const QString key = QString::fromStdString(p.key);
    const QString label = QString::fromStdString(p.label);
    const QString tip = QString::fromStdString(p.tip);
    const int id = current_id;

    if (p.kind == ParamSpec::Kind::SWITCH) {
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        auto* sw = new ToggleSwitch;
        sw->setCheckable(true);
        sw->setToolTip(tip);
        auto* cap = new QLabel(label);
        cap->setObjectName("toggleCaption");
        cap->setToolTip(tip);
        h->addWidget(sw);
        h->addWidget(cap);
        h->addStretch();
        connect(sw, &ToggleSwitch::toggled, this, [this, id, key](bool on) {
            model.set_value(id, key, on ? 1.0 : 0.0);
        });
        switches.insert(key, sw);
        return w;
    }

    if (p.kind == ParamSpec::Kind::CHOICE) {
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);
        auto* cap = new QLabel(label);
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
        choices.insert(key, buttons);
        return w;
    }

    auto* k = new KnobWidget(label, p.min, p.max, p.def, QString::fromStdString(p.unit));
    k->setToolTip(tip);
    k->set_scroll_step(p.step);
    if (p.log_taper) {
        k->set_log_taper(true);
    }
    if (const auto f = formatter_for(p)) {
        k->set_value_formatter(f);
    }
    connect(k, &KnobWidget::value_changed, this, [this, id, key](double v) {
        model.set_value(id, key, v);
    });
    knobs.insert(key, k);
    return k;
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

void ParamPanel::poll_tuner(int reference_hz) {
    const GraphModel::Node* n = model.node(current_id);
    if (!tuner || !n) {
        return;
    }
    tuner->set_reference(reference_hz);
    const auto& t = static_cast<const TunerProcessor&>(n->dsp->processor());
    const float freq = n->enabled ? t.get_last_pitch() : 0.0f;
    if (freq <= 0.0f) {
        tuner->set_reading("-", 0.0f, false);
        return;
    }
    const float ref = static_cast<float>(reference_hz);
    const int midi_note = TunerProcessor::freq_to_midi(freq, ref);
    tuner->set_reading(QString::fromStdString(TunerProcessor::midi_to_name(midi_note)),
                       TunerProcessor::cents_offset(freq, ref), true, freq);
}

} // namespace namx

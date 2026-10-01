#include "MidiMappingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "Common.h"
#include "MidiInput.h"
#include "MidiMapper.h"

namespace npl
{

enum Col
{
    COL_ON,
    COL_SOURCE,
    COL_CHANNEL,
    COL_NUMBER,
    COL_TARGET,
    COL_MODE,
    COL_MIN,
    COL_MAX,
    COL_LEARN,
    COL_DELETE,
    COL_COUNT
};

static QString mapping_dir() {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/NAM Player/MIDI";
}

MidiMappingsDialog::MidiMappingsDialog(MidiMapper& new_mapper, MidiInput* new_input,
                                       QWidget* parent)
    : QDialog(parent)
    , mapper(new_mapper)
    , input(new_input) {
    setObjectName("midiMappings"); // scoped dark-table rules in Theme
    setWindowTitle("MIDI Mappings");
    resize(980, 560);
    auto* root = new QVBoxLayout(this);

    // Devices
    auto* devices = new QGroupBox("Devices");
    auto* dev_lay = new QVBoxLayout(devices);
    device_box = new QVBoxLayout;
    dev_lay->addLayout(device_box);
    auto* clock = new QCheckBox("Follow MIDI clock (set the tempo from incoming clock)");
    clock->setChecked(mapper.get_follow_clock());
    connect(clock, &QCheckBox::toggled, this, [this](bool on) {
        mapper.set_follow_clock(on);
    });
    dev_lay->addWidget(clock);
    root->addWidget(devices);
    rebuild_devices();
    if (input) {
        connect(input, &MidiInput::devices_changed, this, &MidiMappingsDialog::rebuild_devices);
    }

    // Mappings
    table = new QTableWidget(0, COL_COUNT);
    table->setHorizontalHeaderLabels(
        {"On", "Source", "Channel", "#", "Target", "Mode", "Min %", "Max %", "", ""});
    table->verticalHeader()->hide();
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->horizontalHeader()->setSectionResizeMode(COL_TARGET, QHeaderView::Stretch);
    root->addWidget(table, 1);

    activity = new QLabel("Last message: —");
    activity->setObjectName("libraryStatus");
    connect(&mapper, &MidiMapper::activity, activity, [this](const QString& what) {
        activity->setText("Last message: " + what);
    });
    // Rebuild on the next event-loop pass: a change usually comes from a
    // button inside the table, which must not be deleted mid-click.
    auto later = [this] {
        if (!rebuilding) {
            QMetaObject::invokeMethod(this, &MidiMappingsDialog::rebuild_table,
                                      Qt::QueuedConnection);
        }
    };
    connect(&mapper, &MidiMapper::mappings_changed, this, later);
    connect(&mapper, &MidiMapper::learn_changed, this, later);

    // Buttons
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("Add");
    connect(add, &QPushButton::clicked, this, [this] {
        QList<MidiMapping> list = mapper.get_mappings();
        MidiMapping m;
        if (!mapper.get_targets().empty()) {
            m.target = mapper.get_targets().front().id;
        }
        list << m;
        mapper.set_mappings(list);
        mapper.learn_row(list.size() - 1); // most people add by playing the control
    });
    auto* load = new QPushButton("Load…");
    connect(load, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, "Load MIDI Mappings", mapping_dir(),
                                         "NAM MIDI mappings (*.nammidi);;All Files (*)");
        QString err;
        if (!path.isEmpty() && !mapper.load_file(path, &err)) {
            QMessageBox::warning(this, "MIDI Mappings", "Cannot load " + path + ":\n" + err);
        }
    });
    auto* save = new QPushButton("Save…");
    connect(save, &QPushButton::clicked, this, [this] {
        QDir().mkpath(mapping_dir());
        QString path = QFileDialog::getSaveFileName(this, "Save MIDI Mappings",
                                                    mapping_dir() + "/Controller.nammidi",
                                                    "NAM MIDI mappings (*.nammidi);;All Files (*)");
        if (path.isEmpty()) {
            return;
        }
        if (QFileInfo(path).suffix().isEmpty()) {
            path += ".nammidi";
        }
        if (!mapper.save_file(path)) {
            QMessageBox::warning(this, "MIDI Mappings", "Cannot write " + path);
        }
    });
    auto* defaults = new QPushButton("Defaults");
    defaults->setToolTip("Program changes 0–7 → slots 1–8; everything else unmapped");
    connect(defaults, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "MIDI Mappings",
                                  "Replace all mappings with the default set?") ==
            QMessageBox::Yes) {
            mapper.set_mappings(MidiMapper::default_mappings());
        }
    });
    auto* close = new QPushButton("Close");
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(add);
    row->addWidget(load);
    row->addWidget(save);
    row->addWidget(defaults);
    row->addSpacing(12);
    row->addWidget(activity, 1);
    row->addWidget(close);
    root->addLayout(row);

    connect(this, &QDialog::finished, this, [this] {
        mapper.cancel_learn();
    });
    rebuild_table();
}

void MidiMappingsDialog::rebuild_devices() {
    while (QLayoutItem* it = device_box->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    const QStringList devs = input ? input->devices() : QStringList();
    if (devs.isEmpty()) {
        auto* none =
            new QLabel(input ? "No MIDI devices found. Plug one in — it connects automatically."
                             : "MIDI is not available on this system.");
        none->setObjectName("libraryStatus");
        device_box->addWidget(none);
        return;
    }
    for (const QString& d : devs) {
        auto* cb = new QCheckBox(d);
        cb->setChecked(input->is_enabled(d));
        connect(cb, &QCheckBox::toggled, this, [this, d](bool on) {
            input->set_enabled(d, on);
        });
        device_box->addWidget(cb);
    }
}

void MidiMappingsDialog::rebuild_table() {
    rebuilding = true;
    const QList<MidiMapping>& list = mapper.get_mappings();
    table->setRowCount(static_cast<int>(list.size())); // Qt widgets count rows in int
    for (int r = 0; r < list.size(); ++r) {
        const MidiMapping& m = list[r];
        auto commit = [this, r] {
            commit_row(r);
        };

        auto* on = new QCheckBox;
        on->setChecked(m.enabled);
        connect(on, &QCheckBox::toggled, this, commit);
        auto* on_cell = new QWidget;
        auto* on_lay = new QHBoxLayout(on_cell);
        on_lay->setContentsMargins(6, 0, 0, 0);
        on_lay->addWidget(on);
        table->setCellWidget(r, COL_ON, on_cell);

        auto* src = new QComboBox;
        src->addItems({"CC", "Note", "Program"});
        src->setCurrentIndex(m.source);
        connect(src, &QComboBox::currentIndexChanged, this, commit);
        table->setCellWidget(r, COL_SOURCE, src);

        auto* ch = new QComboBox;
        ch->addItem("Any");
        for (int i = 1; i <= 16; ++i) {
            ch->addItem(QString::number(i));
        }
        ch->setCurrentIndex(m.channel + 1);
        connect(ch, &QComboBox::currentIndexChanged, this, commit);
        table->setCellWidget(r, COL_CHANNEL, ch);

        auto* num = new QSpinBox;
        num->setRange(0, 127);
        num->setValue(m.number);
        connect(num, &QSpinBox::valueChanged, this, commit);
        table->setCellWidget(r, COL_NUMBER, num);

        auto* tgt = new QComboBox;
        for (const auto& t : mapper.get_targets()) {
            tgt->addItem(t.group.isEmpty() ? t.label : t.group + " · " + t.label, t.id);
        }
        int idx = tgt->findData(m.target);
        if (idx < 0) {
            tgt->addItem("(unknown) " + m.target, m.target);
            idx = tgt->count() - 1;
        }
        tgt->setCurrentIndex(idx);
        tgt->setMaxVisibleItems(24);
        connect(tgt, &QComboBox::currentIndexChanged, this, commit);
        table->setCellWidget(r, COL_TARGET, tgt);

        auto* mode = new QComboBox;
        mode->addItems({"Absolute", "Relative", "Toggle", "Momentary"});
        mode->setCurrentIndex(m.mode);
        mode->setToolTip("Absolute: the control sets the value · Relative: endless encoder (1–63 "
                         "up, 65–127 down)\n"
                         "Toggle: each press flips / fires · Momentary: on while held");
        connect(mode, &QComboBox::currentIndexChanged, this, commit);
        table->setCellWidget(r, COL_MODE, mode);

        auto pct = [&](double v, Col col) {
            auto* sp = new QSpinBox;
            sp->setRange(0, 100);
            sp->setValue(qRound(v * 100.0));
            sp->setToolTip("Share of the knob's range the controller sweeps (knobs only)");
            connect(sp, &QSpinBox::valueChanged, this, commit);
            table->setCellWidget(r, col, sp);
        };
        pct(m.min, COL_MIN);
        pct(m.max, COL_MAX);

        auto* learn = new QToolButton;
        const bool learning = mapper.learning_row() == r;
        learn->setText(learning ? "Listening…" : "Learn");
        learn->setToolTip("Bind this row to the next control you move");
        connect(learn, &QToolButton::clicked, this, [this, r, learning] {
            if (learning) {
                mapper.cancel_learn();
            } else {
                mapper.learn_row(r);
            }
        });
        table->setCellWidget(r, COL_LEARN, learn);

        auto* del = new QToolButton;
        del->setText("×");
        del->setToolTip("Delete this mapping");
        connect(del, &QToolButton::clicked, this, [this, r] {
            QList<MidiMapping> remaining = mapper.get_mappings();
            if (r < remaining.size()) {
                remaining.removeAt(r);
            }
            mapper.cancel_learn();
            mapper.set_mappings(remaining);
        });
        table->setCellWidget(r, COL_DELETE, del);
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(COL_TARGET, QHeaderView::Stretch);
    rebuilding = false;
}

void MidiMappingsDialog::commit_row(int row) {
    if (rebuilding) {
        return;
    }
    QList<MidiMapping> list = mapper.get_mappings();
    if (row >= list.size()) {
        return;
    }
    MidiMapping& m = list[row];
    QWidget* on_cell = table->cellWidget(row, COL_ON);
    m.enabled = on_cell->findChild<QCheckBox*>()->isChecked();
    m.source = MidiMapping::Source(
        static_cast<QComboBox*>(table->cellWidget(row, COL_SOURCE))->currentIndex());
    m.channel = static_cast<QComboBox*>(table->cellWidget(row, COL_CHANNEL))->currentIndex() - 1;
    m.number = static_cast<QSpinBox*>(table->cellWidget(row, COL_NUMBER))->value();
    m.target =
        static_cast<QComboBox*>(table->cellWidget(row, COL_TARGET))->currentData().toString();
    m.mode = MidiMapping::Mode(
        static_cast<QComboBox*>(table->cellWidget(row, COL_MODE))->currentIndex());
    m.min = static_cast<QSpinBox*>(table->cellWidget(row, COL_MIN))->value() / 100.0;
    m.max = static_cast<QSpinBox*>(table->cellWidget(row, COL_MAX))->value() / 100.0;
    rebuilding = true; // our own edit: the widgets already show it
    mapper.set_mappings(list);
    rebuilding = false;
}

} // namespace npl

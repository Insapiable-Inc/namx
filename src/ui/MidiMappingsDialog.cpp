#include "MidiMappingsDialog.h"

#include "MidiInput.h"
#include "MidiMapper.h"

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

namespace {
enum Col { ColOn, ColSource, ColChannel, ColNumber, ColTarget, ColMode, ColMin, ColMax, ColLearn, ColDelete, ColCount };

QString mappingDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/NAM Player/MIDI";
}
} // namespace

MidiMappingsDialog::MidiMappingsDialog(MidiMapper& mapper, MidiInput* input, QWidget* parent)
    : QDialog(parent)
    , mMapper(mapper)
    , mInput(input)
{
    setWindowTitle("MIDI Mappings");
    resize(980, 560);
    auto* root = new QVBoxLayout(this);

    // ---- Devices ----
    auto* devices = new QGroupBox("Devices");
    auto* devLay  = new QVBoxLayout(devices);
    mDeviceBox    = new QVBoxLayout;
    devLay->addLayout(mDeviceBox);
    auto* clock = new QCheckBox("Follow MIDI clock (set the tempo from incoming clock)");
    clock->setChecked(mMapper.followClock());
    connect(clock, &QCheckBox::toggled, this, [this](bool on) { mMapper.setFollowClock(on); });
    devLay->addWidget(clock);
    root->addWidget(devices);
    rebuildDevices();
    if (mInput) connect(mInput, &MidiInput::devicesChanged, this, &MidiMappingsDialog::rebuildDevices);

    // ---- Mappings ----
    mTable = new QTableWidget(0, ColCount);
    mTable->setHorizontalHeaderLabels({"On", "Source", "Channel", "#", "Target", "Mode", "Min %", "Max %", "", ""});
    mTable->verticalHeader()->hide();
    mTable->setSelectionMode(QAbstractItemView::NoSelection);
    mTable->horizontalHeader()->setSectionResizeMode(ColTarget, QHeaderView::Stretch);
    root->addWidget(mTable, 1);

    mActivity = new QLabel("Last message: —");
    mActivity->setObjectName("libraryStatus");
    connect(&mMapper, &MidiMapper::activity, mActivity,
            [this](const QString& what) { mActivity->setText("Last message: " + what); });
    // Rebuild on the next event-loop pass: a change usually comes from a
    // button inside the table, which must not be deleted mid-click.
    auto later = [this] {
        if (!mRebuilding) QMetaObject::invokeMethod(this, &MidiMappingsDialog::rebuildTable, Qt::QueuedConnection);
    };
    connect(&mMapper, &MidiMapper::mappingsChanged, this, later);
    connect(&mMapper, &MidiMapper::learnChanged, this, later);

    // ---- Buttons ----
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("Add");
    connect(add, &QPushButton::clicked, this, [this] {
        QList<MidiMapping> list = mMapper.mappings();
        MidiMapping m;
        if (!mMapper.targets().empty()) m.target = mMapper.targets().front().id;
        list << m;
        mMapper.setMappings(list);
        mMapper.learnRow(list.size() - 1); // most people add by playing the control
    });
    auto* load = new QPushButton("Load…");
    connect(load, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Load MIDI Mappings", mappingDir(),
                                                          "NAM MIDI mappings (*.nammidi);;All Files (*)");
        QString err;
        if (!path.isEmpty() && !mMapper.loadFile(path, &err))
            QMessageBox::warning(this, "MIDI Mappings", "Cannot load " + path + ":\n" + err);
    });
    auto* save = new QPushButton("Save…");
    connect(save, &QPushButton::clicked, this, [this] {
        QDir().mkpath(mappingDir());
        QString path = QFileDialog::getSaveFileName(this, "Save MIDI Mappings", mappingDir() + "/Controller.nammidi",
                                                    "NAM MIDI mappings (*.nammidi);;All Files (*)");
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += ".nammidi";
        if (!mMapper.saveFile(path))
            QMessageBox::warning(this, "MIDI Mappings", "Cannot write " + path);
    });
    auto* defaults = new QPushButton("Defaults");
    defaults->setToolTip("Program changes 0–7 → slots 1–8; everything else unmapped");
    connect(defaults, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "MIDI Mappings", "Replace all mappings with the default set?")
            == QMessageBox::Yes)
            mMapper.setMappings(MidiMapper::defaultMappings());
    });
    auto* close = new QPushButton("Close");
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(add);
    row->addWidget(load);
    row->addWidget(save);
    row->addWidget(defaults);
    row->addSpacing(12);
    row->addWidget(mActivity, 1);
    row->addWidget(close);
    root->addLayout(row);

    connect(this, &QDialog::finished, this, [this] { mMapper.cancelLearn(); });
    rebuildTable();
}

void MidiMappingsDialog::rebuildDevices()
{
    while (QLayoutItem* it = mDeviceBox->takeAt(0))
    {
        delete it->widget();
        delete it;
    }
    const QStringList devs = mInput ? mInput->devices() : QStringList();
    if (devs.isEmpty())
    {
        auto* none = new QLabel(mInput ? "No MIDI devices found. Plug one in — it connects automatically."
                                       : "MIDI is not available on this system.");
        none->setObjectName("libraryStatus");
        mDeviceBox->addWidget(none);
        return;
    }
    for (const QString& d : devs)
    {
        auto* cb = new QCheckBox(d);
        cb->setChecked(mInput->isEnabled(d));
        connect(cb, &QCheckBox::toggled, this, [this, d](bool on) { mInput->setEnabled(d, on); });
        mDeviceBox->addWidget(cb);
    }
}

void MidiMappingsDialog::rebuildTable()
{
    mRebuilding = true;
    const auto& list = mMapper.mappings();
    mTable->setRowCount(list.size());
    for (int r = 0; r < list.size(); ++r)
    {
        const MidiMapping& m = list[r];
        auto commit = [this, r] { commitRow(r); };

        auto* on = new QCheckBox;
        on->setChecked(m.enabled);
        connect(on, &QCheckBox::toggled, this, commit);
        auto* onCell = new QWidget;
        auto* onLay  = new QHBoxLayout(onCell);
        onLay->setContentsMargins(6, 0, 0, 0);
        onLay->addWidget(on);
        mTable->setCellWidget(r, ColOn, onCell);

        auto* src = new QComboBox;
        src->addItems({"CC", "Note", "Program"});
        src->setCurrentIndex(m.source);
        connect(src, &QComboBox::currentIndexChanged, this, commit);
        mTable->setCellWidget(r, ColSource, src);

        auto* ch = new QComboBox;
        ch->addItem("Any");
        for (int i = 1; i <= 16; ++i) ch->addItem(QString::number(i));
        ch->setCurrentIndex(m.channel + 1);
        connect(ch, &QComboBox::currentIndexChanged, this, commit);
        mTable->setCellWidget(r, ColChannel, ch);

        auto* num = new QSpinBox;
        num->setRange(0, 127);
        num->setValue(m.number);
        connect(num, &QSpinBox::valueChanged, this, commit);
        mTable->setCellWidget(r, ColNumber, num);

        auto* tgt = new QComboBox;
        for (const auto& t : mMapper.targets())
            tgt->addItem(t.group.isEmpty() ? t.label : t.group + " · " + t.label, t.id);
        int idx = tgt->findData(m.target);
        if (idx < 0) { tgt->addItem("(unknown) " + m.target, m.target); idx = tgt->count() - 1; }
        tgt->setCurrentIndex(idx);
        tgt->setMaxVisibleItems(24);
        connect(tgt, &QComboBox::currentIndexChanged, this, commit);
        mTable->setCellWidget(r, ColTarget, tgt);

        auto* mode = new QComboBox;
        mode->addItems({"Absolute", "Relative", "Toggle", "Momentary"});
        mode->setCurrentIndex(m.mode);
        mode->setToolTip("Absolute: the control sets the value · Relative: endless encoder (1–63 up, 65–127 down)\n"
                         "Toggle: each press flips / fires · Momentary: on while held");
        connect(mode, &QComboBox::currentIndexChanged, this, commit);
        mTable->setCellWidget(r, ColMode, mode);

        auto pct = [&](double v, Col col) {
            auto* sp = new QSpinBox;
            sp->setRange(0, 100);
            sp->setValue(qRound(v * 100.0));
            sp->setToolTip("Share of the knob's range the controller sweeps (knobs only)");
            connect(sp, &QSpinBox::valueChanged, this, commit);
            mTable->setCellWidget(r, col, sp);
        };
        pct(m.min, ColMin);
        pct(m.max, ColMax);

        auto* learn = new QToolButton;
        const bool learning = mMapper.learningRow() == r;
        learn->setText(learning ? "Listening…" : "Learn");
        learn->setToolTip("Bind this row to the next control you move");
        connect(learn, &QToolButton::clicked, this, [this, r, learning] {
            if (learning) mMapper.cancelLearn();
            else          mMapper.learnRow(r);
        });
        mTable->setCellWidget(r, ColLearn, learn);

        auto* del = new QToolButton;
        del->setText("×");
        del->setToolTip("Delete this mapping");
        connect(del, &QToolButton::clicked, this, [this, r] {
            QList<MidiMapping> list = mMapper.mappings();
            if (r < list.size()) list.removeAt(r);
            mMapper.cancelLearn();
            mMapper.setMappings(list);
        });
        mTable->setCellWidget(r, ColDelete, del);
    }
    mTable->resizeColumnsToContents();
    mTable->horizontalHeader()->setSectionResizeMode(ColTarget, QHeaderView::Stretch);
    mRebuilding = false;
}

void MidiMappingsDialog::commitRow(int row)
{
    if (mRebuilding) return;
    QList<MidiMapping> list = mMapper.mappings();
    if (row >= list.size()) return;
    MidiMapping& m = list[row];
    auto* onCell = mTable->cellWidget(row, ColOn);
    m.enabled = onCell->findChild<QCheckBox*>()->isChecked();
    m.source  = MidiMapping::Source(static_cast<QComboBox*>(mTable->cellWidget(row, ColSource))->currentIndex());
    m.channel = static_cast<QComboBox*>(mTable->cellWidget(row, ColChannel))->currentIndex() - 1;
    m.number  = static_cast<QSpinBox*>(mTable->cellWidget(row, ColNumber))->value();
    m.target  = static_cast<QComboBox*>(mTable->cellWidget(row, ColTarget))->currentData().toString();
    m.mode    = MidiMapping::Mode(static_cast<QComboBox*>(mTable->cellWidget(row, ColMode))->currentIndex());
    m.min     = static_cast<QSpinBox*>(mTable->cellWidget(row, ColMin))->value() / 100.0;
    m.max     = static_cast<QSpinBox*>(mTable->cellWidget(row, ColMax))->value() / 100.0;
    mRebuilding = true; // our own edit: the widgets already show it
    mMapper.setMappings(list);
    mRebuilding = false;
}

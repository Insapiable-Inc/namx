#include "PresetBar.h"

#include <algorithm>
#include <functional>

#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QSettings>
#include <QShortcut>
#include <QStyle>
#include <QToolButton>

#include "LibraryIndex.h"

static void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}

PresetBar::PresetBar(const LibraryIndex& index, QMenu* presetMenu, QWidget* parent)
    : QFrame(parent)
    , mIndex(index) {
    setObjectName("presetBar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(4);

    auto tool = [this](const QString& text, const QString& tip, const char* name) {
        auto* b = new QToolButton(this);
        b->setObjectName(name);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    auto* caption = new QLabel("Preset", this);
    caption->setObjectName("presetCaption");
    lay->addWidget(caption);

    auto* prev = tool("‹", "Previous preset in the library", "heroArrow");
    auto* next = tool("›", "Next preset in the library", "heroArrow");
    connect(prev, &QToolButton::clicked, this, [this] {
        emit stepRequested(-1);
    });
    connect(next, &QToolButton::clicked, this, [this] {
        emit stepRequested(+1);
    });

    // Name button: the library's presets as a menu, folders as submenus.
    mName = tool({}, {}, "presetName");
    mName->setPopupMode(QToolButton::InstantPopup);
    auto* listMenu = new QMenu(mName);
    connect(listMenu, &QMenu::aboutToShow, this, [this, listMenu] {
        populatePresetMenu(listMenu);
    });
    mName->setMenu(listMenu);

    mModified = new QLabel("*", this);
    mModified->setObjectName("presetModified");
    mModified->setToolTip("Modified since the preset was loaded or saved");

    learnMenuFor(prev, "action:presetPrev");
    learnMenuFor(next, "action:presetNext");
    lay->addWidget(prev);
    lay->addWidget(mName);
    lay->addWidget(mModified);
    lay->addWidget(next);
    lay->addSpacing(8);

    auto* save = tool("Save", "Save the preset (Ctrl+S)", "presetTool");
    auto* saveAs = tool("Save As…", "Save as a new preset (Ctrl+Shift+S)", "presetTool");
    connect(save, &QToolButton::clicked, this, &PresetBar::saveRequested);
    connect(saveAs, &QToolButton::clicked, this, &PresetBar::saveAsRequested);
    lay->addWidget(save);
    lay->addWidget(saveAs);
    lay->addStretch(1);

    // Quick slots 1–8: click / Ctrl+N loads, right-click stores or clears.
    auto* slotsCaption = new QLabel("Slots", this);
    slotsCaption->setObjectName("presetCaption");
    lay->addWidget(slotsCaption);
    mSlotPaths = QSettings().value("presets/slots").toStringList();
    while (mSlotPaths.size() < kSlotCount) {
        mSlotPaths << QString();
    }
    for (int i = 0; i < kSlotCount; ++i) {
        auto* b = tool(QString::number(i + 1), {}, "presetSlot");
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QToolButton::clicked, this, [this, i] {
            activateSlot(i);
        });
        connect(b, &QToolButton::customContextMenuRequested, this, [this, i, b](const QPoint& p) {
            showSlotMenu(i, b->mapToGlobal(p));
        });
        auto* sc = new QShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, i] {
            activateSlot(i);
        });
        mSlots[i] = b;
        mActionWidgets.insert(QString("action:slot%1").arg(i + 1), b);
        lay->addWidget(b);
    }
    lay->addSpacing(12);

    // A/B compare: two in-memory states; the owner swaps them.
    for (int i = 0; i < 2; ++i) {
        mAb[i] = tool(i == 0 ? "A" : "B", {}, "presetAb");
        mAb[i]->setCheckable(true);
        connect(mAb[i], &QToolButton::clicked, this, [this, i] {
            mAb[i]->setChecked(i == mAbActive); // the owner confirms via setAbState
            emit abSwitchRequested(i);
        });
        lay->addWidget(mAb[i]);
    }
    learnMenuFor(mAb[0], "action:abToggle");
    learnMenuFor(mAb[1], "action:abToggle");
    mAbCopy = tool({}, {}, "presetTool");
    connect(mAbCopy, &QToolButton::clicked, this, &PresetBar::abCopyRequested);
    lay->addWidget(mAbCopy);
    lay->addSpacing(8);

    auto* more = tool("⋯", "Preset menu", "presetTool");
    more->setPopupMode(QToolButton::InstantPopup);
    more->setMenu(presetMenu);
    lay->addWidget(more);

    setPreset({}, false);
    setAbState(0);
}

void PresetBar::learnMenuFor(QWidget* w, const QString& actionId) {
    if (!mActionWidgets.contains(actionId)) {
        mActionWidgets.insert(actionId, w);
    }
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w, &QWidget::customContextMenuRequested, this, [this, w, actionId](const QPoint& p) {
        if (!mLearnHook) {
            return;
        }
        QMenu menu(this);
        mLearnHook(actionId, &menu);
        if (!menu.isEmpty()) {
            menu.exec(w->mapToGlobal(p));
        }
    });
}

void PresetBar::recallSlot(int i) {
    if (i < 0 || i >= kSlotCount) {
        return;
    }
    const QString& p = mSlotPaths[i];
    if (!p.isEmpty() && QFile::exists(p)) {
        emit presetChosen(p);
    }
}

void PresetBar::setPreset(const QString& path, bool modified) {
    mPath = path;
    if (path.isEmpty()) {
        mName->setText("Untitled");
        mName->setToolTip("Not saved as a preset — click to pick one");
        mModified->hide();
    } else {
        mName->setText(QFileInfo(path).completeBaseName());
        mName->setToolTip(QDir::toNativeSeparators(path));
        mModified->setVisible(modified);
    }
    mName->setProperty("untitled", path.isEmpty());
    repolish(mName);
    updateSlots();
}

void PresetBar::setAbState(int active) {
    mAbActive = active;
    for (int i = 0; i < 2; ++i) {
        mAb[i]->setChecked(i == active);
        mAb[i]->setToolTip(i == active
                               ? QString("Editing state %1").arg(i == 0 ? 'A' : 'B')
                               : QString("Switch to state %1 (the current one is kept as %2)")
                                     .arg(i == 0 ? 'A' : 'B', i == 0 ? 'B' : 'A'));
    }
    mAbCopy->setText(active == 0 ? "A→B" : "B→A");
    mAbCopy->setToolTip(QString("Copy the current state over %1").arg(active == 0 ? 'B' : 'A'));
}

void PresetBar::populatePresetMenu(QMenu* menu) {
    menu->clear();

    QList<const LibraryEntry*> presets;
    for (const auto& e : mIndex.entries()) {
        if (e.kind == LibraryEntry::Kind::Preset) {
            presets << &e;
        }
    }
    if (presets.isEmpty()) {
        menu->addAction("No presets in the library")->setEnabled(false);
        return;
    }
    std::sort(presets.begin(), presets.end(), [](const LibraryEntry* a, const LibraryEntry* b) {
        if (a->folder != b->folder) {
            return a->folder.compare(b->folder, Qt::CaseInsensitive) < 0;
        }
        return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });

    // Folder path → submenu, created on demand; root-level files after the
    // folders so the tree reads like a file manager.
    QMap<QString, QMenu*> folders;
    std::function<QMenu*(const QString&)> menuFor = [&](const QString& folder) -> QMenu* {
        if (folder.isEmpty()) {
            return menu;
        }
        if (auto it = folders.find(folder); it != folders.end()) {
            return *it;
        }
        const qsizetype slash = folder.lastIndexOf('/');
        QMenu* parent = menuFor(slash < 0 ? QString() : folder.left(slash));
        QMenu* sub = parent->addMenu(folder.mid(slash + 1));
        folders.insert(folder, sub);
        return sub;
    };
    for (const LibraryEntry* e : presets) {
        if (!e->folder.isEmpty()) {
            menuFor(e->folder);
        }
    }

    for (const LibraryEntry* e : presets) {
        QAction* a = menuFor(e->folder)->addAction(e->name);
        a->setCheckable(true);
        a->setChecked(e->path == mPath);
        a->setToolTip(e->summary);
        const QString path = e->path;
        connect(a, &QAction::triggered, this, [this, path] {
            emit presetChosen(path);
        });
    }
}

void PresetBar::activateSlot(int i) {
    const QString& p = mSlotPaths[i];
    if (!p.isEmpty()) {
        if (QFile::exists(p)) {
            emit presetChosen(p);
        }
        return;
    }
    // Empty slot: a click stores the current preset there.
    if (mPath.isEmpty()) {
        return;
    }
    mSlotPaths[i] = mPath;
    QSettings().setValue("presets/slots", mSlotPaths);
    updateSlots();
}

void PresetBar::showSlotMenu(int i, const QPoint& globalPos) {
    QMenu menu(this);
    QAction* store =
        menu.addAction(mPath.isEmpty() ? QString("Store current preset (save it first)")
                                       : QString("Store \"%1\" in slot %2")
                                             .arg(QFileInfo(mPath).completeBaseName())
                                             .arg(i + 1));
    store->setEnabled(!mPath.isEmpty());
    QAction* clear = menu.addAction(QString("Clear slot %1").arg(i + 1));
    clear->setEnabled(!mSlotPaths[i].isEmpty());
    if (mLearnHook) {
        menu.addSeparator();
        mLearnHook(QString("action:slot%1").arg(i + 1), &menu);
    }

    QAction* picked = menu.exec(globalPos);
    if (!picked || (picked != store && picked != clear)) {
        return;
    }
    mSlotPaths[i] = picked == store ? mPath : QString();
    QSettings().setValue("presets/slots", mSlotPaths);
    updateSlots();
}

void PresetBar::updateSlots() {
    for (int i = 0; i < kSlotCount; ++i) {
        const QString& p = mSlotPaths[i];
        QToolButton* b = mSlots[i];
        const bool filled = !p.isEmpty();
        const bool missing = filled && !QFile::exists(p);
        if (!filled) {
            b->setToolTip(
                QString("Slot %1 is empty — click to store the current preset").arg(i + 1));
        } else if (missing) {
            b->setToolTip(
                QString("Slot %1: %2 (file missing)").arg(i + 1).arg(QDir::toNativeSeparators(p)));
        } else {
            b->setToolTip(
                QString("Slot %1: %2 (Ctrl+%1)").arg(i + 1).arg(QFileInfo(p).completeBaseName()));
        }
        b->setProperty("filled", filled && !missing);
        b->setProperty("active", filled && p == mPath);
        repolish(b);
    }
}

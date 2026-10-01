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

#include "Common.h"
#include "LibraryIndex.h"

namespace npl
{

static void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
}

PresetBar::PresetBar(const LibraryIndex& new_index, QMenu* preset_menu, QWidget* parent)
    : QFrame(parent)
    , library_index(new_index) {
    setObjectName("presetBar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(4);

    auto tool = [this](const QString& text, const QString& tip, const char* new_name) {
        auto* b = new QToolButton(this);
        b->setObjectName(new_name);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    auto* caption = new QLabel("Preset", this);
    caption->setObjectName("presetCaption");
    lay->addWidget(caption);

    QToolButton* prev = tool("‹", "Previous preset in the library", "heroArrow");
    QToolButton* next = tool("›", "Next preset in the library", "heroArrow");
    connect(prev, &QToolButton::clicked, this, [this] {
        emit step_requested(-1);
    });
    connect(next, &QToolButton::clicked, this, [this] {
        emit step_requested(+1);
    });

    // Name button: the library's presets as a menu, folders as submenus.
    name = tool({}, {}, "presetName");
    name->setPopupMode(QToolButton::InstantPopup);
    auto* list_menu = new QMenu(name);
    connect(list_menu, &QMenu::aboutToShow, this, [this, list_menu] {
        populate_preset_menu(list_menu);
    });
    name->setMenu(list_menu);

    modified = new QLabel("*", this);
    modified->setObjectName("presetModified");
    modified->setToolTip("Modified since the preset was loaded or saved");

    learn_menu_for(prev, "action:presetPrev");
    learn_menu_for(next, "action:presetNext");
    lay->addWidget(prev);
    lay->addWidget(name);
    lay->addWidget(modified);
    lay->addWidget(next);
    lay->addSpacing(8);

    QToolButton* save = tool("Save", "Save the preset (Ctrl+S)", "presetTool");
    QToolButton* save_as = tool("Save As…", "Save as a new preset (Ctrl+Shift+S)", "presetTool");
    connect(save, &QToolButton::clicked, this, &PresetBar::save_requested);
    connect(save_as, &QToolButton::clicked, this, &PresetBar::save_as_requested);
    lay->addWidget(save);
    lay->addWidget(save_as);
    lay->addStretch(1);

    // Quick slots 1–8: click / Ctrl+N loads, right-click stores or clears.
    auto* slots_caption = new QLabel("Slots", this);
    slots_caption->setObjectName("presetCaption");
    lay->addWidget(slots_caption);
    slot_paths = QSettings().value("presets/slots").toStringList();
    while (slot_paths.size() < SLOT_COUNT) {
        slot_paths << QString();
    }
    for (int i = 0; i < SLOT_COUNT; ++i) {
        QToolButton* b = tool(QString::number(i + 1), {}, "presetSlot");
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QToolButton::clicked, this, [this, i] {
            activate_slot(i);
        });
        connect(b, &QToolButton::customContextMenuRequested, this, [this, i, b](const QPoint& p) {
            show_slot_menu(i, b->mapToGlobal(p));
        });
        auto* sc = new QShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, i] {
            activate_slot(i);
        });
        quick_slots[i] = b;
        action_widgets.insert(QString("action:slot%1").arg(i + 1), b);
        lay->addWidget(b);
    }
    lay->addSpacing(12);

    // A/B compare: two in-memory states; the owner swaps them.
    for (int i = 0; i < 2; ++i) {
        ab[i] = tool(i == 0 ? "A" : "B", {}, "presetAb");
        ab[i]->setCheckable(true);
        connect(ab[i], &QToolButton::clicked, this, [this, i] {
            ab[i]->setChecked(i == ab_active); // the owner confirms via setAbState
            emit ab_switch_requested(i);
        });
        lay->addWidget(ab[i]);
    }
    learn_menu_for(ab[0], "action:abToggle");
    learn_menu_for(ab[1], "action:abToggle");
    ab_copy = tool({}, {}, "presetTool");
    connect(ab_copy, &QToolButton::clicked, this, &PresetBar::ab_copy_requested);
    lay->addWidget(ab_copy);
    lay->addSpacing(8);

    QToolButton* more = tool("⋯", "Preset menu", "presetTool");
    more->setPopupMode(QToolButton::InstantPopup);
    more->setMenu(preset_menu);
    lay->addWidget(more);

    set_preset({}, false);
    set_ab_state(0);
}

void PresetBar::learn_menu_for(QWidget* w, const QString& action_id) {
    if (!action_widgets.contains(action_id)) {
        action_widgets.insert(action_id, w);
    }
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w, &QWidget::customContextMenuRequested, this, [this, w, action_id](const QPoint& p) {
        if (!learn_hook) {
            return;
        }
        QMenu menu(this);
        learn_hook(action_id, &menu);
        if (!menu.isEmpty()) {
            menu.exec(w->mapToGlobal(p));
        }
    });
}

void PresetBar::recall_slot(int i) {
    if (i < 0 || i >= SLOT_COUNT) {
        return;
    }
    const QString& p = slot_paths[i];
    if (!p.isEmpty() && QFile::exists(p)) {
        emit preset_chosen(p);
    }
}

void PresetBar::set_preset(const QString& new_path, bool new_modified) {
    path = new_path;
    if (new_path.isEmpty()) {
        name->setText("Untitled");
        name->setToolTip("Not saved as a preset — click to pick one");
        modified->hide();
    } else {
        name->setText(QFileInfo(new_path).completeBaseName());
        name->setToolTip(QDir::toNativeSeparators(new_path));
        modified->setVisible(new_modified);
    }
    name->setProperty("untitled", new_path.isEmpty());
    repolish(name);
    update_slots();
}

void PresetBar::set_ab_state(int active) {
    ab_active = active;
    for (int i = 0; i < 2; ++i) {
        ab[i]->setChecked(i == active);
        ab[i]->setToolTip(i == active
                              ? QString("Editing state %1").arg(i == 0 ? 'A' : 'B')
                              : QString("Switch to state %1 (the current one is kept as %2)")
                                    .arg(i == 0 ? 'A' : 'B', i == 0 ? 'B' : 'A'));
    }
    ab_copy->setText(active == 0 ? "A→B" : "B→A");
    ab_copy->setToolTip(QString("Copy the current state over %1").arg(active == 0 ? 'B' : 'A'));
}

void PresetBar::populate_preset_menu(QMenu* menu) {
    menu->clear();

    QList<const LibraryEntry*> presets;
    for (const auto& e : library_index.get_entries()) {
        if (e.kind == LibraryEntry::Kind::PRESET) {
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
    std::function<QMenu*(const QString&)> menu_for = [&](const QString& folder) -> QMenu* {
        if (folder.isEmpty()) {
            return menu;
        }
        if (auto it = folders.find(folder); it != folders.end()) {
            return *it;
        }
        const qsizetype slash = folder.lastIndexOf('/');
        QMenu* parent = menu_for(slash < 0 ? QString() : folder.left(slash));
        QMenu* sub = parent->addMenu(folder.mid(slash + 1));
        folders.insert(folder, sub);
        return sub;
    };
    for (const LibraryEntry* e : presets) {
        if (!e->folder.isEmpty()) {
            menu_for(e->folder);
        }
    }

    for (const LibraryEntry* e : presets) {
        QAction* a = menu_for(e->folder)->addAction(e->name);
        a->setCheckable(true);
        a->setChecked(e->path == path);
        a->setToolTip(e->summary);
        const QString entry_path = e->path;
        connect(a, &QAction::triggered, this, [this, entry_path] {
            emit preset_chosen(entry_path);
        });
    }
}

void PresetBar::activate_slot(int i) {
    const QString& p = slot_paths[i];
    if (!p.isEmpty()) {
        if (QFile::exists(p)) {
            emit preset_chosen(p);
        }
        return;
    }
    // Empty slot: a click stores the current preset there.
    if (path.isEmpty()) {
        return;
    }
    slot_paths[i] = path;
    QSettings().setValue("presets/slots", slot_paths);
    update_slots();
}

void PresetBar::show_slot_menu(int i, const QPoint& global_pos) {
    QMenu menu(this);
    QAction* store = menu.addAction(path.isEmpty() ? QString("Store current preset (save it first)")
                                                   : QString("Store \"%1\" in slot %2")
                                                         .arg(QFileInfo(path).completeBaseName())
                                                         .arg(i + 1));
    store->setEnabled(!path.isEmpty());
    QAction* clear = menu.addAction(QString("Clear slot %1").arg(i + 1));
    clear->setEnabled(!slot_paths[i].isEmpty());
    if (learn_hook) {
        menu.addSeparator();
        learn_hook(QString("action:slot%1").arg(i + 1), &menu);
    }

    QAction* picked = menu.exec(global_pos);
    if (!picked || (picked != store && picked != clear)) {
        return;
    }
    slot_paths[i] = picked == store ? path : QString();
    QSettings().setValue("presets/slots", slot_paths);
    update_slots();
}

void PresetBar::update_slots() {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const QString& p = slot_paths[i];
        QToolButton* b = quick_slots[i];
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
        b->setProperty("active", filled && p == path);
        repolish(b);
    }
}

} // namespace npl

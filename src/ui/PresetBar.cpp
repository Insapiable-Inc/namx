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

PresetBar::PresetBar(const LibraryIndex& new_index, QMenu* preset_menu, QWidget* parent)
    : QFrame(parent)
    , m_library_index(new_index) {
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

    auto* prev = tool("‹", "Previous preset in the library", "heroArrow");
    auto* next = tool("›", "Next preset in the library", "heroArrow");
    connect(prev, &QToolButton::clicked, this, [this] {
        emit step_requested(-1);
    });
    connect(next, &QToolButton::clicked, this, [this] {
        emit step_requested(+1);
    });

    // Name button: the library's presets as a menu, folders as submenus.
    m_name = tool({}, {}, "presetName");
    m_name->setPopupMode(QToolButton::InstantPopup);
    auto* list_menu = new QMenu(m_name);
    connect(list_menu, &QMenu::aboutToShow, this, [this, list_menu] {
        populate_preset_menu(list_menu);
    });
    m_name->setMenu(list_menu);

    m_modified = new QLabel("*", this);
    m_modified->setObjectName("presetModified");
    m_modified->setToolTip("Modified since the preset was loaded or saved");

    learn_menu_for(prev, "action:presetPrev");
    learn_menu_for(next, "action:presetNext");
    lay->addWidget(prev);
    lay->addWidget(m_name);
    lay->addWidget(m_modified);
    lay->addWidget(next);
    lay->addSpacing(8);

    auto* save = tool("Save", "Save the preset (Ctrl+S)", "presetTool");
    auto* save_as = tool("Save As…", "Save as a new preset (Ctrl+Shift+S)", "presetTool");
    connect(save, &QToolButton::clicked, this, &PresetBar::save_requested);
    connect(save_as, &QToolButton::clicked, this, &PresetBar::save_as_requested);
    lay->addWidget(save);
    lay->addWidget(save_as);
    lay->addStretch(1);

    // Quick slots 1–8: click / Ctrl+N loads, right-click stores or clears.
    auto* slots_caption = new QLabel("Slots", this);
    slots_caption->setObjectName("presetCaption");
    lay->addWidget(slots_caption);
    m_slot_paths = QSettings().value("presets/slots").toStringList();
    while (m_slot_paths.size() < K_SLOT_COUNT) {
        m_slot_paths << QString();
    }
    for (int i = 0; i < K_SLOT_COUNT; ++i) {
        auto* b = tool(QString::number(i + 1), {}, "presetSlot");
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
        m_quick_slots[i] = b;
        m_action_widgets.insert(QString("action:slot%1").arg(i + 1), b);
        lay->addWidget(b);
    }
    lay->addSpacing(12);

    // A/B compare: two in-memory states; the owner swaps them.
    for (int i = 0; i < 2; ++i) {
        m_ab[i] = tool(i == 0 ? "A" : "B", {}, "presetAb");
        m_ab[i]->setCheckable(true);
        connect(m_ab[i], &QToolButton::clicked, this, [this, i] {
            m_ab[i]->setChecked(i == m_ab_active); // the owner confirms via setAbState
            emit ab_switch_requested(i);
        });
        lay->addWidget(m_ab[i]);
    }
    learn_menu_for(m_ab[0], "action:abToggle");
    learn_menu_for(m_ab[1], "action:abToggle");
    m_ab_copy = tool({}, {}, "presetTool");
    connect(m_ab_copy, &QToolButton::clicked, this, &PresetBar::ab_copy_requested);
    lay->addWidget(m_ab_copy);
    lay->addSpacing(8);

    auto* more = tool("⋯", "Preset menu", "presetTool");
    more->setPopupMode(QToolButton::InstantPopup);
    more->setMenu(preset_menu);
    lay->addWidget(more);

    set_preset({}, false);
    set_ab_state(0);
}

void PresetBar::learn_menu_for(QWidget* w, const QString& action_id) {
    if (!m_action_widgets.contains(action_id)) {
        m_action_widgets.insert(action_id, w);
    }
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w, &QWidget::customContextMenuRequested, this, [this, w, action_id](const QPoint& p) {
        if (!m_learn_hook) {
            return;
        }
        QMenu menu(this);
        m_learn_hook(action_id, &menu);
        if (!menu.isEmpty()) {
            menu.exec(w->mapToGlobal(p));
        }
    });
}

void PresetBar::recall_slot(int i) {
    if (i < 0 || i >= K_SLOT_COUNT) {
        return;
    }
    const QString& p = m_slot_paths[i];
    if (!p.isEmpty() && QFile::exists(p)) {
        emit preset_chosen(p);
    }
}

void PresetBar::set_preset(const QString& new_path, bool new_modified) {
    m_path = new_path;
    if (new_path.isEmpty()) {
        m_name->setText("Untitled");
        m_name->setToolTip("Not saved as a preset — click to pick one");
        m_modified->hide();
    } else {
        m_name->setText(QFileInfo(new_path).completeBaseName());
        m_name->setToolTip(QDir::toNativeSeparators(new_path));
        m_modified->setVisible(new_modified);
    }
    m_name->setProperty("untitled", new_path.isEmpty());
    repolish(m_name);
    update_slots();
}

void PresetBar::set_ab_state(int active) {
    m_ab_active = active;
    for (int i = 0; i < 2; ++i) {
        m_ab[i]->setChecked(i == active);
        m_ab[i]->setToolTip(i == active
                                ? QString("Editing state %1").arg(i == 0 ? 'A' : 'B')
                                : QString("Switch to state %1 (the current one is kept as %2)")
                                      .arg(i == 0 ? 'A' : 'B', i == 0 ? 'B' : 'A'));
    }
    m_ab_copy->setText(active == 0 ? "A→B" : "B→A");
    m_ab_copy->setToolTip(QString("Copy the current state over %1").arg(active == 0 ? 'B' : 'A'));
}

void PresetBar::populate_preset_menu(QMenu* menu) {
    menu->clear();

    QList<const LibraryEntry*> presets;
    for (const auto& e : m_library_index.get_entries()) {
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
        a->setChecked(e->path == m_path);
        a->setToolTip(e->summary);
        const QString entry_path = e->path;
        connect(a, &QAction::triggered, this, [this, entry_path] {
            emit preset_chosen(entry_path);
        });
    }
}

void PresetBar::activate_slot(int i) {
    const QString& p = m_slot_paths[i];
    if (!p.isEmpty()) {
        if (QFile::exists(p)) {
            emit preset_chosen(p);
        }
        return;
    }
    // Empty slot: a click stores the current preset there.
    if (m_path.isEmpty()) {
        return;
    }
    m_slot_paths[i] = m_path;
    QSettings().setValue("presets/slots", m_slot_paths);
    update_slots();
}

void PresetBar::show_slot_menu(int i, const QPoint& global_pos) {
    QMenu menu(this);
    QAction* store =
        menu.addAction(m_path.isEmpty() ? QString("Store current preset (save it first)")
                                        : QString("Store \"%1\" in slot %2")
                                              .arg(QFileInfo(m_path).completeBaseName())
                                              .arg(i + 1));
    store->setEnabled(!m_path.isEmpty());
    QAction* clear = menu.addAction(QString("Clear slot %1").arg(i + 1));
    clear->setEnabled(!m_slot_paths[i].isEmpty());
    if (m_learn_hook) {
        menu.addSeparator();
        m_learn_hook(QString("action:slot%1").arg(i + 1), &menu);
    }

    QAction* picked = menu.exec(global_pos);
    if (!picked || (picked != store && picked != clear)) {
        return;
    }
    m_slot_paths[i] = picked == store ? m_path : QString();
    QSettings().setValue("presets/slots", m_slot_paths);
    update_slots();
}

void PresetBar::update_slots() {
    for (int i = 0; i < K_SLOT_COUNT; ++i) {
        const QString& p = m_slot_paths[i];
        QToolButton* b = m_quick_slots[i];
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
        b->setProperty("active", filled && p == m_path);
        repolish(b);
    }
}

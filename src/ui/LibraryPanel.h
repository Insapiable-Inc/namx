#pragma once

#include <QAbstractListModel>
#include <QListView>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QWidget>

#include "Common.h"
#include "LibraryIndex.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTimer;

namespace npl
{

// All library entries (every kind) as a flat list model. Favorites, recents
// and the currently loaded file per kind are overlaid here as roles so the
// proxies and the delegate never touch QSettings.
class LibraryListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        PATH_ROLE = Qt::UserRole + 1,
        KIND_ROLE,
        NAME_ROLE,
        FOLDER_ROLE,
        SUMMARY_ROLE,
        DETAILS_ROLE,
        FAVORITE_ROLE,
        RECENT_RANK_ROLE,
        MTIME_ROLE,
        LOADED_ROLE,
    };

    explicit LibraryListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void set_entries(const QList<LibraryEntry>& new_entries);
    void set_favorites(const QSet<QString>& favs);
    void set_recents(const QStringList& new_recents); // most recent first
    void set_loaded(LibraryEntry::Kind kind, const QString& path);

    bool is_favorite(const QString& path) const {
        return favorites.contains(path);
    }
    int row_of(const QString& path) const;
    QString get_loaded(LibraryEntry::Kind kind) const {
        return loaded[static_cast<int>(kind)];
    }

private:
    QList<LibraryEntry> entries;
    QSet<QString> favorites;
    QStringList recents;
    QString loaded[3];
};

// One proxy per kind: kind filter, search words, All / Favorites / Recent
// mode and the sort key. Recent mode always sorts by recency.
class LibraryFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum class Mode
    {
        ALL,
        FAVORITES,
        RECENT
    };
    enum class Sort
    {
        NAME,
        NEWEST
    };

    explicit LibraryFilterProxy(LibraryEntry::Kind new_kind, QObject* parent = nullptr);

    void set_search(const QString& text);
    void set_mode(Mode m);
    void set_sort_key(Sort s);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override;

private:
    LibraryEntry::Kind kind;
    QStringList words;
    Mode mode{Mode::ALL};
    Sort sort_order{Sort::NAME};
};

// Two-line rows (name / summary), a ★ toggle at the right edge, and an
// accent bar + accent name for the entry that is currently loaded.
class LibraryItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    static QRect star_rect(const QRect& row);
};

// QListView that routes a click on the ★ to favoriteToggled instead of
// changing the selection, and Return/Enter to activated.
class LibraryListView : public QListView
{
    Q_OBJECT
public:
    using QListView::QListView;
signals:
    void favorite_toggled(const QString& path);

protected:
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    // Typed letters/digits are effect shortcuts window-wide; the search field
    // is the way to search, so type-ahead in the list is off.
    void keyboardSearch(const QString&) override {
    }
};

// The Library dock content: Models / IRs / Presets sections, search,
// favorites, recents, sort, and the list. Selecting an entry (click, arrow
// keys, PgUp/PgDn, Return) emits activated(); the owner loads the file and
// reports back through setLoadedPath() so the list can mark it.
class LibraryPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LibraryPanel(LibraryIndex& new_index, QWidget* parent = nullptr);

    // Switch to a section and give the list keyboard focus.
    void show_section(LibraryEntry::Kind kind);
    void focus_search();

    // Mark the loaded file of a kind (empty = nothing loaded).
    void set_loaded_path(LibraryEntry::Kind kind, const QString& path);
    QString loaded_path(LibraryEntry::Kind kind) const;
    // Push a path to the top of the recents list (any load, any source).
    void note_used(const QString& path);

    // Path `step` rows away from `fromPath` in the section's current visible
    // order (wraps around); empty when the section is empty.
    QString neighbour(LibraryEntry::Kind kind, const QString& from_path, int step) const;

    // Root-folder editor dialog; applies to the index on OK.

signals:
    // The folders button: the owner opens the Library settings page.
    void folders_requested();
    // The user picked a file in the list.
    void activated(LibraryEntry::Kind kind, const QString& path);

private:
    void set_section(int new_section, bool focus_list);
    void on_entries_changed();
    void on_current_changed();
    void activate_current();
    void toggle_favorite(const QString& path);
    void update_counts();
    void restore_current_from_loaded();
    void apply_search(const QString& text);
    void set_mode(int mode); // 0 All, 1 Favorites, 2 Recent (the mode buttons)
    void set_sort_key(int key); // index into the sort box
    LibraryFilterProxy* proxy(LibraryEntry::Kind kind) const {
        return proxies[static_cast<int>(kind)];
    }

    LibraryIndex& library_index;
    LibraryListModel* model = nullptr;
    LibraryFilterProxy* proxies[3]{};
    QToolButton* section_btn[3]{};
    QToolButton* mode_btn[3]{};
    QComboBox* sort_box = nullptr;
    QLineEdit* search = nullptr;
    LibraryListView* view = nullptr;
    QLabel* status = nullptr;
    QTimer* activate_timer = nullptr; // debounces arrow-key browsing
    int section = 0;
    bool syncing = false; // programmatic selection changes
    QSet<QString> favorites;
    QStringList recents;
};

} // namespace npl

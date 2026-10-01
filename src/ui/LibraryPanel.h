#pragma once

#include <QAbstractListModel>
#include <QListView>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QWidget>

#include "LibraryIndex.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;
class QTimer;

/// All library entries (every kind) as a flat list model. Favorites, recents
/// and the currently loaded file per kind are overlaid here as roles so the
/// proxies and the delegate never touch QSettings.
class LibraryListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        PathRole = Qt::UserRole + 1,
        KindRole,
        NameRole,
        FolderRole,
        SummaryRole,
        DetailsRole,
        FavoriteRole,
        RecentRankRole,
        MtimeRole,
        LoadedRole,
    };

    explicit LibraryListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void setEntries(const QList<LibraryEntry>& entries);
    void setFavorites(const QSet<QString>& favs);
    void setRecents(const QStringList& recents); // most recent first
    void setLoaded(LibraryEntry::Kind kind, const QString& path);

    bool isFavorite(const QString& path) const {
        return mFavorites.contains(path);
    }
    int rowOf(const QString& path) const;
    QString loaded(LibraryEntry::Kind kind) const {
        return mLoaded[static_cast<int>(kind)];
    }

private:
    QList<LibraryEntry> mEntries;
    QSet<QString> mFavorites;
    QStringList mRecents;
    QString mLoaded[3];
};

/// One proxy per kind: kind filter, search words, All / Favorites / Recent
/// mode and the sort key. Recent mode always sorts by recency.
class LibraryFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum class Mode
    {
        All,
        Favorites,
        Recent
    };
    enum class Sort
    {
        Name,
        Newest
    };

    explicit LibraryFilterProxy(LibraryEntry::Kind kind, QObject* parent = nullptr);

    void setSearch(const QString& text);
    void setMode(Mode m);
    void setSortKey(Sort s);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override;

private:
    LibraryEntry::Kind mKind;
    QStringList mWords;
    Mode mMode{Mode::All};
    Sort mSort{Sort::Name};
};

/// Two-line rows (name / summary), a ★ toggle at the right edge, and an
/// accent bar + accent name for the entry that is currently loaded.
class LibraryItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    static QRect starRect(const QRect& row);
};

/// QListView that routes a click on the ★ to favoriteToggled instead of
/// changing the selection, and Return/Enter to activated.
class LibraryListView : public QListView
{
    Q_OBJECT
public:
    using QListView::QListView;
signals:
    void favoriteToggled(const QString& path);

protected:
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void keyPressEvent(QKeyEvent* ev) override;
    // Typed letters/digits are effect shortcuts window-wide; the search field
    // is the way to search, so type-ahead in the list is off.
    void keyboardSearch(const QString&) override {
    }
};

/// The Library dock content: Models / IRs / Presets sections, search,
/// favorites, recents, sort, and the list. Selecting an entry (click, arrow
/// keys, PgUp/PgDn, Return) emits activated(); the owner loads the file and
/// reports back through setLoadedPath() so the list can mark it.
class LibraryPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LibraryPanel(LibraryIndex& index, QWidget* parent = nullptr);

    /// Switch to a section and give the list keyboard focus.
    void showSection(LibraryEntry::Kind kind);
    void focusSearch();

    /// Mark the loaded file of a kind (empty = nothing loaded).
    void setLoadedPath(LibraryEntry::Kind kind, const QString& path);
    QString loadedPath(LibraryEntry::Kind kind) const;
    /// Push a path to the top of the recents list (any load, any source).
    void noteUsed(const QString& path);

    /// Path `step` rows away from `fromPath` in the section's current visible
    /// order (wraps around); empty when the section is empty.
    QString neighbour(LibraryEntry::Kind kind, const QString& fromPath, int step) const;

    /// Root-folder editor dialog; applies to the index on OK.
    void editFolders();

signals:
    /// The user picked a file in the list.
    void activated(LibraryEntry::Kind kind, const QString& path);

private:
    void setSection(int section, bool focusList);
    void onEntriesChanged();
    void onCurrentChanged();
    void activateCurrent();
    void toggleFavorite(const QString& path);
    void updateCounts();
    void restoreCurrentFromLoaded();
    LibraryFilterProxy* proxy(LibraryEntry::Kind kind) const {
        return mProxies[static_cast<int>(kind)];
    }

    LibraryIndex& mIndex;
    LibraryListModel* mModel = nullptr;
    LibraryFilterProxy* mProxies[3]{};
    QToolButton* mSectionBtn[3]{};
    QToolButton* mModeBtn[3]{};
    QComboBox* mSortBox = nullptr;
    QLineEdit* mSearch = nullptr;
    LibraryListView* mView = nullptr;
    QLabel* mStatus = nullptr;
    QTimer* mActivateTimer = nullptr; // debounces arrow-key browsing
    int mSection = 0;
    bool mSyncing = false; // programmatic selection changes
    QSet<QString> mFavorites;
    QStringList mRecents;
};

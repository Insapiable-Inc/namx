#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QListView>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QWidget>

#include "LibraryIndex.h"
#include "Tone3000Client.h"

class QLabel;
class QLineEdit;
class QMouseEvent;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QToolButton;

/// Tone cards for one list (trending / latest / favorites / …).
class ToneListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        ToneIdRole = Qt::UserRole + 1,
        TitleRole,
        GearRole,
        FormatRole,
        CreatorRole,
        FavoriteRole,
        DownloadedRole,
        ThumbRole,
        AvatarRole,
        LicenseRole
    };

    ToneListModel(Tone3000Client& client, LibraryIndex& index, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void setTones(const QList<T3kTone>& tones);
    const T3kTone* tone(int row) const {
        return row >= 0 && row < mTones.size() ? &mTones[row] : nullptr;
    }
    void setFavorite(int toneId, bool on);
    void refreshDownloaded();

private:
    void requestImages();

    Tone3000Client& mClient;
    LibraryIndex& mIndex;
    QList<T3kTone> mTones;
    QHash<QString, QPixmap> mImages; // url → scaled pixmap (thumbs + avatars)
    QSet<QString> mRequested;
    QHash<int, bool> mDownloaded; // tone id → any model in the library
};

/// Card: 44 px image, title, "gear · format", creator avatar + @name, ★ and ✓.
class ToneDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    static QRect starRect(const QRect& row);
};

/// Models of one tone with per-row download state.
class ToneModelsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum State
    {
        Idle,
        Downloading,
        Done,
        Failed
    };
    enum Role
    {
        ModelIdRole = Qt::UserRole + 1,
        NameRole,
        InfoRole,
        StateRole,
        ProgressRole,
        LocalPathRole,
        ErrorRole
    };

    explicit ToneModelsModel(QObject* parent = nullptr)
        : QAbstractListModel(parent) {
    }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void setModels(const QList<T3kModel>& models, const T3kTone& tone, LibraryIndex& index);
    const T3kModel* model(int row) const {
        return row >= 0 && row < mModels.size() ? &mModels[row].model : nullptr;
    }
    int rowOf(int modelId) const;
    void setState(int modelId, State s, const QString& pathOrError = {});
    void setProgress(int modelId, int percent);
    QList<T3kModel> pending() const; // not yet downloaded

private:
    struct Row
    {
        T3kModel model;
        State state = Idle;
        int progress = 0;
        QString path, error;
    };
    QList<Row> mModels;
};

class ToneModelDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
};

/// The TONE3000 tab of the Library dock. States: no publishable key → setup;
/// signed out → partnership splash; signed in → bounded lists (Trending,
/// Latest, Favorites, Downloads, Mine) + "Browse TONE3000" (select flow);
/// tone detail with per-model download / load.
class Tone3000Panel : public QWidget
{
    Q_OBJECT
public:
    Tone3000Panel(Tone3000Client& client, LibraryIndex& index, QWidget* parent = nullptr);

    /// Open a tone's detail page (after the select flow, or from a preset).
    void showTone(int toneId);
    /// Start the browser-based catalog picker.
    void browseCatalog();

signals:
    /// A downloaded model/IR should be loaded into the rig.
    void loadRequested(LibraryEntry::Kind kind, const QString& path);

protected:
    // Logo click → site; tone card click → detail / ★.
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    enum Page
    {
        PageSetup,
        PageSplash,
        PageBrowse,
        PageDetail
    };

    bool handleToneViewClick(QMouseEvent* me);
    static QString pickDownloadDir(QWidget* parent);

    void buildHeader();
    void buildSetupPage();
    void buildSplashPage();
    void buildBrowsePage();
    void buildDetailPage();

    void syncState(); // pick the page from key / auth state
    void updateUserHeader();
    void selectList(int which, bool force = false);
    void loadList(bool useCache);
    void showListStatus(const QString& text);
    void openDetail(const T3kTone& tone);
    void loadDetailModels();
    void startDownload(int row);
    void downloadAll();
    void onModelRowClicked(const QModelIndex& idx);
    void toggleFavorite(int toneId, bool on);
    void setThumbnail(QLabel* label, const QString& url, const QSize& size, bool round);

    Tone3000Client& mClient;
    LibraryIndex& mIndex;

    QStackedWidget* mPages = nullptr;
    QLabel* mLogo = nullptr;
    QToolButton* mUserBtn = nullptr;
    QLabel* mNotice = nullptr; // transient status line under the header

    // Setup
    QLineEdit* mKeyEdit = nullptr;
    // Browse
    QToolButton* mListBtn[5]{};
    int mList = 0;
    int mPage = 1, mTotalPages = 1;
    ToneListModel* mToneModel = nullptr;
    QListView* mToneView = nullptr;
    QLabel* mListStatus = nullptr;
    QToolButton* mPrevPage = nullptr;
    QToolButton* mNextPage = nullptr;
    QPushButton* mBrowseBtn = nullptr;
    // Detail
    T3kTone mDetailTone;
    QLabel* mDetailImage = nullptr;
    QLabel* mDetailTitle = nullptr;
    QLabel* mDetailMeta = nullptr;
    QLabel* mDetailCreatorAvatar = nullptr;
    QLabel* mDetailCreator = nullptr;
    QLabel* mDetailDesc = nullptr;
    QToolButton* mDetailFav = nullptr;
    QLabel* mDetailStatus = nullptr;
    ToneModelsModel* mModelsModel = nullptr;
    QListView* mModelsView = nullptr;
    QPushButton* mDownloadAll = nullptr;
    int mDetailRequest = 0; // ignore stale replies
    bool mNoticeBusy = false;
};

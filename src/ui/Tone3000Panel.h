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
        TONE_ID_ROLE = Qt::UserRole + 1,
        TITLE_ROLE,
        GEAR_ROLE,
        FORMAT_ROLE,
        CREATOR_ROLE,
        FAVORITE_ROLE,
        DOWNLOADED_ROLE,
        THUMB_ROLE,
        AVATAR_ROLE,
        LICENSE_ROLE
    };

    ToneListModel(Tone3000Client& new_client, LibraryIndex& new_index, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void set_tones(const QList<T3kTone>& new_tones);
    const T3kTone* tone(int row) const {
        return row >= 0 && row < m_tones.size() ? &m_tones[row] : nullptr;
    }
    void set_favorite(int tone_id, bool on);
    void refresh_downloaded();

private:
    void request_images();

    Tone3000Client& m_client;
    LibraryIndex& m_library_index;
    QList<T3kTone> m_tones;
    QHash<QString, QPixmap> m_images; // url → scaled pixmap (thumbs + avatars)
    QSet<QString> m_requested;
    QHash<int, bool> m_downloaded; // tone id → any model in the library
};

/// Card: 44 px image, title, "gear · format", creator avatar + @name, ★ and ✓.
class ToneDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    static QRect star_rect(const QRect& row);
};

/// Models of one tone with per-row download state.
class ToneModelsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum State
    {
        IDLE,
        DOWNLOADING,
        DONE,
        FAILED
    };
    enum Role
    {
        MODEL_ID_ROLE = Qt::UserRole + 1,
        NAME_ROLE,
        INFO_ROLE,
        STATE_ROLE,
        PROGRESS_ROLE,
        LOCAL_PATH_ROLE,
        ERROR_ROLE
    };

    explicit ToneModelsModel(QObject* parent = nullptr)
        : QAbstractListModel(parent) {
    }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void set_models(const QList<T3kModel>& new_models, const T3kTone& tone, LibraryIndex& index);
    const T3kModel* model(int row) const {
        return row >= 0 && row < m_models.size() ? &m_models[row].model : nullptr;
    }
    int row_of(int model_id) const;
    void set_state(int model_id, State s, const QString& path_or_error = {});
    void set_progress(int model_id, int percent);
    QList<T3kModel> pending() const; // not yet downloaded

private:
    struct Row
    {
        T3kModel model;
        State state = IDLE;
        int progress = 0;
        QString path, error;
    };
    QList<Row> m_models;
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
    Tone3000Panel(Tone3000Client& new_client, LibraryIndex& new_index, QWidget* parent = nullptr);

    /// Open a tone's detail page (after the select flow, or from a preset).
    void show_tone(int tone_id);
    /// Start the browser-based catalog picker.
    void browse_catalog();

signals:
    /// A downloaded model/IR should be loaded into the rig.
    void load_requested(LibraryEntry::Kind kind, const QString& path);

protected:
    // Logo click → site; tone card click → detail / ★.
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    enum Page
    {
        PAGE_SETUP,
        PAGE_SPLASH,
        PAGE_BROWSE,
        PAGE_DETAIL
    };

    bool handle_tone_view_click(QMouseEvent* me);
    static QString pick_download_dir(QWidget* parent);

    void build_header();
    void build_setup_page();
    void build_splash_page();
    void build_browse_page();
    void build_detail_page();

    void sync_state(); // pick the page from key / auth state
    void update_user_header();
    void select_list(int which, bool force = false);
    void load_list(bool use_cache);
    void show_list_status(const QString& text);
    void open_detail(const T3kTone& tone);
    void load_detail_models();
    void start_download(int row);
    void download_all();
    void on_model_row_clicked(const QModelIndex& idx);
    void toggle_favorite(int tone_id, bool on);
    void set_thumbnail(QLabel* label, const QString& url, const QSize& size, bool round);

    Tone3000Client& m_client;
    LibraryIndex& m_library_index;

    QStackedWidget* m_pages = nullptr;
    QLabel* m_logo = nullptr;
    QToolButton* m_user_btn = nullptr;
    QLabel* m_notice = nullptr; // transient status line under the header

    // Setup
    QLineEdit* m_key_edit = nullptr;
    // Browse
    QToolButton* m_list_btn[5]{};
    int m_list = 0;
    int m_list_page = 1, m_total_pages = 1;
    ToneListModel* m_tone_model = nullptr;
    QListView* m_tone_view = nullptr;
    QLabel* m_list_status = nullptr;
    QToolButton* m_prev_page = nullptr;
    QToolButton* m_next_page = nullptr;
    QPushButton* m_browse_btn = nullptr;
    // Detail
    T3kTone m_detail_tone;
    QLabel* m_detail_image = nullptr;
    QLabel* m_detail_title = nullptr;
    QLabel* m_detail_meta = nullptr;
    QLabel* m_detail_creator_avatar = nullptr;
    QLabel* m_detail_creator = nullptr;
    QLabel* m_detail_desc = nullptr;
    QToolButton* m_detail_fav = nullptr;
    QLabel* m_detail_status = nullptr;
    ToneModelsModel* m_models_model = nullptr;
    QListView* m_models_view = nullptr;
    QPushButton* m_download_all_button = nullptr;
    int m_detail_request = 0; // ignore stale replies
    bool m_notice_busy = false;
};

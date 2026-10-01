#include "LibraryPanel.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QApplication>
#include <QCollator>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Theme.h"

namespace
{

constexpr int K_ROW_HEIGHT = 42;
constexpr int K_STAR_WIDTH = 26;
constexpr int K_ACCENT_BAR = 3;
constexpr int K_MAX_RECENTS = 30;

const char* k_section_names[3] = {"Models", "IRs", "Presets"};
const char* k_section_single[3] = {"model", "IR", "preset"};

QColor kind_accent(LibraryEntry::Kind kind) {
    switch (kind) {
        case LibraryEntry::Kind::MODEL:
            return Theme::accent("nam");
        case LibraryEntry::Kind::IR:
            return Theme::accent("ir");
        case LibraryEntry::Kind::PRESET:
            return Theme::generic_accent();
    }
    return Theme::generic_accent();
}

} // namespace

// ===========================================================================
// LibraryListModel
// ===========================================================================

LibraryListModel::LibraryListModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int LibraryListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant LibraryListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size()) {
        return {};
    }
    const LibraryEntry& e = m_entries[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case NAME_ROLE:
            return e.name;
        case Qt::ToolTipRole: {
            QString tip = "<b>" + e.name.toHtmlEscaped() + "</b>";
            if (!e.details.isEmpty()) {
                tip += "<br>" + e.details.toHtmlEscaped().replace('\n', "<br>");
            }
            tip += "<br><i>" + QDir::toNativeSeparators(e.path).toHtmlEscaped() + "</i>";
            return tip;
        }
        case PATH_ROLE:
            return e.path;
        case KIND_ROLE:
            return static_cast<int>(e.kind);
        case FOLDER_ROLE:
            return e.folder;
        case SUMMARY_ROLE:
            return e.summary;
        case DETAILS_ROLE:
            return e.details;
        case FAVORITE_ROLE:
            return m_favorites.contains(e.path);
        case RECENT_RANK_ROLE:
            return static_cast<int>(m_recents.indexOf(e.path));
        case MTIME_ROLE:
            return e.mtime_ms;
        case LOADED_ROLE:
            return m_loaded[static_cast<int>(e.kind)] == e.path;
        default:
            return {};
    }
}

void LibraryListModel::set_entries(const QList<LibraryEntry>& new_entries) {
    beginResetModel();
    m_entries = new_entries;
    endResetModel();
}

void LibraryListModel::set_favorites(const QSet<QString>& favs) {
    m_favorites = favs;
    if (!m_entries.isEmpty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {FAVORITE_ROLE});
    }
}

void LibraryListModel::set_recents(const QStringList& new_recents) {
    m_recents = new_recents;
    if (!m_entries.isEmpty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {RECENT_RANK_ROLE});
    }
}

void LibraryListModel::set_loaded(LibraryEntry::Kind kind, const QString& path) {
    QString& slot = m_loaded[static_cast<int>(kind)];
    const int before = row_of(slot);
    slot = QDir::cleanPath(path);
    const int after = row_of(slot);
    if (before >= 0) {
        emit dataChanged(index(before), index(before), {LOADED_ROLE});
    }
    if (after >= 0) {
        emit dataChanged(index(after), index(after), {LOADED_ROLE});
    }
}

int LibraryListModel::row_of(const QString& path) const {
    if (path.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == path) {
            return i;
        }
    }
    return -1;
}

// ===========================================================================
// LibraryFilterProxy
// ===========================================================================

LibraryFilterProxy::LibraryFilterProxy(LibraryEntry::Kind new_kind, QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_kind(new_kind) {
    setDynamicSortFilter(true);
    sort(0);
}

void LibraryFilterProxy::set_search(const QString& text) {
    // Qt 6.10 deprecated invalidateFilter(); Ubuntu 22.04's Qt 6.2 lacks the
    // replacement.
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_words = text.simplified().split(' ', Qt::SkipEmptyParts);
    endFilterChange();
#else
    mWords = text.simplified().split(' ', Qt::SkipEmptyParts);
    invalidateFilter();
#endif
}

void LibraryFilterProxy::set_mode(Mode m) {
    m_mode = m;
    invalidate();
}

void LibraryFilterProxy::set_sort_key(Sort s) {
    m_sort = s;
    invalidate();
}

bool LibraryFilterProxy::filterAcceptsRow(int row, const QModelIndex& parent) const {
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    if (i.data(LibraryListModel::KIND_ROLE).toInt() != static_cast<int>(m_kind)) {
        return false;
    }
    if (m_mode == Mode::FAVORITES && !i.data(LibraryListModel::FAVORITE_ROLE).toBool()) {
        return false;
    }
    if (m_mode == Mode::RECENT && i.data(LibraryListModel::RECENT_RANK_ROLE).toInt() < 0) {
        return false;
    }
    if (!m_words.isEmpty()) {
        const QString hay = i.data(LibraryListModel::NAME_ROLE).toString() + ' ' +
                            i.data(LibraryListModel::FOLDER_ROLE).toString() + ' ' +
                            i.data(LibraryListModel::SUMMARY_ROLE).toString();
        for (const QString& w : m_words) {
            if (!hay.contains(w, Qt::CaseInsensitive)) {
                return false;
            }
        }
    }
    return true;
}

bool LibraryFilterProxy::lessThan(const QModelIndex& a, const QModelIndex& b) const {
    if (m_mode == Mode::RECENT) {
        return a.data(LibraryListModel::RECENT_RANK_ROLE).toInt() <
               b.data(LibraryListModel::RECENT_RANK_ROLE).toInt();
    }
    if (m_sort == Sort::NEWEST) {
        const qint64 ma = a.data(LibraryListModel::MTIME_ROLE).toLongLong();
        const qint64 mb = b.data(LibraryListModel::MTIME_ROLE).toLongLong();
        if (ma != mb) {
            return ma > mb;
        }
    }
    static QCollator collator = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    const int by_name = collator.compare(a.data(LibraryListModel::NAME_ROLE).toString(),
                                         b.data(LibraryListModel::NAME_ROLE).toString());
    if (by_name != 0) {
        return by_name < 0;
    }
    return a.data(LibraryListModel::PATH_ROLE).toString() <
           b.data(LibraryListModel::PATH_ROLE).toString();
}

// ===========================================================================
// LibraryItemDelegate
// ===========================================================================

QRect LibraryItemDelegate::star_rect(const QRect& row) {
    return QRect(row.right() - K_STAR_WIDTH, row.top(), K_STAR_WIDTH, row.height());
}

QSize LibraryItemDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const {
    return QSize(opt.rect.width(), K_ROW_HEIGHT);
}

void LibraryItemDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                                const QModelIndex& idx) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    const QRect r = opt.rect;
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = opt.state & QStyle::State_MouseOver;
    const bool loaded = idx.data(LibraryListModel::LOADED_ROLE).toBool();
    const bool fav = idx.data(LibraryListModel::FAVORITE_ROLE).toBool();
    const auto kind =
        static_cast<LibraryEntry::Kind>(idx.data(LibraryListModel::KIND_ROLE).toInt());
    const QColor accent = kind_accent(kind);

    if (selected) {
        p->fillRect(r, Theme::track_hover());
    } else if (hovered) {
        p->fillRect(r, Theme::track());
    }

    if (loaded) {
        p->fillRect(QRect(r.left(), r.top() + 6, K_ACCENT_BAR, r.height() - 12), accent);
    }

    // Star column, then the text block fills what's left.
    const QRect star = star_rect(r);
    {
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() + 3);
        p->setFont(f);
        p->setPen(fav ? accent : (selected || hovered ? Theme::dim_text() : Theme::track()));
        p->drawText(star, Qt::AlignCenter, fav ? QStringLiteral("★") : QStringLiteral("☆"));
    }

    const int left = r.left() + 10;
    const int width = star.left() - 4 - left;

    QFont name_font = opt.font;
    name_font.setWeight(loaded ? QFont::DemiBold : QFont::Normal);
    p->setFont(name_font);
    const QFontMetrics nfm(name_font);
    p->setPen(loaded ? accent : opt.palette.color(QPalette::Text));
    const QString name =
        nfm.elidedText(idx.data(LibraryListModel::NAME_ROLE).toString(), Qt::ElideMiddle, width);
    p->drawText(QRect(left, r.top() + 5, width, nfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                name);

    QFont sub_font = opt.font;
    sub_font.setPointSizeF(sub_font.pointSizeF() - 1);
    p->setFont(sub_font);
    const QFontMetrics sfm(sub_font);
    p->setPen(Theme::dim_text());
    QStringList second;
    const QString folder = idx.data(LibraryListModel::FOLDER_ROLE).toString();
    if (!folder.isEmpty()) {
        second << folder;
    }
    const QString summary = idx.data(LibraryListModel::SUMMARY_ROLE).toString();
    if (!summary.isEmpty()) {
        second << summary;
    }
    const QString sum = sfm.elidedText(second.join(QStringLiteral(" · ")), Qt::ElideRight, width);
    p->drawText(QRect(left, r.top() + 5 + nfm.height() + 1, width, sfm.height()),
                Qt::AlignLeft | Qt::AlignVCenter, sum);

    p->restore();
}

// ===========================================================================
// LibraryListView
// ===========================================================================

void LibraryListView::mousePressEvent(QMouseEvent* ev) {
    const QModelIndex idx = indexAt(ev->pos());
    if (idx.isValid() && ev->button() == Qt::LeftButton &&
        LibraryItemDelegate::star_rect(visualRect(idx)).contains(ev->pos())) {
        emit favorite_toggled(idx.data(LibraryListModel::PATH_ROLE).toString());
        return;
    }
    QListView::mousePressEvent(ev);
}

void LibraryListView::mouseDoubleClickEvent(QMouseEvent* ev) {
    const QModelIndex idx = indexAt(ev->pos());
    if (idx.isValid() && LibraryItemDelegate::star_rect(visualRect(idx)).contains(ev->pos())) {
        return; // a fast double click on the star is two toggles, not a load
    }
    QListView::mouseDoubleClickEvent(ev);
}

void LibraryListView::keyPressEvent(QKeyEvent* ev) {
    if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter) {
        if (currentIndex().isValid()) {
            emit activated(currentIndex());
        }
        ev->accept();
        return;
    }
    QListView::keyPressEvent(ev);
}

// ===========================================================================
// Folders dialog
// ===========================================================================

namespace
{

class FoldersDialog : public QDialog
{
public:
    FoldersDialog(const LibraryRoots& roots, QWidget* parent)
        : QDialog(parent) {
        setWindowTitle("Library Folders");
        resize(560, 380);
        auto* lay = new QVBoxLayout(this);

        auto* hint = new QLabel("Each folder is scanned recursively, and the library follows "
                                "changes on disk. A folder can serve several kinds.");
        hint->setWordWrap(true);
        lay->addWidget(hint);

        auto* tabs = new QTabWidget;
        const QStringList* root_lists[3] = {&roots.models, &roots.irs, &roots.presets};
        for (int i = 0; i < 3; ++i) {
            auto* page = new QWidget;
            auto* pl = new QHBoxLayout(page);
            m_lists[i] = new QListWidget;
            m_lists[i]->addItems(*root_lists[i]);
            pl->addWidget(m_lists[i], 1);

            auto* btns = new QVBoxLayout;
            auto* add = new QPushButton("Add…");
            auto* rm = new QPushButton("Remove");
            btns->addWidget(add);
            btns->addWidget(rm);
            btns->addStretch();
            pl->addLayout(btns);

            QListWidget* lw = m_lists[i];
            connect(add, &QPushButton::clicked, this, [this, lw, i] {
                const QString start =
                    lw->count() ? lw->item(lw->count() - 1)->text() : QDir::homePath();
                const QString dir = QFileDialog::getExistingDirectory(
                    this, QString("Add %1 folder").arg(k_section_names[i]), start);
                if (dir.isEmpty()) {
                    return;
                }
                const QString clean = QDir::cleanPath(dir);
                for (int r = 0; r < lw->count(); ++r) {
                    if (lw->item(r)->text() == clean) {
                        return;
                    }
                }
                lw->addItem(clean);
            });
            connect(rm, &QPushButton::clicked, this, [lw] {
                delete lw->currentItem();
            });
            tabs->addTab(page, k_section_names[i]);
        }
        lay->addWidget(tabs, 1);

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        lay->addWidget(bb);
    }

    LibraryRoots roots() const {
        auto items = [](QListWidget* lw) {
            QStringList l;
            for (int r = 0; r < lw->count(); ++r) {
                l << lw->item(r)->text();
            }
            return l;
        };
        LibraryRoots r;
        r.models = items(m_lists[0]);
        r.irs = items(m_lists[1]);
        r.presets = items(m_lists[2]);
        return r;
    }

private:
    QListWidget* m_lists[3]{};
};

} // namespace

// ===========================================================================
// LibraryPanel
// ===========================================================================

LibraryPanel::LibraryPanel(LibraryIndex& new_index, QWidget* parent)
    : QWidget(parent)
    , m_library_index(new_index) {
    setObjectName("libraryPanel");
    setMinimumWidth(240);

    QSettings s;
    m_favorites = QSet<QString>(s.value("library/favorites").toStringList().begin(),
                                s.value("library/favorites").toStringList().end());
    m_recents = s.value("library/recents").toStringList();

    m_model = new LibraryListModel(this);
    m_model->set_favorites(m_favorites);
    m_model->set_recents(m_recents);
    for (int i = 0; i < 3; ++i) {
        m_proxies[i] = new LibraryFilterProxy(static_cast<LibraryEntry::Kind>(i), this);
        m_proxies[i]->setSourceModel(m_model);
    }

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // Section switch: Models | IRs | Presets (counts appended by updateCounts).
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);
        for (int i = 0; i < 3; ++i) {
            auto* b = new QToolButton;
            b->setObjectName("librarySection");
            b->setText(k_section_names[i]);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [this, i] {
                set_section(i, true);
            });
            row->addWidget(b);
            m_section_btn[i] = b;
        }
        lay->addLayout(row);
    }

    m_search = new QLineEdit;
    m_search->setObjectName("librarySearch");
    m_search->setPlaceholderText("Search…");
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
        for (auto* p : m_proxies) {
            p->set_search(t);
        }
        update_counts();
        restore_current_from_loaded();
    });
    lay->addWidget(m_search);

    // Mode buttons + sort key.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);
        const char* labels[3] = {"All", "★", "Recent"};
        const char* tips[3] = {"Everything", "Favorites only", "Recently used"};
        for (int i = 0; i < 3; ++i) {
            auto* b = new QToolButton;
            b->setObjectName("libraryMode");
            b->setText(labels[i]);
            b->setToolTip(tips[i]);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [this, i] {
                for (int k = 0; k < 3; ++k) {
                    m_mode_btn[k]->setChecked(k == i);
                }
                for (auto* p : m_proxies) {
                    p->set_mode(static_cast<LibraryFilterProxy::Mode>(i));
                }
                m_sort_box->setEnabled(i != 2);
                QSettings().setValue("library/mode", i);
                update_counts();
                restore_current_from_loaded();
            });
            row->addWidget(b);
            m_mode_btn[i] = b;
        }
        row->addStretch();
        m_sort_box = new QComboBox;
        m_sort_box->setObjectName("librarySort");
        m_sort_box->addItems({"Name", "Newest"});
        m_sort_box->setToolTip("Sort order");
        m_sort_box->setFocusPolicy(Qt::NoFocus);
        connect(m_sort_box, &QComboBox::currentIndexChanged, this, [this](int i) {
            for (auto* p : m_proxies) {
                p->set_sort_key(static_cast<LibraryFilterProxy::Sort>(i));
            }
            QSettings().setValue("library/sort", i);
            restore_current_from_loaded();
        });
        row->addWidget(m_sort_box);
        lay->addLayout(row);
    }

    m_view = new LibraryListView;
    m_view->setObjectName("libraryList");
    m_view->setItemDelegate(new LibraryItemDelegate(m_view));
    m_view->setUniformItemSizes(true);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setMouseTracking(true);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setFrameShape(QFrame::NoFrame);
    connect(m_view, &LibraryListView::favorite_toggled, this, &LibraryPanel::toggle_favorite);
    connect(m_view, &QAbstractItemView::activated, this, [this] {
        activate_current();
    });
    lay->addWidget(m_view, 1);

    // Footer: count / scanning state, folders, rescan.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        m_status = new QLabel;
        m_status->setObjectName("libraryStatus");
        row->addWidget(m_status, 1);

        auto* folders = new QToolButton;
        folders->setObjectName("libraryTool");
        folders->setText("Folders…");
        folders->setAutoRaise(true);
        folders->setFocusPolicy(Qt::NoFocus);
        folders->setToolTip("Choose which folders the library scans");
        connect(folders, &QToolButton::clicked, this, &LibraryPanel::edit_folders);
        row->addWidget(folders);

        auto* rescan = new QToolButton;
        rescan->setObjectName("libraryTool");
        rescan->setText("↻");
        rescan->setAutoRaise(true);
        rescan->setFocusPolicy(Qt::NoFocus);
        rescan->setToolTip("Rescan the library folders");
        connect(rescan, &QToolButton::clicked, &m_library_index, &LibraryIndex::rescan);
        row->addWidget(rescan);
        lay->addLayout(row);
    }

    // Browsing with the arrow keys loads the entry under the cursor once the
    // cursor rests — a full model load per keystroke would stutter.
    m_activate_timer = new QTimer(this);
    m_activate_timer->setSingleShot(true);
    m_activate_timer->setInterval(180);
    connect(m_activate_timer, &QTimer::timeout, this, &LibraryPanel::activate_current);

    connect(&m_library_index, &LibraryIndex::scan_started, this, &LibraryPanel::update_counts);
    connect(&m_library_index, &LibraryIndex::entries_changed, this,
            &LibraryPanel::on_entries_changed);

    const int mode = std::clamp(s.value("library/mode", 0).toInt(), 0, 2);
    m_mode_btn[mode]->setChecked(true);
    for (auto* p : m_proxies) {
        p->set_mode(static_cast<LibraryFilterProxy::Mode>(mode));
    }
    m_sort_box->setCurrentIndex(std::clamp(s.value("library/sort", 0).toInt(), 0, 1));
    m_sort_box->setEnabled(mode != 2);

    set_section(std::clamp(s.value("library/section", 0).toInt(), 0, 2), false);
    on_entries_changed();
}

void LibraryPanel::set_section(int new_section, bool focus_list) {
    m_section = new_section;
    for (int i = 0; i < 3; ++i) {
        m_section_btn[i]->setChecked(i == new_section);
    }
    QSettings().setValue("library/section", new_section);

    m_syncing = true;
    if (m_view->selectionModel()) {
        disconnect(m_view->selectionModel(), nullptr, this, nullptr);
    }
    m_view->setModel(m_proxies[new_section]);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &LibraryPanel::on_current_changed);
    m_syncing = false;

    restore_current_from_loaded();
    update_counts();
    if (focus_list) {
        m_view->setFocus(Qt::OtherFocusReason);
    }
}

void LibraryPanel::show_section(LibraryEntry::Kind kind) {
    set_section(static_cast<int>(kind), true);
}

void LibraryPanel::focus_search() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void LibraryPanel::on_entries_changed() {
    m_syncing = true;
    m_model->set_entries(m_library_index.get_entries());
    m_syncing = false;
    restore_current_from_loaded();
    update_counts();
}

void LibraryPanel::restore_current_from_loaded() {
    // Selection follows the loaded file; never fires a load.
    const auto kind = static_cast<LibraryEntry::Kind>(m_section);
    const int src_row = m_model->row_of(m_model->get_loaded(kind));
    m_syncing = true;
    if (src_row >= 0) {
        const QModelIndex pi = m_proxies[m_section]->mapFromSource(m_model->index(src_row));
        m_view->setCurrentIndex(pi); // invalid index clears the selection
        if (pi.isValid()) {
            m_view->scrollTo(pi, QAbstractItemView::EnsureVisible);
        }
    } else {
        m_view->setCurrentIndex({});
    }
    m_syncing = false;
}

void LibraryPanel::on_current_changed() {
    if (m_syncing) {
        return;
    }
    if (!m_view->currentIndex().isValid()) {
        return;
    }
    m_activate_timer->start();
}

void LibraryPanel::activate_current() {
    m_activate_timer->stop();
    const QModelIndex idx = m_view->currentIndex();
    if (!idx.isValid()) {
        return;
    }
    const QString path = idx.data(LibraryListModel::PATH_ROLE).toString();
    const auto kind =
        static_cast<LibraryEntry::Kind>(idx.data(LibraryListModel::KIND_ROLE).toInt());
    emit activated(kind, path);
}

void LibraryPanel::toggle_favorite(const QString& path) {
    if (m_favorites.contains(path)) {
        m_favorites.remove(path);
    } else {
        m_favorites.insert(path);
    }
    QSettings().setValue("library/favorites", QStringList(m_favorites.begin(), m_favorites.end()));
    m_syncing = true;
    m_model->set_favorites(m_favorites);
    m_syncing = false;
    update_counts();
}

void LibraryPanel::note_used(const QString& path) {
    const QString c = QDir::cleanPath(path);
    if (c.isEmpty()) {
        return;
    }
    m_recents.removeAll(c);
    m_recents.prepend(c);
    while (m_recents.size() > K_MAX_RECENTS) {
        m_recents.removeLast();
    }
    QSettings().setValue("library/recents", m_recents);
    m_syncing = true;
    m_model->set_recents(m_recents);
    m_syncing = false;
    update_counts();
}

void LibraryPanel::set_loaded_path(LibraryEntry::Kind kind, const QString& path) {
    m_syncing = true;
    m_model->set_loaded(kind, path);
    m_syncing = false;
    if (static_cast<int>(kind) == m_section) {
        restore_current_from_loaded();
    }
}

QString LibraryPanel::loaded_path(LibraryEntry::Kind kind) const {
    return m_model->get_loaded(kind);
}

QString LibraryPanel::neighbour(LibraryEntry::Kind kind, const QString& from_path, int step) const {
    LibraryFilterProxy* p = proxy(kind);
    const int n = p->rowCount();
    if (n == 0) {
        return {};
    }
    int row = -1;
    const int src = m_model->row_of(QDir::cleanPath(from_path));
    if (src >= 0) {
        row = p->mapFromSource(m_model->index(src)).row();
    }
    if (row < 0) {
        row = step > 0 ? -1 : n; // nothing loaded: first / last
    }
    row = ((row + step) % n + n) % n;
    return p->index(row, 0).data(LibraryListModel::PATH_ROLE).toString();
}

void LibraryPanel::update_counts() {
    // Section buttons show the unfiltered totals; the footer the visible count.
    int total[3] = {0, 0, 0};
    for (const auto& e : m_library_index.get_entries()) {
        ++total[static_cast<int>(e.kind)];
    }
    for (int i = 0; i < 3; ++i) {
        m_section_btn[i]->setText(QString("%1  %2").arg(k_section_names[i]).arg(total[i]));
    }

    const int visible = m_proxies[m_section]->rowCount();
    QString text;
    if (m_library_index.is_scanning()) {
        text = "Scanning…";
    } else if (total[m_section] == 0) {
        text = QString("No %1s found. Add folders below.").arg(k_section_single[m_section]);
    } else if (visible != total[m_section]) {
        text = QString("%1 of %2").arg(visible).arg(total[m_section]);
    } else {
        text = QString("%1 %2%3")
                   .arg(visible)
                   .arg(k_section_single[m_section])
                   .arg(visible == 1 ? "" : "s");
    }
    m_status->setText(text);
}

void LibraryPanel::edit_folders() {
    FoldersDialog dlg(m_library_index.get_roots(), window());
    if (dlg.exec() == QDialog::Accepted) {
        m_library_index.set_roots(dlg.roots());
    }
}

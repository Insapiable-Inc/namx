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
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Common.h"
#include "Theme.h"

namespace namx
{

constexpr int ROW_HEIGHT = 42;
constexpr int STAR_WIDTH = 26;
constexpr int ACCENT_BAR = 3;
constexpr int MAX_RECENTS = 30;

static const char* const SECTION_NAMES[3] = {"Models", "IRs", "Presets"};
static const char* const SECTION_SINGLE[3] = {"model", "IR", "preset"};

static QColor kind_accent(LibraryEntry::Kind kind) {
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

LibraryListModel::LibraryListModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int LibraryListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries.size());
}

QVariant LibraryListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= entries.size()) {
        return {};
    }
    const LibraryEntry& e = entries[index.row()];
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
            return favorites.contains(e.path);
        case RECENT_RANK_ROLE:
            return static_cast<int>(recents.indexOf(e.path));
        case MTIME_ROLE:
            return e.mtime_ms;
        case LOADED_ROLE:
            return loaded[static_cast<int>(e.kind)] == e.path;
        default:
            return {};
    }
}

void LibraryListModel::set_entries(const QList<LibraryEntry>& new_entries) {
    beginResetModel();
    entries = new_entries;
    endResetModel();
}

void LibraryListModel::set_favorites(const QSet<QString>& favs) {
    favorites = favs;
    if (!entries.isEmpty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {FAVORITE_ROLE});
    }
}

void LibraryListModel::set_recents(const QStringList& new_recents) {
    recents = new_recents;
    if (!entries.isEmpty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {RECENT_RANK_ROLE});
    }
}

void LibraryListModel::set_loaded(LibraryEntry::Kind kind, const QString& path) {
    QString& slot = loaded[static_cast<int>(kind)];
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
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].path == path) {
            return i;
        }
    }
    return -1;
}

LibraryFilterProxy::LibraryFilterProxy(LibraryEntry::Kind new_kind, QObject* parent)
    : QSortFilterProxyModel(parent)
    , kind(new_kind) {
    setDynamicSortFilter(true);
    sort(0);
}

void LibraryFilterProxy::set_search(const QString& text) {
    // Qt 6.10 deprecated invalidateFilter(); Ubuntu 22.04's Qt 6.2 lacks the
    // replacement.
    search_text = text;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    words = text.simplified().split(' ', Qt::SkipEmptyParts);
    endFilterChange();
#else
    words = text.simplified().split(' ', Qt::SkipEmptyParts);
    invalidateFilter();
#endif
}

void LibraryFilterProxy::set_mode(Mode m) {
    mode = m;
    invalidate();
}

void LibraryFilterProxy::set_sort_key(Sort s) {
    sort_order = s;
    invalidate();
}

bool LibraryFilterProxy::filterAcceptsRow(int row, const QModelIndex& parent) const {
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    if (i.data(LibraryListModel::KIND_ROLE).toInt() != static_cast<int>(kind)) {
        return false;
    }
    if (mode == Mode::FAVORITES && !i.data(LibraryListModel::FAVORITE_ROLE).toBool()) {
        return false;
    }
    if (mode == Mode::RECENT && i.data(LibraryListModel::RECENT_RANK_ROLE).toInt() < 0) {
        return false;
    }
    if (!words.isEmpty()) {
        const QString hay = i.data(LibraryListModel::NAME_ROLE).toString() + ' ' +
                            i.data(LibraryListModel::FOLDER_ROLE).toString() + ' ' +
                            i.data(LibraryListModel::SUMMARY_ROLE).toString();
        for (const QString& w : words) {
            if (!hay.contains(w, Qt::CaseInsensitive)) {
                return false;
            }
        }
    }
    return true;
}

// Natural, case-insensitive order ("Amp 2" before "Amp 10").
static QCollator numeric_collator() {
    QCollator c;
    c.setNumericMode(true);
    c.setCaseSensitivity(Qt::CaseInsensitive);
    return c;
}

bool LibraryFilterProxy::lessThan(const QModelIndex& a, const QModelIndex& b) const {
    if (mode == Mode::RECENT) {
        return a.data(LibraryListModel::RECENT_RANK_ROLE).toInt() <
               b.data(LibraryListModel::RECENT_RANK_ROLE).toInt();
    }
    if (sort_order == Sort::NEWEST) {
        const qint64 ma = a.data(LibraryListModel::MTIME_ROLE).toLongLong();
        const qint64 mb = b.data(LibraryListModel::MTIME_ROLE).toLongLong();
        if (ma != mb) {
            return ma > mb;
        }
    }
    static QCollator collator = numeric_collator();
    const int by_name = collator.compare(a.data(LibraryListModel::NAME_ROLE).toString(),
                                         b.data(LibraryListModel::NAME_ROLE).toString());
    if (by_name != 0) {
        return by_name < 0;
    }
    return a.data(LibraryListModel::PATH_ROLE).toString() <
           b.data(LibraryListModel::PATH_ROLE).toString();
}

QRect LibraryItemDelegate::star_rect(const QRect& row) {
    return QRect(row.right() - STAR_WIDTH, row.top(), STAR_WIDTH, row.height());
}

QSize LibraryItemDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const {
    // Taller when a larger font needs it: 5 px, the name, 1 px, the details line, 3 px.
    QFont sub_font = opt.font;
    sub_font.setPointSizeF(sub_font.pointSizeF() - 1);
    const int text = 5 + QFontMetrics(opt.font).height() + 1 + QFontMetrics(sub_font).height() + 3;
    return QSize(opt.rect.width(), std::max(ROW_HEIGHT, text));
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

    // Union's list row: inset 2 px and rounded; hovered = the accent tint and an accent
    // border, selected = the accent, its text in the highlighted-text colour.
    const QRectF row = QRectF(r).adjusted(2.5, 2.5, -2.5, -2.5);
    if (selected || hovered) {
        p->setPen(Theme::generic_accent());
        p->setBrush(selected ? Theme::generic_accent() : Theme::list_hover());
        p->drawRoundedRect(row, 5, 5);
    }
    const QColor on_row = selected ? Theme::highlighted_text() : QColor();

    if (loaded) {
        p->fillRect(QRect(r.left() + 3, r.top() + 8, ACCENT_BAR, r.height() - 16),
                    selected ? on_row : accent);
    }

    // Star column, then the text block fills what's left.
    const QRect star = star_rect(r);
    {
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() + 3);
        p->setFont(f);
        p->setPen(selected ? on_row : fav ? accent : hovered ? Theme::dim_text() : Theme::track());
        p->drawText(star, Qt::AlignCenter, fav ? QStringLiteral("★") : QStringLiteral("☆"));
    }

    const int left = r.left() + 10;
    const int width = star.left() - 4 - left;

    QFont name_font = opt.font;
    name_font.setWeight(loaded ? QFont::DemiBold : QFont::Normal);
    p->setFont(name_font);
    const QFontMetrics nfm(name_font);
    p->setPen(selected ? on_row : loaded ? accent : opt.palette.color(QPalette::Text));
    const QString name =
        nfm.elidedText(idx.data(LibraryListModel::NAME_ROLE).toString(), Qt::ElideMiddle, width);
    p->drawText(QRect(left, r.top() + 5, width, nfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                name);

    QFont sub_font = opt.font;
    sub_font.setPointSizeF(sub_font.pointSizeF() - 1);
    p->setFont(sub_font);
    const QFontMetrics sfm(sub_font);
    p->setPen(selected ? on_row : Theme::dim_text());
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

LibraryPanel::LibraryPanel(LibraryIndex& new_index, QWidget* parent)
    : QWidget(parent)
    , library_index(new_index) {
    setObjectName("libraryPanel");
    setMinimumWidth(240);

    QSettings s;
    // One list: begin() and end() must come from the same container.
    const QStringList saved_favorites = s.value("library/favorites").toStringList();
    favorites = QSet<QString>(saved_favorites.begin(), saved_favorites.end());
    recents = s.value("library/recents").toStringList();

    model = new LibraryListModel(this);
    model->set_favorites(favorites);
    model->set_recents(recents);
    for (int i = 0; i < 3; ++i) {
        proxies[i] = new LibraryFilterProxy(static_cast<LibraryEntry::Kind>(i), this);
        proxies[i]->setSourceModel(model);
    }

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // Section switch: Models | IRs | Presets (counts appended by updateCounts).
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(0); // tabs, side by side
        for (int i = 0; i < 3; ++i) {
            auto* b = new QToolButton;
            b->setObjectName("librarySection");
            // The outer tabs round their outer corner (the stylesheet's tabPosition).
            b->setProperty("tabPosition", i == 0 ? "first" : i == 2 ? "last" : "middle");
            b->setText(SECTION_NAMES[i]);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [this, i] {
                set_section(i, true);
            });
            row->addWidget(b);
            section_btn[i] = b;
        }
        lay->addLayout(row);
    }

    search = new QLineEdit;
    search->setObjectName("librarySearch");
    search->setPlaceholderText("Search…");
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, &LibraryPanel::apply_search);
    lay->addWidget(search);

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
                set_mode(i);
            });
            row->addWidget(b);
            mode_btn[i] = b;
        }
        row->addStretch();
        sort_box = new QComboBox;
        sort_box->setObjectName("librarySort");
        sort_box->addItems({"Sort by: Name", "Sort by: Newest"});
        sort_box->setToolTip("Sort order");
        sort_box->setFocusPolicy(Qt::NoFocus);
        connect(sort_box, &QComboBox::currentIndexChanged, this, &LibraryPanel::set_sort_key);
        row->addWidget(sort_box);
        lay->addLayout(row);
    }

    view = new LibraryListView;
    view->setObjectName("libraryList");
    view->setItemDelegate(new LibraryItemDelegate(view));
    view->setUniformItemSizes(true);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setMouseTracking(true);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setFrameShape(QFrame::NoFrame);
    connect(view, &LibraryListView::favorite_toggled, this, &LibraryPanel::toggle_favorite);
    connect(view, &QAbstractItemView::activated, this, [this] {
        activate_current();
    });
    lay->addWidget(view, 1);

    // Footer: count / scanning state, folders, rescan.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        status = new QLabel;
        status->setObjectName("libraryStatus");
        row->addWidget(status, 1);

        auto* folders = new QToolButton;
        folders->setObjectName("libraryTool");
        folders->setText("Folders…");
        folders->setAutoRaise(true);
        folders->setFocusPolicy(Qt::NoFocus);
        folders->setToolTip("Choose which folders the library scans");
        connect(folders, &QToolButton::clicked, this, &LibraryPanel::folders_requested);
        row->addWidget(folders);

        auto* rescan = new QToolButton;
        rescan->setObjectName("libraryTool");
        rescan->setText("↻");
        rescan->setAutoRaise(true);
        rescan->setFocusPolicy(Qt::NoFocus);
        rescan->setToolTip("Rescan the library folders");
        connect(rescan, &QToolButton::clicked, &library_index, &LibraryIndex::rescan);
        row->addWidget(rescan);
        lay->addLayout(row);
    }

    // Browsing with the arrow keys loads the entry under the cursor once the
    // cursor rests — a full model load per keystroke would stutter.
    activate_timer = new QTimer(this);
    activate_timer->setSingleShot(true);
    activate_timer->setInterval(180);
    connect(activate_timer, &QTimer::timeout, this, &LibraryPanel::activate_current);

    connect(&library_index, &LibraryIndex::scan_started, this, &LibraryPanel::update_counts);
    connect(&library_index, &LibraryIndex::entries_changed, this,
            &LibraryPanel::on_entries_changed);

    // Mode and sort per section (library/modes, library/sorts: one entry per section);
    // the single global library/mode and library/sort of older versions seed all three.
    const QVariantList modes = s.value("library/modes").toList();
    const QVariantList sorts = s.value("library/sorts").toList();
    for (int i = 0; i < 3; ++i) {
        const int mode =
            std::clamp((i < modes.size() ? modes[i] : s.value("library/mode", 0)).toInt(), 0, 2);
        const int sort =
            std::clamp((i < sorts.size() ? sorts[i] : s.value("library/sort", 0)).toInt(), 0, 1);
        proxies[i]->set_mode(static_cast<LibraryFilterProxy::Mode>(mode));
        proxies[i]->set_sort_key(static_cast<LibraryFilterProxy::Sort>(sort));
    }

    set_section(std::clamp(s.value("library/section", 0).toInt(), 0, 2), false);
    on_entries_changed();
}

void LibraryPanel::set_section(int new_section, bool focus_list) {
    section = new_section;
    for (int i = 0; i < 3; ++i) {
        section_btn[i]->setChecked(i == new_section);
    }
    QSettings().setValue("library/section", new_section);

    syncing = true;
    QItemSelectionModel* previous = view->selectionModel();
    if (previous) {
        disconnect(previous, nullptr, this, nullptr);
    }
    view->setModel(proxies[new_section]);
    delete previous; // setModel creates a new one and never frees the old (Qt docs)
    show_mode_and_sort();
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &LibraryPanel::on_current_changed);
    syncing = false;
    {
        const QSignalBlocker block(search);
        search->setText(proxies[new_section]->get_search());
    }

    restore_current_from_loaded();
    update_counts();
    if (focus_list) {
        view->setFocus(Qt::OtherFocusReason);
    }
}

void LibraryPanel::show_section(LibraryEntry::Kind kind) {
    set_section(static_cast<int>(kind), true);
}

void LibraryPanel::focus_search() {
    search->setFocus(Qt::ShortcutFocusReason);
    search->selectAll();
}

void LibraryPanel::on_entries_changed() {
    syncing = true;
    model->set_entries(library_index.get_entries());
    syncing = false;
    restore_current_from_loaded();
    update_counts();
}

void LibraryPanel::restore_current_from_loaded() {
    // Selection follows the loaded file; never fires a load.
    const auto kind = static_cast<LibraryEntry::Kind>(section);
    const int src_row = model->row_of(model->get_loaded(kind));
    syncing = true;
    if (src_row >= 0) {
        const QModelIndex pi = proxies[section]->mapFromSource(model->index(src_row));
        view->setCurrentIndex(pi); // invalid index clears the selection
        if (pi.isValid()) {
            view->scrollTo(pi, QAbstractItemView::EnsureVisible);
        }
    } else {
        view->setCurrentIndex({});
    }
    syncing = false;
}

void LibraryPanel::on_current_changed() {
    if (syncing) {
        return;
    }
    if (!view->currentIndex().isValid()) {
        return;
    }
    activate_timer->start();
}

void LibraryPanel::activate_current() {
    activate_timer->stop();
    const QModelIndex idx = view->currentIndex();
    if (!idx.isValid()) {
        return;
    }
    const QString path = idx.data(LibraryListModel::PATH_ROLE).toString();
    const auto kind =
        static_cast<LibraryEntry::Kind>(idx.data(LibraryListModel::KIND_ROLE).toInt());
    emit activated(kind, path);
}

void LibraryPanel::toggle_favorite(const QString& path) {
    if (favorites.contains(path)) {
        favorites.remove(path);
    } else {
        favorites.insert(path);
    }
    QStringList sorted(favorites.begin(), favorites.end());
    sorted.sort(); // a QSet iterates in hash order: the file would change on every toggle
    QSettings().setValue("library/favorites", sorted);
    syncing = true;
    model->set_favorites(favorites);
    syncing = false;
    update_counts();
}

void LibraryPanel::note_used(const QString& path) {
    const QString c = QDir::cleanPath(path);
    if (c.isEmpty()) {
        return;
    }
    recents.removeAll(c);
    recents.prepend(c);
    while (recents.size() > MAX_RECENTS) {
        recents.removeLast();
    }
    QSettings().setValue("library/recents", recents);
    syncing = true;
    model->set_recents(recents);
    syncing = false;
    update_counts();
}

void LibraryPanel::set_loaded_path(LibraryEntry::Kind kind, const QString& path) {
    syncing = true;
    model->set_loaded(kind, path);
    syncing = false;
    if (static_cast<int>(kind) == section) {
        restore_current_from_loaded();
    }
}

QString LibraryPanel::loaded_path(LibraryEntry::Kind kind) const {
    return model->get_loaded(kind);
}

QString LibraryPanel::neighbour(LibraryEntry::Kind kind, const QString& from_path, int step) const {
    LibraryFilterProxy* p = proxy(kind);
    const int n = p->rowCount();
    if (n == 0) {
        return {};
    }
    int row = -1;
    const int src = model->row_of(QDir::cleanPath(from_path));
    if (src >= 0) {
        row = p->mapFromSource(model->index(src)).row();
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
    for (const auto& e : library_index.get_entries()) {
        ++total[static_cast<int>(e.kind)];
    }
    for (int i = 0; i < 3; ++i) {
        section_btn[i]->setText(QString("%1 (%2)").arg(SECTION_NAMES[i]).arg(total[i]));
    }

    const int visible = proxies[section]->rowCount();
    QString text;
    if (library_index.is_scanning()) {
        text = "Scanning…";
    } else if (total[section] == 0) {
        text = QString("No %1s found. Add folders below.").arg(SECTION_SINGLE[section]);
    } else if (visible != total[section]) {
        text = QString("%1 of %2").arg(visible).arg(total[section]);
    } else {
        text = QString("%1 %2%3")
                   .arg(visible)
                   .arg(SECTION_SINGLE[section])
                   .arg(visible == 1 ? "" : "s");
    }
    status->setText(text);
}

void LibraryPanel::apply_search(const QString& text) {
    proxies[section]->set_search(text);
    update_counts();
    restore_current_from_loaded();
}

void LibraryPanel::set_mode(int mode) {
    proxies[section]->set_mode(static_cast<LibraryFilterProxy::Mode>(mode));
    show_mode_and_sort();
    save_modes_and_sorts();
    update_counts();
    restore_current_from_loaded();
}

void LibraryPanel::set_sort_key(int key) {
    proxies[section]->set_sort_key(static_cast<LibraryFilterProxy::Sort>(key));
    save_modes_and_sorts();
    restore_current_from_loaded();
}

void LibraryPanel::show_mode_and_sort() {
    const int mode = static_cast<int>(proxies[section]->get_mode());
    for (int k = 0; k < 3; ++k) {
        mode_btn[k]->setChecked(k == mode);
    }
    const QSignalBlocker block(sort_box);
    sort_box->setCurrentIndex(static_cast<int>(proxies[section]->get_sort_key()));
    sort_box->setEnabled(mode != 2); // Recent always sorts by recency
}

void LibraryPanel::save_modes_and_sorts() const {
    QVariantList modes, sorts;
    for (const LibraryFilterProxy* p : proxies) {
        modes << static_cast<int>(p->get_mode());
        sorts << static_cast<int>(p->get_sort_key());
    }
    QSettings s;
    s.setValue("library/modes", modes);
    s.setValue("library/sorts", sorts);
}

} // namespace namx

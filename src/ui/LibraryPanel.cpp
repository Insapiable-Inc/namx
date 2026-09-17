#include "LibraryPanel.h"
#include "Theme.h"

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

#include <algorithm>

namespace {

constexpr int kRowHeight   = 42;
constexpr int kStarWidth   = 26;
constexpr int kAccentBar   = 3;
constexpr int kMaxRecents  = 30;

const char* kSectionNames[3]  = {"Models", "IRs", "Presets"};
const char* kSectionSingle[3] = {"model", "IR", "preset"};

QColor kindAccent(LibraryEntry::Kind kind)
{
    switch (kind)
    {
    case LibraryEntry::Kind::Model:  return Theme::accent("nam");
    case LibraryEntry::Kind::IR:     return Theme::accent("ir");
    case LibraryEntry::Kind::Preset: return Theme::genericAccent();
    }
    return Theme::genericAccent();
}

} // namespace

// ===========================================================================
// LibraryListModel
// ===========================================================================

LibraryListModel::LibraryListModel(QObject* parent) : QAbstractListModel(parent) {}

int LibraryListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(mEntries.size());
}

QVariant LibraryListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= mEntries.size()) return {};
    const LibraryEntry& e = mEntries[index.row()];
    switch (role)
    {
    case Qt::DisplayRole:
    case NameRole:       return e.name;
    case Qt::ToolTipRole:
    {
        QString tip = "<b>" + e.name.toHtmlEscaped() + "</b>";
        if (!e.details.isEmpty())
            tip += "<br>" + e.details.toHtmlEscaped().replace('\n', "<br>");
        tip += "<br><i>" + QDir::toNativeSeparators(e.path).toHtmlEscaped() + "</i>";
        return tip;
    }
    case PathRole:       return e.path;
    case KindRole:       return static_cast<int>(e.kind);
    case FolderRole:     return e.folder;
    case SummaryRole:    return e.summary;
    case DetailsRole:    return e.details;
    case FavoriteRole:   return mFavorites.contains(e.path);
    case RecentRankRole: return static_cast<int>(mRecents.indexOf(e.path));
    case MtimeRole:      return e.mtimeMs;
    case LoadedRole:     return mLoaded[static_cast<int>(e.kind)] == e.path;
    default:             return {};
    }
}

void LibraryListModel::setEntries(const QList<LibraryEntry>& entries)
{
    beginResetModel();
    mEntries = entries;
    endResetModel();
}

void LibraryListModel::setFavorites(const QSet<QString>& favs)
{
    mFavorites = favs;
    if (!mEntries.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {FavoriteRole});
}

void LibraryListModel::setRecents(const QStringList& recents)
{
    mRecents = recents;
    if (!mEntries.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {RecentRankRole});
}

void LibraryListModel::setLoaded(LibraryEntry::Kind kind, const QString& path)
{
    QString& slot = mLoaded[static_cast<int>(kind)];
    const int before = rowOf(slot);
    slot = QDir::cleanPath(path);
    const int after = rowOf(slot);
    if (before >= 0) emit dataChanged(index(before), index(before), {LoadedRole});
    if (after  >= 0) emit dataChanged(index(after),  index(after),  {LoadedRole});
}

int LibraryListModel::rowOf(const QString& path) const
{
    if (path.isEmpty()) return -1;
    for (int i = 0; i < mEntries.size(); ++i)
        if (mEntries[i].path == path) return i;
    return -1;
}

// ===========================================================================
// LibraryFilterProxy
// ===========================================================================

LibraryFilterProxy::LibraryFilterProxy(LibraryEntry::Kind kind, QObject* parent)
    : QSortFilterProxyModel(parent), mKind(kind)
{
    setDynamicSortFilter(true);
    sort(0);
}

void LibraryFilterProxy::setSearch(const QString& text)
{
    // Qt 6.10 deprecated invalidateFilter(); Ubuntu 22.04's Qt 6.2 lacks the
    // replacement.
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    mWords = text.simplified().split(' ', Qt::SkipEmptyParts);
    endFilterChange();
#else
    mWords = text.simplified().split(' ', Qt::SkipEmptyParts);
    invalidateFilter();
#endif
}

void LibraryFilterProxy::setMode(Mode m)
{
    mMode = m;
    invalidate();
}

void LibraryFilterProxy::setSortKey(Sort s)
{
    mSort = s;
    invalidate();
}

bool LibraryFilterProxy::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    if (i.data(LibraryListModel::KindRole).toInt() != static_cast<int>(mKind)) return false;
    if (mMode == Mode::Favorites && !i.data(LibraryListModel::FavoriteRole).toBool()) return false;
    if (mMode == Mode::Recent && i.data(LibraryListModel::RecentRankRole).toInt() < 0) return false;
    if (!mWords.isEmpty())
    {
        const QString hay = i.data(LibraryListModel::NameRole).toString() + ' '
                            + i.data(LibraryListModel::FolderRole).toString() + ' '
                            + i.data(LibraryListModel::SummaryRole).toString();
        for (const QString& w : mWords)
            if (!hay.contains(w, Qt::CaseInsensitive)) return false;
    }
    return true;
}

bool LibraryFilterProxy::lessThan(const QModelIndex& a, const QModelIndex& b) const
{
    if (mMode == Mode::Recent)
        return a.data(LibraryListModel::RecentRankRole).toInt()
               < b.data(LibraryListModel::RecentRankRole).toInt();
    if (mSort == Sort::Newest)
    {
        const qint64 ma = a.data(LibraryListModel::MtimeRole).toLongLong();
        const qint64 mb = b.data(LibraryListModel::MtimeRole).toLongLong();
        if (ma != mb) return ma > mb;
    }
    static QCollator collator = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    const int byName = collator.compare(a.data(LibraryListModel::NameRole).toString(),
                                        b.data(LibraryListModel::NameRole).toString());
    if (byName != 0) return byName < 0;
    return a.data(LibraryListModel::PathRole).toString()
           < b.data(LibraryListModel::PathRole).toString();
}

// ===========================================================================
// LibraryItemDelegate
// ===========================================================================

QRect LibraryItemDelegate::starRect(const QRect& row)
{
    return QRect(row.right() - kStarWidth, row.top(), kStarWidth, row.height());
}

QSize LibraryItemDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const
{
    return QSize(opt.rect.width(), kRowHeight);
}

void LibraryItemDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                                const QModelIndex& idx) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    const QRect r = opt.rect;
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered  = opt.state & QStyle::State_MouseOver;
    const bool loaded   = idx.data(LibraryListModel::LoadedRole).toBool();
    const bool fav      = idx.data(LibraryListModel::FavoriteRole).toBool();
    const auto kind     = static_cast<LibraryEntry::Kind>(idx.data(LibraryListModel::KindRole).toInt());
    const QColor accent = kindAccent(kind);

    if (selected)      p->fillRect(r, Theme::trackHover());
    else if (hovered)  p->fillRect(r, Theme::track());

    if (loaded)
        p->fillRect(QRect(r.left(), r.top() + 6, kAccentBar, r.height() - 12), accent);

    // Star column, then the text block fills what's left.
    const QRect star = starRect(r);
    {
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() + 3);
        p->setFont(f);
        p->setPen(fav ? accent : (selected || hovered ? Theme::dimText() : Theme::track()));
        p->drawText(star, Qt::AlignCenter, fav ? QStringLiteral("★") : QStringLiteral("☆"));
    }

    const int left  = r.left() + 10;
    const int width = star.left() - 4 - left;

    QFont nameFont = opt.font;
    nameFont.setWeight(loaded ? QFont::DemiBold : QFont::Normal);
    p->setFont(nameFont);
    const QFontMetrics nfm(nameFont);
    p->setPen(loaded ? accent : opt.palette.color(QPalette::Text));
    const QString name = nfm.elidedText(idx.data(LibraryListModel::NameRole).toString(),
                                        Qt::ElideMiddle, width);
    p->drawText(QRect(left, r.top() + 5, width, nfm.height()),
                Qt::AlignLeft | Qt::AlignVCenter, name);

    QFont subFont = opt.font;
    subFont.setPointSizeF(subFont.pointSizeF() - 1);
    p->setFont(subFont);
    const QFontMetrics sfm(subFont);
    p->setPen(Theme::dimText());
    QStringList second;
    const QString folder = idx.data(LibraryListModel::FolderRole).toString();
    if (!folder.isEmpty()) second << folder;
    const QString summary = idx.data(LibraryListModel::SummaryRole).toString();
    if (!summary.isEmpty()) second << summary;
    const QString sum = sfm.elidedText(second.join(QStringLiteral(" · ")), Qt::ElideRight, width);
    p->drawText(QRect(left, r.top() + 5 + nfm.height() + 1, width, sfm.height()),
                Qt::AlignLeft | Qt::AlignVCenter, sum);

    p->restore();
}

// ===========================================================================
// LibraryListView
// ===========================================================================

void LibraryListView::mousePressEvent(QMouseEvent* ev)
{
    const QModelIndex idx = indexAt(ev->pos());
    if (idx.isValid() && ev->button() == Qt::LeftButton
        && LibraryItemDelegate::starRect(visualRect(idx)).contains(ev->pos()))
    {
        emit favoriteToggled(idx.data(LibraryListModel::PathRole).toString());
        return;
    }
    QListView::mousePressEvent(ev);
}

void LibraryListView::mouseDoubleClickEvent(QMouseEvent* ev)
{
    const QModelIndex idx = indexAt(ev->pos());
    if (idx.isValid() && LibraryItemDelegate::starRect(visualRect(idx)).contains(ev->pos()))
        return; // a fast double click on the star is two toggles, not a load
    QListView::mouseDoubleClickEvent(ev);
}

void LibraryListView::keyPressEvent(QKeyEvent* ev)
{
    if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter)
    {
        if (currentIndex().isValid()) emit activated(currentIndex());
        ev->accept();
        return;
    }
    QListView::keyPressEvent(ev);
}

// ===========================================================================
// Folders dialog
// ===========================================================================

namespace {

class FoldersDialog : public QDialog
{
public:
    FoldersDialog(const LibraryRoots& roots, QWidget* parent) : QDialog(parent)
    {
        setWindowTitle("Library Folders");
        resize(560, 380);
        auto* lay = new QVBoxLayout(this);

        auto* hint = new QLabel("Each folder is scanned recursively, and the library follows "
                                "changes on disk. A folder can serve several kinds.");
        hint->setWordWrap(true);
        lay->addWidget(hint);

        auto* tabs = new QTabWidget;
        const QStringList* lists[3] = {&roots.models, &roots.irs, &roots.presets};
        for (int i = 0; i < 3; ++i)
        {
            auto* page = new QWidget;
            auto* pl   = new QHBoxLayout(page);
            mLists[i]  = new QListWidget;
            mLists[i]->addItems(*lists[i]);
            pl->addWidget(mLists[i], 1);

            auto* btns = new QVBoxLayout;
            auto* add  = new QPushButton("Add…");
            auto* rm   = new QPushButton("Remove");
            btns->addWidget(add);
            btns->addWidget(rm);
            btns->addStretch();
            pl->addLayout(btns);

            QListWidget* lw = mLists[i];
            connect(add, &QPushButton::clicked, this, [this, lw, i] {
                const QString start = lw->count() ? lw->item(lw->count() - 1)->text() : QDir::homePath();
                const QString dir = QFileDialog::getExistingDirectory(
                    this, QString("Add %1 folder").arg(kSectionNames[i]), start);
                if (dir.isEmpty()) return;
                const QString clean = QDir::cleanPath(dir);
                for (int r = 0; r < lw->count(); ++r)
                    if (lw->item(r)->text() == clean) return;
                lw->addItem(clean);
            });
            connect(rm, &QPushButton::clicked, this, [lw] { delete lw->currentItem(); });
            tabs->addTab(page, kSectionNames[i]);
        }
        lay->addWidget(tabs, 1);

        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        lay->addWidget(bb);
    }

    LibraryRoots roots() const
    {
        auto items = [](QListWidget* lw) {
            QStringList l;
            for (int r = 0; r < lw->count(); ++r) l << lw->item(r)->text();
            return l;
        };
        LibraryRoots r;
        r.models  = items(mLists[0]);
        r.irs     = items(mLists[1]);
        r.presets = items(mLists[2]);
        return r;
    }

private:
    QListWidget* mLists[3]{};
};

} // namespace

// ===========================================================================
// LibraryPanel
// ===========================================================================

LibraryPanel::LibraryPanel(LibraryIndex& index, QWidget* parent)
    : QWidget(parent)
    , mIndex(index)
{
    setObjectName("libraryPanel");
    setMinimumWidth(240);

    QSettings s;
    mFavorites = QSet<QString>(s.value("library/favorites").toStringList().begin(),
                               s.value("library/favorites").toStringList().end());
    mRecents   = s.value("library/recents").toStringList();

    mModel = new LibraryListModel(this);
    mModel->setFavorites(mFavorites);
    mModel->setRecents(mRecents);
    for (int i = 0; i < 3; ++i)
    {
        mProxies[i] = new LibraryFilterProxy(static_cast<LibraryEntry::Kind>(i), this);
        mProxies[i]->setSourceModel(mModel);
    }

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // Section switch: Models | IRs | Presets (counts appended by updateCounts).
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);
        for (int i = 0; i < 3; ++i)
        {
            auto* b = new QToolButton;
            b->setObjectName("librarySection");
            b->setText(kSectionNames[i]);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [this, i] { setSection(i, true); });
            row->addWidget(b);
            mSectionBtn[i] = b;
        }
        lay->addLayout(row);
    }

    mSearch = new QLineEdit;
    mSearch->setObjectName("librarySearch");
    mSearch->setPlaceholderText("Search…");
    mSearch->setClearButtonEnabled(true);
    connect(mSearch, &QLineEdit::textChanged, this, [this](const QString& t) {
        for (auto* p : mProxies) p->setSearch(t);
        updateCounts();
        restoreCurrentFromLoaded();
    });
    lay->addWidget(mSearch);

    // Mode buttons + sort key.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);
        const char* labels[3] = {"All", "★", "Recent"};
        const char* tips[3]   = {"Everything", "Favorites only", "Recently used"};
        for (int i = 0; i < 3; ++i)
        {
            auto* b = new QToolButton;
            b->setObjectName("libraryMode");
            b->setText(labels[i]);
            b->setToolTip(tips[i]);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [this, i] {
                for (int k = 0; k < 3; ++k) mModeBtn[k]->setChecked(k == i);
                for (auto* p : mProxies) p->setMode(static_cast<LibraryFilterProxy::Mode>(i));
                mSortBox->setEnabled(i != 2);
                QSettings().setValue("library/mode", i);
                updateCounts();
                restoreCurrentFromLoaded();
            });
            row->addWidget(b);
            mModeBtn[i] = b;
        }
        row->addStretch();
        mSortBox = new QComboBox;
        mSortBox->setObjectName("librarySort");
        mSortBox->addItems({"Name", "Newest"});
        mSortBox->setToolTip("Sort order");
        mSortBox->setFocusPolicy(Qt::NoFocus);
        connect(mSortBox, &QComboBox::currentIndexChanged, this, [this](int i) {
            for (auto* p : mProxies) p->setSortKey(static_cast<LibraryFilterProxy::Sort>(i));
            QSettings().setValue("library/sort", i);
            restoreCurrentFromLoaded();
        });
        row->addWidget(mSortBox);
        lay->addLayout(row);
    }

    mView = new LibraryListView;
    mView->setObjectName("libraryList");
    mView->setItemDelegate(new LibraryItemDelegate(mView));
    mView->setUniformItemSizes(true);
    mView->setSelectionMode(QAbstractItemView::SingleSelection);
    mView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mView->setMouseTracking(true);
    mView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    mView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mView->setFrameShape(QFrame::NoFrame);
    connect(mView, &LibraryListView::favoriteToggled, this, &LibraryPanel::toggleFavorite);
    connect(mView, &QAbstractItemView::activated, this, [this] { activateCurrent(); });
    lay->addWidget(mView, 1);

    // Footer: count / scanning state, folders, rescan.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        mStatus = new QLabel;
        mStatus->setObjectName("libraryStatus");
        row->addWidget(mStatus, 1);

        auto* folders = new QToolButton;
        folders->setObjectName("libraryTool");
        folders->setText("Folders…");
        folders->setAutoRaise(true);
        folders->setFocusPolicy(Qt::NoFocus);
        folders->setToolTip("Choose which folders the library scans");
        connect(folders, &QToolButton::clicked, this, &LibraryPanel::editFolders);
        row->addWidget(folders);

        auto* rescan = new QToolButton;
        rescan->setObjectName("libraryTool");
        rescan->setText("↻");
        rescan->setAutoRaise(true);
        rescan->setFocusPolicy(Qt::NoFocus);
        rescan->setToolTip("Rescan the library folders");
        connect(rescan, &QToolButton::clicked, &mIndex, &LibraryIndex::rescan);
        row->addWidget(rescan);
        lay->addLayout(row);
    }

    // Browsing with the arrow keys loads the entry under the cursor once the
    // cursor rests — a full model load per keystroke would stutter.
    mActivateTimer = new QTimer(this);
    mActivateTimer->setSingleShot(true);
    mActivateTimer->setInterval(180);
    connect(mActivateTimer, &QTimer::timeout, this, &LibraryPanel::activateCurrent);

    connect(&mIndex, &LibraryIndex::scanStarted, this, &LibraryPanel::updateCounts);
    connect(&mIndex, &LibraryIndex::entriesChanged, this, &LibraryPanel::onEntriesChanged);

    const int mode = std::clamp(s.value("library/mode", 0).toInt(), 0, 2);
    mModeBtn[mode]->setChecked(true);
    for (auto* p : mProxies) p->setMode(static_cast<LibraryFilterProxy::Mode>(mode));
    mSortBox->setCurrentIndex(std::clamp(s.value("library/sort", 0).toInt(), 0, 1));
    mSortBox->setEnabled(mode != 2);

    setSection(std::clamp(s.value("library/section", 0).toInt(), 0, 2), false);
    onEntriesChanged();
}

void LibraryPanel::setSection(int section, bool focusList)
{
    mSection = section;
    for (int i = 0; i < 3; ++i) mSectionBtn[i]->setChecked(i == section);
    QSettings().setValue("library/section", section);

    mSyncing = true;
    if (mView->selectionModel())
        disconnect(mView->selectionModel(), nullptr, this, nullptr);
    mView->setModel(mProxies[section]);
    connect(mView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &LibraryPanel::onCurrentChanged);
    mSyncing = false;

    restoreCurrentFromLoaded();
    updateCounts();
    if (focusList) mView->setFocus(Qt::OtherFocusReason);
}

void LibraryPanel::showSection(LibraryEntry::Kind kind)
{
    setSection(static_cast<int>(kind), true);
}

void LibraryPanel::focusSearch()
{
    mSearch->setFocus(Qt::ShortcutFocusReason);
    mSearch->selectAll();
}

void LibraryPanel::onEntriesChanged()
{
    mSyncing = true;
    mModel->setEntries(mIndex.entries());
    mSyncing = false;
    restoreCurrentFromLoaded();
    updateCounts();
}

void LibraryPanel::restoreCurrentFromLoaded()
{
    // Selection follows the loaded file; never fires a load.
    const auto kind = static_cast<LibraryEntry::Kind>(mSection);
    const int srcRow = mModel->rowOf(mModel->loaded(kind));
    mSyncing = true;
    if (srcRow >= 0)
    {
        const QModelIndex pi = mProxies[mSection]->mapFromSource(mModel->index(srcRow));
        mView->setCurrentIndex(pi); // invalid index clears the selection
        if (pi.isValid()) mView->scrollTo(pi, QAbstractItemView::EnsureVisible);
    }
    else
    {
        mView->setCurrentIndex({});
    }
    mSyncing = false;
}

void LibraryPanel::onCurrentChanged()
{
    if (mSyncing) return;
    if (!mView->currentIndex().isValid()) return;
    mActivateTimer->start();
}

void LibraryPanel::activateCurrent()
{
    mActivateTimer->stop();
    const QModelIndex idx = mView->currentIndex();
    if (!idx.isValid()) return;
    const QString path = idx.data(LibraryListModel::PathRole).toString();
    const auto kind = static_cast<LibraryEntry::Kind>(idx.data(LibraryListModel::KindRole).toInt());
    emit activated(kind, path);
}

void LibraryPanel::toggleFavorite(const QString& path)
{
    if (mFavorites.contains(path)) mFavorites.remove(path);
    else                           mFavorites.insert(path);
    QSettings().setValue("library/favorites", QStringList(mFavorites.begin(), mFavorites.end()));
    mSyncing = true;
    mModel->setFavorites(mFavorites);
    mSyncing = false;
    updateCounts();
}

void LibraryPanel::noteUsed(const QString& path)
{
    const QString c = QDir::cleanPath(path);
    if (c.isEmpty()) return;
    mRecents.removeAll(c);
    mRecents.prepend(c);
    while (mRecents.size() > kMaxRecents) mRecents.removeLast();
    QSettings().setValue("library/recents", mRecents);
    mSyncing = true;
    mModel->setRecents(mRecents);
    mSyncing = false;
    updateCounts();
}

void LibraryPanel::setLoadedPath(LibraryEntry::Kind kind, const QString& path)
{
    mSyncing = true;
    mModel->setLoaded(kind, path);
    mSyncing = false;
    if (static_cast<int>(kind) == mSection)
        restoreCurrentFromLoaded();
}

QString LibraryPanel::loadedPath(LibraryEntry::Kind kind) const
{
    return mModel->loaded(kind);
}

QString LibraryPanel::neighbour(LibraryEntry::Kind kind, const QString& fromPath, int step) const
{
    LibraryFilterProxy* p = proxy(kind);
    const int n = p->rowCount();
    if (n == 0) return {};
    int row = -1;
    const int src = mModel->rowOf(QDir::cleanPath(fromPath));
    if (src >= 0) row = p->mapFromSource(mModel->index(src)).row();
    if (row < 0) row = step > 0 ? -1 : n;       // nothing loaded: first / last
    row = ((row + step) % n + n) % n;
    return p->index(row, 0).data(LibraryListModel::PathRole).toString();
}

void LibraryPanel::updateCounts()
{
    // Section buttons show the unfiltered totals; the footer the visible count.
    int total[3] = {0, 0, 0};
    for (const auto& e : mIndex.entries()) ++total[static_cast<int>(e.kind)];
    for (int i = 0; i < 3; ++i)
        mSectionBtn[i]->setText(QString("%1  %2").arg(kSectionNames[i]).arg(total[i]));

    const int visible = mProxies[mSection]->rowCount();
    QString text;
    if (mIndex.isScanning())
        text = "Scanning…";
    else if (total[mSection] == 0)
        text = QString("No %1s found. Add folders below.").arg(kSectionSingle[mSection]);
    else if (visible != total[mSection])
        text = QString("%1 of %2").arg(visible).arg(total[mSection]);
    else
        text = QString("%1 %2%3").arg(visible).arg(kSectionSingle[mSection]).arg(visible == 1 ? "" : "s");
    mStatus->setText(text);
}

void LibraryPanel::editFolders()
{
    FoldersDialog dlg(mIndex.roots(), window());
    if (dlg.exec() == QDialog::Accepted)
        mIndex.setRoots(dlg.roots());
}

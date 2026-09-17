#include "Tone3000Panel.h"
#include "Theme.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kThumb      = 44;
constexpr int kAvatar     = 16;
constexpr int kRowHeight  = 58;
constexpr int kStarWidth  = 26;
constexpr int kModelRowH  = 34;

const char* kListNames[5] = {"Trending", "Latest", "Favorites", "Downloads", "Mine"};
const Tone3000Client::ListKind kListKinds[5] = {
    Tone3000Client::ListKind::Trending, Tone3000Client::ListKind::Latest,
    Tone3000Client::ListKind::Favorited, Tone3000Client::ListKind::Downloaded,
    Tone3000Client::ListKind::Created,
};

QPixmap roundedThumb(const QPixmap& src, int size, bool circle)
{
    if (src.isNull()) return {};
    // Cover-crop to a square, then round the corners.
    QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect crop((scaled.width() - size) / 2, (scaled.height() - size) / 2, size, size);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    if (circle) path.addEllipse(0, 0, size, size);
    else        path.addRoundedRect(0, 0, size, size, 5, 5);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled.copy(crop));
    return out;
}

QString countLabel(int n)
{
    if (n >= 1000) return QString::number(n / 1000.0, 'f', n >= 10000 ? 0 : 1) + "k";
    return QString::number(n);
}

QString modelInfo(const T3kModel& m, const T3kTone& t)
{
    QStringList parts;
    if (t.format == "ir") parts << "IR";
    else if (m.architecture == "2") parts << "NAM A2";
    else if (m.architecture == "1") parts << "NAM A1";
    else if (m.architecture == "custom") parts << "NAM custom";
    if (!m.size.isEmpty() && m.size != "custom") parts << m.size;
    return parts.join(" · ");
}

} // namespace

// ===========================================================================
// ToneListModel
// ===========================================================================

ToneListModel::ToneListModel(Tone3000Client& client, LibraryIndex& index, QObject* parent)
    : QAbstractListModel(parent), mClient(client), mIndex(index)
{
    connect(&mIndex, &LibraryIndex::entriesChanged, this, &ToneListModel::refreshDownloaded);
}

int ToneListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(mTones.size());
}

QVariant ToneListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= mTones.size()) return {};
    const T3kTone& t = mTones[index.row()];
    switch (role)
    {
    case Qt::DisplayRole:
    case TitleRole:      return t.title;
    case Qt::ToolTipRole:
    {
        QString tip = "<b>" + t.title.toHtmlEscaped() + "</b><br>" + t.gearLabel() + " · " + t.formatLabel()
                      + " · " + t.licenseLabel() + "<br>by @" + t.user.username.toHtmlEscaped()
                      + QString("<br>%1 models · %2 downloads · %3 favorites")
                            .arg(t.modelsCount).arg(t.downloads).arg(t.favorites);
        if (!t.makes.isEmpty()) tip += "<br>" + t.makes.join(", ").toHtmlEscaped();
        return tip;
    }
    case ToneIdRole:     return t.id;
    case GearRole:       return t.gearLabel();
    case FormatRole:     return t.formatLabel();
    case CreatorRole:    return t.user.username;
    case LicenseRole:    return t.licenseLabel();
    case FavoriteRole:   return t.isFavorite;
    case DownloadedRole: return mDownloaded.value(t.id, false);
    case ThumbRole:      return mImages.value(t.imageUrl());
    case AvatarRole:     return mImages.value(t.user.avatarUrl);
    default:             return {};
    }
}

void ToneListModel::setTones(const QList<T3kTone>& tones)
{
    beginResetModel();
    mTones = tones;
    endResetModel();
    refreshDownloaded();
    requestImages();
}

void ToneListModel::setFavorite(int toneId, bool on)
{
    for (int i = 0; i < mTones.size(); ++i)
    {
        if (mTones[i].id != toneId) continue;
        mTones[i].isFavorite = on;
        mTones[i].favorites += on ? 1 : -1;
        emit dataChanged(index(i), index(i), {FavoriteRole});
    }
}

void ToneListModel::refreshDownloaded()
{
    mDownloaded.clear();
    for (const auto& t : mTones)
        mDownloaded[t.id] = mIndex.hasTone3000Tone(t.id);
    if (!mTones.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {DownloadedRole});
}

void ToneListModel::requestImages()
{
    auto want = [this](const QString& url, int size, bool circle, int role) {
        if (url.isEmpty() || mImages.contains(url) || mRequested.contains(url)) return;
        mRequested.insert(url);
        mClient.fetchImage(url, QSize(size * 2, size * 2), [this, url, size, circle, role](QPixmap px) {
            mRequested.remove(url);
            if (px.isNull()) return;
            mImages.insert(url, roundedThumb(px, size, circle));
            for (int i = 0; i < mTones.size(); ++i)
                if (mTones[i].imageUrl() == url || mTones[i].user.avatarUrl == url)
                    emit dataChanged(index(i), index(i), {role});
        });
    };
    for (const auto& t : mTones)
    {
        want(t.imageUrl(), kThumb, false, ThumbRole);
        want(t.user.avatarUrl, kAvatar, true, AvatarRole);
    }
}

// ===========================================================================
// ToneDelegate
// ===========================================================================

QRect ToneDelegate::starRect(const QRect& row)
{
    return QRect(row.right() - kStarWidth, row.top(), kStarWidth, row.height());
}

QSize ToneDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const
{
    return QSize(opt.rect.width(), kRowHeight);
}

void ToneDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRect r = opt.rect;
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered  = opt.state & QStyle::State_MouseOver;
    if (selected)     p->fillRect(r, Theme::trackHover());
    else if (hovered) p->fillRect(r, Theme::track());

    // Thumbnail (placeholder square while loading).
    const QRect thumbRect(r.left() + 8, r.top() + (r.height() - kThumb) / 2, kThumb, kThumb);
    const QPixmap thumb = idx.data(ToneListModel::ThumbRole).value<QPixmap>();
    if (thumb.isNull())
    {
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::well());
        p->drawRoundedRect(thumbRect, 5, 5);
    }
    else
    {
        p->drawPixmap(thumbRect.topLeft(), thumb);
    }

    // Star.
    const QRect star = starRect(r);
    const bool fav = idx.data(ToneListModel::FavoriteRole).toBool();
    {
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() + 3);
        p->setFont(f);
        p->setPen(fav ? Theme::genericAccent() : (selected || hovered ? Theme::dimText() : Theme::track()));
        p->drawText(star, Qt::AlignCenter, fav ? QStringLiteral("★") : QStringLiteral("☆"));
    }

    const int left  = thumbRect.right() + 10;
    const int width = star.left() - 4 - left;
    const bool downloaded = idx.data(ToneListModel::DownloadedRole).toBool();

    QFont nameFont = opt.font;
    nameFont.setWeight(QFont::DemiBold);
    p->setFont(nameFont);
    const QFontMetrics nfm(nameFont);
    p->setPen(opt.palette.color(QPalette::Text));
    QString title = idx.data(ToneListModel::TitleRole).toString();
    int titleWidth = width;
    if (downloaded)
    {
        // ✓ after the title: at least one model of this tone is in the library.
        const QString mark = QStringLiteral(" ✓");
        titleWidth -= nfm.horizontalAdvance(mark);
        const QString t = nfm.elidedText(title, Qt::ElideRight, titleWidth);
        p->drawText(QRect(left, r.top() + 8, titleWidth, nfm.height()), Qt::AlignLeft | Qt::AlignVCenter, t);
        p->setPen(Theme::genericAccent());
        p->drawText(QRect(left + nfm.horizontalAdvance(t), r.top() + 8, nfm.horizontalAdvance(mark) + 2, nfm.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, mark);
    }
    else
    {
        p->drawText(QRect(left, r.top() + 8, width, nfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    nfm.elidedText(title, Qt::ElideRight, width));
    }

    QFont subFont = opt.font;
    subFont.setPointSizeF(subFont.pointSizeF() - 1);
    p->setFont(subFont);
    const QFontMetrics sfm(subFont);
    p->setPen(Theme::dimText());
    const QString line2 = idx.data(ToneListModel::GearRole).toString() + " · "
                          + idx.data(ToneListModel::FormatRole).toString();
    const int y2 = r.top() + 8 + nfm.height() + 1;
    p->drawText(QRect(left, y2, width, sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                sfm.elidedText(line2, Qt::ElideRight, width));

    // Creator: avatar + @username on the third line.
    const int y3 = y2 + sfm.height() + 1;
    int cx = left;
    const QPixmap avatar = idx.data(ToneListModel::AvatarRole).value<QPixmap>();
    if (!avatar.isNull())
    {
        p->drawPixmap(cx, y3 + (sfm.height() - kAvatar) / 2, avatar);
        cx += kAvatar + 4;
    }
    else
    {
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::well());
        p->drawEllipse(QRect(cx, y3 + (sfm.height() - kAvatar) / 2, kAvatar, kAvatar));
        cx += kAvatar + 4;
    }
    p->setPen(Theme::dimText());
    p->drawText(QRect(cx, y3, width - (cx - left), sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                sfm.elidedText("@" + idx.data(ToneListModel::CreatorRole).toString(), Qt::ElideRight,
                               width - (cx - left)));
    p->restore();
}

// ===========================================================================
// ToneModelsModel / ToneModelDelegate
// ===========================================================================

int ToneModelsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(mModels.size());
}

QVariant ToneModelsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= mModels.size()) return {};
    const Row& r = mModels[index.row()];
    switch (role)
    {
    case Qt::DisplayRole:
    case NameRole:      return r.model.name;
    case Qt::ToolTipRole:
        return r.state == Done ? QDir::toNativeSeparators(r.path)
             : r.state == Failed ? r.error
             : QStringLiteral("Click to download into the library");
    case ModelIdRole:   return r.model.id;
    case InfoRole:      return r.model.size; // replaced by setModels' info string
    case StateRole:     return static_cast<int>(r.state);
    case ProgressRole:  return r.progress;
    case LocalPathRole: return r.path;
    case ErrorRole:     return r.error;
    default:            return {};
    }
}

void ToneModelsModel::setModels(const QList<T3kModel>& models, const T3kTone& tone, LibraryIndex& index)
{
    beginResetModel();
    mModels.clear();
    for (const T3kModel& m : models)
    {
        Row r;
        r.model = m;
        r.model.size = modelInfo(m, tone); // reuse the field as the info line
        if (const LibraryEntry* e = index.findTone3000Model(m.id))
        {
            r.state = Done;
            r.path  = e->path;
        }
        else
        {
            const QString local = Tone3000Client::localPathFor(m, tone);
            if (QFileInfo::exists(local)) { r.state = Done; r.path = local; }
        }
        mModels << r;
    }
    endResetModel();
}

int ToneModelsModel::rowOf(int modelId) const
{
    for (int i = 0; i < mModels.size(); ++i)
        if (mModels[i].model.id == modelId) return i;
    return -1;
}

void ToneModelsModel::setState(int modelId, State s, const QString& pathOrError)
{
    const int row = rowOf(modelId);
    if (row < 0) return;
    Row& r = mModels[row];
    r.state = s;
    if (s == Done)   { r.path = pathOrError; r.progress = 100; }
    if (s == Failed) { r.error = pathOrError; }
    if (s == Idle || s == Downloading) r.progress = 0;
    emit dataChanged(index(row), index(row));
}

void ToneModelsModel::setProgress(int modelId, int percent)
{
    const int row = rowOf(modelId);
    if (row < 0) return;
    mModels[row].progress = percent;
    emit dataChanged(index(row), index(row), {ProgressRole});
}

QList<T3kModel> ToneModelsModel::pending() const
{
    QList<T3kModel> out;
    for (const Row& r : mModels)
        if (r.state == Idle || r.state == Failed) out << r.model;
    return out;
}

QSize ToneModelDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const
{
    return QSize(opt.rect.width(), kModelRowH);
}

void ToneModelDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRect r = opt.rect;
    const bool hovered = opt.state & QStyle::State_MouseOver;
    if (hovered) p->fillRect(r, Theme::track());

    const auto state = static_cast<ToneModelsModel::State>(idx.data(ToneModelsModel::StateRole).toInt());
    const int progress = idx.data(ToneModelsModel::ProgressRole).toInt();

    // Action pill on the right.
    QString action;
    QColor  pillColor = Theme::trackHover();
    QColor  textColor = opt.palette.color(QPalette::Text);
    switch (state)
    {
    case ToneModelsModel::Idle:        action = "Download"; break;
    case ToneModelsModel::Downloading: action = progress > 0 ? QString("%1 %").arg(progress) : QStringLiteral("…"); break;
    case ToneModelsModel::Done:        action = "Load"; pillColor = Theme::genericAccent(); textColor = Theme::displayBg(); break;
    case ToneModelsModel::Failed:      action = "Retry"; pillColor = Theme::ledRed(); textColor = Theme::displayBg(); break;
    }
    QFont pillFont = opt.font;
    pillFont.setWeight(QFont::DemiBold);
    const QFontMetrics pfm(pillFont);
    const int pillW = pfm.horizontalAdvance(action) + 18;
    const QRect pill(r.right() - 8 - pillW, r.top() + (r.height() - 22) / 2, pillW, 22);
    p->setPen(Qt::NoPen);
    p->setBrush(pillColor);
    p->drawRoundedRect(pill, 11, 11);
    if (state == ToneModelsModel::Downloading && progress > 0)
    {
        p->setBrush(Theme::genericAccent());
        p->setClipRect(QRect(pill.left(), pill.top(), pill.width() * progress / 100, pill.height()));
        p->drawRoundedRect(pill, 11, 11);
        p->setClipping(false);
    }
    p->setFont(pillFont);
    p->setPen(textColor);
    p->drawText(pill, Qt::AlignCenter, action);

    const int left  = r.left() + 10;
    const int width = pill.left() - 8 - left;
    p->setFont(opt.font);
    const QFontMetrics fm(opt.font);
    p->setPen(opt.palette.color(QPalette::Text));
    const QString name = idx.data(ToneModelsModel::NameRole).toString();
    const QString info = idx.data(ToneModelsModel::InfoRole).toString();
    if (info.isEmpty())
    {
        p->drawText(QRect(left, r.top(), width, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(name, Qt::ElideMiddle, width));
    }
    else
    {
        QFont sub = opt.font;
        sub.setPointSizeF(sub.pointSizeF() - 1);
        const QFontMetrics sfm(sub);
        const int total = fm.height() + sfm.height();
        const int top = r.top() + (r.height() - total) / 2;
        p->drawText(QRect(left, top, width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(name, Qt::ElideMiddle, width));
        p->setFont(sub);
        p->setPen(Theme::dimText());
        p->drawText(QRect(left, top + fm.height(), width, sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    sfm.elidedText(info, Qt::ElideRight, width));
    }
    p->restore();
}

// ===========================================================================
// Tone3000Panel
// ===========================================================================

Tone3000Panel::Tone3000Panel(Tone3000Client& client, LibraryIndex& index, QWidget* parent)
    : QWidget(parent)
    , mClient(client)
    , mIndex(index)
{
    setObjectName("tone3000Panel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    buildHeader();
    lay->addWidget(mLogo->parentWidget());
    mNotice = new QLabel;
    mNotice->setObjectName("t3kNotice");
    mNotice->setWordWrap(true);
    mNotice->hide();
    lay->addWidget(mNotice);

    mPages = new QStackedWidget;
    lay->addWidget(mPages, 1);
    buildSetupPage();
    buildSplashPage();
    buildBrowsePage();
    buildDetailPage();

    connect(&mClient, &Tone3000Client::authChanged, this, [this] { syncState(); });
    connect(&mClient, &Tone3000Client::userChanged, this, [this] { updateUserHeader(); });
    connect(&mClient, &Tone3000Client::status, this, [this](const QString& m) {
        mNotice->setText(m);
        mNotice->show();
        QTimer::singleShot(6000, mNotice, &QWidget::hide);
    });
    connect(&mClient, &Tone3000Client::authFinished, this, [this](bool ok, const QString& err, int toneId) {
        mBrowseBtn->setEnabled(true);
        if (!ok)
        {
            mNotice->setText(err);
            mNotice->show();
            QTimer::singleShot(8000, mNotice, &QWidget::hide);
            syncState();
            return;
        }
        mNotice->hide();
        syncState();
        if (toneId > 0) showTone(toneId);
    });
    connect(&mClient, &Tone3000Client::downloadProgress, this, [this](int modelId, qint64 rec, qint64 tot) {
        if (tot > 0) mModelsModel->setProgress(modelId, static_cast<int>(rec * 100 / tot));
    });

    syncState();
}

// --- Header ------------------------------------------------------------------

void Tone3000Panel::buildHeader()
{
    auto* header = new QWidget;
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(2, 0, 0, 0);
    row->setSpacing(6);

    mLogo = new QLabel(header);
    mLogo->setObjectName("t3kLogo");
    mLogo->setPixmap(QPixmap(":/tone3000/tone3000-logo.png")
                         .scaledToHeight(20, Qt::SmoothTransformation));
    mLogo->setToolTip("TONE3000 — NAM captures and IRs");
    mLogo->setCursor(Qt::PointingHandCursor);
    row->addWidget(mLogo, 0, Qt::AlignVCenter);
    row->addStretch();

    mUserBtn = new QToolButton;
    mUserBtn->setObjectName("t3kUser");
    mUserBtn->setAutoRaise(true);
    mUserBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    mUserBtn->setPopupMode(QToolButton::InstantPopup);
    mUserBtn->setFocusPolicy(Qt::NoFocus);
    auto* menu = new QMenu(mUserBtn);
    connect(menu->addAction("Open my TONE3000 profile"), &QAction::triggered, this, [this] {
        const QString u = mClient.user().url;
        QDesktopServices::openUrl(QUrl(u.isEmpty() ? QString(Tone3000Client::kSiteUrl) : u));
    });
    connect(menu->addAction("Change download folder…"), &QAction::triggered, this, [this] {
        const QString dir = pickDownloadDir(this);
        if (dir.isEmpty()) return;
        QSettings().setValue("library/tone3000Dir", dir);
        mIndex.rescan();
    });
    connect(menu->addAction("TONE3000 publishable key…"), &QAction::triggered, this, [this] {
        bool ok = false;
        const QString key = QInputDialog::getText(window(), "TONE3000 key",
            "Publishable key (t3k_pub_…) from tone3000.com → Settings → API Keys.\n"
            "Leave empty to use the key this build was compiled with.",
            QLineEdit::Normal, QSettings().value("tone3000/clientId").toString(), &ok);
        if (!ok) return;
        Tone3000Client::setClientId(key);
        mClient.signOut();
        syncState();
    });
    menu->addSeparator();
    connect(menu->addAction("Sign out"), &QAction::triggered, this, [this] { mClient.signOut(); });
    mUserBtn->setMenu(menu);
    row->addWidget(mUserBtn, 0, Qt::AlignVCenter);

    mLogo->installEventFilter(this);
    updateUserHeader();
}

QString Tone3000Panel::pickDownloadDir(QWidget* parent)
{
    return QFileDialog::getExistingDirectory(parent, "TONE3000 download folder",
                                             Tone3000Client::downloadDir());
}

bool Tone3000Panel::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == mLogo && ev->type() == QEvent::MouseButtonRelease)
    {
        QDesktopServices::openUrl(QUrl(Tone3000Client::kSiteUrl));
        return true;
    }
    if (mToneView && obj == mToneView->viewport() && ev->type() == QEvent::MouseButtonPress)
    {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton && handleToneViewClick(me))
            return true;
    }
    return QWidget::eventFilter(obj, ev);
}

void Tone3000Panel::updateUserHeader()
{
    const T3kUser& u = mClient.user();
    const bool in = mClient.isSignedIn();
    mUserBtn->setVisible(in);
    if (!in) return;
    mUserBtn->setText(u.username.isEmpty() ? QStringLiteral("account") : "@" + u.username);
    mUserBtn->setToolTip(u.label());
    mUserBtn->setIcon(QIcon());
    if (!u.avatarUrl.isEmpty())
    {
        const QString url = u.avatarUrl;
        mClient.fetchImage(url, QSize(64, 64), [this, url](QPixmap px) {
            if (px.isNull() || mClient.user().avatarUrl != url) return;
            mUserBtn->setIcon(QIcon(roundedThumb(px, 18, true)));
        });
    }
}

// --- Pages -------------------------------------------------------------------

void Tone3000Panel::buildSetupPage()
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 12, 8, 8);
    lay->setSpacing(10);
    auto* text = new QLabel(
        "<b>TONE3000 needs an app key.</b><br><br>"
        "This build was compiled without a TONE3000 publishable key. Create one on "
        "<a href=\"https://www.tone3000.com/settings\">tone3000.com → Settings → API Keys</a> "
        "(the redirect URI is <code>http://localhost:51731/callback</code>) and paste it here. "
        "It is not a secret; it only identifies the app.");
    text->setWordWrap(true);
    text->setOpenExternalLinks(true);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);
    lay->addWidget(text);
    mKeyEdit = new QLineEdit;
    mKeyEdit->setObjectName("librarySearch");
    mKeyEdit->setPlaceholderText("t3k_pub_…");
    lay->addWidget(mKeyEdit);
    auto* save = new QPushButton("Save key");
    connect(save, &QPushButton::clicked, this, [this] {
        Tone3000Client::setClientId(mKeyEdit->text());
        syncState();
    });
    connect(mKeyEdit, &QLineEdit::returnPressed, save, &QPushButton::click);
    lay->addWidget(save, 0, Qt::AlignLeft);
    lay->addStretch();
    mPages->insertWidget(PageSetup, page);
}

void Tone3000Panel::buildSplashPage()
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 18, 8, 8);
    lay->setSpacing(14);

    auto* logo = new QLabel;
    logo->setPixmap(QPixmap(":/tone3000/tone3000-logo.png").scaledToWidth(200, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignHCenter);
    lay->addWidget(logo);

    auto* title = new QLabel("NAM Player × TONE3000");
    title->setObjectName("t3kSplashTitle");
    title->setAlignment(Qt::AlignHCenter);
    lay->addWidget(title);

    auto* copy = new QLabel(
        "NAM Player has partnered with TONE3000 to give you access to a massive library of "
        "Neural Amp Modeler (NAM) captures and IRs of real analog gear, created by a global "
        "community of musicians.");
    copy->setWordWrap(true);
    copy->setAlignment(Qt::AlignHCenter);
    lay->addWidget(copy);

    auto* cont = new QPushButton("Continue");
    cont->setObjectName("t3kPrimary");
    cont->setToolTip("Sign in with your TONE3000 account in the browser and pick a tone");
    connect(cont, &QPushButton::clicked, this, [this] { browseCatalog(); });
    lay->addWidget(cont, 0, Qt::AlignHCenter);

    auto* fine = new QLabel("Sign-in happens in your browser: TONE3000 sends a code to your e-mail. "
                            "Downloads go to the library's TONE3000 folder.");
    fine->setObjectName("t3kFinePrint");
    fine->setWordWrap(true);
    fine->setAlignment(Qt::AlignHCenter);
    lay->addWidget(fine);
    lay->addStretch();
    mPages->insertWidget(PageSplash, page);
}

void Tone3000Panel::buildBrowsePage()
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    // List switch.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(0);
        for (int i = 0; i < 5; ++i)
        {
            auto* b = new QToolButton;
            b->setObjectName("librarySection");
            b->setText(kListNames[i]);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [this, i] { selectList(i); });
            row->addWidget(b);
            mListBtn[i] = b;
        }
        lay->addLayout(row);
    }

    // The persistent path to the whole catalog: the select flow in the browser.
    mBrowseBtn = new QPushButton("Browse TONE3000…");
    mBrowseBtn->setObjectName("t3kBrowse");
    mBrowseBtn->setIcon(QIcon(QPixmap(":/tone3000/t3k-mark.png").scaledToHeight(14, Qt::SmoothTransformation)));
    mBrowseBtn->setToolTip("Search the full TONE3000 catalog in your browser and pick a tone to bring back here");
    mBrowseBtn->setFocusPolicy(Qt::NoFocus);
    connect(mBrowseBtn, &QPushButton::clicked, this, [this] { browseCatalog(); });
    lay->addWidget(mBrowseBtn);

    mToneModel = new ToneListModel(mClient, mIndex, this);
    mToneView = new QListView;
    mToneView->setObjectName("libraryList");
    mToneView->setModel(mToneModel);
    mToneView->setItemDelegate(new ToneDelegate(mToneView));
    mToneView->setUniformItemSizes(true);
    mToneView->setSelectionMode(QAbstractItemView::NoSelection);
    mToneView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mToneView->setMouseTracking(true);
    mToneView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    mToneView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mToneView->setFrameShape(QFrame::NoFrame);
    mToneView->viewport()->installEventFilter(this);
    lay->addWidget(mToneView, 1);

    // Footer: status + paging + refresh.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        mListStatus = new QLabel;
        mListStatus->setObjectName("libraryStatus");
        mListStatus->setWordWrap(true);
        row->addWidget(mListStatus, 1);
        mPrevPage = new QToolButton;
        mPrevPage->setObjectName("libraryTool");
        mPrevPage->setText("‹");
        mPrevPage->setAutoRaise(true);
        mPrevPage->setFocusPolicy(Qt::NoFocus);
        connect(mPrevPage, &QToolButton::clicked, this, [this] { if (mPage > 1) { --mPage; loadList(true); } });
        mNextPage = new QToolButton;
        mNextPage->setObjectName("libraryTool");
        mNextPage->setText("›");
        mNextPage->setAutoRaise(true);
        mNextPage->setFocusPolicy(Qt::NoFocus);
        connect(mNextPage, &QToolButton::clicked, this, [this] { if (mPage < mTotalPages) { ++mPage; loadList(true); } });
        auto* refresh = new QToolButton;
        refresh->setObjectName("libraryTool");
        refresh->setText("↻");
        refresh->setAutoRaise(true);
        refresh->setFocusPolicy(Qt::NoFocus);
        refresh->setToolTip("Reload this list from TONE3000");
        connect(refresh, &QToolButton::clicked, this, [this] { loadList(false); });
        row->addWidget(mPrevPage);
        row->addWidget(mNextPage);
        row->addWidget(refresh);
        lay->addLayout(row);
    }

    mPages->insertWidget(PageBrowse, page);
}

void Tone3000Panel::buildDetailPage()
{
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(6);

    // Top row: back + open on site + favorite.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        auto* back = new QToolButton;
        back->setObjectName("libraryTool");
        back->setText("‹ Back");
        back->setAutoRaise(true);
        back->setFocusPolicy(Qt::NoFocus);
        connect(back, &QToolButton::clicked, this, [this] { mPages->setCurrentIndex(PageBrowse); });
        row->addWidget(back);
        row->addStretch();
        mDetailFav = new QToolButton;
        mDetailFav->setObjectName("libraryMode");
        mDetailFav->setCheckable(true);
        mDetailFav->setAutoRaise(true);
        mDetailFav->setFocusPolicy(Qt::NoFocus);
        mDetailFav->setToolTip("Favorite on TONE3000");
        connect(mDetailFav, &QToolButton::clicked, this, [this](bool on) { toggleFavorite(mDetailTone.id, on); });
        row->addWidget(mDetailFav);
        auto* open = new QToolButton;
        open->setObjectName("libraryTool");
        open->setText("Open on TONE3000 ↗");
        open->setAutoRaise(true);
        open->setFocusPolicy(Qt::NoFocus);
        connect(open, &QToolButton::clicked, this, [this] {
            if (!mDetailTone.url.isEmpty()) QDesktopServices::openUrl(QUrl(mDetailTone.url));
        });
        row->addWidget(open);
        outer->addLayout(row);
    }

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget;
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(2, 0, 2, 0);
    lay->setSpacing(6);

    mDetailImage = new QLabel;
    mDetailImage->setObjectName("t3kDetailImage");
    mDetailImage->setAlignment(Qt::AlignCenter);
    mDetailImage->setMinimumHeight(120);
    lay->addWidget(mDetailImage);

    mDetailTitle = new QLabel;
    mDetailTitle->setObjectName("t3kDetailTitle");
    mDetailTitle->setWordWrap(true);
    lay->addWidget(mDetailTitle);

    mDetailMeta = new QLabel;
    mDetailMeta->setObjectName("t3kDetailMeta");
    mDetailMeta->setWordWrap(true);
    lay->addWidget(mDetailMeta);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        mDetailCreatorAvatar = new QLabel;
        mDetailCreatorAvatar->setFixedSize(20, 20);
        row->addWidget(mDetailCreatorAvatar);
        mDetailCreator = new QLabel;
        mDetailCreator->setObjectName("t3kDetailMeta");
        mDetailCreator->setOpenExternalLinks(true);
        mDetailCreator->setTextInteractionFlags(Qt::TextBrowserInteraction);
        row->addWidget(mDetailCreator, 1);
        lay->addLayout(row);
    }

    mDetailDesc = new QLabel;
    mDetailDesc->setObjectName("t3kDetailDesc");
    mDetailDesc->setWordWrap(true);
    mDetailDesc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(mDetailDesc);

    auto* modelsHeader = new QLabel("Models");
    modelsHeader->setObjectName("t3kSection");
    lay->addWidget(modelsHeader);

    mDetailStatus = new QLabel;
    mDetailStatus->setObjectName("libraryStatus");
    mDetailStatus->setWordWrap(true);
    lay->addWidget(mDetailStatus);

    mModelsModel = new ToneModelsModel(this);
    mModelsView = new QListView;
    mModelsView->setObjectName("libraryList");
    mModelsView->setModel(mModelsModel);
    mModelsView->setItemDelegate(new ToneModelDelegate(mModelsView));
    mModelsView->setUniformItemSizes(true);
    mModelsView->setSelectionMode(QAbstractItemView::NoSelection);
    mModelsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mModelsView->setMouseTracking(true);
    mModelsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mModelsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mModelsView->setFrameShape(QFrame::NoFrame);
    mModelsView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    connect(mModelsView, &QListView::clicked, this, &Tone3000Panel::onModelRowClicked);
    connect(mModelsModel, &QAbstractItemModel::modelReset, this, [this] {
        mModelsView->setFixedHeight(std::max(1, mModelsModel->rowCount()) * kModelRowH + 4);
    });
    lay->addWidget(mModelsView);

    mDownloadAll = new QPushButton("Download all");
    mDownloadAll->setFocusPolicy(Qt::NoFocus);
    connect(mDownloadAll, &QPushButton::clicked, this, &Tone3000Panel::downloadAll);
    lay->addWidget(mDownloadAll, 0, Qt::AlignLeft);

    auto* license = new QLabel;
    license->setObjectName("t3kFinePrint");
    license->setWordWrap(true);
    license->setText("Models are shared by their creators under the license shown above. "
                     "Please respect it, and credit them when you share presets.");
    lay->addWidget(license);
    lay->addStretch();

    scroll->setWidget(body);
    outer->addWidget(scroll, 1);
    mPages->insertWidget(PageDetail, page);
}

// --- State -------------------------------------------------------------------

void Tone3000Panel::syncState()
{
    updateUserHeader();
    if (!Tone3000Client::hasClientId())
    {
        mLogo->parentWidget()->show();
        mKeyEdit->setText(QSettings().value("tone3000/clientId").toString());
        mPages->setCurrentIndex(PageSetup);
        return;
    }
    if (!mClient.isSignedIn())
    {
        mLogo->parentWidget()->hide(); // the splash carries the big logo
        mPages->setCurrentIndex(PageSplash);
        return;
    }
    mLogo->parentWidget()->show();
    if (mPages->currentIndex() != PageDetail)
    {
        mPages->setCurrentIndex(PageBrowse);
        selectList(std::clamp(QSettings().value("tone3000/list", 0).toInt(), 0, 4), true);
    }
}

void Tone3000Panel::browseCatalog()
{
    mBrowseBtn->setEnabled(false);
    mNotice->setText("Waiting for the browser… pick a tone on TONE3000 and you'll be brought back here.");
    mNotice->show();
    mClient.startAuth(Tone3000Client::Flow::SelectTone);
}

void Tone3000Panel::selectList(int which, bool force)
{
    if (which == mList && !force && mToneModel->rowCount() > 0) return;
    if (which != mList) mPage = 1;
    mList = which;
    for (int i = 0; i < 5; ++i) mListBtn[i]->setChecked(i == which);
    QSettings().setValue("tone3000/list", which);
    loadList(true);
}

void Tone3000Panel::showListStatus(const QString& text)
{
    mListStatus->setText(text);
}

void Tone3000Panel::loadList(bool useCache)
{
    const auto kind = kListKinds[mList];
    const bool paged = kind == Tone3000Client::ListKind::Favorited
                       || kind == Tone3000Client::ListKind::Downloaded
                       || kind == Tone3000Client::ListKind::Created;
    showListStatus("Loading…");
    mPrevPage->setVisible(paged);
    mNextPage->setVisible(paged);
    const int myList = mList, myPage = mPage;
    mClient.listTones(kind, mPage, [this, myList, myPage, paged](const Tone3000Client::Result& r) {
        if (myList != mList || myPage != mPage) return; // switched meanwhile
        if (!r.ok)
        {
            if (r.offline)           showListStatus("Offline. The local library still works; TONE3000 will be back when you are.");
            else if (r.authRequired) { showListStatus(r.error); syncState(); }
            else                     showListStatus(r.error);
            return;
        }
        QList<T3kTone> tones;
        for (const QJsonValue v : r.object().value("data").toArray())
            tones << Tone3000Client::parseTone(v.toObject());
        mToneModel->setTones(tones);
        mToneView->scrollToTop();
        mTotalPages = paged ? std::max(1, r.object().value("total_pages").toInt(1)) : 1;
        mPrevPage->setEnabled(mPage > 1);
        mNextPage->setEnabled(mPage < mTotalPages);
        if (tones.isEmpty())
            showListStatus(myList == 2 ? "No favorites yet. ★ a tone here or on TONE3000."
                         : myList == 3 ? "Nothing downloaded on TONE3000 yet."
                         : myList == 4 ? "You haven't published tones on TONE3000."
                                       : "Nothing here right now.");
        else if (paged)
            showListStatus(QString("%1 tones · page %2 of %3").arg(r.object().value("total").toInt(tones.size()))
                               .arg(mPage).arg(mTotalPages));
        else
            showListStatus(QString("%1 tones").arg(tones.size()));
    }, useCache);
}

bool Tone3000Panel::handleToneViewClick(QMouseEvent* me)
{
    const QModelIndex idx = mToneView->indexAt(me->pos());
    if (!idx.isValid()) return false;
    const T3kTone* t = mToneModel->tone(idx.row());
    if (!t) return false;
    if (ToneDelegate::starRect(mToneView->visualRect(idx)).contains(me->pos()))
    {
        toggleFavorite(t->id, !t->isFavorite);
        return true;
    }
    openDetail(*t);
    return true;
}

void Tone3000Panel::toggleFavorite(int toneId, bool on)
{
    mToneModel->setFavorite(toneId, on);
    if (mDetailTone.id == toneId) { mDetailTone.isFavorite = on; mDetailFav->setChecked(on); mDetailFav->setText(on ? "★" : "☆"); }
    mClient.setFavorite(toneId, on, [this, toneId, on](const Tone3000Client::Result& r) {
        if (r.ok) return;
        mToneModel->setFavorite(toneId, !on); // revert
        if (mDetailTone.id == toneId) { mDetailTone.isFavorite = !on; mDetailFav->setChecked(!on); mDetailFav->setText(!on ? "★" : "☆"); }
        mNotice->setText(r.offline ? QStringLiteral("Offline — favorite not saved.") : r.error);
        mNotice->show();
        QTimer::singleShot(6000, mNotice, &QWidget::hide);
    });
}

// --- Detail ------------------------------------------------------------------

void Tone3000Panel::showTone(int toneId)
{
    mPages->setCurrentIndex(PageDetail);
    mDetailTitle->setText("Loading…");
    mDetailMeta->clear();
    mDetailCreator->clear();
    mDetailCreatorAvatar->clear();
    mDetailDesc->clear();
    mDetailImage->clear();
    mDetailStatus->clear();
    mModelsModel->setModels({}, {}, mIndex);
    const int req = ++mDetailRequest;
    mClient.fetchTone(toneId, [this, req](const Tone3000Client::Result& r) {
        if (req != mDetailRequest) return;
        if (!r.ok)
        {
            mDetailTitle->setText(r.offline ? QStringLiteral("Offline") : QStringLiteral("Could not load this tone"));
            mDetailStatus->setText(r.error);
            return;
        }
        openDetail(Tone3000Client::parseTone(r.object()));
    });
}

void Tone3000Panel::openDetail(const T3kTone& tone)
{
    mDetailTone = tone;
    mPages->setCurrentIndex(PageDetail);

    mDetailTitle->setText(tone.title);
    QStringList meta{tone.gearLabel(), tone.formatLabel(), tone.licenseLabel()};
    if (!tone.makes.isEmpty()) meta << tone.makes.join(", ");
    meta << QString("%1 downloads · %2 favorites").arg(countLabel(tone.downloads), countLabel(tone.favorites));
    mDetailMeta->setText(meta.join(" · "));
    mDetailCreator->setText(QString("<a href=\"%1\">@%2</a>%3")
                                .arg(tone.user.url.isEmpty() ? Tone3000Client::kSiteUrl : tone.user.url,
                                     tone.user.username.toHtmlEscaped(),
                                     tone.user.displayName.isEmpty() || tone.user.displayName == tone.user.username
                                         ? QString() : " · " + tone.user.displayName.toHtmlEscaped()));
    mDetailDesc->setText(tone.description.trimmed());
    mDetailDesc->setVisible(!tone.description.trimmed().isEmpty());
    mDetailFav->setChecked(tone.isFavorite);
    mDetailFav->setText(tone.isFavorite ? "★" : "☆");
    mDetailStatus->setText("Loading models…");
    mDownloadAll->setEnabled(false);

    setThumbnail(mDetailImage, tone.imageUrl(), QSize(600, 400), false);
    setThumbnail(mDetailCreatorAvatar, tone.user.avatarUrl, QSize(20, 20), true);

    // Fresh tone (description, counts) if we came from a list card.
    if (tone.description.isEmpty())
    {
        const int req = ++mDetailRequest;
        mClient.fetchTone(tone.id, [this, req](const Tone3000Client::Result& r) {
            if (req != mDetailRequest || !r.ok) return;
            const T3kTone full = Tone3000Client::parseTone(r.object());
            if (full.id != mDetailTone.id) return;
            mDetailTone.description = full.description;
            mDetailDesc->setText(full.description.trimmed());
            mDetailDesc->setVisible(!full.description.trimmed().isEmpty());
        });
    }
    loadDetailModels();
}

void Tone3000Panel::setThumbnail(QLabel* label, const QString& url, const QSize& size, bool round)
{
    label->clear();
    if (url.isEmpty()) { label->setVisible(!round ? false : true); return; }
    label->setVisible(true);
    mClient.fetchImage(url, size, [this, label, url, size, round](QPixmap px) {
        if (px.isNull()) return;
        if (label == mDetailImage && mDetailTone.imageUrl() != url) return;
        if (label == mDetailCreatorAvatar && mDetailTone.user.avatarUrl != url) return;
        if (round)
        {
            label->setPixmap(roundedThumb(px, size.height(), true));
            return;
        }
        // Fit the panel width, rounded corners.
        const int w = std::max(120, label->width() - 4);
        QPixmap scaled = px.scaledToWidth(std::min(w, px.width() * 2), Qt::SmoothTransformation);
        if (scaled.height() > 220) scaled = scaled.scaledToHeight(220, Qt::SmoothTransformation);
        QPixmap out(scaled.size());
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(out.rect(), 6, 6);
        p.setClipPath(path);
        p.drawPixmap(0, 0, scaled);
        label->setPixmap(out);
    });
}

void Tone3000Panel::loadDetailModels()
{
    const int req = ++mDetailRequest;
    const T3kTone tone = mDetailTone;
    mClient.listModels(tone, [this, req, tone](const Tone3000Client::Result& r, QList<T3kModel> models) {
        if (req != mDetailRequest) return;
        if (!r.ok)
        {
            mDetailStatus->setText(r.offline ? QStringLiteral("Offline — models can't be listed right now.") : r.error);
            return;
        }
        mModelsModel->setModels(models, tone, mIndex);
        const int pending = mModelsModel->pending().size();
        mDetailStatus->setText(models.isEmpty() ? QStringLiteral("This tone has no downloadable models.")
                               : pending == 0    ? QStringLiteral("All models are in your library.")
                                                 : QString("%1 models · click one to download").arg(models.size()));
        mDownloadAll->setEnabled(pending > 0);
        mDownloadAll->setText(pending > 0 && pending < models.size()
                                  ? QString("Download the other %1").arg(pending)
                                  : QStringLiteral("Download all"));
    });
}

void Tone3000Panel::onModelRowClicked(const QModelIndex& idx)
{
    const auto state = static_cast<ToneModelsModel::State>(idx.data(ToneModelsModel::StateRole).toInt());
    if (state == ToneModelsModel::Done)
    {
        const QString path = idx.data(ToneModelsModel::LocalPathRole).toString();
        bool ok = false;
        const auto kind = LibraryIndex::kindForPath(path, &ok);
        if (ok) emit loadRequested(kind, path);
        return;
    }
    if (state == ToneModelsModel::Downloading) return;
    startDownload(idx.row());
}

void Tone3000Panel::startDownload(int row)
{
    const T3kModel* m = mModelsModel->model(row);
    if (!m) return;
    const T3kModel model = *m;
    const T3kTone  tone  = mDetailTone;
    mModelsModel->setState(model.id, ToneModelsModel::Downloading);
    mClient.downloadModel(model, tone, [this, model](bool ok, QString path, QString err) {
        mModelsModel->setState(model.id, ok ? ToneModelsModel::Done : ToneModelsModel::Failed,
                               ok ? path : err);
        if (ok)
        {
            mIndex.rescan(); // a new folder isn't watched until scanned once
            const int pending = mModelsModel->pending().size();
            mDownloadAll->setEnabled(pending > 0);
            mDetailStatus->setText(pending == 0 ? QStringLiteral("All models are in your library.")
                                                : QString("%1 left to download").arg(pending));
        }
        else
        {
            mDetailStatus->setText(err);
        }
    });
}

void Tone3000Panel::downloadAll()
{
    for (int i = 0; i < mModelsModel->rowCount(); ++i)
    {
        const auto state = static_cast<ToneModelsModel::State>(
            mModelsModel->index(i).data(ToneModelsModel::StateRole).toInt());
        if (state == ToneModelsModel::Idle || state == ToneModelsModel::Failed)
            startDownload(i);
    }
    mDownloadAll->setEnabled(false);
}

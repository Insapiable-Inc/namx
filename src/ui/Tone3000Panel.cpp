#include "Tone3000Panel.h"

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

#include "Common.h"
#include "Theme.h"

namespace npl
{

constexpr int THUMB = 44;
constexpr int AVATAR = 16;
constexpr int ROW_HEIGHT = 58;
constexpr int STAR_WIDTH = 26;
constexpr int MODEL_ROW_H = 34;

static const char* k_list_names[5] = {"Trending", "Latest", "Favorites", "Downloads", "Mine"};
static const Tone3000Client::ListKind LIST_KINDS[5] = {
    Tone3000Client::ListKind::TRENDING,  Tone3000Client::ListKind::LATEST,
    Tone3000Client::ListKind::FAVORITED, Tone3000Client::ListKind::DOWNLOADED,
    Tone3000Client::ListKind::CREATED,
};

static QPixmap rounded_thumb(const QPixmap& src, int size, bool circle) {
    if (src.isNull()) {
        return {};
    }
    // Cover-crop to a square, then round the corners.
    QPixmap scaled =
        src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect crop((scaled.width() - size) / 2, (scaled.height() - size) / 2, size, size);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    if (circle) {
        path.addEllipse(0, 0, size, size);
    } else {
        path.addRoundedRect(0, 0, size, size, 5, 5);
    }
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled.copy(crop));
    return out;
}

static QString count_label(int n) {
    if (n >= 1000) {
        return QString::number(n / 1000.0, 'f', n >= 10000 ? 0 : 1) + "k";
    }
    return QString::number(n);
}

static QString model_info(const T3kModel& m, const T3kTone& t) {
    QStringList parts;
    if (t.format == "ir") {
        parts << "IR";
    } else if (m.architecture == "2") {
        parts << "NAM A2";
    } else if (m.architecture == "1") {
        parts << "NAM A1";
    } else if (m.architecture == "custom") {
        parts << "NAM custom";
    }
    if (!m.size.isEmpty() && m.size != "custom") {
        parts << m.size;
    }
    return parts.join(" · ");
}

// ===========================================================================
// ToneListModel
// ===========================================================================

ToneListModel::ToneListModel(Tone3000Client& new_client, LibraryIndex& new_index, QObject* parent)
    : QAbstractListModel(parent)
    , client(new_client)
    , library_index(new_index) {
    connect(&library_index, &LibraryIndex::entries_changed, this,
            &ToneListModel::refresh_downloaded);
}

int ToneListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(tones.size());
}

QVariant ToneListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= tones.size()) {
        return {};
    }
    const T3kTone& t = tones[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case TITLE_ROLE:
            return t.title;
        case Qt::ToolTipRole: {
            QString tip = "<b>" + t.title.toHtmlEscaped() + "</b><br>" + t.gear_label() + " · " +
                          t.format_label() + " · " + t.license_label() + "<br>by @" +
                          t.user.username.toHtmlEscaped() +
                          QString("<br>%1 models · %2 downloads · %3 favorites")
                              .arg(t.models_count)
                              .arg(t.downloads)
                              .arg(t.favorites);
            if (!t.makes.isEmpty()) {
                tip += "<br>" + t.makes.join(", ").toHtmlEscaped();
            }
            return tip;
        }
        case TONE_ID_ROLE:
            return t.id;
        case GEAR_ROLE:
            return t.gear_label();
        case FORMAT_ROLE:
            return t.format_label();
        case CREATOR_ROLE:
            return t.user.username;
        case LICENSE_ROLE:
            return t.license_label();
        case FAVORITE_ROLE:
            return t.is_favorite;
        case DOWNLOADED_ROLE:
            return downloaded.value(t.id, false);
        case THUMB_ROLE:
            return images.value(t.image_url());
        case AVATAR_ROLE:
            return images.value(t.user.avatar_url);
        default:
            return {};
    }
}

void ToneListModel::set_tones(const QList<T3kTone>& new_tones) {
    beginResetModel();
    tones = new_tones;
    endResetModel();
    refresh_downloaded();
    request_images();
}

void ToneListModel::set_favorite(int tone_id, bool on) {
    for (int i = 0; i < tones.size(); ++i) {
        if (tones[i].id != tone_id) {
            continue;
        }
        tones[i].is_favorite = on;
        tones[i].favorites += on ? 1 : -1;
        emit dataChanged(index(i), index(i), {FAVORITE_ROLE});
    }
}

void ToneListModel::refresh_downloaded() {
    downloaded.clear();
    for (const auto& t : tones) {
        downloaded[t.id] = library_index.has_tone3000_tone(t.id);
    }
    if (!tones.isEmpty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {DOWNLOADED_ROLE});
    }
}

void ToneListModel::request_images() {
    auto want = [this](const QString& url, int size, bool circle, int role) {
        if (url.isEmpty() || images.contains(url) || requested.contains(url)) {
            return;
        }
        requested.insert(url);
        client.fetch_image(
            url, QSize(size * 2, size * 2), [this, url, size, circle, role](QPixmap px) {
                requested.remove(url);
                if (px.isNull()) {
                    return;
                }
                images.insert(url, rounded_thumb(px, size, circle));
                for (int i = 0; i < tones.size(); ++i) {
                    if (tones[i].image_url() == url || tones[i].user.avatar_url == url) {
                        emit dataChanged(index(i), index(i), {role});
                    }
                }
            });
    };
    for (const auto& t : tones) {
        want(t.image_url(), THUMB, false, THUMB_ROLE);
        want(t.user.avatar_url, AVATAR, true, AVATAR_ROLE);
    }
}

// ===========================================================================
// ToneDelegate
// ===========================================================================

QRect ToneDelegate::star_rect(const QRect& row) {
    return QRect(row.right() - STAR_WIDTH, row.top(), STAR_WIDTH, row.height());
}

QSize ToneDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const {
    return QSize(opt.rect.width(), ROW_HEIGHT);
}

void ToneDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                         const QModelIndex& idx) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRect r = opt.rect;
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = opt.state & QStyle::State_MouseOver;
    if (selected) {
        p->fillRect(r, Theme::track_hover());
    } else if (hovered) {
        p->fillRect(r, Theme::track());
    }

    // Thumbnail (placeholder square while loading).
    const QRect thumb_rect(r.left() + 8, r.top() + (r.height() - THUMB) / 2, THUMB, THUMB);
    const QPixmap thumb = idx.data(ToneListModel::THUMB_ROLE).value<QPixmap>();
    if (thumb.isNull()) {
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::well());
        p->drawRoundedRect(thumb_rect, 5, 5);
    } else {
        p->drawPixmap(thumb_rect.topLeft(), thumb);
    }

    // Star.
    const QRect star = star_rect(r);
    const bool fav = idx.data(ToneListModel::FAVORITE_ROLE).toBool();
    {
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() + 3);
        p->setFont(f);
        p->setPen(fav ? Theme::generic_accent()
                      : (selected || hovered ? Theme::dim_text() : Theme::track()));
        p->drawText(star, Qt::AlignCenter, fav ? QStringLiteral("★") : QStringLiteral("☆"));
    }

    const int left = thumb_rect.right() + 10;
    const int width = star.left() - 4 - left;
    const bool downloaded = idx.data(ToneListModel::DOWNLOADED_ROLE).toBool();

    QFont name_font = opt.font;
    name_font.setWeight(QFont::DemiBold);
    p->setFont(name_font);
    const QFontMetrics nfm(name_font);
    p->setPen(opt.palette.color(QPalette::Text));
    QString title = idx.data(ToneListModel::TITLE_ROLE).toString();
    int title_width = width;
    if (downloaded) {
        // ✓ after the title: at least one model of this tone is in the library.
        const QString mark = QStringLiteral(" ✓");
        title_width -= nfm.horizontalAdvance(mark);
        const QString t = nfm.elidedText(title, Qt::ElideRight, title_width);
        p->drawText(QRect(left, r.top() + 8, title_width, nfm.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, t);
        p->setPen(Theme::generic_accent());
        p->drawText(QRect(left + nfm.horizontalAdvance(t), r.top() + 8,
                          nfm.horizontalAdvance(mark) + 2, nfm.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, mark);
    } else {
        p->drawText(QRect(left, r.top() + 8, width, nfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    nfm.elidedText(title, Qt::ElideRight, width));
    }

    QFont sub_font = opt.font;
    sub_font.setPointSizeF(sub_font.pointSizeF() - 1);
    p->setFont(sub_font);
    const QFontMetrics sfm(sub_font);
    p->setPen(Theme::dim_text());
    const QString line2 = idx.data(ToneListModel::GEAR_ROLE).toString() + " · " +
                          idx.data(ToneListModel::FORMAT_ROLE).toString();
    const int y2 = r.top() + 8 + nfm.height() + 1;
    p->drawText(QRect(left, y2, width, sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                sfm.elidedText(line2, Qt::ElideRight, width));

    // Creator: avatar + @username on the third line.
    const int y3 = y2 + sfm.height() + 1;
    int cx = left;
    const QPixmap avatar = idx.data(ToneListModel::AVATAR_ROLE).value<QPixmap>();
    if (!avatar.isNull()) {
        p->drawPixmap(cx, y3 + (sfm.height() - AVATAR) / 2, avatar);
        cx += AVATAR + 4;
    } else {
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::well());
        p->drawEllipse(QRect(cx, y3 + (sfm.height() - AVATAR) / 2, AVATAR, AVATAR));
        cx += AVATAR + 4;
    }
    p->setPen(Theme::dim_text());
    p->drawText(QRect(cx, y3, width - (cx - left), sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                sfm.elidedText("@" + idx.data(ToneListModel::CREATOR_ROLE).toString(),
                               Qt::ElideRight, width - (cx - left)));
    p->restore();
}

// ===========================================================================
// ToneModelsModel / ToneModelDelegate
// ===========================================================================

int ToneModelsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(models.size());
}

QVariant ToneModelsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= models.size()) {
        return {};
    }
    const Row& r = models[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case NAME_ROLE:
            return r.model.name;
        case Qt::ToolTipRole:
            return r.state == DONE     ? QDir::toNativeSeparators(r.path)
                   : r.state == FAILED ? r.error
                                       : QStringLiteral("Click to download into the library");
        case MODEL_ID_ROLE:
            return r.model.id;
        case INFO_ROLE:
            return r.model.size; // replaced by setModels' info string
        case STATE_ROLE:
            return static_cast<int>(r.state);
        case PROGRESS_ROLE:
            return r.progress;
        case LOCAL_PATH_ROLE:
            return r.path;
        case ERROR_ROLE:
            return r.error;
        default:
            return {};
    }
}

void ToneModelsModel::set_models(const QList<T3kModel>& new_models, const T3kTone& tone,
                                 LibraryIndex& index) {
    beginResetModel();
    models.clear();
    for (const T3kModel& m : new_models) {
        Row r;
        r.model = m;
        r.model.size = model_info(m, tone); // reuse the field as the info line
        if (const LibraryEntry* e = index.find_tone3000_model(m.id)) {
            r.state = DONE;
            r.path = e->path;
        } else {
            const QString local = Tone3000Client::local_path_for(m, tone);
            if (QFileInfo::exists(local)) {
                r.state = DONE;
                r.path = local;
            }
        }
        models << r;
    }
    endResetModel();
}

int ToneModelsModel::row_of(int model_id) const {
    for (int i = 0; i < models.size(); ++i) {
        if (models[i].model.id == model_id) {
            return i;
        }
    }
    return -1;
}

void ToneModelsModel::set_state(int model_id, State s, const QString& path_or_error) {
    const int row = row_of(model_id);
    if (row < 0) {
        return;
    }
    Row& r = models[row];
    r.state = s;
    if (s == DONE) {
        r.path = path_or_error;
        r.progress = 100;
    }
    if (s == FAILED) {
        r.error = path_or_error;
    }
    if (s == IDLE || s == DOWNLOADING) {
        r.progress = 0;
    }
    emit dataChanged(index(row), index(row));
}

void ToneModelsModel::set_progress(int model_id, int percent) {
    const int row = row_of(model_id);
    if (row < 0) {
        return;
    }
    models[row].progress = percent;
    emit dataChanged(index(row), index(row), {PROGRESS_ROLE});
}

QList<T3kModel> ToneModelsModel::pending() const {
    QList<T3kModel> out;
    for (const Row& r : models) {
        if (r.state == IDLE || r.state == FAILED) {
            out << r.model;
        }
    }
    return out;
}

QSize ToneModelDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const {
    return QSize(opt.rect.width(), MODEL_ROW_H);
}

void ToneModelDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                              const QModelIndex& idx) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const QRect r = opt.rect;
    const bool hovered = opt.state & QStyle::State_MouseOver;
    if (hovered) {
        p->fillRect(r, Theme::track());
    }

    const auto state =
        static_cast<ToneModelsModel::State>(idx.data(ToneModelsModel::STATE_ROLE).toInt());
    const int progress = idx.data(ToneModelsModel::PROGRESS_ROLE).toInt();

    // Action pill on the right.
    QString action;
    QColor pill_color = Theme::track_hover();
    QColor text_color = opt.palette.color(QPalette::Text);
    switch (state) {
        case ToneModelsModel::IDLE:
            action = "Download";
            break;
        case ToneModelsModel::DOWNLOADING:
            action = progress > 0 ? QString("%1 %").arg(progress) : QStringLiteral("…");
            break;
        case ToneModelsModel::DONE:
            action = "Load";
            pill_color = Theme::generic_accent();
            text_color = Theme::display_bg();
            break;
        case ToneModelsModel::FAILED:
            action = "Retry";
            pill_color = Theme::led_red();
            text_color = Theme::display_bg();
            break;
    }
    QFont pill_font = opt.font;
    pill_font.setWeight(QFont::DemiBold);
    const QFontMetrics pfm(pill_font);
    const int pill_w = pfm.horizontalAdvance(action) + 18;
    const QRect pill(r.right() - 8 - pill_w, r.top() + (r.height() - 22) / 2, pill_w, 22);
    p->setPen(Qt::NoPen);
    p->setBrush(pill_color);
    p->drawRoundedRect(pill, 11, 11);
    if (state == ToneModelsModel::DOWNLOADING && progress > 0) {
        p->setBrush(Theme::generic_accent());
        p->setClipRect(
            QRect(pill.left(), pill.top(), pill.width() * progress / 100, pill.height()));
        p->drawRoundedRect(pill, 11, 11);
        p->setClipping(false);
    }
    p->setFont(pill_font);
    p->setPen(text_color);
    p->drawText(pill, Qt::AlignCenter, action);

    const int left = r.left() + 10;
    const int width = pill.left() - 8 - left;
    p->setFont(opt.font);
    const QFontMetrics fm(opt.font);
    p->setPen(opt.palette.color(QPalette::Text));
    const QString name = idx.data(ToneModelsModel::NAME_ROLE).toString();
    const QString info = idx.data(ToneModelsModel::INFO_ROLE).toString();
    if (info.isEmpty()) {
        p->drawText(QRect(left, r.top(), width, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(name, Qt::ElideMiddle, width));
    } else {
        QFont sub = opt.font;
        sub.setPointSizeF(sub.pointSizeF() - 1);
        const QFontMetrics sfm(sub);
        const int total = fm.height() + sfm.height();
        const int top = r.top() + (r.height() - total) / 2;
        p->drawText(QRect(left, top, width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(name, Qt::ElideMiddle, width));
        p->setFont(sub);
        p->setPen(Theme::dim_text());
        p->drawText(QRect(left, top + fm.height(), width, sfm.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, sfm.elidedText(info, Qt::ElideRight, width));
    }
    p->restore();
}

// ===========================================================================
// Tone3000Panel
// ===========================================================================

Tone3000Panel::Tone3000Panel(Tone3000Client& new_client, LibraryIndex& new_index, QWidget* parent)
    : QWidget(parent)
    , client(new_client)
    , library_index(new_index) {
    setObjectName("tone3000Panel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    build_header();
    lay->addWidget(logo->parentWidget());
    notice = new QLabel;
    notice->setObjectName("t3kNotice");
    notice->setWordWrap(true);
    notice->hide();
    lay->addWidget(notice);

    pages = new QStackedWidget;
    lay->addWidget(pages, 1);
    build_setup_page();
    build_splash_page();
    build_browse_page();
    build_detail_page();

    connect(&client, &Tone3000Client::auth_changed, this, [this] {
        sync_state();
    });
    connect(&client, &Tone3000Client::user_changed, this, [this] {
        update_user_header();
    });
    connect(&client, &Tone3000Client::status, this, [this](const QString& m) {
        notice->setText(m);
        notice->show();
        QTimer::singleShot(6000, notice, &QWidget::hide);
    });
    connect(&client, &Tone3000Client::auth_finished, this,
            [this](bool ok, const QString& err, int tone_id) {
                browse_btn->setEnabled(true);
                if (!ok) {
                    notice->setText(err);
                    notice->show();
                    QTimer::singleShot(8000, notice, &QWidget::hide);
                    sync_state();
                    return;
                }
                notice->hide();
                sync_state();
                if (tone_id > 0) {
                    show_tone(tone_id);
                }
            });
    connect(&client, &Tone3000Client::download_progress, this,
            [this](int model_id, qint64 rec, qint64 tot) {
                if (tot > 0) {
                    models_model->set_progress(model_id, static_cast<int>(rec * 100 / tot));
                }
            });

    sync_state();
}

// --- Header ------------------------------------------------------------------

void Tone3000Panel::build_header() {
    auto* header = new QWidget;
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(2, 0, 0, 0);
    row->setSpacing(6);

    logo = new QLabel(header);
    logo->setObjectName("t3kLogo");
    logo->setPixmap(
        QPixmap(":/tone3000/tone3000-logo.png").scaledToHeight(20, Qt::SmoothTransformation));
    logo->setToolTip("TONE3000 — NAM captures and IRs");
    logo->setCursor(Qt::PointingHandCursor);
    row->addWidget(logo, 0, Qt::AlignVCenter);
    row->addStretch();

    user_btn = new QToolButton;
    user_btn->setObjectName("t3kUser");
    user_btn->setAutoRaise(true);
    user_btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    user_btn->setPopupMode(QToolButton::InstantPopup);
    user_btn->setFocusPolicy(Qt::NoFocus);
    auto* menu = new QMenu(user_btn);
    connect(menu->addAction("Open my TONE3000 profile"), &QAction::triggered, this, [this] {
        const QString u = client.get_user().url;
        QDesktopServices::openUrl(QUrl(u.isEmpty() ? QString(Tone3000Client::SITE_URL) : u));
    });
    connect(menu->addAction("Change download folder…"), &QAction::triggered, this, [this] {
        const QString dir = pick_download_dir(this);
        if (dir.isEmpty()) {
            return;
        }
        QSettings().setValue("library/tone3000Dir", dir);
        library_index.rescan();
    });
    connect(menu->addAction("TONE3000 publishable key…"), &QAction::triggered, this, [this] {
        bool ok = false;
        const QString key = QInputDialog::getText(
            window(), "TONE3000 key",
            "Publishable key (t3k_pub_…) from tone3000.com → Settings → API Keys.\n"
            "Leave empty to use the key this build was compiled with.",
            QLineEdit::Normal, QSettings().value("tone3000/clientId").toString(), &ok);
        if (!ok) {
            return;
        }
        Tone3000Client::set_client_id(key);
        client.sign_out();
        sync_state();
    });
    menu->addSeparator();
    connect(menu->addAction("Sign out"), &QAction::triggered, this, [this] {
        client.sign_out();
    });
    user_btn->setMenu(menu);
    row->addWidget(user_btn, 0, Qt::AlignVCenter);

    logo->installEventFilter(this);
    update_user_header();
}

QString Tone3000Panel::pick_download_dir(QWidget* parent) {
    return QFileDialog::getExistingDirectory(parent, "TONE3000 download folder",
                                             Tone3000Client::download_dir());
}

bool Tone3000Panel::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == logo && ev->type() == QEvent::MouseButtonRelease) {
        QDesktopServices::openUrl(QUrl(Tone3000Client::SITE_URL));
        return true;
    }
    if (tone_view && obj == tone_view->viewport() && ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton && handle_tone_view_click(me)) {
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void Tone3000Panel::update_user_header() {
    const T3kUser& u = client.get_user();
    const bool in = client.is_signed_in();
    user_btn->setVisible(in);
    if (!in) {
        return;
    }
    user_btn->setText(u.username.isEmpty() ? QStringLiteral("account") : "@" + u.username);
    user_btn->setToolTip(u.label());
    user_btn->setIcon(QIcon());
    if (!u.avatar_url.isEmpty()) {
        const QString url = u.avatar_url;
        client.fetch_image(url, QSize(64, 64), [this, url](QPixmap px) {
            if (px.isNull() || client.get_user().avatar_url != url) {
                return;
            }
            user_btn->setIcon(QIcon(rounded_thumb(px, 18, true)));
        });
    }
}

// --- Pages -------------------------------------------------------------------

void Tone3000Panel::build_setup_page() {
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
    key_edit = new QLineEdit;
    key_edit->setObjectName("librarySearch");
    key_edit->setPlaceholderText("t3k_pub_…");
    lay->addWidget(key_edit);
    auto* save = new QPushButton("Save key");
    connect(save, &QPushButton::clicked, this, [this] {
        Tone3000Client::set_client_id(key_edit->text());
        sync_state();
    });
    connect(key_edit, &QLineEdit::returnPressed, save, &QPushButton::click);
    lay->addWidget(save, 0, Qt::AlignLeft);
    lay->addStretch();
    pages->insertWidget(PAGE_SETUP, page);
}

void Tone3000Panel::build_splash_page() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(8, 18, 8, 8);
    lay->setSpacing(14);

    auto* splash_logo = new QLabel;
    splash_logo->setPixmap(
        QPixmap(":/tone3000/tone3000-logo.png").scaledToWidth(200, Qt::SmoothTransformation));
    splash_logo->setAlignment(Qt::AlignHCenter);
    lay->addWidget(splash_logo);

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
    connect(cont, &QPushButton::clicked, this, [this] {
        browse_catalog();
    });
    lay->addWidget(cont, 0, Qt::AlignHCenter);

    auto* fine =
        new QLabel("Sign-in happens in your browser: TONE3000 sends a code to your e-mail. "
                   "Downloads go to the library's TONE3000 folder.");
    fine->setObjectName("t3kFinePrint");
    fine->setWordWrap(true);
    fine->setAlignment(Qt::AlignHCenter);
    lay->addWidget(fine);
    lay->addStretch();
    pages->insertWidget(PAGE_SPLASH, page);
}

void Tone3000Panel::build_browse_page() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    // List switch.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(0);
        for (int i = 0; i < 5; ++i) {
            auto* b = new QToolButton;
            b->setObjectName("librarySection");
            b->setText(k_list_names[i]);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [this, i] {
                select_list(i);
            });
            row->addWidget(b);
            list_btn[i] = b;
        }
        lay->addLayout(row);
    }

    // The persistent path to the whole catalog: the select flow in the browser.
    browse_btn = new QPushButton("Browse TONE3000…");
    browse_btn->setObjectName("t3kBrowse");
    browse_btn->setIcon(
        QIcon(QPixmap(":/tone3000/t3k-mark.png").scaledToHeight(14, Qt::SmoothTransformation)));
    browse_btn->setToolTip(
        "Search the full TONE3000 catalog in your browser and pick a tone to bring back here");
    browse_btn->setFocusPolicy(Qt::NoFocus);
    connect(browse_btn, &QPushButton::clicked, this, [this] {
        browse_catalog();
    });
    lay->addWidget(browse_btn);

    tone_model = new ToneListModel(client, library_index, this);
    tone_view = new QListView;
    tone_view->setObjectName("libraryList");
    tone_view->setModel(tone_model);
    tone_view->setItemDelegate(new ToneDelegate(tone_view));
    tone_view->setUniformItemSizes(true);
    tone_view->setSelectionMode(QAbstractItemView::NoSelection);
    tone_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tone_view->setMouseTracking(true);
    tone_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tone_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tone_view->setFrameShape(QFrame::NoFrame);
    tone_view->viewport()->installEventFilter(this);
    lay->addWidget(tone_view, 1);

    // Footer: status + paging + refresh.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        list_status = new QLabel;
        list_status->setObjectName("libraryStatus");
        list_status->setWordWrap(true);
        row->addWidget(list_status, 1);
        prev_page = new QToolButton;
        prev_page->setObjectName("libraryTool");
        prev_page->setText("‹");
        prev_page->setAutoRaise(true);
        prev_page->setFocusPolicy(Qt::NoFocus);
        connect(prev_page, &QToolButton::clicked, this, [this] {
            if (list_page > 1) {
                --list_page;
                load_list(true);
            }
        });
        next_page = new QToolButton;
        next_page->setObjectName("libraryTool");
        next_page->setText("›");
        next_page->setAutoRaise(true);
        next_page->setFocusPolicy(Qt::NoFocus);
        connect(next_page, &QToolButton::clicked, this, [this] {
            if (list_page < total_pages) {
                ++list_page;
                load_list(true);
            }
        });
        auto* refresh = new QToolButton;
        refresh->setObjectName("libraryTool");
        refresh->setText("↻");
        refresh->setAutoRaise(true);
        refresh->setFocusPolicy(Qt::NoFocus);
        refresh->setToolTip("Reload this list from TONE3000");
        connect(refresh, &QToolButton::clicked, this, [this] {
            load_list(false);
        });
        row->addWidget(prev_page);
        row->addWidget(next_page);
        row->addWidget(refresh);
        lay->addLayout(row);
    }

    pages->insertWidget(PAGE_BROWSE, page);
}

void Tone3000Panel::build_detail_page() {
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
        connect(back, &QToolButton::clicked, this, [this] {
            pages->setCurrentIndex(PAGE_BROWSE);
        });
        row->addWidget(back);
        row->addStretch();
        detail_fav = new QToolButton;
        detail_fav->setObjectName("libraryMode");
        detail_fav->setCheckable(true);
        detail_fav->setAutoRaise(true);
        detail_fav->setFocusPolicy(Qt::NoFocus);
        detail_fav->setToolTip("Favorite on TONE3000");
        connect(detail_fav, &QToolButton::clicked, this, [this](bool on) {
            toggle_favorite(detail_tone.id, on);
        });
        row->addWidget(detail_fav);
        auto* open = new QToolButton;
        open->setObjectName("libraryTool");
        open->setText("Open on TONE3000 ↗");
        open->setAutoRaise(true);
        open->setFocusPolicy(Qt::NoFocus);
        connect(open, &QToolButton::clicked, this, [this] {
            if (!detail_tone.url.isEmpty()) {
                QDesktopServices::openUrl(QUrl(detail_tone.url));
            }
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

    detail_image = new QLabel;
    detail_image->setObjectName("t3kDetailImage");
    detail_image->setAlignment(Qt::AlignCenter);
    detail_image->setMinimumHeight(120);
    lay->addWidget(detail_image);

    detail_title = new QLabel;
    detail_title->setObjectName("t3kDetailTitle");
    detail_title->setWordWrap(true);
    lay->addWidget(detail_title);

    detail_meta = new QLabel;
    detail_meta->setObjectName("t3kDetailMeta");
    detail_meta->setWordWrap(true);
    lay->addWidget(detail_meta);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        detail_creator_avatar = new QLabel;
        detail_creator_avatar->setFixedSize(20, 20);
        row->addWidget(detail_creator_avatar);
        detail_creator = new QLabel;
        detail_creator->setObjectName("t3kDetailMeta");
        detail_creator->setOpenExternalLinks(true);
        detail_creator->setTextInteractionFlags(Qt::TextBrowserInteraction);
        row->addWidget(detail_creator, 1);
        lay->addLayout(row);
    }

    detail_desc = new QLabel;
    detail_desc->setObjectName("t3kDetailDesc");
    detail_desc->setWordWrap(true);
    detail_desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(detail_desc);

    auto* models_header = new QLabel("Models");
    models_header->setObjectName("t3kSection");
    lay->addWidget(models_header);

    detail_status = new QLabel;
    detail_status->setObjectName("libraryStatus");
    detail_status->setWordWrap(true);
    lay->addWidget(detail_status);

    models_model = new ToneModelsModel(this);
    models_view = new QListView;
    models_view->setObjectName("libraryList");
    models_view->setModel(models_model);
    models_view->setItemDelegate(new ToneModelDelegate(models_view));
    models_view->setUniformItemSizes(true);
    models_view->setSelectionMode(QAbstractItemView::NoSelection);
    models_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    models_view->setMouseTracking(true);
    models_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    models_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    models_view->setFrameShape(QFrame::NoFrame);
    models_view->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    connect(models_view, &QListView::clicked, this, &Tone3000Panel::on_model_row_clicked);
    connect(models_model, &QAbstractItemModel::modelReset, this, [this] {
        models_view->setFixedHeight(std::max(1, models_model->rowCount()) * MODEL_ROW_H + 4);
    });
    lay->addWidget(models_view);

    download_all_button = new QPushButton("Download all");
    download_all_button->setFocusPolicy(Qt::NoFocus);
    connect(download_all_button, &QPushButton::clicked, this, &Tone3000Panel::download_all);
    lay->addWidget(download_all_button, 0, Qt::AlignLeft);

    auto* license = new QLabel;
    license->setObjectName("t3kFinePrint");
    license->setWordWrap(true);
    license->setText("Models are shared by their creators under the license shown above. "
                     "Please respect it, and credit them when you share presets.");
    lay->addWidget(license);
    lay->addStretch();

    scroll->setWidget(body);
    outer->addWidget(scroll, 1);
    pages->insertWidget(PAGE_DETAIL, page);
}

// --- State -------------------------------------------------------------------

void Tone3000Panel::sync_state() {
    update_user_header();
    if (!Tone3000Client::has_client_id()) {
        logo->parentWidget()->show();
        key_edit->setText(QSettings().value("tone3000/clientId").toString());
        pages->setCurrentIndex(PAGE_SETUP);
        return;
    }
    if (!client.is_signed_in()) {
        logo->parentWidget()->hide(); // the splash carries the big logo
        pages->setCurrentIndex(PAGE_SPLASH);
        return;
    }
    logo->parentWidget()->show();
    if (pages->currentIndex() != PAGE_DETAIL) {
        pages->setCurrentIndex(PAGE_BROWSE);
        select_list(std::clamp(QSettings().value("tone3000/list", 0).toInt(), 0, 4), true);
    }
}

void Tone3000Panel::browse_catalog() {
    browse_btn->setEnabled(false);
    notice->setText(
        "Waiting for the browser… pick a tone on TONE3000 and you'll be brought back here.");
    notice->show();
    client.start_auth(Tone3000Client::Flow::SELECT_TONE);
}

void Tone3000Panel::select_list(int which, bool force) {
    if (which == list && !force && tone_model->rowCount() > 0) {
        return;
    }
    if (which != list) {
        list_page = 1;
    }
    list = which;
    for (int i = 0; i < 5; ++i) {
        list_btn[i]->setChecked(i == which);
    }
    QSettings().setValue("tone3000/list", which);
    load_list(true);
}

void Tone3000Panel::show_list_status(const QString& text) {
    list_status->setText(text);
}

void Tone3000Panel::load_list(bool use_cache) {
    const Tone3000Client::ListKind kind = LIST_KINDS[list];
    const bool paged = kind == Tone3000Client::ListKind::FAVORITED ||
                       kind == Tone3000Client::ListKind::DOWNLOADED ||
                       kind == Tone3000Client::ListKind::CREATED;
    show_list_status("Loading…");
    prev_page->setVisible(paged);
    next_page->setVisible(paged);
    const int my_list = list, my_page = list_page;
    client.list_tones(
        kind, list_page,
        [this, my_list, my_page, paged](const Tone3000Client::Result& r) {
            if (my_list != list || my_page != list_page) {
                return; // switched meanwhile
            }
            if (!r.ok) {
                if (r.offline) {
                    show_list_status(
                        "Offline. The local library still works; TONE3000 will be back "
                        "when you are.");
                } else if (r.auth_required) {
                    show_list_status(r.error);
                    sync_state();
                } else {
                    show_list_status(r.error);
                }
                return;
            }
            QList<T3kTone> tones;
            for (const QJsonValue v : r.object().value("data").toArray()) {
                tones << Tone3000Client::parse_tone(v.toObject());
            }
            tone_model->set_tones(tones);
            tone_view->scrollToTop();
            total_pages = paged ? std::max(1, r.object().value("total_pages").toInt(1)) : 1;
            prev_page->setEnabled(list_page > 1);
            next_page->setEnabled(list_page < total_pages);
            if (tones.isEmpty()) {
                show_list_status(my_list == 2   ? "No favorites yet. ★ a tone here or on TONE3000."
                                 : my_list == 3 ? "Nothing downloaded on TONE3000 yet."
                                 : my_list == 4 ? "You haven't published tones on TONE3000."
                                                : "Nothing here right now.");
            } else if (paged) {
                show_list_status(QString("%1 tones · page %2 of %3")
                                     .arg(r.object().value("total").toInt(
                                         static_cast<int>(tones.size()))) // JSON ints are int
                                     .arg(list_page)
                                     .arg(total_pages));
            } else {
                show_list_status(QString("%1 tones").arg(tones.size()));
            }
        },
        use_cache);
}

bool Tone3000Panel::handle_tone_view_click(QMouseEvent* me) {
    const QModelIndex idx = tone_view->indexAt(me->pos());
    if (!idx.isValid()) {
        return false;
    }
    const T3kTone* t = tone_model->tone(idx.row());
    if (!t) {
        return false;
    }
    if (ToneDelegate::star_rect(tone_view->visualRect(idx)).contains(me->pos())) {
        toggle_favorite(t->id, !t->is_favorite);
        return true;
    }
    open_detail(*t);
    return true;
}

void Tone3000Panel::toggle_favorite(int tone_id, bool on) {
    tone_model->set_favorite(tone_id, on);
    if (detail_tone.id == tone_id) {
        detail_tone.is_favorite = on;
        detail_fav->setChecked(on);
        detail_fav->setText(on ? "★" : "☆");
    }
    client.set_favorite(tone_id, on, [this, tone_id, on](const Tone3000Client::Result& r) {
        if (r.ok) {
            return;
        }
        tone_model->set_favorite(tone_id, !on); // revert
        if (detail_tone.id == tone_id) {
            detail_tone.is_favorite = !on;
            detail_fav->setChecked(!on);
            detail_fav->setText(!on ? "★" : "☆");
        }
        notice->setText(r.offline ? QStringLiteral("Offline — favorite not saved.") : r.error);
        notice->show();
        QTimer::singleShot(6000, notice, &QWidget::hide);
    });
}

// --- Detail ------------------------------------------------------------------

void Tone3000Panel::show_tone(int tone_id) {
    pages->setCurrentIndex(PAGE_DETAIL);
    detail_title->setText("Loading…");
    detail_meta->clear();
    detail_creator->clear();
    detail_creator_avatar->clear();
    detail_desc->clear();
    detail_image->clear();
    detail_status->clear();
    models_model->set_models({}, {}, library_index);
    const int req = ++detail_request;
    client.fetch_tone(tone_id, [this, req](const Tone3000Client::Result& r) {
        if (req != detail_request) {
            return;
        }
        if (!r.ok) {
            detail_title->setText(r.offline ? QStringLiteral("Offline")
                                            : QStringLiteral("Could not load this tone"));
            detail_status->setText(r.error);
            return;
        }
        open_detail(Tone3000Client::parse_tone(r.object()));
    });
}

void Tone3000Panel::open_detail(const T3kTone& tone) {
    detail_tone = tone;
    pages->setCurrentIndex(PAGE_DETAIL);

    detail_title->setText(tone.title);
    QStringList meta{tone.gear_label(), tone.format_label(), tone.license_label()};
    if (!tone.makes.isEmpty()) {
        meta << tone.makes.join(", ");
    }
    meta << QString("%1 downloads · %2 favorites")
                .arg(count_label(tone.downloads), count_label(tone.favorites));
    detail_meta->setText(meta.join(" · "));
    detail_creator->setText(
        QString("<a href=\"%1\">@%2</a>%3")
            .arg(tone.user.url.isEmpty() ? Tone3000Client::SITE_URL : tone.user.url,
                 tone.user.username.toHtmlEscaped(),
                 tone.user.display_name.isEmpty() || tone.user.display_name == tone.user.username
                     ? QString()
                     : " · " + tone.user.display_name.toHtmlEscaped()));
    detail_desc->setText(tone.description.trimmed());
    detail_desc->setVisible(!tone.description.trimmed().isEmpty());
    detail_fav->setChecked(tone.is_favorite);
    detail_fav->setText(tone.is_favorite ? "★" : "☆");
    detail_status->setText("Loading models…");
    download_all_button->setEnabled(false);

    set_thumbnail(detail_image, tone.image_url(), QSize(600, 400), false);
    set_thumbnail(detail_creator_avatar, tone.user.avatar_url, QSize(20, 20), true);

    // Fresh tone (description, counts) if we came from a list card.
    if (tone.description.isEmpty()) {
        const int req = ++detail_request;
        client.fetch_tone(tone.id, [this, req](const Tone3000Client::Result& r) {
            if (req != detail_request || !r.ok) {
                return;
            }
            const T3kTone full = Tone3000Client::parse_tone(r.object());
            if (full.id != detail_tone.id) {
                return;
            }
            detail_tone.description = full.description;
            detail_desc->setText(full.description.trimmed());
            detail_desc->setVisible(!full.description.trimmed().isEmpty());
        });
    }
    load_detail_models();
}

void Tone3000Panel::set_thumbnail(QLabel* label, const QString& url, const QSize& size,
                                  bool round) {
    label->clear();
    if (url.isEmpty()) {
        label->setVisible(!round ? false : true);
        return;
    }
    label->setVisible(true);
    client.fetch_image(url, size, [this, label, url, size, round](QPixmap px) {
        if (px.isNull()) {
            return;
        }
        if (label == detail_image && detail_tone.image_url() != url) {
            return;
        }
        if (label == detail_creator_avatar && detail_tone.user.avatar_url != url) {
            return;
        }
        if (round) {
            label->setPixmap(rounded_thumb(px, size.height(), true));
            return;
        }
        // Fit the panel width, rounded corners.
        const int w = std::max(120, label->width() - 4);
        QPixmap scaled = px.scaledToWidth(std::min(w, px.width() * 2), Qt::SmoothTransformation);
        if (scaled.height() > 220) {
            scaled = scaled.scaledToHeight(220, Qt::SmoothTransformation);
        }
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

void Tone3000Panel::load_detail_models() {
    const int req = ++detail_request;
    const T3kTone tone = detail_tone;
    client.list_models(
        tone, [this, req, tone](const Tone3000Client::Result& r, QList<T3kModel> models) {
            if (req != detail_request) {
                return;
            }
            if (!r.ok) {
                detail_status->setText(
                    r.offline ? QStringLiteral("Offline — models can't be listed right now.")
                              : r.error);
                return;
            }
            models_model->set_models(models, tone, library_index);
            const qsizetype pending = models_model->pending().size();
            detail_status->setText(
                models.isEmpty() ? QStringLiteral("This tone has no downloadable models.")
                : pending == 0   ? QStringLiteral("All models are in your library.")
                                 : QString("%1 models · click one to download").arg(models.size()));
            download_all_button->setEnabled(pending > 0);
            download_all_button->setText(pending > 0 && pending < models.size()
                                             ? QString("Download the other %1").arg(pending)
                                             : QStringLiteral("Download all"));
        });
}

void Tone3000Panel::on_model_row_clicked(const QModelIndex& idx) {
    const auto state =
        static_cast<ToneModelsModel::State>(idx.data(ToneModelsModel::STATE_ROLE).toInt());
    if (state == ToneModelsModel::DONE) {
        const QString path = idx.data(ToneModelsModel::LOCAL_PATH_ROLE).toString();
        bool ok = false;
        const LibraryEntry::Kind kind = LibraryIndex::kind_for_path(path, &ok);
        if (ok) {
            emit load_requested(kind, path);
        }
        return;
    }
    if (state == ToneModelsModel::DOWNLOADING) {
        return;
    }
    start_download(idx.row());
}

void Tone3000Panel::start_download(int row) {
    const T3kModel* m = models_model->model(row);
    if (!m) {
        return;
    }
    const T3kModel model = *m;
    const T3kTone tone = detail_tone;
    models_model->set_state(model.id, ToneModelsModel::DOWNLOADING);
    client.download_model(model, tone, [this, model](bool ok, QString path, QString err) {
        models_model->set_state(model.id, ok ? ToneModelsModel::DONE : ToneModelsModel::FAILED,
                                ok ? path : err);
        if (ok) {
            library_index.rescan(); // a new folder isn't watched until scanned once
            const qsizetype pending = models_model->pending().size();
            download_all_button->setEnabled(pending > 0);
            detail_status->setText(pending == 0 ? QStringLiteral("All models are in your library.")
                                                : QString("%1 left to download").arg(pending));
        } else {
            detail_status->setText(err);
        }
    });
}

void Tone3000Panel::download_all() {
    for (int i = 0; i < models_model->rowCount(); ++i) {
        const auto state = static_cast<ToneModelsModel::State>(
            models_model->index(i).data(ToneModelsModel::STATE_ROLE).toInt());
        if (state == ToneModelsModel::IDLE || state == ToneModelsModel::FAILED) {
            start_download(i);
        }
    }
    download_all_button->setEnabled(false);
}

} // namespace npl

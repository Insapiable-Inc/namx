#include "TopBar.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

#include "Metronome.h"
#include "NumberField.h"
#include "TempoBar.h"
#include "TempoSync.h"

namespace namx
{

namespace
{

QString kind_label(TopBar::SearchEntry::Kind kind) {
    switch (kind) {
        case TopBar::SearchEntry::Kind::NODE:
            return "Effect";
        case TopBar::SearchEntry::Kind::PRESET:
            return "Preset";
        case TopBar::SearchEntry::Kind::MODEL:
            return "Amp";
        case TopBar::SearchEntry::Kind::IR:
            return "Cab";
    }
    return {};
}

QFrame* separator() {
    auto* line = new QFrame;
    line->setObjectName("topBarLine");
    line->setFixedWidth(1);
    return line;
}

} // namespace

QToolButton* TopBar::tool(const QString& text, const QString& tip) {
    auto* b = new QToolButton;
    b->setObjectName("topBarTool");
    b->setText(text);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

TopBar::TopBar(Metronome& metronome, QWidget* parent)
    : QFrame(parent)
    , metro(metronome) {
    setObjectName("topBar");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(18, 8, 12, 8);
    lay->setSpacing(10);

    auto* logo = new QLabel("NAMX");
    logo->setObjectName("logo");
    lay->addWidget(logo);
    lay->addSpacing(6);
    lay->addWidget(separator());
    lay->addSpacing(4);

    preset_button = new QToolButton;
    preset_button->setObjectName("presetTitle");
    preset_button->setPopupMode(QToolButton::InstantPopup);
    preset_button->setToolTip("Presets: open, save, step through");
    presets = new QMenu(preset_button);
    connect(presets, &QMenu::aboutToShow, this, &TopBar::preset_menu_about_to_show);
    preset_button->setMenu(presets);
    lay->addWidget(preset_button);
    QToolButton* rename = tool("✎", "Rename the preset (Save As when it has no file yet)");
    connect(rename, &QToolButton::clicked, this, &TopBar::rename_requested);
    lay->addWidget(rename);
    lay->addStretch(1);

    // Metronome: play, tempo, and the full tempo bar in a popup.
    tempo_popup = new QFrame(this, Qt::Popup);
    tempo_popup->setObjectName("tempoPopup");
    auto* popup_lay = new QVBoxLayout(tempo_popup);
    popup_lay->setContentsMargins(6, 6, 6, 6);
    tempo_bar = new TempoBar(metro, tempo_popup);
    popup_lay->addWidget(tempo_bar);

    auto* metro_box = new QFrame;
    metro_box->setObjectName("metroBox");
    auto* mlay = new QHBoxLayout(metro_box);
    mlay->setContentsMargins(4, 2, 4, 2);
    mlay->setSpacing(4);
    metro_button = tool("♩", "Metronome on / off (Space)");
    metro_button->setObjectName("metroToggle");
    metro_button->setCheckable(true);
    connect(metro_button, &QToolButton::clicked, this, [this] {
        tempo_bar->toggle_play();
        metro_button->setChecked(metro.is_running());
    });
    mlay->addWidget(metro_button);
    bpm = new NumberField({.minimum = TempoSync::MIN_BPM,
                           .maximum = TempoSync::MAX_BPM,
                           .decimals = 1,
                           .step = 1.0,
                           .unit = "BPM",
                           .reset_value = TempoSync::DEFAULT_BPM});
    bpm->setToolTip("Tempo: drag up/down or scroll, click to type");
    bpm->set_value(tempo_bar->get_bpm());
    connect(bpm, &NumberField::value_changed, tempo_bar, &TempoBar::set_bpm);
    connect(tempo_bar, &TempoBar::bpm_changed, this, [this](double v) {
        const QSignalBlocker block(bpm);
        bpm->set_value(v);
    });
    mlay->addWidget(bpm);
    QToolButton* more = tool("▾", "Tap tempo, time signature, click sound, level…");
    connect(more, &QToolButton::clicked, this, [this, metro_box] {
        tempo_popup->adjustSize();
        const QPoint at = metro_box->mapToGlobal(QPoint(metro_box->width(), metro_box->height()));
        tempo_popup->move(at.x() - tempo_popup->width(), at.y() + 4);
        tempo_popup->show();
    });
    mlay->addWidget(more);
    lay->addWidget(metro_box);

    // Search.
    search = new QLineEdit;
    search->setObjectName("topSearch");
    search->setPlaceholderText("Search presets, amps, cabs, effects…");
    search->setClearButtonEnabled(true);
    search->setMinimumWidth(300);
    search_model = new QStandardItemModel(this);
    completer = new QCompleter(search_model, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionRole(Qt::DisplayRole);
    completer->setMaxVisibleItems(14);
    search->setCompleter(completer);
    connect(completer, qOverload<const QModelIndex&>(&QCompleter::activated), this,
            [this](const QModelIndex& index) {
                const int row = index.data(Qt::UserRole).toInt();
                if (row >= 0 && row < search_entries.size()) {
                    emit search_picked(search_entries[row]);
                }
                QMetaObject::invokeMethod(search, &QLineEdit::clear, Qt::QueuedConnection);
            });
    lay->addWidget(search);

    star = tool("☆", "Favourite preset");
    star->setCheckable(true);
    connect(star, &QToolButton::toggled, this, [this](bool on) {
        star->setText(on ? "★" : "☆");
    });
    connect(star, &QToolButton::clicked, this, &TopBar::favorite_toggled);
    lay->addWidget(star);

    QToolButton* menu_button = tool("⋮", "Menu");
    menu_button->setPopupMode(QToolButton::InstantPopup);
    main = new QMenu(menu_button);
    menu_button->setMenu(main);
    lay->addWidget(menu_button);

    QToolButton* settings = tool("⚙", "Settings (Ctrl+,)");
    connect(settings, &QToolButton::clicked, this, &TopBar::settings_requested);
    lay->addWidget(settings);
}

void TopBar::set_preset(const QString& name, bool modified, bool untitled) {
    preset_button->setText(name + (modified ? "  *" : ""));
    preset_button->setProperty("untitled", untitled);
    preset_button->style()->unpolish(preset_button);
    preset_button->style()->polish(preset_button);
}

void TopBar::set_favorite(bool on) {
    const QSignalBlocker block(star);
    star->setChecked(on);
    star->setText(on ? "★" : "☆");
}

void TopBar::set_search_entries(const QList<SearchEntry>& entries) {
    search_entries = entries;
    search_model->clear();
    for (int i = 0; i < entries.size(); ++i) {
        auto* item = new QStandardItem(
            QString("%1  ·  %2").arg(entries[i].text, kind_label(entries[i].kind)));
        item->setData(i, Qt::UserRole);
        search_model->appendRow(item);
    }
}

void TopBar::poll() {
    tempo_bar->poll();
    if (metro_button->isChecked() != metro.is_running()) {
        metro_button->setChecked(metro.is_running());
    }
}

void TopBar::focus_search() {
    search->setFocus();
    search->selectAll();
}

} // namespace namx

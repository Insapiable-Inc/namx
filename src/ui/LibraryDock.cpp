#include "LibraryDock.h"

#include <QHBoxLayout>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "LibraryPanel.h"
#include "Tone3000Panel.h"

LibraryDock::LibraryDock(LibraryIndex& index, Tone3000Client& client, QWidget* parent)
    : QWidget(parent) {
    setObjectName("libraryDockContent");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* tabs = new QWidget;
    tabs->setObjectName("libraryTabs");
    auto* row = new QHBoxLayout(tabs);
    row->setContentsMargins(6, 4, 6, 0);
    row->setSpacing(2);
    const char* names[2] = {"Library", "TONE3000"};
    for (int i = 0; i < 2; ++i) {
        auto* b = new QToolButton;
        b->setObjectName("libraryTab");
        b->setText(names[i]);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, [this, i] {
            set_tab(static_cast<Tab>(i));
        });
        row->addWidget(b);
        m_tab_btn[i] = b;
    }
    lay->addWidget(tabs);

    m_stack = new QStackedWidget;
    m_local = new LibraryPanel(index);
    m_t3k = new Tone3000Panel(client, index);
    m_stack->addWidget(m_local);
    m_stack->addWidget(m_t3k);
    lay->addWidget(m_stack, 1);

    set_tab(QSettings().value("library/tab", 0).toInt() == 1 ? TONE3000 : LOCAL);
}

int LibraryDock::current_index() const {
    return m_stack->currentIndex();
}

void LibraryDock::set_tab(Tab tab) {
    m_stack->setCurrentIndex(static_cast<int>(tab));
    for (int i = 0; i < 2; ++i) {
        m_tab_btn[i]->setChecked(i == static_cast<int>(tab));
    }
    QSettings().setValue("library/tab", static_cast<int>(tab));
}

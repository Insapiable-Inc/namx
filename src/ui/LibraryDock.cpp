#include "LibraryDock.h"
#include "LibraryPanel.h"
#include "Tone3000Panel.h"

#include <QHBoxLayout>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

LibraryDock::LibraryDock(LibraryIndex& index, Tone3000Client& client, QWidget* parent)
    : QWidget(parent)
{
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
    for (int i = 0; i < 2; ++i)
    {
        auto* b = new QToolButton;
        b->setObjectName("libraryTab");
        b->setText(names[i]);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, [this, i] { setTab(static_cast<Tab>(i)); });
        row->addWidget(b);
        mTabBtn[i] = b;
    }
    lay->addWidget(tabs);

    mStack = new QStackedWidget;
    mLocal = new LibraryPanel(index);
    mT3k   = new Tone3000Panel(client, index);
    mStack->addWidget(mLocal);
    mStack->addWidget(mT3k);
    lay->addWidget(mStack, 1);

    setTab(QSettings().value("library/tab", 0).toInt() == 1 ? Tone3000 : Local);
}

int LibraryDock::currentIndex() const { return mStack->currentIndex(); }

void LibraryDock::setTab(Tab tab)
{
    mStack->setCurrentIndex(static_cast<int>(tab));
    for (int i = 0; i < 2; ++i) mTabBtn[i]->setChecked(i == static_cast<int>(tab));
    QSettings().setValue("library/tab", static_cast<int>(tab));
}

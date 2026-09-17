#pragma once

#include <QWidget>

class LibraryIndex;
class LibraryPanel;
class QStackedWidget;
class QToolButton;
class Tone3000Client;
class Tone3000Panel;

/// Content of the Library dock: a two-way switch between the local library
/// and the TONE3000 community tab, each a full panel with its own state.
class LibraryDock : public QWidget
{
    Q_OBJECT
public:
    enum Tab { Local = 0, Tone3000 = 1 };

    LibraryDock(LibraryIndex& index, Tone3000Client& client, QWidget* parent = nullptr);

    LibraryPanel*  local()    const { return mLocal; }
    Tone3000Panel* tone3000() const { return mT3k; }

    void setTab(Tab tab);
    Tab  tab() const { return static_cast<Tab>(mStack ? currentIndex() : 0); }

private:
    int currentIndex() const;

    QToolButton*    mTabBtn[2]{};
    QStackedWidget* mStack = nullptr;
    LibraryPanel*   mLocal = nullptr;
    Tone3000Panel*  mT3k   = nullptr;
};

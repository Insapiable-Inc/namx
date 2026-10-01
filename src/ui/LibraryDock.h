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
    enum Tab
    {
        LOCAL = 0,
        TONE3000 = 1
    };

    LibraryDock(LibraryIndex& index, Tone3000Client& client, QWidget* parent = nullptr);

    LibraryPanel* local() const {
        return m_local;
    }
    Tone3000Panel* tone3000() const {
        return m_t3k;
    }

    void set_tab(Tab tab);
    Tab tab() const {
        return static_cast<Tab>(m_stack ? current_index() : 0);
    }

private:
    int current_index() const;

    QToolButton* m_tab_btn[2]{};
    QStackedWidget* m_stack = nullptr;
    LibraryPanel* m_local = nullptr;
    Tone3000Panel* m_t3k = nullptr;
};

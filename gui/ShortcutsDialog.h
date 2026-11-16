// ShortcutsDialog.h - the Keyboard Shortcuts window (F1).
//
// Cards per area, each action with its keys drawn as key caps, and a search box that filters
// by action or key ("comment", "ctrl k", "f2"). The table is data (entries()), so the window,
// the welcome page and the tests all read the same list.
#pragma once
#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <vector>

class QLabel;
class QLineEdit;
class QFrame;
class QScrollArea;

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    struct Entry {
        QString area;            // card title
        QString action;
        QStringList keys;        // alternatives, "Ctrl+Shift+K"; caps are split at '+'
        bool chars = false;      // keys are single characters typed in the editor ( [ { " ' ` - shown side by side, not as alternatives
    };
    static const QList<Entry>& entries();
    static QStringList areas();

    explicit ShortcutsDialog(QWidget* parent = nullptr);

    void setFilter(const QString& text);        // what the search box does
    int visibleEntryCount() const;
    int visibleCardCount() const;

private:
    struct Item {
        QWidget* widget;
        QString area;
        QString haystack;                       // action + keys, lower case, '+' replaced by blanks
    };
    struct Card {
        QFrame* frame;
        QString area;
    };
    QLineEdit* search_ = nullptr;
    QLabel* empty_ = nullptr;
    std::vector<Item> items_;
    std::vector<Card> cards_;
    void buildUi();
    QWidget* makeRow(const Entry& e, QWidget* parent);
};

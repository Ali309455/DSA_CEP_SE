#include "ShortcutsDialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include "Icons.h"
#include "Theme.h"

const QList<ShortcutsDialog::Entry>& ShortcutsDialog::entries() {
    static const QList<Entry> list = {
        {"Tabs", "Next tab", {"Ctrl+Tab", "Ctrl+PgDown"}},
        {"Tabs", "Previous tab", {"Ctrl+Shift+Tab", "Ctrl+PgUp"}},
        {"Tabs", "Go to tab number  (9 = last tab)", {"Alt+1-9"}},
        {"Tabs", "New file", {"Ctrl+N"}},
        {"Tabs", "Close tab", {"Ctrl+W"}},

        {"Files and windows", "Go to file (fuzzy search)", {"Ctrl+P"}},
        {"Files and windows", "Open file", {"Ctrl+O"}},
        {"Files and windows", "Open folder", {"Ctrl+Shift+O"}},
        {"Files and windows", "Save", {"Ctrl+S"}},
        {"Files and windows", "Save as", {"Ctrl+Shift+S"}},
        {"Files and windows", "Save all", {"Ctrl+Alt+S"}},
        {"Files and windows", "Rename file", {"F2"}},
        {"Files and windows", "Show / hide sidebar", {"Ctrl+B"}},
        {"Files and windows", "Show / hide terminal", {"Ctrl+`"}},
        {"Files and windows", "Show / hide Problems", {"Ctrl+Shift+M"}},
        {"Files and windows", "Settings", {"Ctrl+,"}},
        {"Files and windows", "Keyboard shortcuts", {"F1"}},
        {"Files and windows", "Quit", {"Ctrl+Q"}},

        {"Run", "Save, build and run the current file in the terminal", {"F5"}},
        {"Run", "Stop the running program", {"Shift+F5"}},
        {"Run", "Check the file for errors (compiler)", {"F7"}},

        {"Lines", "Move line(s) up", {"Alt+Up"}},
        {"Lines", "Move line(s) down", {"Alt+Down"}},
        {"Lines", "Copy line(s) up", {"Shift+Alt+Up"}},
        {"Lines", "Copy line(s) down", {"Shift+Alt+Down", "Ctrl+D"}},
        {"Lines", "Delete line", {"Ctrl+Shift+K"}},
        {"Lines", "New line below", {"Ctrl+Enter"}},
        {"Lines", "New line above", {"Ctrl+Shift+Enter"}},
        {"Lines", "Indent", {"Ctrl+]", "Tab"}},
        {"Lines", "Outdent", {"Ctrl+[", "Shift+Tab"}},
        {"Lines", "Toggle comment", {"Ctrl+/"}},
        {"Lines", "Select line", {"Ctrl+L"}},

        {"Editing", "Undo", {"Ctrl+Z"}},
        {"Editing", "Redo", {"Ctrl+Y", "Ctrl+Shift+Z"}},
        {"Editing", "Cut  (nothing selected = line)", {"Ctrl+X"}},
        {"Editing", "Copy  (nothing selected = line)", {"Ctrl+C"}},
        {"Editing", "Paste", {"Ctrl+V"}},
        {"Editing", "Select all", {"Ctrl+A"}},
        {"Editing", "Autocomplete", {"Ctrl+Space"}},
        {"Editing", "Expand snippet (type the trigger, e.g. for)", {"Tab", "Ctrl+J"}},
        {"Editing", "Zoom in", {"Ctrl+="}},
        {"Editing", "Zoom out", {"Ctrl+-"}},
        {"Editing", "Reset zoom", {"Ctrl+0"}},
        {"Editing", "First non-blank character, then line start", {"Home"}},

        {"Search", "Find", {"Ctrl+F"}},
        {"Search", "Replace", {"Ctrl+H"}},
        {"Search", "Next match", {"F3", "Enter"}},
        {"Search", "Previous match", {"Shift+F3", "Shift+Enter"}},
        {"Search", "Go to line", {"Ctrl+G"}},
        {"Search", "Close the find bar", {"Esc"}},

        {"Brackets and quotes", "Close the pair as you type", {"( [ { \" ' `"}, true},
        {"Brackets and quotes", "Wrap the selected text (again = unwrap)", {"( [ { \" ' `"}, true},
        {"Brackets and quotes", "Remove an empty pair", {"Backspace"}},

        {"Explorer (when focused)", "Open file / open or close folder", {"Enter"}},
        {"Explorer (when focused)", "Rename", {"F2"}},
        {"Explorer (when focused)", "Delete (Recycle Bin)", {"Del"}},
        {"Explorer (when focused)", "Delete permanently", {"Shift+Del"}},
        {"Explorer (when focused)", "Cut / copy / paste files", {"Ctrl+X", "Ctrl+C", "Ctrl+V"}},
        {"Explorer (when focused)", "Move onto a folder  (hold Ctrl to copy)", {"Drag"}},

        {"Terminal (when focused)", "Run the command", {"Enter"}},
        {"Terminal (when focused)", "Previous / next command", {"Up", "Down"}},
        {"Terminal (when focused)", "Stop the running program", {"Ctrl+C"}},
        {"Terminal (when focused)", "Clear the screen", {"Ctrl+L"}},
    };
    return list;
}

QStringList ShortcutsDialog::areas() {
    QStringList out;
    for (const Entry& e : entries())
        if (!out.contains(e.area)) out << e.area;
    return out;
}

namespace {
QLabel* cap(const QString& text, QWidget* parent) {
    QLabel* l = new QLabel(text, parent);
    l->setObjectName("keyCap");
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(24);
    return l;
}
}  // namespace

ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("shortcutsDialog");
    setWindowTitle("Keyboard Shortcuts");
    setWindowIcon(Icons::app());
    resize(960, 700);
    setMinimumSize(640, 440);
    buildUi();
}

QWidget* ShortcutsDialog::makeRow(const Entry& e, QWidget* parent) {
    QWidget* row = new QWidget(parent);
    QHBoxLayout* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);
    QLabel* a = new QLabel(e.action, row);
    a->setObjectName("shortcutDesc");
    a->setWordWrap(true);
    h->addWidget(a, 1);
    QHBoxLayout* keys = new QHBoxLayout();
    keys->setSpacing(4);
    if (e.chars) {
        for (const QString& k : e.keys.first().split(' ', Qt::SkipEmptyParts)) keys->addWidget(cap(k, row));
    } else {
        for (int i = 0; i < e.keys.size(); ++i) {
            if (i > 0) {
                QLabel* o = new QLabel("or", row);
                o->setObjectName("keyOr");
                keys->addSpacing(2);
                keys->addWidget(o);
                keys->addSpacing(2);
            }
            // "Ctrl+=" : the part after the last '+' may itself be a symbol, so only split on '+' that has text on both sides
            const QString k = e.keys[i];
            QStringList caps = k.split('+', Qt::SkipEmptyParts);
            if (k.endsWith('+') && !k.endsWith("++")) caps << "+";
            for (const QString& c : caps) keys->addWidget(cap(c, row));
        }
    }
    h->addLayout(keys);
    return row;
}

void ShortcutsDialog::buildUi() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QWidget* head = new QWidget(this);
    QHBoxLayout* hh = new QHBoxLayout(head);
    hh->setContentsMargins(28, 22, 28, 14);
    QVBoxLayout* titles = new QVBoxLayout();
    titles->setSpacing(2);
    QLabel* title = new QLabel("Keyboard Shortcuts", head);
    title->setObjectName("shortcutsTitle");
    QLabel* sub = new QLabel("Everything you can do without lifting your hands. Press F1 to open this list any time.", head);
    sub->setObjectName("shortcutsSubtitle");
    titles->addWidget(title);
    titles->addWidget(sub);
    hh->addLayout(titles, 1);
    search_ = new QLineEdit(head);
    search_->setPlaceholderText("Search actions or keys");
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(280);
    search_->addAction(Icons::get(Icons::Search, QColor(0x80, 0x80, 0x80)), QLineEdit::LeadingPosition);
    hh->addWidget(search_, 0, Qt::AlignBottom);
    root->addWidget(head);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setObjectName("shortcutsScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QWidget* inner = new QWidget(scroll);
    inner->setObjectName("shortcutsInner");
    QVBoxLayout* innerLay = new QVBoxLayout(inner);
    innerLay->setContentsMargins(28, 4, 28, 24);
    innerLay->setSpacing(0);
    QHBoxLayout* cols = new QHBoxLayout();
    cols->setSpacing(16);
    QVBoxLayout* left = new QVBoxLayout();
    QVBoxLayout* right = new QVBoxLayout();
    left->setSpacing(14);
    right->setSpacing(14);
    cols->addLayout(left, 1);
    cols->addLayout(right, 1);
    cols->setAlignment(Qt::AlignTop);
    innerLay->addLayout(cols);
    empty_ = new QLabel("No shortcut matches your search.", inner);
    empty_->setObjectName("settingsEmpty");
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setMinimumHeight(120);
    empty_->hide();
    innerLay->addWidget(empty_);
    innerLay->addStretch(1);
    scroll->setWidget(inner);
    root->addWidget(scroll, 1);

    // one card per area; cards go to the shorter column, so both columns end at about the same height
    int leftRows = 0, rightRows = 0;
    for (const QString& area : areas()) {
        QFrame* card = new QFrame(inner);
        card->setObjectName("shortcutCard");
        QVBoxLayout* cl = new QVBoxLayout(card);
        cl->setContentsMargins(18, 14, 18, 16);
        cl->setSpacing(9);
        QLabel* cat = new QLabel(area.toUpper(), card);
        cat->setObjectName("shortcutCategory");
        cl->addWidget(cat);
        cl->addSpacing(2);
        int n = 0;
        for (const Entry& e : entries()) {
            if (e.area != area) continue;
            QWidget* row = makeRow(e, card);
            cl->addWidget(row);
            QString hay = e.action + " " + e.keys.join(" ");
            hay.replace('+', ' ');
            items_.push_back({row, area, hay.toLower()});
            ++n;
        }
        cards_.push_back({card, area});
        const bool toLeft = leftRows <= rightRows;
        (toLeft ? left : right)->addWidget(card);
        (toLeft ? leftRows : rightRows) += n + 2;
    }
    left->addStretch(1);
    right->addStretch(1);

    QWidget* foot = new QWidget(this);
    foot->setObjectName("shortcutsFooter");
    QHBoxLayout* fh = new QHBoxLayout(foot);
    fh->setContentsMargins(28, 12, 28, 12);
    QLabel* tip = new QLabel("Tip: change how the editor behaves in Settings (Ctrl+,).", foot);
    tip->setObjectName("shortcutNote");
    QPushButton* close = new QPushButton("Close", foot);
    close->setObjectName("primary");
    close->setDefault(true);
    close->setMinimumWidth(88);
    fh->addWidget(tip, 1);
    fh->addWidget(close);
    root->addWidget(foot);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(search_, &QLineEdit::textChanged, this, &ShortcutsDialog::setFilter);
}

void ShortcutsDialog::setFilter(const QString& text) {
    if (search_->text() != text) { search_->setText(text); return; }       // textChanged calls us again
    const QStringList words = text.toLower().split(' ', Qt::SkipEmptyParts);
    int shown = 0;
    for (const Card& c : cards_) {
        int inCard = 0;
        for (Item& it : items_) {
            if (it.area != c.area) continue;
            bool match = true;
            for (const QString& w : words) match = match && it.haystack.contains(w);
            it.widget->setVisible(match);
            if (match) { ++inCard; ++shown; }
        }
        c.frame->setVisible(inCard > 0);
    }
    empty_->setVisible(shown == 0);
}

int ShortcutsDialog::visibleEntryCount() const {
    int n = 0;
    for (const Item& it : items_) if (!it.widget->isHidden()) ++n;
    return n;
}

int ShortcutsDialog::visibleCardCount() const {
    int n = 0;
    for (const Card& c : cards_) if (!c.frame->isHidden()) ++n;
    return n;
}

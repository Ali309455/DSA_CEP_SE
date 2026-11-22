#include "SidePanels.h"
#include <algorithm>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "CodeView.h"
#include "Icons.h"
#include "Theme.h"

namespace {

QToolButton* headerButton(QWidget* parent, Icons::Glyph g, const QString& tip) {
    QToolButton* b = new QToolButton(parent);
    b->setIcon(Icons::get(g));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setObjectName("explorerButton");
    return b;
}

// The panel's header row, styled like the Explorer's: a small caps title, buttons, a close button.
QWidget* header(QWidget* parent, const QString& title, QHBoxLayout*& row, QToolButton*& close) {
    QWidget* h = new QWidget(parent);
    h->setObjectName("explorerHeader");
    row = new QHBoxLayout(h);
    row->setContentsMargins(14, 6, 6, 4);
    row->setSpacing(1);
    QLabel* t = new QLabel(title, h);
    t->setObjectName("explorerTitle");
    row->addWidget(t, 1);
    close = headerButton(h, Icons::Close, "Hide Panel");
    close->setObjectName("explorerClose");
    return h;
}

QIcon symbolIcon(SymbolKind k) { return Icons::forSymbol(k); }

}  // namespace

// ================================================================ Outline

OutlinePanel::OutlinePanel(QWidget* parent) : QWidget(parent) {
    setObjectName("fileExplorer");                       // same surface colour as the Explorer
    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    QHBoxLayout* row = nullptr;
    QToolButton* close = nullptr;
    QWidget* h = header(this, "OUTLINE", row, close);
    QToolButton* collapse = headerButton(h, Icons::Collapse, "Collapse All");
    QToolButton* expand = headerButton(h, Icons::ChevronDown, "Expand All");
    row->addWidget(expand);
    row->addWidget(collapse);
    row->addSpacing(4);
    row->addWidget(close);
    lay->addWidget(h);

    filter_ = new QLineEdit(this);
    filter_->setObjectName("explorerFilter");
    filter_->setPlaceholderText("Filter symbols");
    filter_->setClearButtonEnabled(true);
    filter_->addAction(Icons::get(Icons::Search), QLineEdit::LeadingPosition);
    QWidget* fw = new QWidget(this);
    QVBoxLayout* fl = new QVBoxLayout(fw);
    fl->setContentsMargins(10, 2, 10, 6);
    fl->addWidget(filter_);
    lay->addWidget(fw);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName("outlineTree");
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setIndentation(14);
    tree_->setUniformRowHeights(true);
    tree_->setIconSize(QSize(16, 16));
    tree_->setFocusPolicy(Qt::NoFocus);
    lay->addWidget(tree_, 1);

    hint_ = new QLabel(this);
    hint_->setObjectName("panelHint");
    hint_->setWordWrap(true);
    hint_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(hint_, 1);
    count_ = new QLabel(this);
    count_->setObjectName("panelFooter");
    lay->addWidget(count_);

    connect(close, &QToolButton::clicked, this, &OutlinePanel::closeRequested);
    connect(collapse, &QToolButton::clicked, tree_, &QTreeWidget::collapseAll);
    connect(expand, &QToolButton::clicked, tree_, &QTreeWidget::expandAll);
    connect(filter_, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) { activate(items_.indexOf(it)); });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) { activate(items_.indexOf(it)); });
    connect(&Theme::notifier(), &Theme::Notifier::changed, this, [this]() { rebuild(); });
    rebuild();
}

void OutlinePanel::setView(CodeView* v) {
    if (v == view_) return;
    for (const auto& c : links_) disconnect(c);
    links_.clear();
    view_ = v;
    if (v) {
        links_ << connect(v, &CodeView::outlineChanged, this, [this]() { rebuild(); });
        links_ << connect(v, &CodeView::stateChanged, this, [this]() { followCursor(); });
        links_ << connect(v, &QObject::destroyed, this, [this]() { setView(nullptr); });
    }
    rebuild();
}

void OutlinePanel::rebuild() {
    tree_->clear();
    items_.clear();
    const bool have = view_ && !view_->symbols().empty();
    tree_->setVisible(have);
    hint_->setVisible(!have);
    if (!view_) {
        hint_->setText("Open a file to see its outline.");
        count_->clear();
        count_->hide();                                      // no footer line under an empty panel
        return;
    }
    if (!have) {
        hint_->setText(view_->languageName() == "Plain Text"
                           ? "Plain text has no symbols."
                           : "No classes or functions found in this file yet.");
        count_->clear();
        count_->hide();                                      // no footer line under an empty panel
        return;
    }
    // the symbols are in document order with a depth: a stack of the open parents builds the tree
    const DynArray<Symbol>& syms = view_->symbols();
    QVector<QTreeWidgetItem*> parents;                   // parents[d]: the last symbol seen at depth d
    for (size_t i = 0; i < syms.size(); ++i) {
        const Symbol& s = syms[i];
        const int d = std::min(static_cast<int>(s.depth), static_cast<int>(parents.size()));
        parents.resize(d);
        QTreeWidgetItem* it = d == 0 ? new QTreeWidgetItem(tree_) : new QTreeWidgetItem(parents[d - 1]);
        // short name in the tree ("CodeView::applyFont" -> "applyFont"); the full one is in the tooltip
        QString text = QString::fromStdString(s.name);
        const int cut = text.lastIndexOf(QStringLiteral("::"));
        if (cut > 0 && !text.mid(cut + 2).isEmpty()) text = text.mid(cut + 2);
        if (!s.detail.empty()) text += QString::fromStdString(s.detail);
        it->setText(0, text);
        it->setIcon(0, symbolIcon(s.kind));
        it->setToolTip(0, QString("%1 %2  ·  line %3").arg(symbolKindName(s.kind), QString::fromStdString(s.name)).arg(s.pos.line + 1));
        items_.append(it);
        parents.append(it);
    }
    tree_->expandAll();
    count_->show();
    count_->setText(QString("%1 symbol%2").arg(items_.size()).arg(items_.size() == 1 ? "" : "s"));
    applyFilter();
    followCursor();
}

void OutlinePanel::applyFilter() {
    const QString f = filter_->text().trimmed();
    // a symbol stays when it matches or one of its children does; children come after their parent,
    // so walking backwards decides every child before its parent
    for (int i = static_cast<int>(items_.size()) - 1; i >= 0; --i) {
        QTreeWidgetItem* it = items_[i];
        bool k = f.isEmpty() || it->text(0).contains(f, Qt::CaseInsensitive);
        for (int c = 0; c < it->childCount() && !k; ++c) k = !it->child(c)->isHidden();
        it->setHidden(!k);
    }
    if (!f.isEmpty()) tree_->expandAll();
}

int OutlinePanel::visibleCount() const {
    int n = 0;
    for (QTreeWidgetItem* it : items_) if (!it->isHidden()) ++n;
    return n;
}

void OutlinePanel::setFilter(const QString& text) { filter_->setText(text); }

bool OutlinePanel::activate(int i) {
    if (!view_ || i < 0 || i >= items_.size() || static_cast<size_t>(i) >= view_->symbols().size()) return false;
    const Symbol& s = view_->symbols()[static_cast<size_t>(i)];
    view_->goToLine(static_cast<int>(s.pos.line) + 1);   // centres the line (and opens folds around it)
    view_->setCursor(s.pos);                             // on the name
    view_->setFocus();
    return true;
}

int OutlinePanel::currentSymbol() const {
    QTreeWidgetItem* it = tree_->currentItem();
    return it ? items_.indexOf(it) : -1;
}

void OutlinePanel::followCursor() {
    if (!view_ || items_.isEmpty()) return;
    const long i = symbolAtLine(view_->symbols(), view_->cursor().line);
    QTreeWidgetItem* want = (i >= 0 && i < items_.size()) ? items_[static_cast<int>(i)] : nullptr;
    if (want == tree_->currentItem()) return;
    const QSignalBlocker block(tree_);
    if (want) { tree_->setCurrentItem(want); tree_->scrollToItem(want); }
    else tree_->setCurrentItem(nullptr);
}

// ================================================================ Undo History

HistoryPanel::HistoryPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("fileExplorer");
    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    QHBoxLayout* row = nullptr;
    QToolButton* close = nullptr;
    QWidget* h = header(this, "UNDO HISTORY", row, close);
    undoBtn_ = headerButton(h, Icons::ArrowUp, "Undo (Ctrl+Z)");
    redoBtn_ = headerButton(h, Icons::ArrowDown, "Redo (Ctrl+Y)");
    row->addWidget(undoBtn_);
    row->addWidget(redoBtn_);
    row->addSpacing(4);
    row->addWidget(close);
    lay->addWidget(h);

    list_ = new QListWidget(this);
    list_->setObjectName("historyList");
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setUniformItemSizes(true);
    lay->addWidget(list_, 1);
    hint_ = new QLabel(this);
    hint_->setObjectName("panelHint");
    hint_->setWordWrap(true);
    hint_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(hint_, 1);
    count_ = new QLabel(this);
    count_->setObjectName("panelFooter");
    count_->setWordWrap(true);
    lay->addWidget(count_);

    connect(close, &QToolButton::clicked, this, &HistoryPanel::closeRequested);
    connect(undoBtn_, &QToolButton::clicked, this, [this]() { if (view_) view_->undo(); });
    connect(redoBtn_, &QToolButton::clicked, this, [this]() { if (view_) view_->redo(); });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { jumpTo(list_->row(it)); });
    connect(&Theme::notifier(), &Theme::Notifier::changed, this, [this]() { rebuild(true); });
    rebuild(true);
}

void HistoryPanel::setView(CodeView* v) {
    if (v == view_) return;
    for (const auto& c : links_) disconnect(c);
    links_.clear();
    view_ = v;
    if (v) {
        links_ << connect(v, &CodeView::stateChanged, this, [this]() { rebuild(); });
        links_ << connect(v, &QObject::destroyed, this, [this]() { setView(nullptr); });
    }
    rebuild(true);
}

int HistoryPanel::entryCount() const { return list_->count(); }

int HistoryPanel::currentRow() const {
    return view_ ? static_cast<int>(view_->editor().undoCount()) : -1;
}

bool HistoryPanel::jumpTo(int row) {
    if (!view_ || row < 0 || row >= list_->count()) return false;
    view_->jumpToHistory(static_cast<size_t>(row));
    rebuild(true);
    view_->setFocus();
    return true;
}

// Row 0 is the state before the oldest step that is still kept; row k is the state after step k.
void HistoryPanel::rebuild(bool force) {
    if (rebuilding_) return;
    const Editor* ed = view_ ? &view_->editor() : nullptr;
    const size_t u = ed ? ed->undoCount() : 0, r = ed ? ed->redoCount() : 0;
    const unsigned rev = view_ ? view_->revision() : 0;
    if (!force && rev == shownRevision_ && u == shownUndo_ && r == shownRedo_) return;   // only the cursor moved
    shownRevision_ = rev; shownUndo_ = u; shownRedo_ = r;
    rebuilding_ = true;
    list_->clear();
    undoBtn_->setEnabled(u > 0);
    redoBtn_->setEnabled(r > 0);
    const bool have = ed && (u + r) > 0;
    list_->setVisible(have);
    hint_->setVisible(!have);
    if (!ed) {
        hint_->setText("Open a file to see its undo history.");
        count_->clear();
        count_->hide();                                      // no footer line under an empty panel
    } else if (!have) {
        hint_->setText("No changes yet. Every edit you make appears here; click a row to go back to it.");
        count_->clear();
        count_->hide();                                      // no footer line under an empty panel
    } else {
        QListWidgetItem* start = new QListWidgetItem(u >= ed->historyLimit() ? "Oldest kept state" : "Opened / start", list_);
        start->setIcon(Icons::get(Icons::File));
        for (size_t i = 0; i < u; ++i) {
            QListWidgetItem* it = new QListWidgetItem(QString::fromStdString(Editor::describeStep(ed->undoStep(i))), list_);
            it->setIcon(Icons::get(Icons::Editor));
        }
        for (size_t j = 0; j < r; ++j) {                  // the future: Redo applies redoStep(0) first
            QListWidgetItem* it = new QListWidgetItem(QString::fromStdString(Editor::describeStep(ed->redoStep(j))), list_);
            it->setIcon(Icons::get(Icons::Editor));
            it->setForeground(Theme::Dim);
            QFont f = it->font();
            f.setItalic(true);
            it->setFont(f);
            it->setToolTip("Undone: click to redo up to here");
        }
        QListWidgetItem* now = list_->item(static_cast<int>(u));
        QFont bold = now->font();
        bold.setBold(true);
        now->setFont(bold);
        now->setToolTip("The text is in this state now");
        list_->setCurrentRow(static_cast<int>(u));
        list_->scrollToItem(now);
        count_->show();
        count_->setText(QString("%1 step%2 · %3 undone · keeps the last %4")
                            .arg(u).arg(u == 1 ? "" : "s").arg(r).arg(ed->historyLimit()));
    }
    rebuilding_ = false;
}

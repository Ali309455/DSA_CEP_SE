#include "FindBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include "CodeEditor.h"
#include "TextUtil.h"

FindBar::FindBar(CodeEditor* editor, QWidget* parent) : QWidget(parent), ed_(editor) {
    find_ = new QLineEdit(this);
    find_->setPlaceholderText(tr("Find"));
    find_->setClearButtonEnabled(true);
    replace_ = new QLineEdit(this);
    replace_->setPlaceholderText(tr("Replace with"));
    replace_->setClearButtonEnabled(true);
    caseBtn_ = new QToolButton(this);
    caseBtn_->setText(QStringLiteral("Aa"));
    caseBtn_->setToolTip(tr("Match case"));
    caseBtn_->setCheckable(true);
    wordBtn_ = new QToolButton(this);
    wordBtn_->setText(QStringLiteral("W"));
    wordBtn_->setToolTip(tr("Whole word"));
    wordBtn_->setCheckable(true);
    prev_ = new QPushButton(QStringLiteral("▲"), this);
    prev_->setToolTip(tr("Previous (Shift+Enter / Shift+F3)"));
    next_ = new QPushButton(QStringLiteral("▼"), this);
    next_->setToolTip(tr("Next (Enter / F3)"));
    rep_ = new QPushButton(tr("Replace"), this);
    repAll_ = new QPushButton(tr("Replace all"), this);
    close_ = new QPushButton(QStringLiteral("✕"), this);
    close_->setFlat(true);
    count_ = new QLabel(this);
    count_->setMinimumWidth(90);
    for (QPushButton* b : {prev_, next_, close_}) b->setMaximumWidth(34);

    auto* row1 = new QHBoxLayout;
    row1->addWidget(find_, 1);
    row1->addWidget(caseBtn_);
    row1->addWidget(wordBtn_);
    row1->addWidget(prev_);
    row1->addWidget(next_);
    row1->addWidget(count_);
    row1->addWidget(close_);
    auto* row2 = new QHBoxLayout;
    row2->addWidget(replace_, 1);
    row2->addWidget(rep_);
    row2->addWidget(repAll_);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(8, 6, 8, 6);
    col->setSpacing(4);
    col->addLayout(row1);
    col->addLayout(row2);

    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &FindBar::refresh);
    connect(find_, &QLineEdit::textChanged, this, [this] { refresh(); if (!matches_.empty()) step(true); });
    connect(caseBtn_, &QToolButton::toggled, this, [this] { refresh(); });
    connect(wordBtn_, &QToolButton::toggled, this, [this] { refresh(); });
    connect(next_, &QPushButton::clicked, this, &FindBar::findNext);
    connect(prev_, &QPushButton::clicked, this, &FindBar::findPrev);
    connect(rep_, &QPushButton::clicked, this, &FindBar::replaceOne);
    connect(repAll_, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(close_, &QPushButton::clicked, this, &FindBar::closeBar);
    connect(ed_, &CodeEditor::textMirrored, this, [this] { if (isVisible()) timer_.start(150); });
    find_->installEventFilter(this);
    replace_->installEventFilter(this);
    hide();
}

SearchOptions FindBar::options() const {
    SearchOptions o;
    o.matchCase = caseBtn_->isChecked();
    o.wholeWord = wordBtn_->isChecked();
    return o;
}

void FindBar::openFind(bool withReplace) {
    QString sel = ed_->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar(0x2029)) && sel.size() < 200) find_->setText(sel);
    replace_->setVisible(true);
    rep_->setVisible(true);
    repAll_->setVisible(true);
    show();
    (withReplace ? replace_ : find_)->setFocus();
    find_->selectAll();
    refresh();
}

void FindBar::closeBar() {
    hide();
    matches_.clear();
    current_ = -1;
    ed_->setSearchMatches(matches_, -1);
    ed_->setFocus();
}

bool FindBar::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Escape) { closeBar(); return true; }
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)) {
            if (o == replace_) replaceOne();
            else step(!(k->modifiers() & Qt::ShiftModifier));
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

// Recompute the match list. An empty search text is "nothing to search", not an error.
bool FindBar::recompute(QString* err) {
    matches_.clear();
    const std::string pat = TextUtil::toStd(find_->text());
    if (pat.empty()) return true;
    try {
        matches_ = findAll(ed_->engine().doc(), pat, options(), true);
    } catch (const std::exception& ex) {
        if (err) *err = QString::fromUtf8(ex.what());
        return false;
    }
    return true;
}

int FindBar::indexOfSelection() const {
    const QTextCursor c = ed_->textCursor();
    if (!c.hasSelection()) return -1;
    const Pos a = ed_->posAt(c.selectionStart()), b = ed_->posAt(c.selectionEnd());
    for (size_t i = 0; i < matches_.size(); ++i)
        if (matches_[i].start == a && matches_[i].end == b) return static_cast<int>(i);
    return -1;
}

void FindBar::refresh() {
    QString err;
    const bool ok = recompute(&err);
    current_ = indexOfSelection();
    ed_->setSearchMatches(matches_, current_);
    updateLabel(!ok || (matches_.empty() && !find_->text().isEmpty()));
}

void FindBar::updateLabel(bool bad) {
    if (find_->text().isEmpty()) count_->setText(QString());
    else if (matches_.empty()) count_->setText(tr("No results"));
    else count_->setText(current_ >= 0 ? tr("%1 of %2").arg(current_ + 1).arg(matches_.size()) : tr("%1 found").arg(matches_.size()));
    count_->setStyleSheet(bad ? QStringLiteral("color:%1;").arg(ed_->theme().error.name()) : QString());
}

// Next / previous match relative to the current selection (or the caret); wraps around.
void FindBar::step(bool forward) {
    refresh();
    if (matches_.empty()) return;
    const QTextCursor c = ed_->textCursor();
    const Pos selStart = ed_->posAt(c.selectionStart());
    const Pos caret = ed_->posAt(c.position());
    int pick = -1;
    if (forward) {
        for (size_t i = 0; i < matches_.size(); ++i) {
            const bool after = c.hasSelection() ? (selStart < matches_[i].start) : !(matches_[i].start < caret);
            if (after) { pick = static_cast<int>(i); break; }
        }
        if (pick < 0) pick = 0;
    } else {
        for (size_t i = matches_.size(); i > 0; --i)
            if (matches_[i - 1].start < selStart) { pick = static_cast<int>(i - 1); break; }
        if (pick < 0) pick = static_cast<int>(matches_.size()) - 1;
    }
    ed_->selectRange(matches_[pick].start, matches_[pick].end);
    current_ = pick;
    ed_->setSearchMatches(matches_, current_);
    updateLabel(false);
}

void FindBar::replaceOne() {
    refresh();
    const std::string pat = TextUtil::toStd(find_->text());
    if (pat.empty() || matches_.empty()) return;
    const int idx = indexOfSelection();
    if (idx < 0) { step(true); return; }          // first press selects the match, second replaces it
    QString err;
    Pos after;
    if (!ed_->replaceNextText(matches_[idx], pat, TextUtil::toStd(replace_->text()), options(), &after, &err)) {
        if (!err.isEmpty()) emit message(err);
    }
    step(true);
}

void FindBar::replaceAll() {
    const std::string pat = TextUtil::toStd(find_->text());
    if (pat.empty()) { emit message(tr("Type the text to replace first.")); return; }
    QString err;
    const ReplaceResult r = ed_->replaceAllText(pat, TextUtil::toStd(replace_->text()), options(), &err);
    if (!err.isEmpty()) { emit message(err); return; }
    emit message(r.found == 0 ? tr("No matches.") : tr("Replaced %1 occurrence(s) - one Undo reverses all.").arg(r.changed));
    refresh();
}

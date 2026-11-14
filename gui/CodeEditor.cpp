#include "CodeEditor.h"

#include <QAbstractTextDocumentLayout>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
#include <QTextBlock>
#include <QUrl>
#include <algorithm>
#include <stdexcept>
#include "CppHighlighter.h"
#include "TextUtil.h"

using namespace TextUtil;

// ====================================================================== gutter

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* e) : QWidget(e), ed_(e) {}
    QSize sizeHint() const override { return QSize(ed_->lineNumberAreaWidth(), 0); }
protected:
    void paintEvent(QPaintEvent* e) override { ed_->lineNumberAreaPaintEvent(e); }
private:
    CodeEditor* ed_;
};

// ====================================================================== UserEdit

UserEdit::UserEdit(CodeEditor& e, const char* label) : e_(e), c_(e.textCursor()) {
    if (e_.userEditDepth_ == 0) {
        e_.commitTyping();
        e_.ed_.beginGroup(label);
    }
    ++e_.userEditDepth_;
    c_.beginEditBlock();
}

UserEdit::~UserEdit() {
    c_.endEditBlock();                       // Qt reports the whole change now (still inside the group)
    if (--e_.userEditDepth_ == 0) e_.ed_.endGroup();
}

// ====================================================================== construction

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent), theme_(Theme::dark_()) {
    setUndoRedoEnabled(false);               // the ENGINE does undo / redo
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    document()->setDocumentMargin(4);  // small padding inside editor
    setCursorWidth(2);
    ed_.setHistoryLimit(1000);
    snips_.loadDefaults();

    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(13);
    setFont(f);

    highlighter_ = new CppHighlighter(document());
    gutter_ = new LineNumberArea(this);
    popup_ = new QListWidget(viewport());
    popup_->setObjectName("completionPopup");
    popup_->setFrameShape(QFrame::StyledPanel);
    popup_->setFrameShadow(QFrame::Plain);
    popup_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popup_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    popup_->setMaximumHeight(200);
    popup_->setMinimumWidth(180);
    popup_->setFocusPolicy(Qt::NoFocus);
    popup_->hide();
    connect(popup_, &QListWidget::itemClicked, this, [this] { acceptCompletion(); setFocus(); });

    analysisTimer_.setSingleShot(true);
    connect(&analysisTimer_, &QTimer::timeout, this, &CodeEditor::runAnalysis);

    connect(document(), &QTextDocument::contentsChange, this, &CodeEditor::onContentsChange);
    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::onCursorMoved);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateExtraSelections(); });

    setTheme(theme_);
    updateTabStop();
    updateLineNumberAreaWidth();
    verticalScrollBar()->setSingleStep(1);
    runAnalysis();
}

CodeEditor::~CodeEditor() {}

void CodeEditor::setTheme(const Theme& t) {
    theme_ = t;
    setStyleSheet(QString("QPlainTextEdit { background:%1; color:%2; selection-background-color:%3; "
                          "selection-color:%2; }")
                      .arg(t.bg.name(), t.fg.name(), t.selection.name()));
    QPalette p = popup_->palette();
    p.setColor(QPalette::Base, t.popupBg);
    p.setColor(QPalette::Text, t.fg);
    p.setColor(QPalette::Highlight, t.popupSel);
    p.setColor(QPalette::HighlightedText, t.fg);
    popup_->setPalette(p);
    highlighter_->setTheme(t);
    updateExtraSelections();
    gutter_->update();
}

QString CodeEditor::displayName() const {
    return filePath_.isEmpty() ? QStringLiteral("untitled") : QFileInfo(filePath_).fileName();
}

void CodeEditor::setTabSize(int spaces) {
    tabSize_ = qBound(1, spaces, 16);
    updateTabStop();
}

void CodeEditor::updateTabStop() { setTabStopDistance(tabSize_ * fontMetrics().horizontalAdvance(QLatin1Char(' '))); }

void CodeEditor::changeEvent(QEvent* e) {
    QPlainTextEdit::changeEvent(e);
    if (e->type() == QEvent::FontChange && gutter_) { updateTabStop(); updateLineNumberAreaWidth(); gutter_->update(); }
}

void CodeEditor::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        const int delta = e->angleDelta().y();
        if (delta > 0) zoomIn(1);
        else if (delta < 0) zoomOut(1);
        e->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(e);
    updateTabStop();
}

// ====================================================================== positions

Pos CodeEditor::posAt(int offset) const {
    QTextBlock b = document()->findBlock(qBound(0, offset, qMax(0, document()->characterCount() - 1)));
    if (!b.isValid()) return Pos{0, 0};
    return Pos{static_cast<size_t>(b.blockNumber()), u16ToByte(b.text(), offset - b.position())};
}

int CodeEditor::offsetOf(Pos p) const {
    QTextBlock b = document()->findBlockByNumber(static_cast<int>(p.line));
    if (!b.isValid()) return -1;
    return b.position() + byteToU16(b.text(), p.col);
}

void CodeEditor::selectRange(Pos a, Pos b, bool center) {
    const int oa = offsetOf(a), ob = offsetOf(b);
    if (oa < 0 || ob < 0) return;
    QTextCursor c(document());
    c.setPosition(oa);
    c.setPosition(ob, QTextCursor::KeepAnchor);
    setTextCursor(c);
    if (center && !viewport()->rect().contains(cursorRect())) centerCursor();
    else ensureCursorVisible();
}

void CodeEditor::goToLine(int oneBased) {
    QTextBlock b = document()->findBlockByNumber(qBound(1, oneBased, blockCount()) - 1);
    QTextCursor c(b);
    setTextCursor(c);
    centerCursor();
    setFocus();
}

QString CodeEditor::widgetText() const {
    QString out;
    bool first = true;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        if (!first) out.append('\n');
        out.append(b.text());
        first = false;
    }
    return out;
}

bool CodeEditor::verifyConsistency(QString* why) const {
    if (!ed_.doc().checkInvariants()) { if (why) *why = "engine invariants broken"; return false; }
    if (ed_.doc().lineCount() != static_cast<size_t>(document()->blockCount())) {
        if (why) *why = "line counts differ";
        return false;
    }
    if (widgetText() != documentText(ed_.doc())) { if (why) *why = "text differs"; return false; }
    return true;
}

// ====================================================================== widget -> engine

void CodeEditor::readEngine(Pos from, int units, QString& out) const {
    const Document& d = ed_.doc();
    size_t line = from.line, col = from.col;
    out.clear();
    int remaining = units;
    while (remaining > 0) {
        if (line >= d.lineCount()) throw std::out_of_range("past the end of the engine document");
        const std::string s = d.getLine(line);
        if (col > s.size()) throw std::out_of_range("column past the end of the line");
        const QString tail = QString::fromUtf8(s.data() + col, static_cast<int>(s.size() - col));
        if (remaining <= tail.size()) { out += tail.left(remaining); return; }
        if (line + 1 >= d.lineCount()) throw std::out_of_range("no line break to remove");
        out += tail;
        out += QLatin1Char('\n');
        remaining -= tail.size() + 1;
        ++line;
        col = 0;
    }
}

void CodeEditor::onContentsChange(int pos, int removed, int added) {
    if (syncing_) return;
    try {
        mirror(pos, removed, added);
    } catch (const std::exception&) {
        resyncEngineFromWidget();            // never leave the two copies different
    }
}

static bool oneChar(const QString& s) {
    return s.size() == 1 || (s.size() == 2 && s.at(0).isHighSurrogate());
}

void CodeEditor::mirror(int pos, int removed, int added) {
    QTextDocument* d = document();
    QTextBlock blk = d->findBlock(pos);
    if (!blk.isValid()) throw std::runtime_error("bad position");
    const int col16 = pos - blk.position();
    const Pos start{static_cast<size_t>(blk.blockNumber()), u16ToByte(blk.text(), col16)};

    QString oldText;
    readEngine(start, removed, oldText);

    QString newText;
    {
        int remaining = added, c16 = col16;
        QTextBlock b = blk;
        while (remaining > 0) {
            if (!b.isValid()) throw std::runtime_error("added text past the end");
            const QString t = b.text();
            const int avail = t.size() - c16;
            if (remaining <= avail) { newText += t.mid(c16, remaining); break; }
            newText += t.mid(c16);
            newText += QLatin1Char('\n');
            remaining -= avail + 1;
            b = b.next();
            c16 = 0;
        }
    }
    if (oldText == newText) return;          // formatting-only signal (syntax colours): nothing to do

    int p = 0, s = 0;
    commonAffixes(oldText, newText, p, s);
    const QString o = oldText.mid(p, oldText.size() - p - s);
    const QString n = newText.mid(p, newText.size() - p - s);
    const Pos a = advancePos(start, oldText, 0, p);
    const Pos b = advancePos(a, oldText, p, oldText.size() - s);
    const std::string ins = toStd(n);

    auto apply = [&]() {
        if (!o.isEmpty()) ed_.deleteRange(a, b);
        if (!ins.empty()) ed_.insertText(a, ins);
    };

    if (userEditDepth_ > 0) {
        apply();                             // inside a UserEdit group: it is one undo step already
    } else {
        Kind k = Kind::Other;
        if (o.isEmpty() && oneChar(n) && n != QLatin1String("\n")) k = Kind::Type;
        else if (n.isEmpty() && oneChar(o) && o != QLatin1String("\n")) k = hint_;
        bool continues = false;
        if (k != Kind::Other && typingOpen_ && typingKind_ == k && typingClock_.elapsed() < 1500) {
            if (k == Kind::Type || k == Kind::Del) continues = (a == typingNext_);
            else continues = (b == typingNext_);
        }
        if (!continues) commitTyping();
        if (k != Kind::Other) {
            if (!continues) {
                ed_.beginGroup(k == Kind::Type ? "typing" : k == Kind::Back ? "backspace" : "delete");
                typingOpen_ = true;
                typingKind_ = k;
            }
            apply();
            typingNext_ = (k == Kind::Type) ? advancePos(a, n, 0, n.size()) : a;
            typingClock_.restart();
            if (k == Kind::Type && n.at(0).isSpace()) commitTyping();   // a word per undo step
        } else {
            const char* label = o.isEmpty() ? "insert text" : n.isEmpty() ? "delete text" : "replace text";
            EditGroup g(ed_, label);
            apply();
        }
    }
    if (ed_.doc().lineCount() != static_cast<size_t>(d->blockCount())) throw std::runtime_error("line count mismatch");
    reportDirty_ = true;
    analysisTimer_.start(250);
    emit textMirrored();
    emit stateChanged();
}

void CodeEditor::commitTyping() {
    if (typingOpen_) {
        ed_.endGroup();
        typingOpen_ = false;
    }
}

void CodeEditor::resyncEngineFromWidget() {
    typingOpen_ = false;
    while (ed_.inGroup()) ed_.endGroup();    // an exception may have left a group open
    const QString w = widgetText();
    const QString e = documentText(ed_.doc());
    if (w == e) return;
    int p = 0, s = 0;
    commonAffixes(e, w, p, s);
    const Pos a = advancePos(Pos{0, 0}, e, 0, p);
    const Pos b = advancePos(a, e, p, e.size() - s);
    try {
        EditGroup g(ed_, "resync");
        ed_.deleteRange(a, b);
        ed_.insertText(a, toStd(w.mid(p, w.size() - p - s)));
    } catch (const std::exception&) {
        ed_.loadText(toStd(w), toStd(filePath_));     // last resort: history is lost, text is safe
        lossy_ = true;
    }
    reportDirty_ = true;
    analysisTimer_.start(250);
    emit message(tr("Editor state was re-synchronised."));
    emit stateChanged();
}

// ====================================================================== engine -> widget

bool CodeEditor::syncFromEngine() {
    const QString target = documentText(ed_.doc());
    const QString cur = widgetText();
    reportDirty_ = true;
    analysisTimer_.start(250);
    if (cur == target) { emit stateChanged(); return false; }
    int p = 0, s = 0;
    commonAffixes(cur, target, p, s);
    const int oldLen = cur.size() - p - s;
    const QString mid = target.mid(p, target.size() - p - s);
    syncing_ = true;
    {
        QTextCursor c(document());
        c.beginEditBlock();
        c.setPosition(p);
        c.setPosition(p + oldLen, QTextCursor::KeepAnchor);
        if (mid.isEmpty()) c.removeSelectedText(); else c.insertText(mid);
        c.endEditBlock();
    }
    syncing_ = false;
    QTextCursor t(document());
    t.setPosition(qMin(p + mid.size(), document()->characterCount() - 1));
    setTextCursor(t);
    ensureCursorVisible();
    emit textMirrored();
    emit stateChanged();
    return true;
}

void CodeEditor::setWidgetFromEngine() {
    typingOpen_ = false;
    syncing_ = true;
    setPlainText(documentText(ed_.doc()));
    syncing_ = false;
    reportDirty_ = true;
    matches_.clear();
    currentMatch_ = -1;
    runAnalysis();
    emit textMirrored();
    emit stateChanged();
}

bool CodeEditor::runEngine(const std::function<void()>& op, QString* error) {
    commitTyping();
    bool ok = true;
    try {
        op();
    } catch (const std::exception& ex) {
        ok = false;
        if (error) *error = QString::fromUtf8(ex.what());
    }
    syncFromEngine();                        // also after a failure: the engine guarantees "unchanged"
    return ok;
}

bool CodeEditor::engineUndo() {
    commitTyping();
    std::string label;
    if (!ed_.undo(&label)) { emit message(tr("Nothing to undo.")); return false; }
    hidePopup();
    syncFromEngine();
    emit message(tr("Undo: %1").arg(fromStd(label)));
    return true;
}

bool CodeEditor::engineRedo() {
    commitTyping();
    std::string label;
    if (!ed_.redo(&label)) { emit message(tr("Nothing to redo.")); return false; }
    hidePopup();
    syncFromEngine();
    emit message(tr("Redo: %1").arg(fromStd(label)));
    return true;
}

ReplaceResult CodeEditor::replaceAllText(const std::string& pat, const std::string& rep, const SearchOptions& o,
                                         QString* error) {
    ReplaceResult r;
    runEngine([&] { r = ed_.replaceAll(pat, rep, o); }, error);
    return r;
}

bool CodeEditor::replaceNextText(const Match& at, const std::string& pat, const std::string& rep,
                                 const SearchOptions& o, Pos* after, QString* error) {
    bool did = false;
    Pos end = at.start;
    if (!runEngine([&] { did = ed_.replaceNext(at.start, pat, rep, o, &end, false); }, error)) return false;
    if (did) {
        const int off = offsetOf(end);
        if (off >= 0) { QTextCursor c(document()); c.setPosition(off); setTextCursor(c); }
        if (after) *after = end;
    }
    return did;
}

// ====================================================================== files

CodeEditor::LoadResult CodeEditor::openFile(const QString& path, bool acceptLossy) {
    LoadResult r;
    if (path.isEmpty()) { r.message = tr("No file name given."); return r; }
    const QFileInfo fi(path);
    if (!fi.exists()) { r.message = tr("'%1' does not exist.").arg(path); return r; }
    if (fi.isDir()) { r.message = tr("'%1' is a folder, not a file.").arg(path); return r; }
    if (fi.size() > 20 * 1024 * 1024) { r.message = tr("'%1' is larger than 20 MB; too big for this editor.").arg(path); return r; }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { r.message = tr("Cannot read '%1': %2").arg(path, f.errorString()); return r; }
    QByteArray data = f.readAll();
    if (f.error() != QFileDevice::NoError) { r.message = tr("Error while reading '%1': %2").arg(path, f.errorString()); return r; }
    f.close();
    if (data.contains('\0')) { r.message = tr("'%1' looks like a binary file (it contains NUL bytes); not opened.").arg(path); return r; }
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);          // UTF-8 byte-order mark
    QString text = QString::fromUtf8(data);
    const bool valid = (text.toUtf8() == data);
    if (!valid && !acceptLossy) {
        r.needsConfirm = true;
        r.message = tr("'%1' is not valid UTF-8 (maybe an old Windows / Latin-1 file).\n"
                       "You can open it anyway: the unreadable bytes are replaced by '\xEF\xBF\xBD' "
                       "and the original file is only changed if you save.").arg(path);
        return r;
    }
    text = sanitize(text);
    ed_.loadText(toStd(text), toStd(path));
    filePath_ = path;
    lossy_ = !valid;
    setWidgetFromEngine();
    QTextCursor c(document());
    setTextCursor(c);
    r.ok = true;
    r.message = tr("Opened '%1'.").arg(path);
    return r;
}

bool CodeEditor::saveFile(const QString& path, QString* error) {
    if (path.isEmpty()) { if (error) *error = tr("No file name given."); return false; }
    commitTyping();
    QString why;
    if (!verifyConsistency(&why)) resyncEngineFromWidget();   // what you see is what is saved
    const std::string s = ed_.doc().toString();
    QSaveFile f(path);                       // writes a temp file, renames on success: no half-written files
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = tr("Cannot write '%1': %2").arg(path, f.errorString()); return false; }
    if (f.write(s.data(), static_cast<qint64>(s.size())) != static_cast<qint64>(s.size())) {
        if (error) *error = tr("Error while writing '%1': %2").arg(path, f.errorString());
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) { if (error) *error = tr("Cannot finish saving '%1': %2").arg(path, f.errorString()); return false; }
    ed_.markSaved(toStd(path));
    filePath_ = path;
    lossy_ = false;
    emit stateChanged();
    return true;
}

void CodeEditor::newDocument() {
    ed_.newDocument();
    filePath_.clear();
    lossy_ = false;
    setWidgetFromEngine();
}

// ====================================================================== analysis + highlights

const BracketReport& CodeEditor::bracketReport() {
    if (reportDirty_) {
        report_ = checkBrackets(ed_.doc());
        reportDirty_ = false;
    }
    return report_;
}

void CodeEditor::runAnalysis() {
    report_ = checkBrackets(ed_.doc());
    reportDirty_ = false;
    issueLines_.clear();
    for (size_t i = 0; i < report_.issues.size(); ++i) issueLines_.insert(static_cast<int>(report_.issues[i].pos.line));
    try {
        const Pos c = cursorPos();
        ac_.refresh(ed_.doc(), &c);
    } catch (const std::exception&) {
        ac_.refresh(ed_.doc());
    }
    updateExtraSelections();
    gutter_->update();
    emit analysisUpdated();
}

void CodeEditor::setSearchMatches(const DynArray<Match>& m, int current) {
    matches_.clear();
    matches_.reserve(static_cast<int>(m.size()));
    for (size_t i = 0; i < m.size(); ++i) matches_.append(m[i]);
    currentMatch_ = current;
    updateExtraSelections();
}

void CodeEditor::updateExtraSelections() {
    QList<QTextEdit::ExtraSelection> sels;
    auto mk = [&](int a, int b, const QColor& bg) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(bg);
        s.cursor = QTextCursor(document());
        s.cursor.setPosition(a);
        s.cursor.setPosition(b, QTextCursor::KeepAnchor);
        sels.append(s);
    };
    {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(theme_.curLine);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = textCursor();
        s.cursor.clearSelection();
        sels.append(s);
    }
    // search matches near the viewport only (keeps huge result lists cheap)
    if (!matches_.isEmpty()) {
        const int first = firstVisibleBlock().blockNumber();
        const int rows = static_cast<int>(viewport()->height() / qMax(1.0, blockBoundingRect(firstVisibleBlock()).height())) + 3;
        const size_t lo = static_cast<size_t>(qMax(0, first - 5)), hi = static_cast<size_t>(first + rows + 5);
        auto it = std::lower_bound(matches_.begin(), matches_.end(), lo,
                                   [](const Match& m, size_t line) { return m.start.line < line; });
        int count = 0;
        for (; it != matches_.end() && it->start.line <= hi && count < 2000; ++it, ++count) {
            const int a = offsetOf(it->start), b = offsetOf(it->end);
            if (a >= 0 && b >= a) mk(a, b, (it - matches_.begin()) == currentMatch_ ? theme_.findCurrent : theme_.findMatch);
        }
    }
    // matching bracket
    try {
        if (!reportDirty_ || ed_.doc().charCount() < 500000) {
            const BracketReport& r = bracketReport();
            Pos br, other;
            if (matchNearCursor(ed_.doc(), r, cursorPos(), br, other)) {
                for (const Pos& p : {br, other}) {
                    const int o = offsetOf(p);
                    if (o >= 0) mk(o, o + 1, theme_.bracketMatch);
                }
            }
        }
    } catch (const std::exception&) {}
    // bracket problems: wavy red underline
    if (!reportDirty_) {
        for (size_t i = 0; i < report_.issues.size(); ++i) {
            const int o = offsetOf(report_.issues[i].pos);
            if (o < 0 || o >= document()->characterCount() - 1) continue;
            QTextEdit::ExtraSelection s;
            s.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            s.format.setUnderlineColor(theme_.error);
            s.cursor = QTextCursor(document());
            s.cursor.setPosition(o);
            s.cursor.setPosition(o + 1, QTextCursor::KeepAnchor);
            sels.append(s);
        }
    }
    setExtraSelections(sels);
}

void CodeEditor::onCursorMoved() {
    updateExtraSelections();
    if (popup_->isVisible()) {
        try { if (prefixBefore(ed_.doc(), cursorPos()).empty()) hidePopup(); } catch (const std::exception&) { hidePopup(); }
    }
    analysisTimer_.start(400);
    emit stateChanged();
}

// ====================================================================== line-number gutter

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    int space = 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return qMax(space, 40);
}

void CodeEditor::updateLineNumberAreaWidth() { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }

void CodeEditor::updateLineNumberArea(const QRect& r, int dy) {
    if (dy) gutter_->scroll(0, dy);
    else gutter_->update(0, r.y(), gutter_->width(), r.height());
    if (r.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    gutter_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    updateExtraSelections();
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* ev) {
    QPainter p(gutter_);
    p.fillRect(ev->rect(), theme_.gutterBg);
    QTextBlock b = firstVisibleBlock();
    int n = b.blockNumber();
    int top = qRound(blockBoundingGeometry(b).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(b).height());
    const int cur = textCursor().blockNumber();
    while (b.isValid() && top <= ev->rect().bottom()) {
        if (b.isVisible() && bottom >= ev->rect().top()) {
            const bool isCurrentLine = (n == cur);
            const int height = fontMetrics().height();
            p.setPen(isCurrentLine ? theme_.gutterActive : theme_.gutterFg);
            if (isCurrentLine) {
                p.fillRect(0, top, 2, height, theme_.accent);
            }
            p.drawText(0, top, lineNumberAreaWidth() - 6, height, Qt::AlignRight | Qt::AlignVCenter, QString::number(n + 1));
            if (issueLines_.contains(n)) {
                p.setPen(Qt::NoPen);
                p.setBrush(theme_.error);
                p.drawEllipse(QPoint(8, top + height / 2), 3, 3);
            }
        }
        b = b.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(b).height());
        ++n;
    }
}

// ====================================================================== keyboard

void CodeEditor::keyPressEvent(QKeyEvent* e) {
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers();
    if (e->matches(QKeySequence::Undo)) { engineUndo(); return; }
    if (e->matches(QKeySequence::Redo) || (key == Qt::Key_Z && (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier))) {
        engineRedo();
        return;
    }
    if (popup_->isVisible()) {
        if (key == Qt::Key_Down || key == Qt::Key_Up) {
            const int r = popup_->currentRow() + (key == Qt::Key_Down ? 1 : -1);
            popup_->setCurrentRow((r + popup_->count()) % popup_->count());
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { acceptCompletion(); return; }
        if (key == Qt::Key_Escape) { hidePopup(); return; }
    }
    if ((mods & Qt::ControlModifier) && key == Qt::Key_Space) { showCompletions(true); return; }
    if (key == Qt::Key_Tab && !(mods & (Qt::ControlModifier | Qt::AltModifier))) {
        if (textCursor().hasSelection()) { indentSelection(true); return; }
        if (expandSnippetAtCursor()) return;
        if (popup_->isVisible()) { acceptCompletion(); return; }
        UserEdit ue(*this, "tab");
        QTextCursor c = textCursor();
        const int col = c.positionInBlock();
        c.insertText(QString(4 - col % 4, QLatin1Char(' ')));
        setTextCursor(c);
        return;
    }
    if (key == Qt::Key_Backtab) { indentSelection(false); return; }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !(mods & Qt::ControlModifier)) { hidePopup(); smartNewline(); return; }
    if (key == Qt::Key_Backspace) hint_ = Kind::Back;
    else if (key == Qt::Key_Delete) hint_ = Kind::Del;
    if (autoClose_ && !(mods & (Qt::ControlModifier | Qt::AltModifier)) && tryAutoClose(e->text())) return;

    QPlainTextEdit::keyPressEvent(e);
    hint_ = Kind::Other;

    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta) return;
    const QString t = e->text();
    if ((!t.isEmpty() && (t.at(0).isLetterOrNumber() || t.at(0) == QLatin1Char('_'))) || key == Qt::Key_Backspace) updateCompletion(false);
    else if (!t.isEmpty() || key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Home || key == Qt::Key_End) hidePopup();
}

void CodeEditor::focusOutEvent(QFocusEvent* e) {
    hidePopup();
    commitTyping();
    QPlainTextEdit::focusOutEvent(e);
}

bool CodeEditor::tryAutoClose(const QString& typed) {
    if (typed.size() != 1) return false;
    const QChar ch = typed.at(0);
    QTextCursor c = textCursor();
    if (c.hasSelection()) return false;
    const QString line = c.block().text();
    const int col = c.positionInBlock();
    const QChar next = col < line.size() ? line.at(col) : QChar();
    const QChar prev = col > 0 ? line.at(col - 1) : QChar();
    const QString closers = QStringLiteral(")]}\"'");
    if (closers.contains(ch) && next == ch) {          // typing the closer that is already there: step over it
        c.movePosition(QTextCursor::Right);
        setTextCursor(c);
        return true;
    }
    const QString openers = QStringLiteral("([{\"'");
    const int idx = openers.indexOf(ch);
    if (idx < 0) return false;
    const bool nextOk = next.isNull() || next.isSpace() || QStringLiteral(")]};,").contains(next);
    if (!nextOk) return false;
    if ((ch == QLatin1Char('"') || ch == QLatin1Char('\'')) && (prev.isLetterOrNumber() || prev == QLatin1Char('_') || prev == ch)) return false;
    const QString pair = QString(ch) + QString(QLatin1Char(")]}\"'"[idx]));
    UserEdit ue(*this, "bracket pair");
    c.insertText(pair);
    c.movePosition(QTextCursor::Left);
    setTextCursor(c);
    return true;
}

void CodeEditor::smartNewline() {
    UserEdit ue(*this, "new line");
    QTextCursor c = textCursor();
    if (c.hasSelection()) c.removeSelectedText();
    const QString line = c.block().text();
    const int col = c.positionInBlock();
    QString indent;
    for (int i = 0; i < qMin(col, line.size()) && (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t')); ++i) indent += line.at(i);
    const bool open = line.left(col).trimmed().endsWith(QLatin1Char('{'));
    const QString inner = indent + (open ? QStringLiteral("    ") : QString());
    if (open && line.mid(col).trimmed().startsWith(QLatin1Char('}'))) {
        c.insertText(QStringLiteral("\n") + inner + QStringLiteral("\n") + indent);
        c.movePosition(QTextCursor::Up);
        c.movePosition(QTextCursor::EndOfBlock);
    } else {
        c.insertText(QStringLiteral("\n") + inner);
    }
    setTextCursor(c);
    ensureCursorVisible();
}

// ---- block operations (each is one undo step)

void CodeEditor::indentSelection(bool in) {
    QTextCursor sel = textCursor();
    const int s = sel.selectionStart(), e = sel.selectionEnd();
    QTextBlock b = document()->findBlock(s);
    QTextBlock last = document()->findBlock(e);
    if (e > s && e == last.position()) last = last.previous();
    UserEdit ue(*this, in ? "indent" : "unindent");
    for (; b.isValid() && b.blockNumber() <= last.blockNumber(); b = b.next()) {
        QTextCursor lc(b);
        if (in) {
            if (!b.text().isEmpty()) lc.insertText(QStringLiteral("    "));
        } else {
            const QString t = b.text();
            int n = 0;
            if (t.startsWith(QLatin1Char('\t'))) n = 1;
            else while (n < 4 && n < t.size() && t.at(n) == QLatin1Char(' ')) ++n;
            if (n > 0) { lc.setPosition(b.position() + n, QTextCursor::KeepAnchor); lc.removeSelectedText(); }
        }
    }
    setTextCursor(sel);
}

void CodeEditor::toggleComment() {
    QTextCursor sel = textCursor();
    const int s = sel.selectionStart(), e = sel.selectionEnd();
    QTextBlock first = document()->findBlock(s), last = document()->findBlock(e);
    if (e > s && e == last.position()) last = last.previous();
    bool allCommented = true, any = false;
    for (QTextBlock b = first; b.isValid() && b.blockNumber() <= last.blockNumber(); b = b.next()) {
        const QString t = b.text().trimmed();
        if (t.isEmpty()) continue;
        any = true;
        if (!t.startsWith(QLatin1String("//"))) allCommented = false;
    }
    if (!any) return;
    UserEdit ue(*this, "toggle comment");
    for (QTextBlock b = first; b.isValid() && b.blockNumber() <= last.blockNumber(); b = b.next()) {
        const QString t = b.text();
        if (t.trimmed().isEmpty()) continue;
        int ind = 0;
        while (ind < t.size() && t.at(ind).isSpace()) ++ind;
        QTextCursor lc(b);
        lc.setPosition(b.position() + ind);
        if (allCommented) {
            int n = 2;
            if (ind + 2 < t.size() && t.at(ind + 2) == QLatin1Char(' ')) n = 3;
            lc.setPosition(b.position() + ind + n, QTextCursor::KeepAnchor);
            lc.removeSelectedText();
        } else {
            lc.insertText(QStringLiteral("// "));
        }
    }
    setTextCursor(sel);
}

void CodeEditor::duplicateLine() {
    QTextCursor sel = textCursor();
    QTextBlock first = document()->findBlock(sel.selectionStart()), last = document()->findBlock(sel.selectionEnd());
    if (sel.hasSelection() && sel.selectionEnd() == last.position()) last = last.previous();
    QStringList lines;
    for (QTextBlock b = first; b.isValid() && b.blockNumber() <= last.blockNumber(); b = b.next()) lines << b.text();
    UserEdit ue(*this, "duplicate line");
    QTextCursor c(last);
    c.movePosition(QTextCursor::EndOfBlock);
    c.insertText(QStringLiteral("\n") + lines.join(QLatin1Char('\n')));
    setTextCursor(c);
}

void CodeEditor::deleteLine() {
    QTextCursor c = textCursor();
    UserEdit ue(*this, "delete line");
    c.select(QTextCursor::LineUnderCursor);
    c.removeSelectedText();
    if (!c.atEnd()) c.deleteChar();
    else if (!c.atStart()) c.deletePreviousChar();
    setTextCursor(c);
}

void CodeEditor::insertLineAbove() {
    QTextCursor c = textCursor();
    UserEdit ue(*this, "insert line above");
    c.movePosition(QTextCursor::StartOfBlock);
    c.insertText(QStringLiteral("\n"));
    c.movePosition(QTextCursor::PreviousBlock);
    setTextCursor(c);
}

void CodeEditor::insertLineBelow() {
    QTextCursor c = textCursor();
    UserEdit ue(*this, "insert line below");
    c.movePosition(QTextCursor::EndOfBlock);
    c.insertText(QStringLiteral("\n"));
    setTextCursor(c);
}

void CodeEditor::deleteWordAtCursor() {
    QTextCursor c = textCursor();
    UserEdit ue(*this, "delete word");
    if (c.hasSelection()) {
        c.removeSelectedText();
    } else {
        c.select(QTextCursor::WordUnderCursor);
        c.removeSelectedText();
    }
    setTextCursor(c);
}

bool CodeEditor::goToMatchingBracket() {
    const BracketReport& r = bracketReport();
    Pos br, other;
    if (matchNearCursor(ed_.doc(), r, cursorPos(), br, other)) {
        const int o = offsetOf(other);
        if (o >= 0) {
            QTextCursor c = textCursor();
            c.setPosition(o);
            setTextCursor(c);
            centerCursor();
            return true;
        }
    }
    return false;
}

// ====================================================================== paste / drop

void CodeEditor::insertFromMimeData(const QMimeData* src) {
    if (src->hasUrls()) {
        QStringList files;
        for (const QUrl& u : src->urls()) if (u.isLocalFile()) files << u.toLocalFile();
        if (!files.isEmpty()) { emit filesDropped(files); return; }
    }
    if (!src->hasText()) return;
    QMimeData clean;
    clean.setText(sanitize(src->text()));
    QPlainTextEdit::insertFromMimeData(&clean);
}

void CodeEditor::contextMenuEvent(QContextMenuEvent* e) {
    QMenu m(this);
    QAction* u = m.addAction(tr("Undo"), this, [this] { engineUndo(); });
    u->setEnabled(ed_.canUndo() || typingOpen_);
    QAction* r = m.addAction(tr("Redo"), this, [this] { engineRedo(); });
    r->setEnabled(ed_.canRedo());
    m.addSeparator();
    m.addAction(tr("Cut"), this, &QPlainTextEdit::cut)->setEnabled(textCursor().hasSelection());
    m.addAction(tr("Copy"), this, &QPlainTextEdit::copy)->setEnabled(textCursor().hasSelection());
    m.addAction(tr("Paste"), this, &QPlainTextEdit::paste);
    m.addSeparator();
    m.addAction(tr("Select All"), this, &QPlainTextEdit::selectAll);
    m.addSeparator();
    m.addAction(tr("Toggle Comment\tCtrl+/"), this, [this] { toggleComment(); });
    m.addAction(tr("Duplicate Line\tCtrl+Shift+D"), this, [this] { duplicateLine(); });
    m.addAction(tr("Delete Line\tCtrl+Shift+K"), this, [this] { deleteLine(); });
    m.addAction(tr("Match Bracket\tCtrl+M"), this, [this] { goToMatchingBracket(); });
    m.addAction(tr("Indent Selection\tTab"), this, [this] { indentSelection(true); });
    m.addAction(tr("Unindent Selection\tShift+Tab"), this, [this] { indentSelection(false); });
    m.exec(e->globalPos());
}

// ====================================================================== autocomplete + snippets

bool CodeEditor::completionVisible() const { return popup_->isVisible(); }

QStringList CodeEditor::completionItems() const {
    QStringList l;
    for (int i = 0; i < popup_->count(); ++i) l << popup_->item(i)->text();
    return l;
}

void CodeEditor::showCompletions(bool force) { updateCompletion(force); }

void CodeEditor::hidePopup() { if (popup_->isVisible()) popup_->hide(); }

void CodeEditor::updateCompletion(bool force) {
    if (textCursor().hasSelection()) { hidePopup(); return; }
    try {
        const std::string pre = prefixBefore(ed_.doc(), cursorPos());
        if (pre.size() < (force ? 1u : 2u)) { hidePopup(); return; }
        const DynArray<std::string> sug = ac_.suggest(pre, 8);
        if (sug.empty()) { hidePopup(); return; }
        popup_->clear();
        for (size_t i = 0; i < sug.size(); ++i) popup_->addItem(fromStd(sug[i]));
        popup_->setCurrentRow(0);
        positionPopup();
        popup_->show();
        popup_->raise();
    } catch (const std::exception&) {
        hidePopup();
    }
}

void CodeEditor::positionPopup() {
    const int rowH = popup_->sizeHintForRow(0) > 0 ? popup_->sizeHintForRow(0) : fontMetrics().height() + 4;
    const int h = qMin(popup_->count(), 8) * rowH + 6;
    int w = 120;
    for (int i = 0; i < popup_->count(); ++i) w = qMax(w, fontMetrics().horizontalAdvance(popup_->item(i)->text()) + 30);
    const QRect r = cursorRect();
    int x = qMin(r.left(), qMax(0, viewport()->width() - w));
    int y = r.bottom() + 2;
    if (y + h > viewport()->height()) y = qMax(0, r.top() - h - 2);
    popup_->setGeometry(x, y, w, h);
}

void CodeEditor::acceptCompletion() {
    QListWidgetItem* it = popup_->currentItem();
    if (!it) { hidePopup(); return; }
    const std::string word = toStd(it->text());
    hidePopup();
    commitTyping();
    try {
        const Pos end = applyCompletion(ed_, cursorPos(), word);
        ac_.accepted(word);
        syncFromEngine();
        const int off = offsetOf(end);
        if (off >= 0) { QTextCursor c(document()); c.setPosition(off); setTextCursor(c); }
    } catch (const std::exception& ex) {
        emit message(tr("Autocomplete: %1").arg(QString::fromUtf8(ex.what())));
        syncFromEngine();
    }
}

bool CodeEditor::expandSnippetAtCursor() {
    try {
        const Pos c = cursorPos();
        Pos st;
        const std::string pre = prefixBefore(ed_.doc(), c, &st);
        if (pre.empty()) return false;
        const Snippet* sn = snips_.find(pre);
        if (!sn) return false;
        const std::string line = ed_.doc().getLine(c.line);
        std::string indent;
        for (size_t i = 0; i < st.col && i < line.size() && (line[i] == ' ' || line[i] == '\t'); ++i) indent.push_back(line[i]);
        const ExpandedSnippet ex = expandBody(sn->body, indent);
        commitTyping();
        hidePopup();
        Pos end;
        {
            EditGroup g(ed_, "snippet '" + pre + "'");
            end = ed_.replaceRange(st, c, ex.text);
        }
        syncFromEngine();
        Pos target = end;
        if (ex.hasCursor) target = ex.cursorLine == 0 ? Pos{st.line, st.col + ex.cursorCol} : Pos{st.line + ex.cursorLine, ex.cursorCol};
        const int off = offsetOf(target);
        if (off >= 0) { QTextCursor tc(document()); tc.setPosition(off); setTextCursor(tc); }
        emit message(tr("Snippet '%1' expanded.").arg(fromStd(pre)));
        return true;
    } catch (const std::exception& ex) {
        emit message(tr("Snippet: %1").arg(QString::fromUtf8(ex.what())));
        syncFromEngine();
        return false;
    }
}

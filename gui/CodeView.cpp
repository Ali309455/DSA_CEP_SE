#include "CodeView.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QPen>
#include <QPolygon>
#include <QToolTip>
#include "Theme.h"
#include "SnippetStore.h"
#include "Breadcrumbs.h"
#include "Formatter.h"
#include "Icons.h"
#include <QDir>
#include <QMenu>
#include <climits>
#include <functional>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QWheelEvent>
#include <algorithm>

// ---------------------------------------------------------------- colours (the current theme)

// colours come from the theme (Theme.h), so the whole window matches
static const QColor& kBg = Theme::Editor;
static const QColor& kCurLine = Theme::CurrentLine;
static const QColor& kSelection = Theme::Selection;
static const QColor& kFind = Theme::FindMatch;
static const QColor& kGutterNum = Theme::GutterNumber;
static const QColor& kGutterCur = Theme::GutterCurrent;
static const QColor& kCursorCol = Theme::Caret;
static const QColor& kGuide = Theme::IndentGuide;
static const QColor* const kTokenColors = Theme::Syntax;

static bool isWc(char c) { return isWordChar(c) || static_cast<unsigned char>(c) >= 0x80; }
static bool isBlankC(char c) { return c == ' ' || c == '\t'; }

// ---------------------------------------------------------------- construction

CodeView::CodeView(QWidget* parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);                 // hover on the gutter shows the fold arrows
    viewport()->setAutoFillBackground(false);
    zoom_ = prefs().fontSize;
    fontFamily_ = prefs().fontFamily;
    tabWidth_ = prefs().tabWidth;
    applyFont();
    verticalScrollBar()->setSingleStep(1);

    blink_.setInterval(530);
    connect(&blink_, &QTimer::timeout, this, [this]() {
        cursorOn_ = !cursorOn_;
        viewport()->update();
    });
    if (prefs().cursorBlink) blink_.start();
    connect(&AppSettings::instance(), &AppSettings::changed, this, &CodeView::applySettings);

    bracketTimer_.setSingleShot(true);
    connect(&bracketTimer_, &QTimer::timeout, this, [this]() { recomputeBrackets(); });
    autoTimer_.setSingleShot(true);
    autoTimer_.setInterval(70);
    connect(&autoTimer_, &QTimer::timeout, this, [this]() { showCompletions(); });

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { viewport()->update(); hidePopup(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() { viewport()->update(); hidePopup(); });

    popup_ = new QListWidget(this);
    popup_->setFocusPolicy(Qt::NoFocus);
    popup_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popup_->setStyleSheet(Theme::popupListStyle());
    connect(&Theme::notifier(), &Theme::Notifier::changed, this, [this]() {       // theme or accent changed: restyle and repaint
        popup_->setStyleSheet(Theme::popupListStyle());
        if (crumbs_) { crumbs_->setCrumbs({}); refreshBreadcrumbs(); }            // the symbol badges use theme colours
        viewport()->update();
    });
    popup_->hide();
    connect(popup_, &QListWidget::itemClicked, this, [this]() { acceptCompletion(); });

    crumbs_ = new Breadcrumbs(this);                     // sits in the top margin above the text
    connect(crumbs_, &Breadcrumbs::crumbClicked, this, &CodeView::openBreadcrumb);
    connect(this, &CodeView::stateChanged, this, &CodeView::refreshBreadcrumbs);
    connect(this, &CodeView::outlineChanged, this, &CodeView::refreshBreadcrumbs);

    lastModified_ = false;
    recomputeBrackets();
    updateGutter();
    updateScrollBars();
}

void CodeView::applyFont() {
    QFont f = Theme::codeFont(zoom_, fontFamily_);
    setFont(f);
    viewport()->setFont(f);
    QFontMetrics fm(f);
    charW_ = std::max(1, fm.horizontalAdvance(QLatin1Char('M')));
    lineH_ = fm.height() + 5;                         // a little air between lines
    updateGutter();
    updateScrollBars();
    viewport()->update();
}

void CodeView::setZoom(int points) {
    zoom_ = std::max(7, std::min(36, points));
    applyFont();
    // zoom is the font-size setting: every tab follows, and it is remembered
    AppSettings::Values v = prefs();
    if (v.fontSize != zoom_) {
        v.fontSize = zoom_;
        AppSettings::instance().apply(v);
    }
    emit stateChanged();
}

// Something in the Settings window changed: pick up what concerns the editor.
void CodeView::applySettings() {
    const AppSettings::Values& s = prefs();
    const bool fontChanged = zoom_ != s.fontSize || fontFamily_ != s.fontFamily;
    zoom_ = s.fontSize;
    fontFamily_ = s.fontFamily;
    if (tabWidth_ != s.tabWidth) { tabWidth_ = s.tabWidth; maxCols_ = 0; }
    if (s.cursorBlink) { if (!blink_.isActive()) blink_.start(); }
    else { blink_.stop(); cursorOn_ = true; }
    if (!s.autoComplete) { autoTimer_.stop(); hidePopup(); }
    invalidateRows();                                             // word wrap on / off, tab width, gutter width
    if (!s.wordWrap) maxCols_ = 0;                                // measure again for the sideways scroll bar
    if (fontChanged) applyFont();                                 // also redoes the gutter and scroll bars
    else { updateGutter(); updateScrollBars(); }
    refreshBreadcrumbs();                                         // shown or hidden
    ensureCursorVisible();
    viewport()->update();
}

// ---------------------------------------------------------------- breadcrumbs

static QString& breadcrumbRoot() { static QString root; return root; }

void CodeView::setBreadcrumbRoot(const QString& dir) { breadcrumbRoot() = dir; }

void CodeView::refreshBreadcrumbs() {
    if (!crumbs_) return;
    const bool on = prefs().breadcrumbs;
    const int top = on ? Breadcrumbs::barHeight() : 0;
    if (viewportMargins().top() != top) setViewportMargins(0, top, 0, 0);
    crumbs_->setVisible(on);
    if (!on) return;
    crumbs_->setGeometry(viewport()->x(), 0, viewport()->width(), top);

    using Crumb = Breadcrumbs::Crumb;
    QVector<Crumb> c;
    const QString path = QString::fromStdString(ed_.doc().path());
    if (path.isEmpty()) {
        Crumb f;
        f.text = "Untitled";
        f.icon = Icons::forFile("untitled.txt");
        f.kind = Breadcrumbs::File;
        c << f;
    } else {
        // inside the open folder: the path from there; otherwise the file's own folder and the file
        const QString root = breadcrumbRoot();
        QString rel = root.isEmpty() ? QString() : QDir(root).relativeFilePath(path);
        QString base = root;
        if (rel.isEmpty() || rel.startsWith("..") || QDir::isAbsolutePath(rel)) {
            const QFileInfo fi(path);
            rel = fi.dir().dirName() + "/" + fi.fileName();
            base = QFileInfo(fi.absolutePath()).absolutePath();
        }
        const QStringList parts = rel.split('/', Qt::SkipEmptyParts);
        QString acc = base;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            acc = QDir(acc).filePath(parts[i]);
            Crumb f;
            f.text = parts[i];
            f.icon = Icons::folder(false);
            f.kind = Breadcrumbs::Folder;
            f.path = acc;
            c << f;
        }
        Crumb f;
        f.text = parts.isEmpty() ? QFileInfo(path).fileName() : parts.last();
        f.icon = Icons::forFile(f.text);
        f.kind = Breadcrumbs::File;
        f.path = path;
        c << f;
    }
    // the symbols around the cursor, outermost first (symbols are in document order, so a parent comes first)
    for (size_t i = 0; i < symbols_.size(); ++i) {
        const Symbol& s = symbols_[i];
        if (s.pos.line > cur_.line || cur_.line > s.endLine) continue;
        Crumb f;
        f.text = QString::fromStdString(s.name) + (s.kind == SymbolKind::Function ? "()" : "");
        f.icon = Icons::forSymbol(s.kind);
        f.kind = Breadcrumbs::Symbol;
        f.symbol = static_cast<long>(i);
        c << f;
    }
    crumbs_->setCrumbs(c);
}

void CodeView::openBreadcrumb(int index, const QPoint& globalBelow) {
    const QVector<Breadcrumbs::Crumb>& cs = crumbs_->crumbs();
    if (index < 0 || index >= cs.size()) return;
    const Breadcrumbs::Crumb& c = cs[index];
    auto jump = [this](size_t i) {
        if (i >= symbols_.size()) return;
        goToLine(static_cast<int>(symbols_[i].pos.line) + 1);
        setCursor(symbols_[i].pos);
        setFocus();
    };
    if (c.kind == Breadcrumbs::Folder) { emit revealInExplorer(c.path); return; }
    if (c.kind == Breadcrumbs::Symbol) { jump(static_cast<size_t>(c.symbol)); return; }
    // the file: a menu of everything in it, nested like the Outline
    QMenu menu(this);
    if (symbols_.empty()) menu.addAction("No classes or functions in this file")->setEnabled(false);
    for (size_t i = 0; i < symbols_.size(); ++i) {
        const Symbol& s = symbols_[i];
        QAction* a = menu.addAction(Icons::forSymbol(s.kind), QString(static_cast<int>(s.depth) * 3, QLatin1Char(' ')) +
                                                                  QString::fromStdString(s.name + s.detail));
        a->setData(static_cast<qulonglong>(i));
    }
    if (QAction* chosen = menu.exec(globalBelow); chosen && chosen->data().isValid()) jump(chosen->data().toULongLong());
}

void CodeView::updateGutter() {
    int digits = 3;
    for (size_t n = ed_.doc().lineCount(); n >= 1000; n /= 10) ++digits;
    gutterW_ = prefs().lineNumbers ? digits * charW_ + 30 : 22;     // the last 16 pixels hold the fold arrows
}

int CodeView::visibleLines() const { return std::max(1, viewport()->height() / lineH_); }

// ---------------------------------------------------------------- rows: folding and word wrap
//
// A "row" is one line of pixels on screen. Without word wrap a visible line is one row and the
// FoldSet maps rows to lines. With word wrap a line takes as many rows as it needs, so the rows are
// kept as prefix sums:  rowStart_[l] = first row of line l  (a folded line adds 0 rows), and
// rowStart_[lineCount] = all rows. Line of a row = binary search, O(log n). The array is rebuilt in
// one O(characters) pass when the text, the folds or the width changed, and only when it is used.

bool CodeView::wrapOn() const { return prefs().wordWrap && ed_.doc().charCount() <= 3000000; }

int CodeView::wrapCols() const { return std::max(20, (viewport()->width() - textLeft() - charW_) / std::max(1, charW_)); }

// Row starts of one line (tabs already expanded). Breaks after the last space that fits, or in the
// middle of a word that is longer than a whole row. Without wrap: one row starting at column 0.
void CodeView::wrapStarts(const QString& s, std::vector<int>& st) const {
    st.assign(1, 0);
    if (!wrapOn()) return;
    const int cols = wrapCols();
    int start = 0;
    while (s.size() - start > cols) {
        int brk = -1;
        for (int j = start + cols; j > start + cols / 3; --j)
            if (s[j - 1] == QLatin1Char(' ') && s[j] != QLatin1Char(' ')) { brk = j; break; }
        if (brk < 0) brk = start + cols;
        st.push_back(brk);
        start = brk;
    }
}

int CodeView::segOf(const std::vector<int>& st, int dcol) {
    int lo = 0, hi = static_cast<int>(st.size()) - 1;      // the last row start <= dcol
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (st[static_cast<size_t>(mid)] <= dcol) lo = mid; else hi = mid - 1;
    }
    return lo;
}

void CodeView::lineSegments(size_t line, std::vector<int>& st) const { wrapStarts(expandTabs(lineText(line)), st); }

void CodeView::ensureRows() const {
    const size_t n = ed_.doc().lineCount();
    const int cols = wrapCols();
    if (!rowsDirty_ && rowsCols_ == cols && rowStart_.size() == n + 1) return;
    rowStart_.clear();
    rowStart_.reserve(n + 1);
    std::vector<int> st;
    size_t total = 0;
    for (size_t l = 0; l < n; ++l) {
        rowStart_.push_back(total);
        if (folds_.isHidden(l)) continue;
        lineSegments(l, st);
        total += st.size();
    }
    rowStart_.push_back(total);
    rowsDirty_ = false;
    rowsCols_ = cols;
}

size_t CodeView::rowOf(size_t line) const {
    if (!wrapOn()) return folds_.rowOf(line);
    ensureRows();
    const size_t n = ed_.doc().lineCount();
    if (line >= n) line = n - 1;
    if (folds_.isHidden(line)) line = folds_.lineAt(folds_.rowOf(line));    // the header above its block
    return rowStart_[line];
}

size_t CodeView::lineAtRow(size_t row) const {
    const size_t n = ed_.doc().lineCount();
    if (!wrapOn()) return std::min(folds_.lineAt(row), n - 1);
    ensureRows();
    if (row >= rowStart_[n]) row = rowStart_[n] - 1;
    size_t lo = 0, hi = n - 1;                             // the first line whose rows end after 'row'
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (rowStart_[mid + 1] <= row) lo = mid + 1; else hi = mid;
    }
    return lo;
}

size_t CodeView::visibleRowCount() const {
    if (!wrapOn()) return folds_.rowCount(ed_.doc().lineCount());
    ensureRows();
    return std::max<size_t>(1, rowStart_[ed_.doc().lineCount()]);
}

size_t CodeView::cursorRow() const {
    size_t r = rowOf(cur_.line);
    if (wrapOn()) {
        std::vector<int> st;
        lineSegments(cur_.line, st);
        r += static_cast<size_t>(segOf(st, displayColumn(cur_)));
    }
    return r;
}

QPoint CodeView::cursorPixel() const {
    int dc = displayColumn(cur_);
    if (wrapOn()) {
        std::vector<int> st;
        lineSegments(cur_.line, st);
        dc -= st[static_cast<size_t>(segOf(st, dc))];
    }
    const size_t row = cursorRow();
    const int x = textLeft() - (wrapOn() ? 0 : horizontalScrollBar()->value()) + dc * charW_;
    const int y = row >= firstVisible() ? static_cast<int>(row - firstVisible()) * lineH_ : 0;
    return QPoint(x, y);
}

// Where the cursor goes on 'row' when it wants to be 'x' columns into the row (Up / Down / Page keys).
Pos CodeView::posAtRow(size_t row, int x) const {
    const size_t line = lineAtRow(row);
    std::vector<int> st;
    lineSegments(line, st);
    const size_t first = rowOf(line);
    const int k = std::min(static_cast<int>(row > first ? row - first : 0), static_cast<int>(st.size()) - 1);
    int dc = st[static_cast<size_t>(k)] + std::max(0, x);
    if (k + 1 < static_cast<int>(st.size())) dc = std::min(dc, st[static_cast<size_t>(k) + 1] - 1);   // stay on this row
    return Pos{line, byteColFor(lineText(line), dc)};
}

void CodeView::updateScrollBars() {
    const int total = static_cast<int>(visibleRowCount());
    verticalScrollBar()->setRange(0, std::max(0, total - 1));
    verticalScrollBar()->setPageStep(visibleLines());
    const int contentW = textLeft() + (maxCols_ + 4) * charW_;
    horizontalScrollBar()->setRange(0, wrapOn() ? 0 : std::max(0, contentW - viewport()->width()));   // wrapped: nothing to the side
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setSingleStep(charW_ * 3);
}

void CodeView::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    if (crumbs_) crumbs_->setGeometry(viewport()->x(), 0, viewport()->width(), viewport()->y());
    updateScrollBars();                                    // with word wrap the width decides the rows
}

// ---------------------------------------------------------------- text helpers

size_t CodeView::prevCharCol(const std::string& line, size_t col) {
    if (col == 0) return 0;
    size_t i = std::min(col, line.size()) - 1;
    while (i > 0 && (static_cast<unsigned char>(line[i]) & 0xC0) == 0x80) --i;
    return i;
}

size_t CodeView::nextCharCol(const std::string& line, size_t col) {
    if (col >= line.size()) return line.size();
    size_t i = col + 1;
    while (i < line.size() && (static_cast<unsigned char>(line[i]) & 0xC0) == 0x80) ++i;
    return i;
}

size_t CodeView::wordLeft(const std::string& line, size_t col) {
    size_t i = std::min(col, line.size());
    while (i > 0 && isBlankC(line[i - 1])) --i;
    if (i == 0) return 0;
    if (isWc(line[i - 1])) { while (i > 0 && isWc(line[i - 1])) --i; }
    else { while (i > 0 && !isWc(line[i - 1]) && !isBlankC(line[i - 1])) --i; }
    return i;
}

size_t CodeView::wordRight(const std::string& line, size_t col) {
    size_t i = std::min(col, line.size());
    const size_t n = line.size();
    if (i < n && isWc(line[i])) { while (i < n && isWc(line[i])) ++i; }
    else if (i < n && !isBlankC(line[i])) { while (i < n && !isWc(line[i]) && !isBlankC(line[i])) ++i; }
    while (i < n && isBlankC(line[i])) ++i;
    return i;
}

int CodeView::displayCol(const std::string& line, size_t byteCol) const {
    int dc = 0;
    const size_t n = std::min(byteCol, line.size());
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if ((c & 0xC0) == 0x80) continue;
        if (c == '\t') dc += tabWidth_ - (dc % tabWidth_);
        else ++dc;
    }
    return dc;
}

size_t CodeView::byteColFor(const std::string& line, int dcol) const {
    int dc = 0;
    size_t i = 0;
    while (i < line.size()) {
        const int width = (line[i] == '\t') ? tabWidth_ - (dc % tabWidth_) : 1;
        if (dcol < dc + (width + 1) / 2) return i;
        dc += width;
        i = nextCharCol(line, i);
    }
    return line.size();
}

int CodeView::displayColumn(Pos p) const { return displayCol(lineText(p.line), p.col); }

QString CodeView::expandTabs(const std::string& line) const {
    const QString u = QString::fromUtf8(line.data(), static_cast<int>(line.size()));
    QString out;
    out.reserve(u.size() + 8);
    int dc = 0;
    for (int i = 0; i < u.size(); ++i) {
        const QChar ch = u[i];
        if (ch == QLatin1Char('\t')) {
            const int w = tabWidth_ - (dc % tabWidth_);
            out.append(QString(w, QLatin1Char(' ')));
            dc += w;
        } else {
            out.append(ch);
            if (!ch.isLowSurrogate()) ++dc;
        }
    }
    return out;
}

Pos CodeView::clamp(Pos p) const {
    const Document& d = ed_.doc();
    if (p.line >= d.lineCount()) p.line = d.lineCount() - 1;
    p.col = std::min(p.col, d.lineLength(p.line));
    return p;
}

// The line and display column under a viewport point ('shift' pixels added to x: half a character
// for clicks, so a click on the right half of a character lands after it).
void CodeView::cellAt(const QPoint& pt, int shift, size_t& line, int& dcol) const {
    const Document& d = ed_.doc();
    const int rowOnScreen = pt.y() < 0 ? 0 : pt.y() / lineH_;
    const size_t r = std::min(firstVisible() + static_cast<size_t>(rowOnScreen), visibleRowCount() - 1);
    line = std::min(lineAtRow(r), d.lineCount() - 1);
    const int x = pt.x() - textLeft() + (wrapOn() ? 0 : horizontalScrollBar()->value()) + shift;
    dcol = x < 0 ? 0 : x / charW_;
    if (wrapOn()) {                                        // which of the line's rows, then columns into that row
        std::vector<int> st;
        lineSegments(line, st);
        const size_t firstRow = rowOf(line);
        const int k = std::min(static_cast<int>(r > firstRow ? r - firstRow : 0), static_cast<int>(st.size()) - 1);
        dcol += st[static_cast<size_t>(k)];
        if (k + 1 < static_cast<int>(st.size())) dcol = std::min(dcol, st[static_cast<size_t>(k) + 1] - 1);
    }
}

Pos CodeView::pointToPos(const QPoint& pt) const {
    size_t line = 0;
    int dcol = 0;
    cellAt(pt, charW_ / 2, line, dcol);
    return Pos{line, byteColFor(lineText(line), dcol)};
}

QString CodeView::selectedText() const {
    if (!hasSelection()) return QString();
    const std::string s = ed_.doc().getRange(selStart(), selEnd());
    return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

void CodeView::lineRangeOfSelection(size_t& from, size_t& to) const {
    from = selStart().line;
    to = selEnd().line;
    if (hasSelection() && selEnd().col == 0 && to > from) --to;
}

void CodeView::ensureCursorVisible() {
    // the cursor never sits in a folded block: going there (search, Go to Line, a click on the
    // Outline, typing after Undo) opens the folds around it
    bool opened = folds_.reveal(cur_.line);
    if (anchor_.line != cur_.line) opened = folds_.reveal(anchor_.line) || opened;
    if (opened) { invalidateRows(); updateScrollBars(); }
    const int vis = visibleLines();
    const int line = static_cast<int>(cursorRow());
    QScrollBar* v = verticalScrollBar();
    if (line < v->value()) v->setValue(line);
    else if (line >= v->value() + vis - 1) v->setValue(std::max(0, line - vis + 2));
    if (wrapOn()) return;                                  // wrapped text never needs sideways scrolling
    const int x = displayColumn(cur_) * charW_;
    QScrollBar* h = horizontalScrollBar();
    const int avail = viewport()->width() - textLeft();
    if (x < h->value()) h->setValue(std::max(0, x - charW_ * 4));
    else if (x + charW_ * 2 > h->value() + avail) h->setValue(x + charW_ * 4 - avail);
}

// ---------------------------------------------------------------- document

bool CodeView::openFile(const QString& path, QString* error) {
    IOResult r = ed_.open(path.toUtf8().constData());
    if (!r.ok) {
        if (error) *error = QString::fromStdString(r.message);
        return false;
    }
    setLanguageForPath(path);
    cur_ = anchor_ = Pos{0, 0};
    wantCol_ = 0;
    maxCols_ = 0;
    blockValid_ = 0;
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    compilerDiags_.clear();
    lastLineCount_ = ed_.doc().lineCount();
    folds_.clear();
    foldRanges_.clear();
    foldLineCount_ = ed_.doc().lineCount();
    edited(0);
    return true;
}

bool CodeView::saveFile(const QString& path, QString* error) {
    IOResult r = ed_.save(path.toUtf8().constData());
    if (!r.ok) {
        if (error) *error = QString::fromStdString(r.message);
        return false;
    }
    setLanguageForPath(path);
    emit stateChanged();
    viewport()->update();
    return true;
}

void CodeView::setLanguageForPath(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    const QString name = QFileInfo(path).fileName().toLower();
    static const QSet<QString> hashExt = {"py", "sh", "bash", "cmake", "yml", "yaml", "toml", "rb", "pl", "r", "ini", "conf", "mk"};
    static const QSet<QString> plainExt = {"txt", "md", "log", "csv", "rst", ""};
    if (name == "cmakelists.txt" || name == "makefile" || hashExt.contains(ext)) { language_ = 1; comment_ = "#"; }
    else if (plainExt.contains(ext)) { language_ = 2; comment_ = "#"; }
    else { language_ = 0; comment_ = "//"; }
    blockValid_ = 0;
    viewport()->update();
}

QString CodeView::languageName() const {
    return language_ == 0 ? QStringLiteral("C / C++ / code") : language_ == 1 ? QStringLiteral("Script") : QStringLiteral("Plain Text");
}

void CodeView::setCursor(Pos p) {
    cur_ = anchor_ = clamp(p);
    wantCol_ = displayColumn(cur_);
    wantX_ = -1;
    ed_.breakTyping();
    ensureCursorVisible();
    cursorOn_ = true;
    emit stateChanged();
    viewport()->update();
}

void CodeView::select(Pos a, Pos b) {
    anchor_ = clamp(a);
    cur_ = clamp(b);
    wantCol_ = displayColumn(cur_);
    wantX_ = -1;
    ed_.breakTyping();
    ensureCursorVisible();
    cursorOn_ = true;
    emit stateChanged();
    viewport()->update();
}

void CodeView::goToLine(int line1) {
    const int n = static_cast<int>(ed_.doc().lineCount());
    const int l = std::max(1, std::min(n, line1)) - 1;
    setCursor(Pos{static_cast<size_t>(l), 0});
    verticalScrollBar()->setValue(std::max(0, static_cast<int>(rowOf(static_cast<size_t>(l))) - visibleLines() / 2));
}

// ---------------------------------------------------------------- folding

void CodeView::shiftFolds(size_t fromLine) {
    const size_t n = ed_.doc().lineCount();
    const long delta = static_cast<long>(n) - static_cast<long>(foldLineCount_);
    foldLineCount_ = n;
    if (delta != 0) folds_.linesChanged(fromLine, delta);
}

// After folding: a cursor inside a block that just closed moves to the end of the block's header.
void CodeView::moveCursorOutOfFolds() {
    const Document& d = ed_.doc();
    if (folds_.isHidden(cur_.line)) {
        const size_t h = lineAtRow(rowOf(cur_.line));
        cur_ = Pos{h, d.lineLength(h)};
    }
    if (folds_.isHidden(anchor_.line)) anchor_ = cur_;
}

void CodeView::foldsChanged() {
    moveCursorOutOfFolds();
    wantCol_ = displayColumn(cur_);
    wantX_ = -1;
    invalidateRows();
    updateScrollBars();
    ensureCursorVisible();
    emit stateChanged();
    viewport()->update();
}

bool CodeView::toggleFold(size_t line) {
    if (dirtyBrackets_) recomputeBrackets();             // the ranges must match the text as it is now
    if (line >= ed_.doc().lineCount()) return false;
    if (folds_.isFolded(line)) {
        folds_.unfold(line);
    } else {
        const FoldRange* r = foldRangeAt(foldRanges_, line);
        if (!r) r = innermostFoldRange(foldRanges_, line);
        if (!r || !folds_.fold(*r)) return false;
    }
    foldsChanged();
    return true;
}

void CodeView::foldAtCursor() {
    if (dirtyBrackets_) recomputeBrackets();
    const FoldRange* r = foldRangeAt(foldRanges_, cur_.line);
    if (!r || folds_.isFolded(r->start)) r = innermostFoldRange(foldRanges_, cur_.line);
    // on the header of a folded block, or in a block whose innermost range is folded: take the next one out
    while (r && folds_.isFolded(r->start)) {
        const FoldRange* outer = nullptr;
        for (size_t i = 0; i < foldRanges_.size() && foldRanges_[i].start < r->start; ++i)
            if (foldRanges_[i].last >= r->last && (!outer || foldRanges_[i].start > outer->start)) outer = &foldRanges_[i];
        r = outer;
    }
    if (!r) { emit statusMessage("Nothing to fold here."); return; }
    folds_.fold(*r);
    foldsChanged();
}

void CodeView::unfoldAtCursor() {
    if (folds_.unfold(cur_.line)) { foldsChanged(); return; }
    // inside an open block of a folded parent is impossible (the cursor is never hidden), so: the
    // innermost folded block whose header is this line or above it
    for (size_t i = folds_.count(); i-- > 0;) {
        const FoldRange r = folds_.folds()[i];
        if (r.start <= cur_.line && cur_.line <= r.last + 1) { folds_.unfold(r.start); foldsChanged(); return; }
    }
    emit statusMessage("Nothing folded here.");
}

void CodeView::foldAll() {
    if (dirtyBrackets_) recomputeBrackets();
    for (size_t i = 0; i < foldRanges_.size(); ++i) folds_.fold(foldRanges_[i]);
    foldsChanged();
}

void CodeView::unfoldAll() {
    if (folds_.empty()) return;
    folds_.clear();
    foldsChanged();
}

void CodeView::jumpToHistory(size_t undoSteps) {
    if (blocked()) return;
    Pos where;
    hidePopup();
    if (ed_.jumpToHistory(undoSteps, &where) == 0) return;
    cur_ = anchor_ = clamp(where);
    blockValid_ = 0;
    compilerDiags_.clear();
    shiftFolds(cur_.line);
    edited(0);
}

// Format Document: the engine formats the text and applies only the changed lines, as ONE undo step.
// The cursor stays on its line and keeps its place relative to the line's first character.
void CodeView::formatDocument() {
    if (blocked()) return;
    if (!canFormat()) {
        emit statusMessage("Format Document works on C, C++, Java, JavaScript and other languages with { } blocks.");
        return;
    }
    hidePopup();
    const auto leading = [](const std::string& s) { size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; return i; };
    const size_t line = cur_.line;
    const size_t oldLead = leading(lineText(line));
    FormatOptions opt;
    opt.indentWidth = static_cast<size_t>(tabWidth_);
    opt.tabWidth = static_cast<size_t>(tabWidth_);
    size_t changed = 0;
    try {
        changed = ::formatDocument(ed_, opt);
    } catch (const std::exception& e) {
        emit statusMessage(QString("Format Document failed: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    if (changed == 0) { emit statusMessage("Already formatted."); return; }
    const size_t l = std::min(line, ed_.doc().lineCount() - 1);
    const size_t newLead = leading(lineText(l));
    const size_t col = cur_.col <= oldLead ? newLead : cur_.col - oldLead + newLead;
    cur_ = anchor_ = clamp(Pos{l, col});
    blockValid_ = 0;
    compilerDiags_.clear();                 // line positions may have moved: the next check redraws the marks
    edited(0);
    emit statusMessage(QString("Formatted: %1 line%2 changed. Ctrl+Z undoes it.").arg(changed).arg(changed == 1 ? "" : "s"));
}

void CodeView::setSearchHighlights(const std::vector<Match>& m) {
    highlights_ = m;
    viewport()->update();
}

void CodeView::clearSearchHighlights() {
    highlights_.clear();
    viewport()->update();
}

void CodeView::notifyEdited(size_t fromLine) {
    cur_ = clamp(cur_);
    anchor_ = clamp(anchor_);
    blockValid_ = 0;
    edited(fromLine);
}

QString CodeView::firstBracketIssue() const {
    if (dirtyBrackets_ || report_.issues.empty()) return QString();
    return QString::fromStdString(report_.issues[0].message);
}

bool CodeView::firstBracketIssuePos(Pos& where) const {
    if (dirtyBrackets_ || report_.issues.empty()) return false;
    where = report_.issues[0].pos;
    return true;
}

// ---------------------------------------------------------------- problems (squiggles)

void CodeView::setCompilerDiagnostics(const std::vector<Diagnostic>& list) {
    compilerDiags_.clear();
    for (const Diagnostic& d : list)
        if (d.severity != Severity::Note) compilerDiags_.push_back(d);
    std::stable_sort(compilerDiags_.begin(), compilerDiags_.end(), [](const Diagnostic& a, const Diagnostic& b) {
        if (a.line != b.line) return a.line < b.line;
        return a.col < b.col;
    });
    emit diagnosticsChanged();
    viewport()->update();
}

std::vector<Diagnostic> CodeView::diagnostics() const {
    std::vector<Diagnostic> out = bracketDiags_;
    out.insert(out.end(), compilerDiags_.begin(), compilerDiags_.end());
    return out;
}

// Lines were added or removed by an edit that started on 'fromLine': marks below it move with
// the text, marks on the edited line are dropped (they are out of date). The next compiler
// check replaces everything anyway; this only keeps the marks near the right place meanwhile.
void CodeView::shiftDiagnostics(size_t fromLine, size_t newLineCount) {
    const long delta = static_cast<long>(newLineCount) - static_cast<long>(lastLineCount_);
    lastLineCount_ = newLineCount;
    if (compilerDiags_.empty()) return;
    const long from = static_cast<long>(fromLine);
    std::vector<Diagnostic> keep;
    keep.reserve(compilerDiags_.size());
    for (const Diagnostic& d : compilerDiags_) {
        if (d.line == 0 || (!d.file.empty() && d.file != ed_.doc().path())) { keep.push_back(d); continue; }   // not in this file: stays
        const long l0 = static_cast<long>(d.line) - 1;
        if (l0 < from) { keep.push_back(d); continue; }
        if (l0 == from) continue;
        const long moved = l0 + delta;
        if (moved <= from || moved >= static_cast<long>(newLineCount)) continue;
        Diagnostic m = d;
        m.line = static_cast<size_t>(moved) + 1;
        keep.push_back(m);
    }
    compilerDiags_.swap(keep);
}

// The squiggles that touch lines [fromLine, toLine). Brackets only while their report is current.
void CodeView::collectSquiggles(size_t fromLine, size_t toLine, std::vector<Squig>& out) const {
    const Document& doc = ed_.doc();
    auto add = [&](const Diagnostic& d, int sev) {
        if (d.line == 0 || out.size() > 3000) return;
        const size_t l = d.line - 1;
        if (l < fromLine || l >= toLine || l >= doc.lineCount()) return;
        const std::string raw = lineText(l);
        Squig s;
        s.line = l;
        s.col = std::min(d.col > 0 ? d.col - 1 : size_t(0), raw.size());
        s.len = squiggleLength(raw, s.col);
        s.sev = sev;
        s.text = QString::fromStdString(d.message) + "  [" + QString::fromStdString(d.source) + "]";
        out.push_back(s);
    };
    if (!dirtyBrackets_)
        for (const Diagnostic& d : bracketDiags_) add(d, 0);
    for (const Diagnostic& d : compilerDiags_) {
        if (!d.file.empty() && d.file != doc.path()) continue;           // a problem in an included header: Problems tab only
        add(d, d.severity == Severity::Warning ? 1 : 0);
    }
}

bool CodeView::viewportEvent(QEvent* e) {
    if (e->type() == QEvent::Leave && gutterHover_) {
        gutterHover_ = false;
        viewport()->setCursor(Qt::IBeamCursor);
        viewport()->update();
    }
    if (e->type() == QEvent::ToolTip) {
        QHelpEvent* he = static_cast<QHelpEvent*>(e);
        QString tip;
        const QPoint pt = he->pos();
        if (pt.x() >= gutterW_ && lineH_ > 0 && charW_ > 0) {
            const size_t row = firstVisible() + static_cast<size_t>(pt.y() / lineH_);
            size_t l = ed_.doc().lineCount();
            int dc = 0;
            if (row < visibleRowCount()) cellAt(pt, 0, l, dc);
            if (l < ed_.doc().lineCount()) {
                std::vector<Squig> sq;
                collectSquiggles(l, l + 1, sq);
                const std::string raw = lineText(l);
                for (const Squig& q : sq) {
                    const int a = displayCol(raw, q.col);
                    int b = displayCol(raw, std::min(raw.size(), q.col + q.len));
                    if (b <= a) b = a + 1;
                    if (dc >= a && dc < b) {
                        if (!tip.isEmpty()) tip += "\n";
                        tip += QString(q.sev == 0 ? "error: " : "warning: ") + q.text;
                    }
                }
            }
        }
        if (tip.isEmpty()) { QToolTip::hideText(); e->ignore(); }
        else QToolTip::showText(he->globalPos(), tip, viewport());
        return true;
    }
    return QAbstractScrollArea::viewportEvent(e);
}

// ---------------------------------------------------------------- snippets

bool CodeView::snippetAtCursor() const {
    if (hasSelection()) return false;
    try {
        const std::string w = prefixBefore(ed_.doc(), cur_);
        if (w.empty() || w.size() > SnippetTable::kMaxTrigger) return false;
        return SnippetStore::instance().table().find(w) != nullptr;
    } catch (const std::exception&) {
        return false;
    }
}

bool CodeView::tryExpandSnippet() {
    if (blocked()) return false;
    snippetMsg_.clear();
    if (hasSelection()) { snippetMsg_ = "Clear the selection first to expand a snippet."; return false; }
    hidePopup();
    SnippetExpansion r;
    try {
        r = applySnippet(ed_, SnippetStore::instance().table(), cur_);
    } catch (const std::exception& ex) {
        snippetMsg_ = QString::fromUtf8(ex.what());
        return false;
    }
    if (!r.expanded) { snippetMsg_ = QString::fromStdString(r.reason); return false; }
    const size_t from = cur_.line;
    cur_ = anchor_ = clamp(r.cursor);
    blockValid_ = 0;
    edited(from);
    emit statusMessage("Snippet \"" + QString::fromStdString(r.trigger) + "\" expanded.");
    return true;
}

void CodeView::expandSnippet() {
    if (blocked()) return;
    if (!tryExpandSnippet()) emit statusMessage(snippetMsg_);
}

void CodeView::edited(size_t fromLine) {
    ++revision_;
    shiftDiagnostics(fromLine, ed_.doc().lineCount());
    shiftFolds(fromLine);
    dirtyBrackets_ = true;
    bracketTimer_.start(80);
    blockValid_ = std::min(blockValid_, fromLine + 1);
    wantCol_ = displayColumn(cur_);
    wantX_ = -1;
    invalidateRows();                                      // word wrap: line lengths changed
    updateGutter();
    updateScrollBars();
    ensureCursorVisible();
    cursorOn_ = true;
    emit textEdited();
    emit stateChanged();
    viewport()->update();
}

void CodeView::recomputeBrackets() {
    if (ed_.doc().charCount() > 4000000) report_ = BracketReport();
    else report_ = checkBrackets(ed_.doc());
    dirtyBrackets_ = false;
    bracketDiags_.clear();
    {
        const DynArray<Diagnostic> b = fromBrackets(report_);
        for (size_t i = 0; i < b.size(); ++i) bracketDiags_.push_back(b[i]);
    }
    // fold ranges and outline come from the same scan: brackets for code, indentation for scripts
    const Document& doc = ed_.doc();
    if (language_ == 0) {
        foldRanges_ = foldRangesFromBrackets(report_, doc.lineCount());
        symbols_ = outlineSymbols(doc, report_);
    } else if (language_ == 1 && doc.charCount() <= 4000000) {
        foldRanges_ = foldRangesByIndent(doc, static_cast<size_t>(tabWidth_));
        symbols_ = outlineScriptSymbols(doc, static_cast<size_t>(tabWidth_));
    } else {
        foldRanges_.clear();
        symbols_.clear();
    }
    foldLineCount_ = doc.lineCount();
    const size_t hiddenBefore = folds_.hiddenLines();
    folds_.keepOnly(foldRanges_);
    if (folds_.hiddenLines() != hiddenBefore) { moveCursorOutOfFolds(); invalidateRows(); updateScrollBars(); }
    emit outlineChanged();
    emit diagnosticsChanged();
    emit stateChanged();
    viewport()->update();
}

// ---------------------------------------------------------------- highlighting

static const QSet<QString>& keywordSet() {
    static const QSet<QString> s = {
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "goto",
        "class", "struct", "union", "enum", "namespace", "template", "typename", "using", "typedef", "public",
        "private", "protected", "virtual", "override", "final", "static", "const", "constexpr", "volatile",
        "inline", "explicit", "friend", "operator", "new", "delete", "try", "catch", "throw", "noexcept",
        "this", "sizeof", "extern", "mutable", "register", "static_cast",
        "dynamic_cast", "reinterpret_cast", "const_cast", "decltype", "import", "from", "def", "lambda", "elif",
        "pass", "function", "var", "let", "async", "await", "package", "interface", "self",
        "extends", "implements", "fn", "pub", "impl", "match", "mod", "in", "is", "not", "and", "or"};
    return s;
}

// Literal values: their own colour (the "constant" syntax colour), like ALL_CAPS names.
static const QSet<QString>& constantSet() {
    static const QSet<QString> s = {"true", "false", "nullptr", "NULL", "None", "True", "False", "null", "undefined", "nil"};
    return s;
}

// MAX_SIZE, EOF, PI: an upper-case name of 2+ characters with no lower-case letters.
static bool isAllCaps(const QString& w) {
    if (w.size() < 2) return false;
    bool letter = false;
    for (const QChar ch : w) {
        if (ch.isLower()) return false;
        letter = letter || ch.isUpper();
    }
    return letter;
}

static bool isOperatorChar(QChar c) {
    switch (c.unicode()) {
        case '+': case '-': case '*': case '/': case '%': case '=': case '<': case '>':
        case '!': case '&': case '|': case '^': case '~': case '?': case ':':
            return true;
        default:
            return false;
    }
}

static const QSet<QString>& typeSet() {
    static const QSet<QString> s = {
        "int", "char", "void", "bool", "double", "float", "long", "short", "unsigned", "signed", "auto",
        "size_t", "string", "vector", "map", "set", "std", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "int8_t", "int16_t", "int32_t", "int64_t", "wchar_t", "ptrdiff_t", "QString", "QWidget", "String",
        "boolean", "byte", "u8", "u16", "u32", "u64", "i32", "i64", "f32", "f64", "usize"};
    return s;
}

void CodeView::tokenize(const QString& s, bool& inBlock, std::vector<Span>& out) const {
    out.clear();
    if (language_ == 2) return;
    const int n = s.size();
    int i = 0;
    if (inBlock && language_ == 0) {
        const int e = s.indexOf(QLatin1String("*/"));
        if (e < 0) { out.push_back({0, n, cComment}); return; }
        out.push_back({0, e + 2, cComment});
        i = e + 2;
        inBlock = false;
    }
    bool onlyBlanksBefore = true;
    for (int k = 0; k < i; ++k) if (!s[k].isSpace()) onlyBlanksBefore = false;
    while (i < n) {
        const QChar c = s[i];
        if (language_ == 0 && c == QLatin1Char('/') && i + 1 < n && s[i + 1] == QLatin1Char('/')) {
            out.push_back({i, n - i, cComment});
            return;
        }
        if (language_ == 1 && c == QLatin1Char('#')) {
            out.push_back({i, n - i, cComment});
            return;
        }
        if (language_ == 0 && c == QLatin1Char('/') && i + 1 < n && s[i + 1] == QLatin1Char('*')) {
            const int e = s.indexOf(QLatin1String("*/"), i + 2);
            if (e < 0) { out.push_back({i, n - i, cComment}); inBlock = true; return; }
            out.push_back({i, e + 2 - i, cComment});
            i = e + 2;
            onlyBlanksBefore = false;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || (c == QLatin1Char('`') && language_ != 2)) {
            int j = i + 1;
            while (j < n && s[j] != c) { if (s[j] == QLatin1Char('\\')) ++j; ++j; }
            const int end = std::min(j + 1, n);
            out.push_back({i, end - i, cString});
            i = end;
            onlyBlanksBefore = false;
            continue;
        }
        if (language_ == 0 && c == QLatin1Char('#') && onlyBlanksBefore) {
            out.push_back({i, n - i, cPreproc});
            return;
        }
        if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && s[i + 1].isDigit())) {
            int j = i + 1;
            while (j < n && (s[j].isLetterOrNumber() || s[j] == QLatin1Char('.') ||
                             (s[j] == QLatin1Char('\'') && j + 1 < n && s[j + 1].isLetterOrNumber()))) ++j;
            out.push_back({i, j - i, cNumber});
            i = j;
            onlyBlanksBefore = false;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i + 1;
            while (j < n && (s[j].isLetterOrNumber() || s[j] == QLatin1Char('_'))) ++j;
            const QString w = s.mid(i, j - i);
            int k = j;                                       // what follows the name decides "function call"
            while (k < n && s[k] == QLatin1Char(' ')) ++k;
            if (keywordSet().contains(w)) out.push_back({i, j - i, cKeyword});
            else if (typeSet().contains(w)) out.push_back({i, j - i, cType});
            else if (constantSet().contains(w) || isAllCaps(w)) out.push_back({i, j - i, cConstant});
            else if (k < n && s[k] == QLatin1Char('(')) out.push_back({i, j - i, cFunction});   // foo(...)
            else if (w[0].isUpper() && language_ == 0) out.push_back({i, j - i, cType});        // class names: CodeView, Pos
            i = j;
            onlyBlanksBefore = false;
            continue;
        }
        if (language_ != 2 && isOperatorChar(c)) {           // + - * / = < > ! & | ^ ~ ? : and runs of them (->, ::, <=)
            int j = i + 1;
            while (j < n && isOperatorChar(s[j])) ++j;
            out.push_back({i, j - i, cOperator});
            i = j;
            onlyBlanksBefore = false;
            continue;
        }
        if (!c.isSpace()) onlyBlanksBefore = false;
        ++i;
    }
}

void CodeView::ensureBlockState(size_t upTo) {
    const size_t need = ed_.doc().lineCount() + 1;
    if (blockAtStart_.size() < need) blockAtStart_.resize(need, 0);
    if (language_ != 0) { blockAtStart_[upTo] = 0; return; }
    if (blockValid_ == 0) { blockAtStart_[0] = 0; blockValid_ = 1; }
    std::vector<Span> spans;
    while (blockValid_ <= upTo && blockValid_ < need) {
        const size_t l = blockValid_ - 1;
        bool st = blockAtStart_[l] != 0;
        const std::string raw = lineText(l);
        tokenize(QString::fromUtf8(raw.data(), static_cast<int>(raw.size())), st, spans);
        blockAtStart_[blockValid_] = st ? 1 : 0;
        ++blockValid_;
    }
}

// ---------------------------------------------------------------- painting

void CodeView::paintEvent(QPaintEvent*) {
    QPainter p(viewport());
    const Document& doc = ed_.doc();
    const int vw = viewport()->width();
    const int vh = viewport()->height();
    p.fillRect(viewport()->rect(), kBg);
    p.setFont(font());
    const QFontMetrics fm(font());
    const int ascent = fm.ascent() + (lineH_ - fm.height()) / 2;

    // rows on screen, and the document lines they show (folded blocks are skipped; with word wrap
    // one line can take several rows, and the first line may start above the top edge)
    const size_t rows = visibleRowCount();
    const size_t firstRow = std::min(firstVisible(), rows - 1);
    const size_t lastRow = std::min(rows, firstRow + static_cast<size_t>(visibleLines()) + 2);
    const size_t first = lineAtRow(firstRow);
    const size_t last = std::min(doc.lineCount(), lineAtRow(lastRow - 1) + 1);
    const int xOff = wrapOn() ? 0 : horizontalScrollBar()->value();
    const int left = textLeft() - xOff;
    ensureBlockState(first);
    bool inBlock = first < blockAtStart_.size() && blockAtStart_[first] != 0;

    Pos b1, b2;
    bool haveBracket = false;
    if (!dirtyBrackets_ && hasFocus() && prefs().matchBrackets) haveBracket = matchNearCursor(doc, report_, cur_, b1, b2);

    const Pos ss = selStart(), se = selEnd();
    const bool sel = hasSelection();
    size_t hi = 0;                                   // first search highlight that can reach 'first'
    {
        size_t lo = 0, up = highlights_.size();
        while (lo < up) {
            size_t mid = (lo + up) / 2;
            if (highlights_[mid].end.line < first) lo = mid + 1; else up = mid;
        }
        hi = lo;
    }

    std::vector<Squig> squigs;
    collectSquiggles(first, last, squigs);

    std::vector<Span> spans;
    std::vector<int> st;                               // where each row of the current line starts (display columns)
    std::vector<std::pair<size_t, int>> shown;         // (line, y of its first row), for the gutter
    const size_t n = doc.lineCount();
    size_t prevLine = first;
    size_t l = first;
    int y = (static_cast<int>(rowOf(first)) - static_cast<int>(firstRow)) * lineH_;
    while (l < n && y < vh) {
        if (l != first && l != prevLine + 1) {               // jumped over a folded block: its comment state
            ensureBlockState(l);
            inBlock = l < blockAtStart_.size() && blockAtStart_[l] != 0;
        }
        prevLine = l;
        const std::string raw = lineText(l);
        const QString s = expandTabs(raw);
        if (!wrapOn() && s.size() > maxCols_) { maxCols_ = s.size(); updateScrollBars(); }
        wrapStarts(s, st);
        const int segs = static_cast<int>(st.size());
        shown.push_back({l, y});

        // display column -> pixel; a range of columns -> one rectangle per row it covers
        auto segOfCol = [&](int dc) { return segOf(st, dc); };
        auto xAt = [&](int dc) { const int k = segOfCol(dc); return left + (dc - st[static_cast<size_t>(k)]) * charW_; };
        auto yAt = [&](int dc) { return y + segOfCol(dc) * lineH_; };
        // calls fn(c0, c1, x0, x1, y) for every row that columns [a, b) touch: the part c0..c1 on that row
        auto forRows = [&](int a, int b, const std::function<void(int, int, int, int, int)>& fn) {
            for (int k = 0; k < segs; ++k) {
                const int s0 = st[static_cast<size_t>(k)], s1 = (k + 1 < segs) ? st[static_cast<size_t>(k) + 1] : INT_MAX;
                const int c0 = std::max(a, s0), c1 = std::min(b, s1);
                if (c0 >= c1) continue;
                fn(c0, c1, left + (c0 - s0) * charW_, left + (c1 - s0) * charW_, y + k * lineH_);
            }
        };

        if (l == cur_.line && !sel && prefs().highlightLine) p.fillRect(gutterW_, y, vw - gutterW_, lineH_ * segs, kCurLine);

        for (size_t k = hi; k < highlights_.size() && highlights_[k].start.line <= l; ++k) {
            const Match& m = highlights_[k];
            if (m.end.line < l) continue;
            const int a = (m.start.line == l) ? displayCol(raw, m.start.col) : 0;
            const int b = (m.end.line == l) ? displayCol(raw, m.end.col) : static_cast<int>(s.size());
            forRows(a, std::max(a + 1, b), [&](int, int, int x0, int x1, int yy) { p.fillRect(x0, yy, std::max(charW_, x1 - x0), lineH_, kFind); });
        }

        if (sel && l >= ss.line && l <= se.line) {
            const int a = (l == ss.line) ? displayCol(raw, ss.col) : 0;
            const int b = (l == se.line) ? displayCol(raw, se.col) : static_cast<int>(s.size()) + 1;
            if (b > a) forRows(a, b, [&](int, int, int x0, int x1, int yy) { p.fillRect(x0, yy, x1 - x0, lineH_, kSelection); });
        }

        int lead = 0;                                // indentation guides (first row of the line)
        while (lead < s.size() && s[lead] == QLatin1Char(' ')) ++lead;
        p.setPen(kGuide);
        for (int c = tabWidth_; prefs().indentGuides && c < lead && c < (segs > 1 ? st[1] : INT_MAX); c += tabWidth_) {
            const int gx = left + c * charW_;
            p.drawLine(gx, y, gx, y + lineH_);
        }

        tokenize(s, inBlock, spans);
        int pos = 0;
        auto drawRun = [&](int a, int len, int color) {
            if (len <= 0) return;
            forRows(a, a + len, [&](int c0, int c1, int x0, int, int yy) {
                const QString t = s.mid(c0, c1 - c0);
                if (t.trimmed().isEmpty()) return;
                p.setPen(kTokenColors[color]);
                p.drawText(x0, yy + ascent, t);
            });
        };
        for (size_t k = 0; k < spans.size(); ++k) {
            drawRun(pos, spans[k].start - pos, cText);
            drawRun(spans[k].start, spans[k].len, spans[k].color);
            pos = spans[k].start + spans[k].len;
        }
        drawRun(pos, s.size() - pos, cText);

        if (folds_.isFolded(l)) {                        // a folded header: a small "⋯" pill after the text
            const int endCol = static_cast<int>(s.size());
            const QRect pill(xAt(endCol) + charW_, yAt(endCol) + 2, charW_ * 3, lineH_ - 4);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::Keycap);
            p.drawRoundedRect(pill, 4, 4);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(Theme::Muted);
            p.drawText(pill, Qt::AlignCenter, QStringLiteral("⋯"));
        }

        for (const Squig& q : squigs) {                 // red = error, yellow = warning
            if (q.line != l) continue;
            const int a = displayCol(raw, q.col);
            int b = displayCol(raw, std::min(raw.size(), q.col + q.len));
            if (b <= a) b = a + 1;
            p.setPen(QPen(q.sev == 0 ? Theme::Error : Theme::Warning, 1));
            forRows(a, b, [&](int, int, int x0, int x1, int yy) {
                const int base = yy + lineH_ - 2;
                QPolygon zig;
                bool up = false;
                for (int x = x0; x <= std::max(x1, x0 + charW_); x += 2) { zig << QPoint(x, up ? base - 2 : base); up = !up; }
                if (zig.size() >= 2) p.drawPolyline(zig);
            });
        }

        if (haveBracket) {
            const Pos* pts[2] = {&b1, &b2};
            for (int k = 0; k < 2; ++k) {
                if (pts[k]->line != l) continue;
                const int dc = displayCol(raw, pts[k]->col);
                const int bx = xAt(dc), by = yAt(dc);
                p.fillRect(bx, by, charW_, lineH_, QColor(0x88, 0x88, 0x88, 70));
                p.setPen(Theme::Muted);
                p.drawRect(bx, by, charW_ - 1, lineH_ - 1);
            }
        }

        if (hasFocus() && cursorOn_ && l == cur_.line) {
            const int dc = displayCol(raw, cur_.col);
            const int cx = xAt(dc), cy = yAt(dc);
            const QColor caret = readOnly_ ? Theme::Dim : kCursorCol;          // read-only: a grey caret, nothing can be typed
            switch (prefs().cursorStyle) {
                case 1: { QColor c = caret; c.setAlpha(150); p.fillRect(cx, cy, charW_, lineH_, c); break; }   // block: the character shows through
                case 2: p.fillRect(cx, cy + lineH_ - 3, charW_, 2, caret); break;                               // underline
                default: p.fillRect(cx, cy, 2, lineH_, caret); break;                                           // line
            }
        }

        y += segs * lineH_;
        const size_t next = folds_.nextVisible(l, n);
        if (next <= l) break;
        l = next;
    }

    // line-number gutter, drawn last so horizontal scrolling slides under it
    p.fillRect(0, 0, gutterW_, vh, kBg);
    // (no divider line: the gutter and the text share one surface, like modern editors)
    for (const auto& ly : shown) {
        if (!prefs().lineNumbers) break;
        p.setPen(ly.first == cur_.line ? kGutterCur : kGutterNum);
        p.drawText(QRect(0, ly.second, gutterW_ - 18, lineH_), Qt::AlignRight | Qt::AlignVCenter, QString::number(ly.first + 1));
    }
    // fold arrows: always on folded blocks, on open blocks while the mouse is over the gutter
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& ly : shown) {
        const size_t fl = ly.first;
        const bool folded = folds_.isFolded(fl);
        if (!folded && !(gutterHover_ && canFold(fl))) continue;
        const qreal cx = foldMarkerLeft() + 7.5, cy = ly.second + lineH_ / 2.0;
        p.setPen(QPen(folded ? kGutterCur : kGutterNum, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPolygonF arrow;
        if (folded) arrow << QPointF(cx - 1.5, cy - 4) << QPointF(cx + 2.5, cy) << QPointF(cx - 1.5, cy + 4);   // >
        else arrow << QPointF(cx - 4, cy - 1.5) << QPointF(cx, cy + 2.5) << QPointF(cx + 4, cy - 1.5);           // v
        p.drawPolyline(arrow);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    if (!squigs.empty()) {                              // a dot in the gutter on every line with a problem
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        for (int sev = 1; sev >= 0; --sev) {            // warnings first, so an error on the same line wins
            p.setBrush(sev == 0 ? Theme::Error : Theme::Warning);
            for (const Squig& q : squigs) {
                if (q.sev != sev) continue;
                for (const auto& ly : shown)
                    if (ly.first == q.line) p.drawEllipse(QRectF(3, ly.second + (lineH_ - 7) / 2.0, 7, 7));
            }
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }
}

// ---------------------------------------------------------------- editing primitives

void CodeView::deleteSelection() {
    if (blocked()) return;
    if (!hasSelection()) return;
    const Pos a = selStart(), b = selEnd();
    ed_.deleteRange(a, b);
    cur_ = anchor_ = a;
    edited(a.line);
}

void CodeView::insertTypedText(const std::string& s) {
    if (blocked()) return;
    if (s.empty()) return;
    const size_t from = selStart().line;
    if (hasSelection()) cur_ = ed_.replaceRange(selStart(), selEnd(), s);
    else cur_ = ed_.insertText(cur_, s);
    anchor_ = cur_;
    edited(from);
}

void CodeView::typeChar(char c) {
    if (blocked()) return;
    const Document& doc = ed_.doc();
    const char closer = pairCloser(c);
    const bool opener = closer != 0;
    const size_t from = selStart().line;

    if (hasSelection()) {
        // wrap the selection: "text" (text) {text} ... ; the same bracket / quote again removes the pair
        bool changed = false;
        if (prefs().wrapSelection && (opener || isWrappedBy(doc, anchor_, cur_, c))) {
            SelRange r = toggleSurround(ed_, anchor_, cur_, c, &changed);
            anchor_ = r.start;
            cur_ = r.end;
            edited(from);
        } else {
            insertTypedText(std::string(1, c));
        }
        return;
    }

    const std::string line = lineText(cur_.line);
    const char next = cur_.col < line.size() ? line[cur_.col] : 0;
    if (prefs().autoPairs && (c == ')' || c == ']' || c == '}' || c == '"' || c == '\'' || c == '`') && next == c) {
        cur_.col += 1;                                  // type over the closing character
        anchor_ = cur_;
        ed_.breakTyping();
        edited(cur_.line);
        return;
    }
    if (opener && prefs().autoPairs && shouldAutoPair(doc, cur_, c)) {
        ed_.insertText(cur_, std::string{c, closer});
        cur_.col += 1;
        anchor_ = cur_;
        edited(cur_.line);
        return;
    }
    cur_ = ed_.typeText(cur_, std::string(1, c));
    anchor_ = cur_;
    edited(cur_.line);
}

void CodeView::backspace(bool word) {
    if (blocked()) return;
    if (hasSelection()) { deleteSelection(); return; }
    const Pos p = cur_;
    if (p.col == 0) {
        if (p.line == 0) return;
        const Pos a{p.line - 1, ed_.doc().lineLength(p.line - 1)};
        ed_.eraseCoalescing(a, p);
        cur_ = anchor_ = a;
        edited(a.line);
        return;
    }
    const std::string line = lineText(p.line);
    size_t from;
    if (word) {
        from = wordLeft(line, p.col);
    } else if (prefs().autoPairs && isEmptyPairAround(ed_.doc(), p)) {
        ed_.deleteRange(Pos{p.line, p.col - 1}, Pos{p.line, p.col + 1});
        cur_.col -= 1;
        anchor_ = cur_;
        edited(p.line);
        return;
    } else if (line.find_first_not_of(' ') == std::string::npos || line.find_first_not_of(' ') >= p.col) {
        from = ((p.col - 1) / static_cast<size_t>(tabWidth_)) * static_cast<size_t>(tabWidth_);   // back to the previous tab stop
    } else {
        from = prevCharCol(line, p.col);
    }
    ed_.eraseCoalescing(Pos{p.line, from}, p);
    cur_ = anchor_ = Pos{p.line, from};
    edited(p.line);
}

void CodeView::deleteForward(bool word) {
    if (blocked()) return;
    if (hasSelection()) { deleteSelection(); return; }
    const Pos p = cur_;
    const std::string line = lineText(p.line);
    if (p.col >= line.size()) {
        if (p.line + 1 >= ed_.doc().lineCount()) return;
        ed_.eraseCoalescing(p, Pos{p.line + 1, 0});
    } else {
        const size_t to = word ? wordRight(line, p.col) : nextCharCol(line, p.col);
        ed_.eraseCoalescing(p, Pos{p.line, to});
    }
    anchor_ = cur_;
    edited(p.line);
}

void CodeView::pressEnter() {
    if (blocked()) return;
    const size_t from = selStart().line;
    {
        EditGroup g(ed_, "new line");
        if (hasSelection()) {
            const Pos a = selStart();
            ed_.deleteRange(a, selEnd());
            cur_ = a;
        }
        cur_ = insertNewLineSmart(ed_, cur_, static_cast<size_t>(tabWidth_));
    }
    anchor_ = cur_;
    edited(from);
}

void CodeView::pressTab(bool back) {
    if (blocked()) return;
    if (back) { outdentSelection(); return; }
    if (hasSelection() && selStart().line != selEnd().line) { indentSelection(); return; }
    if (tryExpandSnippet()) return;                   // "for" + Tab -> the for-loop snippet
    const int n = tabWidth_ - (displayColumn(selStart()) % tabWidth_);
    insertTypedText(std::string(static_cast<size_t>(n), ' '));
}

void CodeView::adjustBoth(const DynArray<ColEdit>& edits) {
    cur_ = adjustPos(cur_, edits);
    anchor_ = adjustPos(anchor_, edits);
}

// ---------------------------------------------------------------- commands (slots)

void CodeView::undo() {
    if (blocked()) return;
    std::string label;
    Pos where;
    hidePopup();
    if (!ed_.undo(&label, &where)) return;
    cur_ = anchor_ = clamp(where);
    blockValid_ = 0;
    compilerDiags_.clear();                 // the edit position is unknown: the next check redraws the marks
    shiftFolds(cur_.line);                  // near enough; the next bracket report puts the folds right
    edited(0);
}

void CodeView::redo() {
    if (blocked()) return;
    std::string label;
    Pos where;
    hidePopup();
    if (!ed_.redo(&label, &where)) return;
    cur_ = anchor_ = clamp(where);
    blockValid_ = 0;
    compilerDiags_.clear();
    shiftFolds(cur_.line);
    edited(0);
}

// Text that was copied as a whole line (copy / cut with nothing selected). Pasting it with
// nothing selected puts it ABOVE the cursor line instead of splitting the line at the cursor.
static QString g_wholeLineClip;

void CodeView::copy() {
    QString t = selectedText();
    if (t.isEmpty()) {
        const std::string line = lineText(cur_.line);
        t = QString::fromUtf8(line.data(), static_cast<int>(line.size())) + QLatin1Char('\n');   // no selection: the whole line
        g_wholeLineClip = t;
    } else {
        g_wholeLineClip.clear();
    }
    QGuiApplication::clipboard()->setText(t);
}

void CodeView::cut() {
    copy();
    if (blocked()) return;
    if (hasSelection()) deleteSelection();
    else deleteLine();
}

void CodeView::paste() {
    if (blocked()) return;
    QString t = QGuiApplication::clipboard()->text();
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (t.isEmpty()) return;
    if (!hasSelection() && t == g_wholeLineClip) {      // a copied whole line goes above the cursor line
        const size_t from = cur_.line;
        ed_.insertText(Pos{cur_.line, 0}, t.toUtf8().toStdString());
        cur_.line += static_cast<size_t>(t.count(QLatin1Char('\n')));
        anchor_ = cur_;
        edited(from);
        return;
    }
    insertTypedText(t.toUtf8().toStdString());
}

void CodeView::selectAll() {
    const Document& d = ed_.doc();
    anchor_ = Pos{0, 0};
    cur_ = Pos{d.lineCount() - 1, d.lineLength(d.lineCount() - 1)};
    ed_.breakTyping();
    emit stateChanged();
    viewport()->update();
}

void CodeView::selectLine() {
    size_t from, to;
    lineRangeOfSelection(from, to);
    const Document& d = ed_.doc();
    anchor_ = Pos{from, 0};
    cur_ = (to + 1 < d.lineCount()) ? Pos{to + 1, 0} : Pos{to, d.lineLength(to)};
    ed_.breakTyping();
    ensureCursorVisible();
    emit stateChanged();
    viewport()->update();
}

void CodeView::moveLinesUp() {
    if (blocked()) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    if (!::moveLines(ed_, from, to, -1)) return;
    cur_.line -= 1;
    anchor_.line -= 1;
    edited(from - 1);
}

void CodeView::moveLinesDown() {
    if (blocked()) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    if (!::moveLines(ed_, from, to, +1)) return;
    cur_.line += 1;
    anchor_.line += 1;
    edited(from);
}

void CodeView::copyLinesDown() {
    if (blocked()) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    duplicateLines(ed_, from, to);
    cur_.line += to - from + 1;                         // the cursor follows the new copy
    anchor_.line += to - from + 1;
    edited(from);
}

void CodeView::copyLinesUp() {
    if (blocked()) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    duplicateLines(ed_, from, to);                      // the cursor stays on the upper copy
    edited(from);
}

void CodeView::deleteLine() {
    if (blocked()) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    ed_.deleteLines(from, to);
    const Document& d = ed_.doc();
    const size_t l = std::min(from, d.lineCount() - 1);
    cur_ = anchor_ = Pos{l, byteColFor(lineText(l), wantCol_)};
    edited(from);
}

void CodeView::toggleComment() {
    if (blocked()) return;
    if (language_ == 2) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    DynArray<ColEdit> edits = toggleLineComment(ed_, from, to, comment_);
    if (edits.empty()) return;
    adjustBoth(edits);
    edited(from);
}

void CodeView::indentSelection() {
    if (blocked()) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    DynArray<ColEdit> edits = indentLines(ed_, from, to, static_cast<size_t>(tabWidth_));
    if (edits.empty()) return;
    adjustBoth(edits);
    edited(from);
}

void CodeView::outdentSelection() {
    if (blocked()) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    DynArray<ColEdit> edits = outdentLines(ed_, from, to, static_cast<size_t>(tabWidth_));
    if (edits.empty()) return;
    adjustBoth(edits);
    edited(from);
}

void CodeView::newLineBelow() {
    if (blocked()) return;
    const size_t l = cur_.line;
    cur_ = anchor_ = insertLineBelow(ed_, l, static_cast<size_t>(tabWidth_));
    edited(l);
}

void CodeView::newLineAbove() {
    if (blocked()) return;
    const size_t l = cur_.line;
    cur_ = anchor_ = insertLineAbove(ed_, l, static_cast<size_t>(tabWidth_));
    edited(l);
}

// ---------------------------------------------------------------- cursor movement

// The column Up / Down aim for, counted from the start of the cursor's row. Without wrap that is
// the remembered column. With wrap it is taken from the cursor the first time and then kept.
int CodeView::wantInRow() {
    if (!wrapOn()) return wantCol_;
    if (wantX_ < 0) {
        std::vector<int> st;
        lineSegments(cur_.line, st);
        const int dc = displayColumn(cur_);
        wantX_ = dc - st[static_cast<size_t>(segOf(st, dc))];
    }
    return wantX_;
}

void CodeView::afterMove(bool extend, bool keepWantCol) {
    if (!extend) anchor_ = cur_;
    if (!keepWantCol) { wantCol_ = displayColumn(cur_); wantX_ = -1; }
    ed_.breakTyping();
    ensureCursorVisible();
    cursorOn_ = true;
    hidePopup();
    emit stateChanged();
    viewport()->update();
}

void CodeView::moveCursor(QKeyEvent* e) {
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const Document& d = ed_.doc();
    bool keepWant = false;
    switch (e->key()) {
        case Qt::Key_Left:
            if (hasSelection() && !shift && !ctrl) { cur_ = selStart(); break; }
            if (cur_.col == 0) {
                const size_t up = folds_.prevVisible(cur_.line);          // over a folded block, not into it
                if (up < cur_.line) cur_ = Pos{up, d.lineLength(up)};
            }
            else cur_.col = ctrl ? wordLeft(lineText(cur_.line), cur_.col) : prevCharCol(lineText(cur_.line), cur_.col);
            break;
        case Qt::Key_Right: {
            if (hasSelection() && !shift && !ctrl) { cur_ = selEnd(); break; }
            const std::string line = lineText(cur_.line);
            if (cur_.col >= line.size()) {
                const size_t down = folds_.nextVisible(cur_.line, d.lineCount());
                if (down > cur_.line) cur_ = Pos{down, 0};
            }
            else cur_.col = ctrl ? wordRight(line, cur_.col) : nextCharCol(line, cur_.col);
            break;
        }
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const bool up = e->key() == Qt::Key_Up;
            keepWant = true;
            // by rows on screen: a folded block is one step, a wrapped line is several
            const size_t row = cursorRow();
            if (up && row == 0) { cur_.col = 0; keepWant = false; break; }
            if (!up && row + 1 >= visibleRowCount()) { cur_.col = d.lineLength(cur_.line); keepWant = false; break; }
            cur_ = posAtRow(up ? row - 1 : row + 1, wantInRow());
            break;
        }
        case Qt::Key_PageUp:
        case Qt::Key_PageDown: {
            const bool up = e->key() == Qt::Key_PageUp;
            const size_t step = static_cast<size_t>(std::max(1, visibleLines() - 1));
            keepWant = true;
            const size_t row = cursorRow();
            const int want = wantInRow();
            cur_ = posAtRow(up ? (row > step ? row - step : 0) : std::min(row + step, visibleRowCount() - 1), want);
            verticalScrollBar()->setValue(verticalScrollBar()->value() + (up ? -1 : 1) * static_cast<int>(step));
            break;
        }
        case Qt::Key_Home:
            if (ctrl) cur_ = Pos{0, 0};
            else cur_.col = smartHomeCol(lineText(cur_.line), cur_.col);
            break;
        case Qt::Key_End:
            if (ctrl) cur_ = Pos{d.lineCount() - 1, d.lineLength(d.lineCount() - 1)};
            else cur_.col = d.lineLength(cur_.line);
            break;
        default: return;
    }
    afterMove(shift, keepWant);
}

// ---------------------------------------------------------------- events

void CodeView::keyPressEvent(QKeyEvent* e) {
    const Qt::KeyboardModifiers m = e->modifiers();
    const bool ctrl = m & Qt::ControlModifier;
    const bool alt = m & Qt::AltModifier;
    cursorOn_ = true;

    if (popupVisible()) {
        switch (e->key()) {
            case Qt::Key_Down: popup_->setCurrentRow(std::min(popup_->currentRow() + 1, popup_->count() - 1)); return;
            case Qt::Key_Up: popup_->setCurrentRow(std::max(popup_->currentRow() - 1, 0)); return;
            case Qt::Key_Return: case Qt::Key_Enter: acceptCompletion(); return;
            case Qt::Key_Tab:                                   // an exact snippet trigger wins over a completion
                if (snippetAtCursor() && tryExpandSnippet()) return;
                acceptCompletion();
                return;
            case Qt::Key_Escape: hidePopup(); return;
            default: break;
        }
    }

    switch (e->key()) {
        case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
        case Qt::Key_Home: case Qt::Key_End: case Qt::Key_PageUp: case Qt::Key_PageDown:
            if (alt) { e->ignore(); return; }
            moveCursor(e);
            return;
        case Qt::Key_Backspace: backspace(ctrl); return;
        case Qt::Key_Delete: deleteForward(ctrl); return;
        case Qt::Key_Return: case Qt::Key_Enter:
            if (ctrl) { e->ignore(); return; }
            pressEnter();
            return;
        case Qt::Key_Tab:
            if (ctrl) { e->ignore(); return; }
            pressTab(false);
            return;
        case Qt::Key_Backtab:
            if (ctrl) { e->ignore(); return; }
            pressTab(true);
            return;
        case Qt::Key_Escape:
            if (hasSelection()) { anchor_ = cur_; emit stateChanged(); viewport()->update(); return; }
            e->ignore();
            return;
        default: break;
    }

    const QString t = e->text();
    const bool altGr = ctrl && alt;                                       // AltGr on Windows reports Ctrl+Alt
    const bool plain = !(m & Qt::MetaModifier) && ((!ctrl && !alt) || altGr);
    if (!plain || t.isEmpty() || t[0].unicode() < 32 || t[0].unicode() == 127) {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    if (blocked()) return;
    const std::string u = t.toUtf8().toStdString();
    if (u.size() == 1) typeChar(u[0]);
    else if (hasSelection()) insertTypedText(u);
    else { cur_ = ed_.typeText(cur_, u); anchor_ = cur_; edited(cur_.line); }

    if (!prefs().autoComplete) hidePopup();
    else if (u.size() == 1 && isWordChar(u[0])) autoTimer_.start();
    else if (u.size() > 1) autoTimer_.start();
    else hidePopup();
}

void CodeView::inputMethodEvent(QInputMethodEvent* e) {
    const QString commit = e->commitString();
    if (!commit.isEmpty() && !blocked()) {
        const std::string u = commit.toUtf8().toStdString();
        if (hasSelection()) insertTypedText(u);
        else { cur_ = ed_.typeText(cur_, u); anchor_ = cur_; edited(cur_.line); }
    }
    e->accept();
}

QVariant CodeView::inputMethodQuery(Qt::InputMethodQuery q) const {
    if (q == Qt::ImCursorRectangle) {
        const QPoint c = cursorPixel();
        return QRect(c.x(), c.y(), 1, lineH_);
    }
    return QAbstractScrollArea::inputMethodQuery(q);
}

void CodeView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QAbstractScrollArea::mousePressEvent(e); return; }
    setFocus();
    hidePopup();
    const bool triple = QDateTime::currentMSecsSinceEpoch() - lastDblClick_ < QApplication::doubleClickInterval();
    const Pos p = pointToPos(e->pos());
    if (!triple && e->pos().x() >= foldMarkerLeft() && e->pos().x() < gutterW_ &&
        (folds_.isFolded(p.line) || canFold(p.line))) {                  // the fold arrow
        dragging_ = false;
        toggleFold(p.line);
        return;
    }
    if (triple || e->pos().x() < gutterW_) {                              // triple click or click on a line number: whole line
        const Document& d = ed_.doc();
        anchor_ = Pos{p.line, 0};
        const size_t next = folds_.nextVisible(p.line, d.lineCount());   // a folded header takes its block along
        cur_ = (next > p.line) ? Pos{next, 0} : Pos{p.line, d.lineLength(p.line)};
        dragging_ = false;
        afterMove(true);
        return;
    }
    cur_ = p;
    afterMove(e->modifiers() & Qt::ShiftModifier);
    dragging_ = true;
}

void CodeView::mouseMoveEvent(QMouseEvent* e) {
    const bool overGutter = e->pos().x() < gutterW_;
    if (overGutter != gutterHover_) {
        gutterHover_ = overGutter;
        viewport()->setCursor(overGutter ? Qt::ArrowCursor : Qt::IBeamCursor);
        viewport()->update();
    }
    if (!dragging_ || !(e->buttons() & Qt::LeftButton)) return;
    cur_ = pointToPos(e->pos());
    afterMove(true);
}

void CodeView::mouseReleaseEvent(QMouseEvent* e) {
    dragging_ = false;
    QAbstractScrollArea::mouseReleaseEvent(e);
}

void CodeView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const Pos p = pointToPos(e->pos());
    const std::string line = lineText(p.line);
    size_t a = p.col, b = p.col;
    if (p.col < line.size() && isWc(line[p.col])) {
        while (a > 0 && isWc(line[a - 1])) --a;
        while (b < line.size() && isWc(line[b])) ++b;
    } else if (p.col > 0 && isWc(line[p.col - 1])) {
        while (a > 0 && isWc(line[a - 1])) --a;
    } else if (p.col < line.size()) {
        b = nextCharCol(line, p.col);
    }
    anchor_ = Pos{p.line, a};
    cur_ = Pos{p.line, b};
    lastDblClick_ = QDateTime::currentMSecsSinceEpoch();
    dragging_ = false;
    afterMove(true);
}

void CodeView::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        const int dy = e->angleDelta().y();
        if (dy != 0) setZoom(zoom_ + (dy > 0 ? 1 : -1));
        e->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
}

void CodeView::focusInEvent(QFocusEvent* e) {
    QAbstractScrollArea::focusInEvent(e);
    cursorOn_ = true;
    viewport()->update();
}

void CodeView::focusOutEvent(QFocusEvent* e) {
    QAbstractScrollArea::focusOutEvent(e);
    hidePopup();
    viewport()->update();
}

// ---------------------------------------------------------------- autocomplete popup

void CodeView::setReadOnly(bool on) {
    if (readOnly_ == on) return;
    readOnly_ = on;
    if (on) { hidePopup(); autoTimer_.stop(); }
    emit stateChanged();
    viewport()->update();
}

bool CodeView::blocked() {
    if (!readOnly_) return false;
    emit statusMessage("Restricted Mode: this file is read-only because its folder is not trusted.");
    return true;
}

bool CodeView::popupVisible() const { return popup_ && popup_->isVisible(); }

void CodeView::hidePopup() {
    autoTimer_.stop();
    if (popup_) popup_->hide();
}

void CodeView::triggerAutocomplete() { if (blocked()) return; showCompletions(true); }

void CodeView::showCompletions(bool force) {
    if (hasSelection()) { hidePopup(); return; }
    const std::string prefix = prefixBefore(ed_.doc(), cur_);
    if (prefix.size() < (force ? 1u : 2u)) { hidePopup(); return; }
    ac_.refresh(ed_.doc());                                              // words of this document join the dictionary
    DynArray<std::string> list = ac_.suggest(prefix, 8);
    popup_->clear();
    // snippets first: every trigger that starts with what is typed (Tab or Enter expands it)
    QStringList snippetTriggers;
    {
        const SnippetTable& table = SnippetStore::instance().table();
        const DynArray<std::string> keys = table.triggers();
        for (size_t i = 0; i < keys.size() && snippetTriggers.size() < 4; ++i)
            if (keys[i].compare(0, prefix.size(), prefix) == 0) snippetTriggers << QString::fromStdString(keys[i]);
    }
    for (const QString& trig : snippetTriggers) {
        QListWidgetItem* it = new QListWidgetItem(trig + "    snippet");
        it->setData(Qt::UserRole, trig);
        it->setData(Qt::UserRole + 1, true);
        it->setForeground(Theme::Accent);
        const Snippet* sn = SnippetStore::instance().table().find(trig.toStdString());
        if (sn) it->setToolTip(QString::fromStdString(sn->body).left(300));
        popup_->addItem(it);
    }
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == prefix) continue;
        const QString w = QString::fromStdString(list[i]);
        if (snippetTriggers.contains(w)) continue;                      // the snippet entry already stands for it
        popup_->addItem(w);
    }
    if (popup_->count() == 0) { popup_->hide(); return; }
    popup_->setCurrentRow(0);
    const int rows = popup_->count();
    int w = 120;
    for (int i = 0; i < rows; ++i) w = std::max(w, fontMetrics().horizontalAdvance(popup_->item(i)->text()) + 36);
    popup_->resize(w, rows * (lineH_ + 4) + 4);
    const QPoint c = cursorPixel();                        // follows folds, word wrap and sideways scrolling
    const int x = c.x() - static_cast<int>(prefix.size()) * charW_;
    int y = c.y() + lineH_;
    if (y + popup_->height() > viewport()->height()) y = std::max(0, y - lineH_ - popup_->height());
    popup_->move(std::max(gutterW_, x), y + viewport()->y());   // the popup belongs to this widget; the text starts below the breadcrumbs
    popup_->show();
    popup_->raise();
}

void CodeView::acceptCompletion() {
    if (blocked()) return;
    QListWidgetItem* it = popup_->currentItem();
    if (!it) { hidePopup(); return; }
    const bool isSnippet = it->data(Qt::UserRole + 1).toBool();
    const std::string word = (isSnippet ? it->data(Qt::UserRole).toString() : it->text()).toStdString();
    hidePopup();
    const size_t from = cur_.line;
    cur_ = applyCompletion(ed_, cur_, word);
    if (!isSnippet) ac_.accepted(word);
    anchor_ = cur_;
    edited(from);
    if (isSnippet) expandSnippet();                                      // the trigger is complete now: replace it by the snippet
}

#include "CodeView.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
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

// ---------------------------------------------------------------- colours (dark theme)

static const QColor kBg(0x1e, 0x1e, 0x1e);
static const QColor kCurLine(0x2a, 0x2d, 0x2e);
static const QColor kSelection(0x26, 0x4f, 0x78);
static const QColor kFind(0x61, 0x4a, 0x1a);
static const QColor kGutterNum(0x85, 0x85, 0x85);
static const QColor kGutterCur(0xc6, 0xc6, 0xc6);
static const QColor kCursorCol(0xae, 0xaf, 0xad);
static const QColor kGuide(0x40, 0x40, 0x40);
static const QColor kTokenColors[] = {
    QColor(0xd4, 0xd4, 0xd4),   // text
    QColor(0x56, 0x9c, 0xd6),   // keyword
    QColor(0x4e, 0xc9, 0xb0),   // type
    QColor(0xce, 0x91, 0x78),   // string
    QColor(0xb5, 0xce, 0xa8),   // number
    QColor(0x6a, 0x99, 0x55),   // comment
    QColor(0xc5, 0x86, 0xc0)};  // preprocessor

static bool isWc(char c) { return isWordChar(c) || static_cast<unsigned char>(c) >= 0x80; }
static bool isBlankC(char c) { return c == ' ' || c == '\t'; }

// ---------------------------------------------------------------- construction

CodeView::CodeView(QWidget* parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setAutoFillBackground(false);
    applyFont();
    verticalScrollBar()->setSingleStep(1);

    blink_.setInterval(530);
    connect(&blink_, &QTimer::timeout, this, [this]() {
        cursorOn_ = !cursorOn_;
        viewport()->update();
    });
    blink_.start();

    bracketTimer_.setSingleShot(true);
    connect(&bracketTimer_, &QTimer::timeout, this, [this]() { recomputeBrackets(); });
    autoTimer_.setSingleShot(true);
    autoTimer_.setInterval(180);
    connect(&autoTimer_, &QTimer::timeout, this, [this]() { showCompletions(); });

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { viewport()->update(); hidePopup(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() { viewport()->update(); hidePopup(); });

    popup_ = new QListWidget(this);
    popup_->setFocusPolicy(Qt::NoFocus);
    popup_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popup_->setStyleSheet(
        "QListWidget{background:#252526;color:#d4d4d4;border:1px solid #454545;outline:0;}"
        "QListWidget::item{padding:2px 8px;}"
        "QListWidget::item:selected{background:#094771;color:#ffffff;}");
    popup_->hide();
    connect(popup_, &QListWidget::itemClicked, this, [this]() { acceptCompletion(); });

    lastModified_ = false;
    recomputeBrackets();
    updateGutter();
    updateScrollBars();
}

void CodeView::applyFont() {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(zoom_);
    f.setStyleHint(QFont::Monospace);
    setFont(f);
    viewport()->setFont(f);
    QFontMetrics fm(f);
    charW_ = std::max(1, fm.horizontalAdvance(QLatin1Char('M')));
    lineH_ = fm.height() + 3;
    updateGutter();
    updateScrollBars();
    viewport()->update();
}

void CodeView::setZoom(int points) {
    zoom_ = std::max(7, std::min(36, points));
    applyFont();
    emit stateChanged();
}

void CodeView::updateGutter() {
    int digits = 3;
    for (size_t n = ed_.doc().lineCount(); n >= 1000; n /= 10) ++digits;
    gutterW_ = digits * charW_ + 22;
}

int CodeView::visibleLines() const { return std::max(1, viewport()->height() / lineH_); }

void CodeView::updateScrollBars() {
    const int total = static_cast<int>(ed_.doc().lineCount());
    verticalScrollBar()->setRange(0, std::max(0, total - 1));
    verticalScrollBar()->setPageStep(visibleLines());
    const int contentW = textLeft() + (maxCols_ + 4) * charW_;
    horizontalScrollBar()->setRange(0, std::max(0, contentW - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setSingleStep(charW_ * 3);
}

void CodeView::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBars();
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

Pos CodeView::pointToPos(const QPoint& pt) const {
    const Document& d = ed_.doc();
    int row = pt.y() < 0 ? 0 : pt.y() / lineH_;
    size_t line = std::min(firstVisible() + static_cast<size_t>(row), d.lineCount() - 1);
    int x = pt.x() - textLeft() + horizontalScrollBar()->value() + charW_ / 2;
    int dcol = x < 0 ? 0 : x / charW_;
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
    const int vis = visibleLines();
    const int line = static_cast<int>(cur_.line);
    QScrollBar* v = verticalScrollBar();
    if (line < v->value()) v->setValue(line);
    else if (line >= v->value() + vis - 1) v->setValue(std::max(0, line - vis + 2));
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
    verticalScrollBar()->setValue(std::max(0, l - visibleLines() / 2));
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

void CodeView::edited(size_t fromLine) {
    dirtyBrackets_ = true;
    bracketTimer_.start(250);
    blockValid_ = std::min(blockValid_, fromLine + 1);
    wantCol_ = displayColumn(cur_);
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
        "this", "nullptr", "true", "false", "sizeof", "extern", "mutable", "register", "static_cast",
        "dynamic_cast", "reinterpret_cast", "const_cast", "decltype", "import", "from", "def", "lambda", "elif",
        "pass", "None", "True", "False", "function", "var", "let", "async", "await", "package", "interface",
        "extends", "implements", "fn", "pub", "impl", "match", "mod", "in", "is", "not", "and", "or"};
    return s;
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
            if (keywordSet().contains(w)) out.push_back({i, j - i, cKeyword});
            else if (typeSet().contains(w)) out.push_back({i, j - i, cType});
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

    const size_t first = std::min(firstVisible(), doc.lineCount() - 1);
    const size_t last = std::min(doc.lineCount(), first + static_cast<size_t>(visibleLines()) + 2);
    const int xOff = horizontalScrollBar()->value();
    const int left = textLeft() - xOff;
    ensureBlockState(first);
    bool inBlock = first < blockAtStart_.size() && blockAtStart_[first] != 0;

    Pos b1, b2;
    bool haveBracket = false;
    if (!dirtyBrackets_ && hasFocus()) haveBracket = matchNearCursor(doc, report_, cur_, b1, b2);

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

    std::vector<Span> spans;
    for (size_t l = first; l < last; ++l) {
        const int y = static_cast<int>(l - first) * lineH_;
        const std::string raw = lineText(l);
        const QString s = expandTabs(raw);
        if (s.size() > maxCols_) { maxCols_ = s.size(); updateScrollBars(); }

        if (l == cur_.line && !sel) p.fillRect(gutterW_, y, vw - gutterW_, lineH_, kCurLine);

        for (size_t k = hi; k < highlights_.size() && highlights_[k].start.line <= l; ++k) {
            const Match& m = highlights_[k];
            if (m.end.line < l) continue;
            const int a = (m.start.line == l) ? displayCol(raw, m.start.col) : 0;
            const int b = (m.end.line == l) ? displayCol(raw, m.end.col) : static_cast<int>(s.size());
            p.fillRect(left + a * charW_, y, std::max(1, b - a) * charW_, lineH_, kFind);
        }

        if (sel && l >= ss.line && l <= se.line) {
            const int a = (l == ss.line) ? displayCol(raw, ss.col) : 0;
            const int b = (l == se.line) ? displayCol(raw, se.col) : static_cast<int>(s.size()) + 1;
            p.fillRect(left + a * charW_, y, std::max(0, b - a) * charW_, lineH_, kSelection);
        }

        int lead = 0;                                // indentation guides
        while (lead < s.size() && s[lead] == QLatin1Char(' ')) ++lead;
        p.setPen(kGuide);
        for (int c = tabWidth_; c < lead; c += tabWidth_) {
            const int gx = left + c * charW_;
            p.drawLine(gx, y, gx, y + lineH_);
        }

        tokenize(s, inBlock, spans);
        int pos = 0;
        auto drawRun = [&](int a, int len, int color) {
            if (len <= 0) return;
            const QString t = s.mid(a, len);
            if (t.trimmed().isEmpty()) return;
            p.setPen(kTokenColors[color]);
            p.drawText(left + a * charW_, y + ascent, t);
        };
        for (size_t k = 0; k < spans.size(); ++k) {
            drawRun(pos, spans[k].start - pos, cText);
            drawRun(spans[k].start, spans[k].len, spans[k].color);
            pos = spans[k].start + spans[k].len;
        }
        drawRun(pos, s.size() - pos, cText);

        if (haveBracket) {
            const Pos* pts[2] = {&b1, &b2};
            for (int k = 0; k < 2; ++k) {
                if (pts[k]->line != l) continue;
                const int bx = left + displayCol(raw, pts[k]->col) * charW_;
                p.fillRect(bx, y, charW_, lineH_, QColor(0x88, 0x88, 0x88, 70));
                p.setPen(QColor(0xaa, 0xaa, 0xaa));
                p.drawRect(bx, y, charW_ - 1, lineH_ - 1);
            }
        }

        if (hasFocus() && cursorOn_ && l == cur_.line)
            p.fillRect(left + displayCol(raw, cur_.col) * charW_, y, 2, lineH_, kCursorCol);
    }

    // line-number gutter, drawn last so horizontal scrolling slides under it
    p.fillRect(0, 0, gutterW_, vh, kBg);
    p.setPen(QColor(0x33, 0x33, 0x33));
    p.drawLine(gutterW_ - 1, 0, gutterW_ - 1, vh);
    for (size_t l = first; l < last; ++l) {
        const int y = static_cast<int>(l - first) * lineH_;
        p.setPen(l == cur_.line ? kGutterCur : kGutterNum);
        p.drawText(QRect(0, y, gutterW_ - 10, lineH_), Qt::AlignRight | Qt::AlignVCenter, QString::number(l + 1));
    }
}

// ---------------------------------------------------------------- editing primitives

void CodeView::deleteSelection() {
    if (!hasSelection()) return;
    const Pos a = selStart(), b = selEnd();
    ed_.deleteRange(a, b);
    cur_ = anchor_ = a;
    edited(a.line);
}

void CodeView::insertTypedText(const std::string& s) {
    if (s.empty()) return;
    const size_t from = selStart().line;
    if (hasSelection()) cur_ = ed_.replaceRange(selStart(), selEnd(), s);
    else cur_ = ed_.insertText(cur_, s);
    anchor_ = cur_;
    edited(from);
}

void CodeView::typeChar(char c) {
    const Document& doc = ed_.doc();
    const char closer = pairCloser(c);
    const bool opener = closer != 0;
    const size_t from = selStart().line;

    if (hasSelection()) {
        if (opener) {                                   // wrap the selection: "text" (text) {text} ...
            SelRange r = surroundSelection(ed_, anchor_, cur_, c);
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
    if ((c == ')' || c == ']' || c == '}' || c == '"' || c == '\'' || c == '`') && next == c) {
        cur_.col += 1;                                  // type over the closing character
        anchor_ = cur_;
        ed_.breakTyping();
        edited(cur_.line);
        return;
    }
    if (opener && shouldAutoPair(doc, cur_, c)) {
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
    } else if (isEmptyPairAround(ed_.doc(), p)) {
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
    if (back) { outdentSelection(); return; }
    if (hasSelection() && selStart().line != selEnd().line) { indentSelection(); return; }
    const int n = tabWidth_ - (displayColumn(selStart()) % tabWidth_);
    insertTypedText(std::string(static_cast<size_t>(n), ' '));
}

void CodeView::adjustBoth(const DynArray<ColEdit>& edits) {
    cur_ = adjustPos(cur_, edits);
    anchor_ = adjustPos(anchor_, edits);
}

// ---------------------------------------------------------------- commands (slots)

void CodeView::undo() {
    std::string label;
    Pos where;
    hidePopup();
    if (!ed_.undo(&label, &where)) return;
    cur_ = anchor_ = clamp(where);
    blockValid_ = 0;
    edited(0);
}

void CodeView::redo() {
    std::string label;
    Pos where;
    hidePopup();
    if (!ed_.redo(&label, &where)) return;
    cur_ = anchor_ = clamp(where);
    blockValid_ = 0;
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
    if (hasSelection()) deleteSelection();
    else deleteLine();
}

void CodeView::paste() {
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
    size_t from, to;
    lineRangeOfSelection(from, to);
    if (!::moveLines(ed_, from, to, -1)) return;
    cur_.line -= 1;
    anchor_.line -= 1;
    edited(from - 1);
}

void CodeView::moveLinesDown() {
    size_t from, to;
    lineRangeOfSelection(from, to);
    if (!::moveLines(ed_, from, to, +1)) return;
    cur_.line += 1;
    anchor_.line += 1;
    edited(from);
}

void CodeView::copyLinesDown() {
    size_t from, to;
    lineRangeOfSelection(from, to);
    duplicateLines(ed_, from, to);
    cur_.line += to - from + 1;                         // the cursor follows the new copy
    anchor_.line += to - from + 1;
    edited(from);
}

void CodeView::copyLinesUp() {
    size_t from, to;
    lineRangeOfSelection(from, to);
    duplicateLines(ed_, from, to);                      // the cursor stays on the upper copy
    edited(from);
}

void CodeView::deleteLine() {
    size_t from, to;
    lineRangeOfSelection(from, to);
    ed_.deleteLines(from, to);
    const Document& d = ed_.doc();
    const size_t l = std::min(from, d.lineCount() - 1);
    cur_ = anchor_ = Pos{l, byteColFor(lineText(l), wantCol_)};
    edited(from);
}

void CodeView::toggleComment() {
    if (language_ == 2) return;
    size_t from, to;
    lineRangeOfSelection(from, to);
    DynArray<ColEdit> edits = toggleLineComment(ed_, from, to, comment_);
    if (edits.empty()) return;
    adjustBoth(edits);
    edited(from);
}

void CodeView::indentSelection() {
    size_t from, to;
    lineRangeOfSelection(from, to);
    DynArray<ColEdit> edits = indentLines(ed_, from, to, static_cast<size_t>(tabWidth_));
    if (edits.empty()) return;
    adjustBoth(edits);
    edited(from);
}

void CodeView::outdentSelection() {
    size_t from, to;
    lineRangeOfSelection(from, to);
    DynArray<ColEdit> edits = outdentLines(ed_, from, to, static_cast<size_t>(tabWidth_));
    if (edits.empty()) return;
    adjustBoth(edits);
    edited(from);
}

void CodeView::newLineBelow() {
    const size_t l = cur_.line;
    cur_ = anchor_ = insertLineBelow(ed_, l, static_cast<size_t>(tabWidth_));
    edited(l);
}

void CodeView::newLineAbove() {
    const size_t l = cur_.line;
    cur_ = anchor_ = insertLineAbove(ed_, l, static_cast<size_t>(tabWidth_));
    edited(l);
}

// ---------------------------------------------------------------- cursor movement

void CodeView::afterMove(bool extend, bool keepWantCol) {
    if (!extend) anchor_ = cur_;
    if (!keepWantCol) wantCol_ = displayColumn(cur_);
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
            if (cur_.col == 0) { if (cur_.line > 0) cur_ = Pos{cur_.line - 1, d.lineLength(cur_.line - 1)}; }
            else cur_.col = ctrl ? wordLeft(lineText(cur_.line), cur_.col) : prevCharCol(lineText(cur_.line), cur_.col);
            break;
        case Qt::Key_Right: {
            if (hasSelection() && !shift && !ctrl) { cur_ = selEnd(); break; }
            const std::string line = lineText(cur_.line);
            if (cur_.col >= line.size()) { if (cur_.line + 1 < d.lineCount()) cur_ = Pos{cur_.line + 1, 0}; }
            else cur_.col = ctrl ? wordRight(line, cur_.col) : nextCharCol(line, cur_.col);
            break;
        }
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const bool up = e->key() == Qt::Key_Up;
            keepWant = true;
            if (up && cur_.line == 0) { cur_.col = 0; keepWant = false; break; }
            if (!up && cur_.line + 1 >= d.lineCount()) { cur_.col = d.lineLength(cur_.line); keepWant = false; break; }
            cur_.line += up ? static_cast<size_t>(-1) : 1;
            cur_.col = byteColFor(lineText(cur_.line), wantCol_);
            break;
        }
        case Qt::Key_PageUp:
        case Qt::Key_PageDown: {
            const bool up = e->key() == Qt::Key_PageUp;
            const size_t step = static_cast<size_t>(std::max(1, visibleLines() - 1));
            keepWant = true;
            if (up) cur_.line = cur_.line > step ? cur_.line - step : 0;
            else cur_.line = std::min(cur_.line + step, d.lineCount() - 1);
            cur_.col = byteColFor(lineText(cur_.line), wantCol_);
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
            case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Tab: acceptCompletion(); return;
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
    const std::string u = t.toUtf8().toStdString();
    if (u.size() == 1) typeChar(u[0]);
    else if (hasSelection()) insertTypedText(u);
    else { cur_ = ed_.typeText(cur_, u); anchor_ = cur_; edited(cur_.line); }

    if (u.size() == 1 && isWordChar(u[0])) autoTimer_.start();
    else if (u.size() > 1) autoTimer_.start();
    else hidePopup();
}

void CodeView::inputMethodEvent(QInputMethodEvent* e) {
    const QString commit = e->commitString();
    if (!commit.isEmpty()) {
        const std::string u = commit.toUtf8().toStdString();
        if (hasSelection()) insertTypedText(u);
        else { cur_ = ed_.typeText(cur_, u); anchor_ = cur_; edited(cur_.line); }
    }
    e->accept();
}

QVariant CodeView::inputMethodQuery(Qt::InputMethodQuery q) const {
    if (q == Qt::ImCursorRectangle) {
        const int x = textLeft() - horizontalScrollBar()->value() + displayColumn(cur_) * charW_;
        const int y = static_cast<int>(cur_.line - std::min(cur_.line, firstVisible())) * lineH_;
        return QRect(x, y, 1, lineH_);
    }
    return QAbstractScrollArea::inputMethodQuery(q);
}

void CodeView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { QAbstractScrollArea::mousePressEvent(e); return; }
    setFocus();
    hidePopup();
    const bool triple = QDateTime::currentMSecsSinceEpoch() - lastDblClick_ < QApplication::doubleClickInterval();
    const Pos p = pointToPos(e->pos());
    if (triple || e->pos().x() < gutterW_) {                              // triple click or click on a line number: whole line
        const Document& d = ed_.doc();
        anchor_ = Pos{p.line, 0};
        cur_ = (p.line + 1 < d.lineCount()) ? Pos{p.line + 1, 0} : Pos{p.line, d.lineLength(p.line)};
        dragging_ = false;
        afterMove(true);
        return;
    }
    cur_ = p;
    afterMove(e->modifiers() & Qt::ShiftModifier);
    dragging_ = true;
}

void CodeView::mouseMoveEvent(QMouseEvent* e) {
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

bool CodeView::popupVisible() const { return popup_ && popup_->isVisible(); }

void CodeView::hidePopup() {
    autoTimer_.stop();
    if (popup_) popup_->hide();
}

void CodeView::triggerAutocomplete() { showCompletions(true); }

void CodeView::showCompletions(bool force) {
    if (hasSelection()) { hidePopup(); return; }
    const std::string prefix = prefixBefore(ed_.doc(), cur_);
    if (prefix.size() < (force ? 1u : 2u)) { hidePopup(); return; }
    ac_.refresh(ed_.doc());                                              // words of this document join the dictionary
    DynArray<std::string> list = ac_.suggest(prefix, 8);
    popup_->clear();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == prefix) continue;
        popup_->addItem(QString::fromStdString(list[i]));
    }
    if (popup_->count() == 0) { popup_->hide(); return; }
    popup_->setCurrentRow(0);
    const int rows = popup_->count();
    int w = 120;
    for (int i = 0; i < rows; ++i) w = std::max(w, fontMetrics().horizontalAdvance(popup_->item(i)->text()) + 36);
    popup_->resize(w, rows * (lineH_ + 4) + 4);
    const int x = textLeft() - horizontalScrollBar()->value() + (displayColumn(cur_) - static_cast<int>(prefix.size())) * charW_;
    int y = static_cast<int>(cur_.line - std::min(cur_.line, firstVisible()) + 1) * lineH_;
    if (y + popup_->height() > viewport()->height()) y = std::max(0, y - lineH_ - popup_->height());
    popup_->move(std::max(gutterW_, x), y);
    popup_->show();
    popup_->raise();
}

void CodeView::acceptCompletion() {
    QListWidgetItem* it = popup_->currentItem();
    if (!it) { hidePopup(); return; }
    const std::string word = it->text().toStdString();
    hidePopup();
    const size_t from = cur_.line;
    cur_ = applyCompletion(ed_, cur_, word);
    ac_.accepted(word);
    anchor_ = cur_;
    edited(from);
}

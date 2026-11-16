#include "EditOps.h"
#include <stdexcept>
#include "Assist.h"      // isWordChar

// ---------------------------------------------------------------- helpers

static void checkRange(const Document& doc, size_t from, size_t to) {
    if (from > to || to >= doc.lineCount())
        throw std::out_of_range("Line range " + std::to_string(from + 1) + "-" + std::to_string(to + 1) +
                                " is not inside the document (" + std::to_string(doc.lineCount()) + " line(s)).");
}

static bool isBlankChar(char c) { return c == ' ' || c == '\t'; }

std::string leadingWhitespace(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && isBlankChar(line[i])) ++i;
    return line.substr(0, i);
}

size_t smartHomeCol(const std::string& line, size_t col) {
    size_t first = leadingWhitespace(line).size();
    return col != first ? first : 0;
}

Pos adjustPos(Pos p, const DynArray<ColEdit>& edits) {
    for (size_t i = 0; i < edits.size(); ++i) {
        const ColEdit& e = edits[i];
        if (e.line != p.line) continue;
        if (e.delta >= 0) {
            if (p.col >= e.at) p.col += static_cast<size_t>(e.delta);
        } else {
            size_t removed = static_cast<size_t>(-e.delta);
            if (p.col >= e.at + removed) p.col -= removed;
            else if (p.col > e.at) p.col = e.at;
        }
    }
    return p;
}

// ---------------------------------------------------------------- line commands

bool moveLines(Editor& ed, size_t from, size_t to, int dir) {
    const Document& doc = ed.doc();
    checkRange(doc, from, to);
    if (dir == 0) return false;
    size_t a, b;                                   // the lines that change: a..b
    std::string block = doc.getRange(Pos{from, 0}, Pos{to, doc.lineLength(to)});
    std::string text;
    if (dir < 0) {
        if (from == 0) return false;
        a = from - 1;
        b = to;
        text = block + "\n" + doc.getLine(from - 1);
    } else {
        if (to + 1 >= doc.lineCount()) return false;
        a = from;
        b = to + 1;
        text = doc.getLine(to + 1) + "\n" + block;
    }
    EditGroup g(ed, "move lines");
    ed.replaceRange(Pos{a, 0}, Pos{b, doc.lineLength(b)}, text);
    return true;
}

void duplicateLines(Editor& ed, size_t from, size_t to) {
    const Document& doc = ed.doc();
    checkRange(doc, from, to);
    std::string block = doc.getRange(Pos{from, 0}, Pos{to, doc.lineLength(to)});
    EditGroup g(ed, "duplicate lines");
    ed.insertText(Pos{to, doc.lineLength(to)}, "\n" + block);
}

DynArray<ColEdit> indentLines(Editor& ed, size_t from, size_t to, size_t width) {
    const Document& doc = ed.doc();
    checkRange(doc, from, to);
    DynArray<ColEdit> edits;
    EditGroup g(ed, "indent");
    const std::string pad(width, ' ');
    for (size_t l = from; l <= to; ++l) {
        if (doc.lineLength(l) == 0) continue;
        ed.insertText(Pos{l, 0}, pad);
        edits.push_back(ColEdit{l, 0, static_cast<long>(width)});
    }
    return edits;
}

DynArray<ColEdit> outdentLines(Editor& ed, size_t from, size_t to, size_t width) {
    const Document& doc = ed.doc();
    checkRange(doc, from, to);
    DynArray<ColEdit> edits;
    EditGroup g(ed, "outdent");
    for (size_t l = from; l <= to; ++l) {
        std::string line = doc.getLine(l);
        size_t k = 0;
        if (!line.empty() && line[0] == '\t') k = 1;
        else while (k < width && k < line.size() && line[k] == ' ') ++k;
        if (k == 0) continue;
        ed.deleteRange(Pos{l, 0}, Pos{l, k});
        edits.push_back(ColEdit{l, 0, -static_cast<long>(k)});
    }
    return edits;
}

DynArray<ColEdit> toggleLineComment(Editor& ed, size_t from, size_t to, const std::string& marker, bool* commented) {
    const Document& doc = ed.doc();
    checkRange(doc, from, to);
    if (marker.empty()) throw std::invalid_argument("The comment marker is empty.");
    DynArray<ColEdit> edits;

    // Pass 1: are all non-blank lines already commented? what is the smallest indentation?
    bool any = false, all = true;
    size_t minIndent = 0;
    for (size_t l = from; l <= to; ++l) {
        std::string line = doc.getLine(l);
        size_t ind = leadingWhitespace(line).size();
        if (ind == line.size()) continue;                       // blank line
        if (!any || ind < minIndent) minIndent = ind;
        any = true;
        if (line.compare(ind, marker.size(), marker) != 0) all = false;
    }
    if (commented) *commented = false;
    if (!any) return edits;                                     // nothing but blank lines

    EditGroup g(ed, all ? "uncomment" : "comment");
    for (size_t l = from; l <= to; ++l) {
        std::string line = doc.getLine(l);
        size_t ind = leadingWhitespace(line).size();
        if (ind == line.size()) continue;
        if (all) {
            size_t n = marker.size();
            if (ind + n < line.size() && line[ind + n] == ' ') ++n;   // the blank we added
            ed.deleteRange(Pos{l, ind}, Pos{l, ind + n});
            edits.push_back(ColEdit{l, ind, -static_cast<long>(n)});
        } else {
            ed.insertText(Pos{l, minIndent}, marker + " ");
            edits.push_back(ColEdit{l, minIndent, static_cast<long>(marker.size() + 1)});
        }
    }
    if (commented) *commented = !all;
    return edits;
}

Pos insertLineBelow(Editor& ed, size_t line, size_t width) {
    const Document& doc = ed.doc();
    if (line >= doc.lineCount()) throw std::out_of_range("Line " + std::to_string(line + 1) + " does not exist.");
    return insertNewLineSmart(ed, Pos{line, doc.lineLength(line)}, width);
}

Pos insertLineAbove(Editor& ed, size_t line, size_t width) {
    (void)width;
    const Document& doc = ed.doc();
    if (line >= doc.lineCount()) throw std::out_of_range("Line " + std::to_string(line + 1) + " does not exist.");
    std::string indent = leadingWhitespace(doc.getLine(line));
    EditGroup g(ed, "insert line above");
    ed.insertText(Pos{line, 0}, indent + "\n");
    return Pos{line, indent.size()};
}

// ---------------------------------------------------------------- typing helpers

char pairCloser(char open) {
    switch (open) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '"': return '"';
        case '\'': return '\'';
        case '`': return '`';
        default: return 0;
    }
}

static bool isQuote(char c) { return c == '"' || c == '\'' || c == '`'; }

bool shouldAutoPair(const Document& doc, Pos at, char open) {
    if (pairCloser(open) == 0) return false;
    std::string line = doc.getLine(at.line);
    if (at.col > line.size()) return false;
    char prev = at.col > 0 ? line[at.col - 1] : 0;
    char next = at.col < line.size() ? line[at.col] : 0;
    if (isQuote(open)) {
        if (isWordChar(prev) || prev == open) return false;     // don't, 1'000, """
    }
    if (next == 0 || isBlankChar(next)) return true;
    return next == ')' || next == ']' || next == '}' || next == ',' || next == ';' || next == '.' || next == ':';
}

bool isEmptyPairAround(const Document& doc, Pos at) {
    std::string line = doc.getLine(at.line);
    if (at.col == 0 || at.col >= line.size()) return false;
    char c = pairCloser(line[at.col - 1]);
    return c != 0 && c == line[at.col];
}

SelRange surroundSelection(Editor& ed, Pos a, Pos b, char open) {
    if (b < a) { Pos t = a; a = b; b = t; }
    char close = pairCloser(open);
    if (close == 0) throw std::invalid_argument(std::string("'") + open + "' does not have a closing partner.");
    if (a == b) throw std::invalid_argument("Nothing is selected.");
    // Check BOTH positions before the first edit, so a bad one cannot leave a half-done change.
    const Document& doc = ed.doc();
    if (b.line >= doc.lineCount() || b.col > doc.lineLength(b.line) || a.col > doc.lineLength(a.line))
        throw std::out_of_range("The selection is not inside the document.");
    EditGroup g(ed, "surround");
    ed.insertText(b, std::string(1, close));        // closer first: it does not move 'a'
    ed.insertText(a, std::string(1, open));
    SelRange r;
    r.start = Pos{a.line, a.col + 1};
    r.end = (b.line == a.line) ? Pos{b.line, b.col + 1} : b;
    return r;
}

// The opener of the pair 'c' belongs to (c itself for openers and quotes); 0 if none.
static char pairOpener(char c) {
    if (pairCloser(c) != 0) return c;
    if (c == ')') return '(';
    if (c == ']') return '[';
    if (c == '}') return '{';
    return 0;
}

bool isWrappedBy(const Document& doc, Pos a, Pos b, char c) {
    if (b < a) { Pos t = a; a = b; b = t; }
    const char open = pairOpener(c);
    if (open == 0 || a == b) return false;
    if (a.line >= doc.lineCount() || b.line >= doc.lineCount()) return false;
    const std::string first = doc.getLine(a.line);
    const std::string last = (b.line == a.line) ? first : doc.getLine(b.line);
    if (a.col == 0 || a.col > first.size() || b.col >= last.size()) return false;
    return first[a.col - 1] == open && last[b.col] == pairCloser(open);
}

SelRange unwrapSelection(Editor& ed, Pos a, Pos b, char c) {
    if (b < a) { Pos t = a; a = b; b = t; }
    if (!isWrappedBy(ed.doc(), a, b, c)) throw std::invalid_argument("The selection is not wrapped by that pair.");
    EditGroup g(ed, "unwrap");
    ed.deleteRange(b, Pos{b.line, b.col + 1});       // closer first: it does not move 'a'
    ed.deleteRange(Pos{a.line, a.col - 1}, a);
    SelRange r;
    r.start = Pos{a.line, a.col - 1};
    r.end = (b.line == a.line) ? Pos{b.line, b.col - 1} : b;
    return r;
}

SelRange toggleSurround(Editor& ed, Pos a, Pos b, char c, bool* changed) {
    if (b < a) { Pos t = a; a = b; b = t; }
    if (changed) *changed = true;
    if (isWrappedBy(ed.doc(), a, b, c)) return unwrapSelection(ed, a, b, c);
    if (pairCloser(c) != 0) return surroundSelection(ed, a, b, c);
    if (changed) *changed = false;
    return SelRange{a, b};
}

Pos insertNewLineSmart(Editor& ed, Pos at, size_t width) {
    const Document& doc = ed.doc();
    std::string line = doc.getLine(at.line);              // throws if the line does not exist
    if (at.col > line.size()) throw std::out_of_range("Column " + std::to_string(at.col + 1) + " is not valid on line " +
                                                     std::to_string(at.line + 1) + ".");
    std::string indent = leadingWhitespace(line.substr(0, at.col));

    // last non-blank character before the cursor, first non-blank after it
    char last = 0, first = 0;
    for (size_t i = at.col; i > 0; --i) if (!isBlankChar(line[i - 1])) { last = line[i - 1]; break; }
    for (size_t i = at.col; i < line.size(); ++i) if (!isBlankChar(line[i])) { first = line[i]; break; }

    const bool opener = (last == '{' || last == '(' || last == '[');
    const std::string unit(width, ' ');
    EditGroup g(ed, "new line");
    if (opener && first == pairCloser(last)) {            // {|}  ->  block with the closer below
        ed.insertText(at, "\n" + indent + unit + "\n" + indent);
        return Pos{at.line + 1, indent.size() + unit.size()};
    }
    std::string text = "\n" + indent + (opener ? unit : std::string());
    Pos end = ed.insertText(at, text);
    return end;
}

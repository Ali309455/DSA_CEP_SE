#include "Search.h"
#include <cctype>
#include <stdexcept>

// ---------------------------------------------------------------- helpers

static bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static char fold(char c, bool matchCase) {
    if (!matchCase && c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

// ---------------------------------------------------------------- KMP on strings

DynArray<size_t> buildFailureTable(const std::string& p) {
    DynArray<size_t> f;
    f.reserve(p.size());
    size_t k = 0;                       // length of the current matched prefix
    for (size_t i = 0; i < p.size(); ++i) {
        if (i == 0) { f.push_back(0); continue; }
        while (k > 0 && p[i] != p[k]) k = f[k - 1];   // fall back, never move i back
        if (p[i] == p[k]) ++k;
        f.push_back(k);
    }
    return f;
}

KmpMatcher::KmpMatcher(const std::string& pattern, bool matchCase)
    : state_(0), matchCase_(matchCase) {
    if (pattern.empty()) throw std::invalid_argument("The search text is empty.");
    pat_.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size(); ++i) pat_.push_back(fold(pattern[i], matchCase));
    fail_ = buildFailureTable(pat_);
}

bool KmpMatcher::feed(char raw) {
    char c = fold(raw, matchCase_);
    while (state_ > 0 && pat_[state_] != c) state_ = fail_[state_ - 1];
    if (pat_[state_] == c) ++state_;
    if (state_ == pat_.size()) {
        state_ = fail_[state_ - 1];     // keep going: allows overlapping matches
        return true;
    }
    return false;
}

DynArray<size_t> kmpFindAll(const std::string& text, const std::string& pattern, bool matchCase) {
    KmpMatcher m(pattern, matchCase);
    DynArray<size_t> out;
    for (size_t i = 0; i < text.size(); ++i)
        if (m.feed(text[i])) out.push_back(i + 1 - pattern.size());
    return out;
}

// ---------------------------------------------------------------- KMP on a Document

std::string normalizePattern(const std::string& pattern) {
    std::string s;
    s.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] != '\r') s.push_back(pattern[i]);
    if (s.empty()) throw std::invalid_argument("The search text is empty.");
    return s;
}

// The position 'len' characters before 'end' in the character stream (a line
// break counts as one character). Only multi-line patterns need to look at
// other lines, so the usual single-line case is a subtraction.
static Pos walkBack(const Document& d, Pos end, size_t len) {
    Pos p = end;
    while (len > p.col) {
        len -= p.col + 1;               // the rest of this line, plus the line break before it
        --p.line;                       // cannot underflow: the match really exists
        p.col = d.lineLength(p.line);
    }
    p.col -= len;
    return p;
}

// Whole-word test. A side is only checked when the search text itself begins
// (or ends) with a word character; otherwise "(foo)" could never match.
// 'cur' is the line being scanned (index 'curLine'), so we never re-read it.
static bool wordBoundariesOk(const Document& d, const Match& m, bool checkLeft, bool checkRight,
                             size_t curLine, const std::string& cur) {
    if (checkLeft && m.start.col > 0) {
        char before = (m.start.line == curLine) ? cur[m.start.col - 1]
                                                : d.getLine(m.start.line)[m.start.col - 1];
        if (isWordChar(before)) return false;
    }
    if (checkRight && m.end.line == curLine && m.end.col < cur.size()) {
        if (isWordChar(cur[m.end.col])) return false;
    }
    return true;
}

// Run KMP over the document from 'from' to the end, calling onMatch(Match) for
// each accepted match in order. onMatch returns false to stop early.
template <class F>
static void scan(const Document& d, Pos from, const std::string& pat, const SearchOptions& opt, F&& onMatch) {
    // Quick exit: the pattern is longer than the whole text (chars + line breaks).
    if (pat.size() > d.charCount() + d.lineCount() - 1) return;

    KmpMatcher km(pat, opt.matchCase);
    const bool checkLeft = opt.wholeWord && isWordChar(pat.front());
    const bool checkRight = opt.wholeWord && isWordChar(pat.back());
    const size_t lines = d.lineCount();

    for (size_t l = from.line; l < lines; ++l) {
        std::string s = d.getLine(l);
        for (size_t c = (l == from.line ? from.col : 0); c < s.size(); ++c) {
            if (!km.feed(s[c])) continue;
            Match m;
            m.end = Pos{l, c + 1};
            m.start = walkBack(d, m.end, pat.size());
            if (!wordBoundariesOk(d, m, checkLeft, checkRight, l, s)) continue;
            if (!onMatch(m)) return;
        }
        if (l + 1 < lines && km.feed('\n')) {          // the line break is part of the stream
            Match m;
            m.end = Pos{l + 1, 0};
            m.start = walkBack(d, m.end, pat.size());
            if (!wordBoundariesOk(d, m, checkLeft, checkRight, l, s)) continue;
            if (!onMatch(m)) return;
        }
    }
}

static void checkFrom(const Document& d, Pos from) {
    if (from.line >= d.lineCount())
        throw std::out_of_range("Line " + std::to_string(from.line + 1) + " does not exist.");
    if (from.col > d.lineLength(from.line))
        throw std::out_of_range("Column " + std::to_string(from.col + 1) + " is not valid on line " +
                                std::to_string(from.line + 1) + ".");
}

DynArray<Match> findAll(const Document& doc, const std::string& pattern,
                        const SearchOptions& opt, bool allowOverlap) {
    std::string pat = normalizePattern(pattern);
    DynArray<Match> out;
    bool haveLast = false;
    Pos lastEnd;
    scan(doc, Pos{0, 0}, pat, opt, [&](const Match& m) {
        if (!allowOverlap && haveLast && m.start < lastEnd) return true;   // overlaps the previous match
        out.push_back(m);
        haveLast = true;
        lastEnd = m.end;
        return true;
    });
    return out;
}

bool findNext(const Document& doc, Pos from, const std::string& pattern,
              const SearchOptions& opt, Match& out, bool wrap) {
    std::string pat = normalizePattern(pattern);
    checkFrom(doc, from);
    bool found = false;
    auto take = [&](const Match& m) { out = m; found = true; return false; };
    scan(doc, from, pat, opt, take);
    if (!found && wrap && (from.line > 0 || from.col > 0))
        scan(doc, Pos{0, 0}, pat, opt, take);          // nothing after 'from': any match is before it
    return found;
}

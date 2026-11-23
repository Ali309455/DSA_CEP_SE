// Brackets.cpp - see Brackets.h.
#include "Brackets.h"
#include "Stack.h"
#include "TextUtil.h"

char bracketPartner(char c) {
    switch (c) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default: return 0;
    }
}

bool BracketReport::matchOf(Pos p, Pos& other) const {
    for (size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].open == p) { other = pairs[i].close; return true; }
        if (pairs[i].close == p) { other = pairs[i].open; return true; }
    }
    return false;
}

namespace {

struct Open {
    char ch;
    Pos pos;
};

std::string where(Pos p) {
    return std::to_string(p.line + 1) + ":" + std::to_string(p.col + 1);
}


class Scanner {
public:
    Scanner(const BracketOptions& o, BracketReport& r) : opt_(o), rep_(r) {}

    void run(const Document& doc) {
        const size_t n = doc.lineCount();
        for (size_t ln = 0; ln < n; ++ln) scanLine(doc.getLine(ln), ln);
        finish();
    }

private:
    enum class State { Code, LineComment, BlockComment, Raw };

    const BracketOptions& opt_;
    BracketReport& rep_;
    Stack<Open> open_;
    State state_ = State::Code;
    bool lineCommentContinues_ = false;
    std::string rawDelim_;      // for raw strings: ")delim\""
    Pos rawStart_;
    Pos commentStart_;

    void addIssue(BracketIssueKind k, Pos pos, Pos related, char found, char expected, const std::string& msg) {
        if (opt_.maxIssues != 0 && rep_.issues.size() >= opt_.maxIssues) { rep_.truncated = true; return; }
        BracketIssue is;
        is.kind = k; is.pos = pos; is.related = related;
        is.found = found; is.expected = expected; is.message = msg;
        rep_.issues.push_back(is);
    }

    void closeMatched(const Open& o, Pos closePos) {
        BracketPair bp;
        bp.open = o.pos; bp.close = closePos; bp.type = o.ch; bp.depth = open_.size();   // already popped
        rep_.pairs.push_back(bp);
    }

    void onOpen(char c, Pos p) {
        open_.push(Open{c, p});
        if (open_.size() > rep_.maxDepth) rep_.maxDepth = open_.size();
    }

    void onClose(char c, Pos p) {
        const char want = bracketPartner(c);   // the opener this closer belongs to
        if (open_.empty()) {
            addIssue(BracketIssueKind::UnmatchedClose, p, p, c, 0,
                     std::string("Unmatched '") + c + "' at " + where(p) + ": nothing is open.");
            return;
        }
        if (open_.top().ch == want) {
            Open o = open_.pop();
            closeMatched(o, p);
            return;
        }
        // Does the closer match an opener further down?
        size_t depth = 1;
        bool found = false;
        for (; depth < open_.size(); ++depth)
            if (open_.peek(depth).ch == want) { found = true; break; }
        if (found) {
            // everything above that opener was never closed
            for (size_t k = 0; k < depth; ++k) {
                Open u = open_.pop();
                addIssue(BracketIssueKind::Unclosed, u.pos, u.pos, u.ch, bracketPartner(u.ch),
                         std::string("'") + u.ch + "' opened at " + where(u.pos) + " is never closed (before '" +
                         c + "' at " + where(p) + ").");
            }
            Open o = open_.pop();
            closeMatched(o, p);
        } else {
            Open o = open_.pop();
            addIssue(BracketIssueKind::Mismatch, p, o.pos, c, bracketPartner(o.ch),
                     std::string("Mismatch at ") + where(p) + ": found '" + c + "' but '" + o.ch + "' opened at " +
                     where(o.pos) + " expects '" + bracketPartner(o.ch) + "'.");
        }
    }

    // Try to start a raw string at s[i] == '"' (the character before it is 'R').
    // On success sets rawDelim_ and returns the index just after '('.
    bool beginRaw(const std::string& s, size_t i, size_t& after) {
        size_t j = i + 1;
        std::string delim;
        while (j < s.size() && s[j] != '(' && delim.size() <= 16) {
            char d = s[j];
            if (d == ' ' || d == ')' || d == '\\' || d == '"' || d == '\t') return false;   // not a valid delimiter
            delim.push_back(d);
            ++j;
        }
        if (j >= s.size() || s[j] != '(' || delim.size() > 16) return false;
        rawDelim_ = ")" + delim + "\"";
        after = j + 1;
        return true;
    }

    void scanLine(const std::string& s, size_t ln) {
        size_t i = 0;
        bool numberToken = false;     // inside a token that started with a digit (for 1'000)
        if (state_ == State::LineComment) {
            if (!lineCommentContinues_) state_ = State::Code;
        }
        if (state_ == State::LineComment) {
            // previous line ended with a backslash: this whole line is still comment
            lineCommentContinues_ = !s.empty() && s[s.size() - 1] == '\\';
            return;
        }
        while (i < s.size()) {
            const char c = s[i];
            if (state_ == State::BlockComment) {
                if (c == '*' && i + 1 < s.size() && s[i + 1] == '/') { state_ = State::Code; i += 2; }
                else ++i;
                continue;
            }
            if (state_ == State::Raw) {
                size_t k = s.find(rawDelim_, i);
                if (k == std::string::npos) return;          // continues on the next line
                i = k + rawDelim_.size();
                state_ = State::Code;
                continue;
            }
            // ---- Code ----
            if (opt_.skipComments && c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                state_ = State::LineComment;
                lineCommentContinues_ = !s.empty() && s[s.size() - 1] == '\\';
                return;
            }
            if (opt_.skipComments && c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                state_ = State::BlockComment;
                commentStart_ = Pos{ln, i};
                i += 2;
                continue;
            }
            if (opt_.skipStrings && c == '"') {
                size_t after = 0;
                if (i > 0 && s[i - 1] == 'R' && beginRaw(s, i, after)) {
                    state_ = State::Raw;
                    rawStart_ = Pos{ln, i};
                    i = after;
                    continue;
                }
                size_t j = i + 1;
                bool closed = false;
                while (j < s.size()) {
                    if (s[j] == '\\') { j += 2; continue; }
                    if (s[j] == '"') { closed = true; break; }
                    ++j;
                }
                if (!closed && !(s.size() > 0 && s[s.size() - 1] == '\\'))
                    addIssue(BracketIssueKind::UnterminatedString, Pos{ln, i}, Pos{ln, i}, 0, 0,
                             "String starting at " + where(Pos{ln, i}) + " has no closing quote on its line.");
                i = closed ? j + 1 : s.size();
                numberToken = false;
                continue;
            }
            if (opt_.skipStrings && c == '\'') {
                const bool separator = numberToken && i + 1 < s.size() && isWordChar(s[i + 1]);
                if (!separator) {
                    size_t j = i + 1;
                    bool closed = false;
                    while (j < s.size()) {
                        if (s[j] == '\\') { j += 2; continue; }
                        if (s[j] == '\'') { closed = true; break; }
                        ++j;
                    }
                    i = closed ? j + 1 : s.size();   // unterminated ' is not reported (apostrophes in #error etc.)
                    numberToken = false;
                    continue;
                }
                ++i;
                continue;
            }
            if (isWordChar(c)) {
                if (i == 0 || !(isWordChar(s[i - 1]) || (s[i - 1] == '\'' && numberToken))) numberToken = isDigitChar(c);
                ++i;
                continue;
            }
            if (c != '\'') numberToken = false;
            if (isOpenBracket(c)) onOpen(c, Pos{ln, i});
            else if (isCloseBracket(c)) onClose(c, Pos{ln, i});
            ++i;
        }
    }

    void finish() {
        if (state_ == State::BlockComment)
            addIssue(BracketIssueKind::UnterminatedComment, commentStart_, commentStart_, 0, 0,
                     "Comment starting at " + where(commentStart_) + " is never closed (missing */).");
        if (state_ == State::Raw)
            addIssue(BracketIssueKind::UnterminatedString, rawStart_, rawStart_, 0, 0,
                     "Raw string starting at " + where(rawStart_) + " is never closed.");
        // Whatever is left was never closed. Report newest first popped, but list in text order.
        const size_t left = open_.size();
        DynArray<Open> rest;
        for (size_t k = 0; k < left; ++k) rest.push_back(open_.pop());
        for (size_t k = rest.size(); k-- > 0;) {
            const Open& u = rest[k];
            addIssue(BracketIssueKind::Unclosed, u.pos, u.pos, u.ch, bracketPartner(u.ch),
                     std::string("'") + u.ch + "' opened at " + where(u.pos) + " is never closed.");
        }
    }
};

}  // namespace

BracketReport checkBrackets(const Document& doc, const BracketOptions& opt) {
    BracketReport rep;
    Scanner sc(opt, rep);
    sc.run(doc);
    return rep;
}

bool matchNearCursor(const Document& doc, const BracketReport& report, Pos cursor,
                     Pos& bracket, Pos& other) {
    if (cursor.line >= doc.lineCount()) throw std::out_of_range("matchNearCursor: line out of range");
    const std::string line = doc.getLine(cursor.line);
    if (cursor.col > line.size()) throw std::out_of_range("matchNearCursor: column out of range");
    if (cursor.col < line.size() && (isOpenBracket(line[cursor.col]) || isCloseBracket(line[cursor.col]))) {
        Pos p{cursor.line, cursor.col};
        if (report.matchOf(p, other)) { bracket = p; return true; }
    }
    if (cursor.col > 0 && (isOpenBracket(line[cursor.col - 1]) || isCloseBracket(line[cursor.col - 1]))) {
        Pos p{cursor.line, cursor.col - 1};
        if (report.matchOf(p, other)) { bracket = p; return true; }
    }
    return false;
}

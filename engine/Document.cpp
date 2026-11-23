#include "Document.h"
#include "TextUtil.h"


struct Document::Node {
    DynArray<char> text;
    Node* prev = nullptr;
    Node* next = nullptr;
};

// ---------------------------------------------------------------- lifetime

Document::Document()
    : head_(nullptr), tail_(nullptr), lineCount_(0), charCount_(0),
      cache_(nullptr), cacheIdx_(0), modified_(false) {
    resetToOneLine();
}

Document::~Document() { freeAll(); }

void Document::freeAll() {
    Node* n = head_;
    while (n) {
        Node* next = n->next;
        delete n;
        n = next;
    }
    head_ = tail_ = cache_ = nullptr;
    lineCount_ = charCount_ = cacheIdx_ = 0;
}

void Document::resetToOneLine() {
    freeAll();
    Node* n = new Node;
    head_ = tail_ = cache_ = n;
    cacheIdx_ = 0;
    lineCount_ = 1;
    charCount_ = 0;
}

void Document::clear() {
    resetToOneLine();
    crlf_ = false;
    path_.clear();
    modified_ = false;
}

// ---------------------------------------------------------------- lookup

// Walk from whichever of {head, tail, cache} is closest to the wanted line.
Document::Node* Document::nodeAt(size_t idx) const {
    if (idx >= lineCount_) throw std::out_of_range("line number out of range");
    size_t last = lineCount_ - 1;
    size_t dHead = idx;
    size_t dTail = last - idx;
    size_t dCache = idx > cacheIdx_ ? idx - cacheIdx_ : cacheIdx_ - idx;

    Node* n;
    size_t at;
    if (dCache <= dHead && dCache <= dTail) {
        n = cache_;
        at = cacheIdx_;
    } else if (dHead <= dTail) {
        n = head_;
        at = 0;
    } else {
        n = tail_;
        at = last;
    }
    while (at < idx) { n = n->next; ++at; }
    while (at > idx) { n = n->prev; --at; }

    cache_ = n;
    cacheIdx_ = idx;
    return n;
}

void Document::checkPos(Pos p) const {
    Node* n = nodeAt(p.line);  // throws if the line does not exist
    if (p.col > n->text.size()) throw std::out_of_range("column out of range for that line");
}

size_t Document::lineLength(size_t line) const { return nodeAt(line)->text.size(); }

std::string Document::getLine(size_t line) const {
    Node* n = nodeAt(line);
    std::string s;
    s.reserve(n->text.size());
    for (size_t i = 0; i < n->text.size(); ++i) s.push_back(n->text[i]);
    return s;
}

std::string Document::getRange(Pos a, Pos b) const {
    orderPos(a, b);
    checkPos(a);
    checkPos(b);
    std::string out;
    Node* n = nodeAt(a.line);
    if (a.line == b.line) {
        for (size_t i = a.col; i < b.col; ++i) out.push_back(n->text[i]);
        return out;
    }
    for (size_t i = a.col; i < n->text.size(); ++i) out.push_back(n->text[i]);
    out.push_back('\n');
    n = n->next;
    for (size_t ln = a.line + 1; ln < b.line; ++ln) {
        for (size_t i = 0; i < n->text.size(); ++i) out.push_back(n->text[i]);
        out.push_back('\n');
        n = n->next;
    }
    for (size_t i = 0; i < b.col; ++i) out.push_back(n->text[i]);
    return out;
}

// ---------------------------------------------------------------- primitives

Pos Document::insertText(Pos at, const std::string& text) {
    checkPos(at);
    Node* cur = nodeAt(at.line);
    size_t curIdx = at.line;

    // Detach the part of the line after the insertion point; it is re-attached
    // to the end of the last inserted line.
    DynArray<char> tail;
    for (size_t i = at.col; i < cur->text.size(); ++i) tail.push_back(cur->text[i]);
    cur->text.truncate(at.col);
    charCount_ -= tail.size();

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            Node* fresh = new Node;
            fresh->prev = cur;
            fresh->next = cur->next;
            if (cur->next) cur->next->prev = fresh; else tail_ = fresh;
            cur->next = fresh;
            cur = fresh;
            ++curIdx;
            ++lineCount_;
        } else {
            cur->text.push_back(c);
            ++charCount_;
        }
    }

    Pos end{curIdx, cur->text.size()};
    cur->text.append(tail);
    charCount_ += tail.size();

    cache_ = cur;
    cacheIdx_ = curIdx;
    if (!text.empty()) modified_ = true;
    return end;
}

std::string Document::deleteRange(Pos a, Pos b) {
    orderPos(a, b);
    checkPos(a);
    checkPos(b);
    if (a == b) return std::string();

    std::string removed = getRange(a, b);
    Node* na = nodeAt(a.line);

    if (a.line == b.line) {
        na->text.erase(a.col, b.col);
        charCount_ -= (b.col - a.col);
    } else {
        Node* nb = nodeAt(b.line);
        size_t before = na->text.size();

        na->text.truncate(a.col);
        for (size_t i = b.col; i < nb->text.size(); ++i) na->text.push_back(nb->text[i]);

        // unlink and free every node after 'na' up to and including 'nb'
        Node* stop = nb->next;
        Node* n = na->next;
        while (n != stop) {
            Node* next = n->next;
            before += n->text.size();
            delete n;
            n = next;
        }
        na->next = stop;
        if (stop) stop->prev = na; else tail_ = na;

        lineCount_ -= (b.line - a.line);
        charCount_ -= (before - na->text.size());
    }

    cache_ = na;
    cacheIdx_ = a.line;
    modified_ = true;
    return removed;
}

// ---------------------------------------------------------------- conveniences

Pos Document::insertNewLine(Pos at) { return insertText(at, "\n"); }

void Document::insertLineBefore(size_t line, const std::string& text) {
    if (line > lineCount_) throw std::out_of_range("line number out of range");
    if (line == lineCount_) {
        size_t last = lineCount_ - 1;
        insertText(Pos{last, lineLength(last)}, "\n" + text);
    } else {
        insertText(Pos{line, 0}, text + "\n");
    }
}

std::string Document::deleteChars(Pos at, size_t count) {
    checkPos(at);
    if (count == 0) throw std::invalid_argument("count must be at least 1");
    size_t len = lineLength(at.line);
    if (at.col >= len) throw std::out_of_range("there is no character at that position");
    size_t endCol = (count > len - at.col) ? len : at.col + count;
    return deleteRange(at, Pos{at.line, endCol});
}

void Document::wordRange(Pos at, Pos& a, Pos& b) const {
    checkPos(at);
    Node* n = nodeAt(at.line);
    if (at.col >= n->text.size()) throw std::out_of_range("there is no character at that position");
    if (!isWordChar(n->text[at.col])) throw std::invalid_argument("there is no word at that position");
    size_t l = at.col;
    size_t r = at.col;
    while (l > 0 && isWordChar(n->text[l - 1])) --l;
    while (r < n->text.size() && isWordChar(n->text[r])) ++r;
    a = Pos{at.line, l};
    b = Pos{at.line, r};
}

std::string Document::deleteWord(Pos at) {
    Pos a, b;
    wordRange(at, a, b);
    return deleteRange(a, b);
}

void Document::lineSpan(size_t from, size_t to, Pos& a, Pos& b) const {
    if (from > to || to >= lineCount_) throw std::out_of_range("line range out of range");
    size_t last = lineCount_ - 1;
    if (to < last) {
        a = Pos{from, 0};
        b = Pos{to + 1, 0};
    } else if (from > 0) {
        // removing up to the end: also take the line break before 'from'
        a = Pos{from - 1, lineLength(from - 1)};
        b = Pos{last, lineLength(last)};
    } else {
        // removing everything: leaves one empty line
        a = Pos{0, 0};
        b = Pos{last, lineLength(last)};
    }
}

std::string Document::deleteLines(size_t from, size_t to) {
    Pos a, b;
    lineSpan(from, to, a, b);
    return deleteRange(a, b);
}

// ---------------------------------------------------------------- whole document

void Document::loadFromString(const std::string& text) {
    resetToOneLine();
    Node* cur = head_;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            Node* fresh = new Node;
            fresh->prev = cur;
            cur->next = fresh;
            tail_ = fresh;
            cur = fresh;
            ++lineCount_;
        } else {
            cur->text.push_back(c);
            ++charCount_;
        }
    }
    // A trailing '\n' ends the last line; it does not start a new one.
    if (!text.empty() && text[text.size() - 1] == '\n' && lineCount_ > 1) {
        Node* last = tail_;
        tail_ = last->prev;
        tail_->next = nullptr;
        delete last;
        --lineCount_;
    }
    cache_ = head_;
    cacheIdx_ = 0;
    modified_ = false;
}

std::string Document::toString() const {
    std::string out;
    if (isEmpty()) return out;
    out.reserve(charCount_ + lineCount_);
    for (Node* n = head_; n; n = n->next) {
        for (size_t i = 0; i < n->text.size(); ++i) out.push_back(n->text[i]);
        out.push_back('\n');
    }
    return out;
}

bool Document::checkInvariants() const {
    if (!head_ || !tail_ || head_->prev || tail_->next) return false;
    size_t lines = 0, chars = 0;
    Node* prev = nullptr;
    for (Node* n = head_; n; n = n->next) {
        if (n->prev != prev) return false;
        ++lines;
        chars += n->text.size();
        prev = n;
    }
    if (prev != tail_) return false;
    if (lines != lineCount_ || chars != charCount_) return false;
    if (!cache_ || cacheIdx_ >= lineCount_) return false;
    Node* check = head_;
    for (size_t i = 0; i < cacheIdx_; ++i) check = check->next;
    return check == cache_;
}

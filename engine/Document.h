// Document.h - the text buffer.
//
// Representation: a DOUBLY LINKED LIST of lines. Each line is a DynArray<char>.
//
// Why: inserting or removing a whole line only rewires pointers (no shifting of
// the rest of the document), and editing inside a line only touches that one
// short array. A cursor cache (last line visited) makes sequential access
// (typing, scrolling, printing) O(1) per step instead of O(n).
//
// Positions are 0-based internally: line in [0, lineCount), col in [0, lineLength].
// col == lineLength means "end of the line".
//
// Everything that changes text goes through two primitives:
//     insertText(pos, text)  and  deleteRange(a, b)
// Every other editing function is built on them. That is what makes undo easy
// (phase 2): the inverse of an insert is a delete of the same range and vice versa.
//
// Invariant: a Document always has at least one line (an "empty" document is
// one empty line).
#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>
#include "DynArray.h"

struct Pos {
    size_t line = 0;
    size_t col = 0;
};

inline bool operator==(const Pos& a, const Pos& b) { return a.line == b.line && a.col == b.col; }
inline bool operator!=(const Pos& a, const Pos& b) { return !(a == b); }
inline bool operator<(const Pos& a, const Pos& b) {
    return a.line < b.line || (a.line == b.line && a.col < b.col);
}
// Swap a and b if needed so that a comes first (a range given "backwards" works the same).
inline void orderPos(Pos& a, Pos& b) {
    if (b < a) { const Pos t = a; a = b; b = t; }
}

class Document {
public:
    Document();
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // ---- whole-document operations ----
    void clear();                                  // back to one empty line, no filename
    void loadFromString(const std::string& text);  // replace content (O(N))
    std::string toString() const;                  // lines joined, each ending in '\n' (O(N))

    // ---- information ----
    size_t lineCount() const { return lineCount_; }
    size_t charCount() const { return charCount_; }  // characters, not counting line breaks
    bool isEmpty() const { return lineCount_ == 1 && charCount_ == 0; }
    size_t lineLength(size_t line) const;                  // O(distance to line)
    std::string getLine(size_t line) const;                // O(distance + L)
    std::string getRange(Pos a, Pos b) const;              // text between a and b ('\n' for breaks)

    // ---- the two editing primitives ----
    // Insert text (may contain '\n') at 'at'. Returns the position just after the
    // inserted text. '\r' characters are ignored. Cost: O(|text| + tail of line).
    Pos insertText(Pos at, const std::string& text);

    // Delete everything between a and b (either order; end exclusive).
    // Returns the deleted text. Cost: O(deleted size + lines removed).
    std::string deleteRange(Pos a, Pos b);

    // ---- conveniences built on the primitives ----
    Pos insertNewLine(Pos at);                              // split the line at 'at'
    void insertLineBefore(size_t line, const std::string& text);  // line == lineCount appends
    std::string deleteChars(Pos at, size_t count);          // within one line, clamped to its end
    std::string deleteWord(Pos at);                         // the word containing 'at'
    std::string deleteLines(size_t from, size_t to);        // inclusive range of whole lines
    // The start and end of the word containing 'at' (what deleteWord removes).
    void wordRange(Pos at, Pos& a, Pos& b) const;
    // Positions that cover whole lines [from, to] (what deleteLines removes).
    void lineSpan(size_t from, size_t to, Pos& a, Pos& b) const;

    // ---- file bookkeeping ----
    const std::string& path() const { return path_; }
    void setPath(const std::string& p) { path_ = p; }
    // Line endings of the file. The text itself never contains '\r'; if the file had
    // Windows line endings (CRLF) this is true and saving writes them back, so opening and
    // saving a file does not rewrite every line ending. A new / cleared document is false (LF).
    bool crlf() const { return crlf_; }
    void setCrlf(bool v) { crlf_ = v; }
    bool modified() const { return modified_; }
    void markSaved() { modified_ = false; }

    // Walks the list and verifies links and counters. For tests.
    bool checkInvariants() const;

private:
    struct Node;
    Node* head_;
    Node* tail_;
    size_t lineCount_;
    size_t charCount_;
    mutable Node* cache_;      // last node looked up
    mutable size_t cacheIdx_;  // its line number
    std::string path_;
    bool modified_;
    bool crlf_ = false;

    Node* nodeAt(size_t line) const;     // O(min(line, n-line, |line-cache|))
    void checkPos(Pos p) const;
    void freeAll();
    void resetToOneLine();
};

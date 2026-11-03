// Search.h - Knuth-Morris-Pratt (KMP) string search (phase 3).
//
// Two layers:
//
//  1. KMP on plain strings   (buildFailureTable, KmpMatcher, kmpFindAll)
//  2. KMP on a Document      (findAll, findNext)
//
// Why KMP: the naive search can re-read the same characters again and again
// (searching "aaaaab" in "aaaa...a" is O(n*m)). KMP precomputes, for every
// prefix of the pattern, the length of its longest proper prefix that is also
// a suffix (the "failure table"). When a mismatch happens it falls back along
// that table instead of going back in the text, so the text is read exactly
// once:   build table O(m)  +  scan O(n)  =  O(n + m)  in the worst case,
// with O(m) extra memory.
//
// The document is searched as ONE stream of characters: the characters of line
// 0, a '\n', the characters of line 1, a '\n', ... So a pattern may contain
// '\n' and match across lines. The text is never copied as a whole: the matcher
// is fed one character at a time and keeps its state between lines.
//
// Rules shared by all functions here:
//   * An empty search text is an error (std::invalid_argument).
//   * '\r' is dropped from the search text (the document never contains it).
//   * Positions are 0-based {line, col}; a Match's end is EXCLUSIVE.
//   * Searching an empty document is not an error: there are simply 0 matches.
//   * Searching never changes the document.
#pragma once
#include <cstddef>
#include <string>
#include "Document.h"
#include "DynArray.h"

struct SearchOptions {
    bool matchCase = true;    // false: 'a' equals 'A' (ASCII letters)
    bool wholeWord = false;   // true: a match must not touch a letter, digit or '_'
                              //       on a side where the search text itself starts/ends with one
};

struct Match {
    Pos start;   // first character of the match
    Pos end;     // just after the last character
};

// ---------------------------------------------------------------- plain strings

// failure[i] = length of the longest proper prefix of pattern[0..i] that is
// also a suffix of it. O(m) time and space. (Empty pattern -> empty table.)
DynArray<size_t> buildFailureTable(const std::string& pattern);

// Feeds characters one at a time and tells when the pattern has just ended.
// After a match it continues from the failure table, so OVERLAPPING matches
// ("aa" in "aaa") are all reported.
class KmpMatcher {
public:
    // Throws std::invalid_argument if the pattern is empty.
    KmpMatcher(const std::string& pattern, bool matchCase);
    void reset() { state_ = 0; }
    bool feed(char c);                       // true if a match ends at this character
    size_t patternLength() const { return pat_.size(); }
private:
    std::string pat_;                        // already case-folded when !matchCase
    DynArray<size_t> fail_;
    size_t state_;                           // how many pattern chars are currently matched
    bool matchCase_;
};

// Start offsets (0-based) of every occurrence of 'pattern' in 'text',
// overlapping ones included, in increasing order.
// Throws std::invalid_argument for an empty pattern.
DynArray<size_t> kmpFindAll(const std::string& text, const std::string& pattern, bool matchCase = true);

// ---------------------------------------------------------------- documents

// Remove '\r' from the search text; throws std::invalid_argument if nothing is left.
std::string normalizePattern(const std::string& pattern);

// Every occurrence in document order. With allowOverlap == false a match that
// starts before the previous one ended is skipped (what "replace all" uses).
// Throws std::invalid_argument for an empty search text.
// Time O(N + m) for N characters in the document; memory O(m + number of matches).
DynArray<Match> findAll(const Document& doc, const std::string& pattern,
                        const SearchOptions& opt = SearchOptions(), bool allowOverlap = true);

// First occurrence that starts at or after 'from'. If there is none and 'wrap'
// is true, the search continues from the start of the document. Returns false
// when nothing is found. Throws std::out_of_range if 'from' is not a valid
// position, std::invalid_argument for an empty search text.
bool findNext(const Document& doc, Pos from, const std::string& pattern,
              const SearchOptions& opt, Match& out, bool wrap = true);

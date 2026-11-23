// Formatter.h - "Format Document" for C, C++ and other brace languages (version B: indentation and
// spacing; long lines are never re-wrapped).
//
// What it does
//   * re-indents every line from the nesting of { } (a stack, like the bracket checker), with
//     case labels one level in from their switch, their statements one more, access specifiers
//     (public: / private: / protected:) at the level of their class, and namespace bodies NOT
//     indented (the style this project uses);
//   * normalises spaces between tokens: "a=b" -> "a = b", "f (x)" -> "f(x)", "if(" -> "if (",
//     "x ,y" -> "x, y", "( a )" -> "(a)", "){" -> ") {", "}else" -> "} else", "a+b" -> "a + b",
//     runs of blanks -> one blank;
//   * removes trailing blanks and keeps at most maxBlankLines empty lines in a row.
//
// What it never touches (so it cannot break code)
//   * the inside of strings, character literals, raw strings and comments;
//   * preprocessor lines (#include, #define and their \ continuations);
//   * lines it cannot place with certainty: lines inside an unclosed ( or [ from an earlier line,
//     continuation lines of a statement (after "if (x)" with no brace, a wrapped expression, a
//     constructor's ": a(1)" list ...) and the inside of a multi-line comment keep their position
//     RELATIVE to the line they continue. They move with it, but their own alignment is kept.
//   * Only whitespace between tokens changes, never a token itself, and blanks are never removed
//     between two tokens that could merge (like "- -"), so the program means exactly the same.
//
// How it works (data structures)
//   tokenizer   a small state machine per line (code, block comment, raw string, string continued
//               with \), with the state carried to the next line: O(characters).
//   indentation a Stack of open brackets; each entry remembers the indentation of the line that
//               opened it, so a closer lines up with its opener and a block's body is one level in:
//               O(tokens).
//   spacing     rules decided per pair of neighbouring tokens: O(tokens).
//   applying    the old and new lines are compared with a line diff (longest common subsequence,
//               dynamic programming, after cutting the common start and end), and only the changed
//               lines are replaced, bottom to top, inside ONE undo group: one Ctrl+Z restores the
//               file, and lines that did not change keep their place (cursor, folds, marks).
#pragma once
#include <cstddef>
#include <string>
#include "DynArray.h"

class Editor;

struct FormatOptions {
    size_t indentWidth = 4;      // columns per nesting level
    size_t tabWidth = 4;         // how wide an existing tab is when measuring old indentation
    size_t maxBlankLines = 2;    // longer runs of empty lines are shortened
    bool spacing = true;         // false: only indentation, trailing blanks and blank lines
};

// Format a whole file given as lines (no '\n' inside). Always returns at least one line.
DynArray<std::string> formatLines(const DynArray<std::string>& lines, const FormatOptions& opt = FormatOptions());
// The same for text with '\n' line breaks (the result has no trailing '\n' unless the input had one).
std::string formatText(const std::string& text, const FormatOptions& opt = FormatOptions());

// A block of lines that differs: old lines [oldStart, oldEnd) become new lines [newStart, newEnd).
struct LineHunk {
    size_t oldStart = 0, oldEnd = 0;
    size_t newStart = 0, newEnd = 0;
};
// The changed blocks between two versions, top to bottom. Common first / last lines are skipped;
// the rest is aligned by the longest common subsequence of lines when that table is small enough
// (at most kMaxDiffCells cells), otherwise the middle is one block.
DynArray<LineHunk> diffLines(const DynArray<std::string>& a, const DynArray<std::string>& b);
const size_t kMaxDiffCells = 4000000;

// Format the editor's document in ONE undo step labelled "format document". Returns how many lines
// changed (0: it was already formatted, nothing recorded).
size_t formatDocument(Editor& ed, const FormatOptions& opt = FormatOptions());

// EditOps.h - the "code editor" editing commands (phase 5). No GUI code.
//
// Everything here works THROUGH the Editor, so each command is exactly ONE undo step
// (an EditGroup around the primitive edits). The GUI only decides which command a key
// press means and then moves its cursor / selection using what the command returns.
//
//   line commands : moveLines (Alt+Up / Alt+Down), duplicateLines, indentLines,
//                   outdentLines, toggleLineComment, insertLineBelow / insertLineAbove
//   typing helpers: auto-closing pairs, wrapping a selection in ( ) { } [ ] " ' `,
//                   smart Enter (auto-indent, block between braces), smart Home
//
// Lines are 0-based, columns count bytes (as everywhere in the engine).
#pragma once
#include <cstddef>
#include <string>
#include "Document.h"
#include "DynArray.h"
#include "Editor.h"

// ---------------------------------------------------------------- column bookkeeping

// "delta characters were inserted (+) or removed (-) on 'line' starting at column 'at'".
// A line command returns one of these per line it touched, so the GUI can keep the
// cursor and the selection where the user expects them (see adjustPos).
struct ColEdit {
    size_t line;
    size_t at;
    long delta;
};

// Where a position ends up after the edits (at most one edit per line). A position
// inside removed text moves to the start of the removed text.
Pos adjustPos(Pos p, const DynArray<ColEdit>& edits);

// ---------------------------------------------------------------- line commands

// Move the whole lines [from, to] one line up (dir < 0) or down (dir > 0). Returns false,
// changing nothing, when the block is already at the top / bottom. One undo step.
// Cost O(characters in the moved lines + the neighbour line).
// Throws std::out_of_range if the range is not inside the document.
bool moveLines(Editor& ed, size_t from, size_t to, int dir);

// Copy the lines [from, to] and put the copy directly below them. One undo step.
void duplicateLines(Editor& ed, size_t from, size_t to);

// Add 'width' spaces at the start of every non-empty line in [from, to]
// (empty lines stay empty, no trailing blanks).
DynArray<ColEdit> indentLines(Editor& ed, size_t from, size_t to, size_t width = 4);

// Remove one level of indentation from every line in [from, to]: one tab, or up to
// 'width' spaces.
DynArray<ColEdit> outdentLines(Editor& ed, size_t from, size_t to, size_t width = 4);

// If every non-blank line in [from, to] starts (after its indentation) with 'marker',
// remove the marker and one blank after it; otherwise add "marker " to all non-blank lines
// at the smallest indentation among them. *commented tells which one happened.
// Blank lines are never touched.
DynArray<ColEdit> toggleLineComment(Editor& ed, size_t from, size_t to,
                                    const std::string& marker = "//", bool* commented = nullptr);

// A new, indented line below / above 'line'. Returns the cursor position on it.
Pos insertLineBelow(Editor& ed, size_t line, size_t width = 4);
Pos insertLineAbove(Editor& ed, size_t line, size_t width = 4);

// ---------------------------------------------------------------- typing helpers

// The closing character for ( [ { " ' ` ; 0 for anything else.
char pairCloser(char open);

// Should typing 'open' at 'at' also insert its closing partner? Brackets: only if what
// follows is the end of the line, a blank or a closing bracket / , ; . (not in front of a
// word). Quotes: additionally not right after a letter, digit or the same quote, so
// don't, 1'000 and "" "" keep working.
bool shouldAutoPair(const Document& doc, Pos at, char open);

// True if the cursor sits between an opener and its partner: ( | )  [ | ]  { | }  " | "  ' | '
// (Backspace then removes both).
bool isEmptyPairAround(const Document& doc, Pos at);

struct SelRange {
    Pos start;
    Pos end;
};

// Wrap the text between a and b with 'open' and its partner (one undo step). Returns the
// range of the ORIGINAL text in the new document, so the selection can be kept on it.
// Throws std::invalid_argument if nothing is selected (a == b) or 'open' has no partner,
// std::out_of_range for bad positions; nothing changes then.
SelRange surroundSelection(Editor& ed, Pos a, Pos b, char open);

// Enter: split the line at 'at', keep its indentation, add one level after { ( [ , and if
// the cursor is between a pair ( {|} ) open a block with the closer on its own line.
// One undo step. Returns the new cursor position.
Pos insertNewLineSmart(Editor& ed, Pos at, size_t width = 4);

// ---------------------------------------------------------------- small string helpers

// The leading blanks (spaces and tabs) of a line.
std::string leadingWhitespace(const std::string& line);

// Home key: first press goes to the first non-blank character, next press to column 0.
size_t smartHomeCol(const std::string& line, size_t col);

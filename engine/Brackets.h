// Brackets.h - bracket checker (phase 4), built on our own Stack.
//
// Checks that every  (  [  {  has a partner  )  ]  }  of the same kind, in the
// right order, in C++ source text. Classic stack algorithm:
//     opener -> push (character + position)
//     closer -> pop and compare; a different kind, or an empty stack, is an error
//     end    -> whatever is still on the stack was never closed
//
// Brackets that are NOT code are ignored, otherwise `")"` or `// :-)` would be
// reported as errors:
//     "string literals"   'c' character literals   R"x( raw strings )x"
//     // line comments (a trailing backslash continues them)   /* block comments */
// A ' between digits (1'000'000) is a digit separator, not a character literal.
//
// The whole document is scanned once, line by line, with the lexer state kept
// between lines (block comments and raw strings can span lines):
//     time O(N), extra memory O(deepest nesting + number of pairs).
// The document is never copied and never changed.
//
// Recovery (so one typo gives one message, not a cascade):
//   * closer on an empty stack ................ UnmatchedClose, nothing popped
//   * closer matches an opener further down ... the openers above it are reported
//                                               Unclosed and popped, then it pairs up
//   * closer matches nothing on the stack ..... Mismatch, the top opener is popped
//                                               (treated as a wrong-kind typo)
// Issue list is capped (maxIssues); 'truncated' says so.
#pragma once
#include <cstddef>
#include <string>
#include "Document.h"
#include "DynArray.h"

struct BracketPair {
    Pos open;        // position of the opening bracket
    Pos close;       // position of the closing bracket
    char type;       // the opener: '(' '[' or '{'
    size_t depth;    // 0 = outermost (handy for coloured brackets in the GUI)
};

enum class BracketIssueKind {
    UnmatchedClose,       // ')' with nothing open
    Mismatch,             // '(' closed by ']'
    Unclosed,             // '{' never closed
    UnterminatedComment,  // /* with no */
    UnterminatedString    // " with no closing " on the line (or raw string never ended)
};

struct BracketIssue {
    BracketIssueKind kind;
    Pos pos;             // where the problem is (the offending closer, or the unclosed opener)
    Pos related;         // Mismatch: the opener it should have matched. Otherwise == pos.
    char found;          // the bracket character involved (0 for comment / string issues)
    char expected;       // Mismatch: the closer that was expected (else 0)
    std::string message; // ready to show, 1-based line:column
};

struct BracketOptions {
    bool skipStrings = true;    // ignore brackets inside "..." '...' and raw strings
    bool skipComments = true;   // ignore brackets inside // and /* */
    size_t maxIssues = 200;     // stop recording after this many (0 = no limit)
};

struct BracketReport {
    DynArray<BracketPair> pairs;     // every matched pair, ordered by closing position
    DynArray<BracketIssue> issues;   // in the order found; openers still open at the end come last
    bool truncated = false;          // more issues existed than maxIssues
    size_t maxDepth = 0;             // deepest nesting seen

    bool ok() const { return issues.empty(); }

    // The partner of the bracket character at exactly 'p'. False if 'p' is not a
    // matched bracket. O(number of pairs).
    bool matchOf(Pos p, Pos& other) const;
};

// Scan the whole document. Never throws for ordinary text (an empty document
// is simply OK); never changes the document.
BracketReport checkBrackets(const Document& doc, const BracketOptions& opt = BracketOptions());

// For "highlight the matching bracket": looks at the character AT the cursor,
// then the one just BEFORE it (the usual editor behaviour). On success 'bracket'
// is the bracket next to the cursor and 'other' its partner.
// Throws std::out_of_range if 'cursor' is not a valid position.
bool matchNearCursor(const Document& doc, const BracketReport& report, Pos cursor,
                     Pos& bracket, Pos& other);

// The closing bracket for an opener / the opening bracket for a closer; 0 if 'c' is neither.
char bracketPartner(char c);
inline bool isOpenBracket(char c) { return c == '(' || c == '[' || c == '{'; }
inline bool isCloseBracket(char c) { return c == ')' || c == ']' || c == '}'; }

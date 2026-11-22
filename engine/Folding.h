// Folding.h - code folding (collapse a block to its first line).
//
// Two parts:
//
//   foldRanges...  WHICH lines can be folded. A range is a header line that stays visible and the
//                  lines under it that disappear:  start = header,  last = last hidden line.
//                  * C-like code: every bracket pair from the bracket checker (Brackets.h) that spans
//                    3 or more lines. The line with the closing bracket stays visible, so
//                    "} else {" never hides code:      void f() {   ...   }
//                  * Python / scripts: by indentation, with a stack of open blocks.
//
//   FoldSet        WHAT is folded now, and the mapping between document lines and the rows the
//                  editor shows. Folds are kept sorted by start line; folds inside folds are allowed.
//                  From them the set builds the hidden lines as disjoint intervals, each with the
//                  number of hidden lines before it (prefix sums). Then
//                      rowOf(line), lineAt(row), isHidden(line) ... O(log k)   k = folded ranges
//                      fold / unfold / reveal ............................. O(k)
//                  so painting, scrolling and Up / Down cost the same with 0 or 500 folds.
#pragma once
#include <cstddef>
#include "Brackets.h"
#include "Document.h"
#include "DynArray.h"

struct FoldRange {
    size_t start = 0;    // the header line: stays visible
    size_t last = 0;     // the last hidden line (> start)
};

// Bracket pairs that span at least 3 lines, sorted by start line, one range per start line
// (the largest one, e.g. "{" wins over a "(" that opens on the same line and closes earlier).
// O(L + P): the pairs are bucketed by their opening line (counting sort), L lines, P pairs.
DynArray<FoldRange> foldRangesFromBrackets(const BracketReport& report, size_t lineCount);

// Indentation blocks: a non-blank line followed by more-indented lines (blank lines inside belong
// to the block, trailing blank lines do not). Tabs count as 'tabWidth' columns. O(N) with a stack.
DynArray<FoldRange> foldRangesByIndent(const Document& doc, size_t tabWidth = 4);

// The range that starts exactly on 'line', or nullptr. Binary search, O(log R).
const FoldRange* foldRangeAt(const DynArray<FoldRange>& ranges, size_t line);
// The smallest range that contains 'line' (its header or one of its hidden lines), or nullptr. O(R).
const FoldRange* innermostFoldRange(const DynArray<FoldRange>& ranges, size_t line);

class FoldSet {
public:
    bool empty() const { return folds_.empty(); }
    size_t count() const { return folds_.size(); }
    const DynArray<FoldRange>& folds() const { return folds_; }   // sorted by start

    bool isFolded(size_t startLine) const;
    bool fold(const FoldRange& r);            // false if it is already folded or not a valid range
    bool unfold(size_t startLine);            // false if nothing was folded there
    void clear();

    // ---- the line <-> row mapping ----
    bool isHidden(size_t line) const;
    size_t hiddenLines() const { return hiddenTotal_; }
    size_t rowCount(size_t lineCount) const { return lineCount > hiddenTotal_ ? lineCount - hiddenTotal_ : 1; }
    // The row that shows 'line'. A hidden line maps to the row of the header above its block.
    size_t rowOf(size_t line) const;
    // The document line shown on 'row' (rows past the end give the last visible line).
    size_t lineAt(size_t row) const;
    // The next / previous visible line (Up / Down). Stays on 'line' at the ends.
    size_t nextVisible(size_t line, size_t lineCount) const;
    size_t prevVisible(size_t line) const;

    // Unfold every fold that hides 'line' (the cursor went there). True if something changed.
    bool reveal(size_t line);

    // The text changed: an edit that began on 'fromLine' added (delta > 0) or removed lines.
    // Folds below move with the text, a fold around the edit grows or shrinks, a fold that would
    // become empty is dropped. Use keepOnly() once fresh ranges are known.
    void linesChanged(size_t fromLine, long delta);
    // Keep only the folds whose start line is still foldable, with that range's new extent.
    void keepOnly(const DynArray<FoldRange>& valid);

private:
    DynArray<FoldRange> folds_;     // what the user folded, sorted by start
    DynArray<size_t> hs_, he_;      // hidden lines as disjoint intervals [hs_[k], he_[k]], sorted
    DynArray<size_t> before_;       // hidden lines in intervals 0 .. k-1
    size_t hiddenTotal_ = 0;
    void rebuild();
    size_t intervalsAtOrBefore(size_t line) const;   // how many intervals start at or before 'line'
};

#include "Folding.h"
#include <string>
#include "Stack.h"

// ---------------------------------------------------------------- which lines can fold

// One slot per line holds the largest 'last' seen for a range starting there (0 = none).
// Walking the slots in order gives the ranges already sorted: a counting sort by start line.
static DynArray<FoldRange> collect(const DynArray<size_t>& lastOf) {
    DynArray<FoldRange> out;
    for (size_t l = 0; l < lastOf.size(); ++l)
        if (lastOf[l] > l) { FoldRange r; r.start = l; r.last = lastOf[l]; out.push_back(r); }
    return out;
}

DynArray<FoldRange> foldRangesFromBrackets(const BracketReport& report, size_t lineCount) {
    DynArray<size_t> lastOf;
    lastOf.resize(lineCount, 0);
    for (size_t i = 0; i < report.pairs.size(); ++i) {
        const BracketPair& p = report.pairs[i];
        if (p.close.line < p.open.line + 2 || p.open.line >= lineCount) continue;   // needs a line to hide
        const size_t last = p.close.line - 1;                                       // the closer's line stays
        if (last > lastOf[p.open.line]) lastOf[p.open.line] = last;
    }
    return collect(lastOf);
}

DynArray<FoldRange> foldRangesByIndent(const Document& doc, size_t tabWidth) {
    if (tabWidth == 0) tabWidth = 4;
    const size_t n = doc.lineCount();
    DynArray<size_t> lastOf;
    lastOf.resize(n, 0);
    struct Open { size_t line; size_t indent; };
    Stack<Open> open;                      // blocks whose end has not been seen yet
    size_t lastNonBlank = 0;
    bool any = false;
    for (size_t l = 0; l < n; ++l) {
        const std::string s = doc.getLine(l);
        size_t ind = 0, i = 0;
        for (; i < s.size() && (s[i] == ' ' || s[i] == '\t'); ++i) ind += (s[i] == '\t') ? tabWidth - ind % tabWidth : 1;
        if (i == s.size()) continue;                                        // blank: belongs to whatever is open
        while (!open.empty() && open.top().indent >= ind) {                 // this line ends the blocks it is not inside
            const Open o = open.pop();
            if (lastNonBlank > o.line) lastOf[o.line] = lastNonBlank;
        }
        open.push(Open{l, ind});
        lastNonBlank = l;
        any = true;
    }
    while (any && !open.empty()) {
        const Open o = open.pop();
        if (lastNonBlank > o.line) lastOf[o.line] = lastNonBlank;
    }
    return collect(lastOf);
}

const FoldRange* foldRangeAt(const DynArray<FoldRange>& ranges, size_t line) {
    size_t lo = 0, hi = ranges.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (ranges[mid].start < line) lo = mid + 1; else hi = mid;
    }
    return (lo < ranges.size() && ranges[lo].start == line) ? &ranges[lo] : nullptr;
}

const FoldRange* innermostFoldRange(const DynArray<FoldRange>& ranges, size_t line) {
    const FoldRange* best = nullptr;
    for (size_t i = 0; i < ranges.size() && ranges[i].start <= line; ++i)
        if (line <= ranges[i].last && (!best || ranges[i].last - ranges[i].start < best->last - best->start)) best = &ranges[i];
    return best;
}

// ---------------------------------------------------------------- FoldSet

bool FoldSet::isFolded(size_t startLine) const { return foldRangeAt(folds_, startLine) != nullptr; }

bool FoldSet::fold(const FoldRange& r) {
    if (r.last <= r.start || isFolded(r.start)) return false;
    size_t pos = 0;
    while (pos < folds_.size() && folds_[pos].start < r.start) ++pos;
    folds_.insert(pos, r);
    rebuild();
    return true;
}

bool FoldSet::unfold(size_t startLine) {
    for (size_t i = 0; i < folds_.size(); ++i) {
        if (folds_[i].start != startLine) continue;
        folds_.erase(i);
        rebuild();
        return true;
    }
    return false;
}

void FoldSet::clear() {
    folds_.clear();
    rebuild();
}

// Merge the folded ranges' hidden parts [start + 1, last] into disjoint intervals with prefix sums.
// A fold inside another fold is simply covered by the outer interval.
void FoldSet::rebuild() {
    hs_.clear(); he_.clear(); before_.clear();
    hiddenTotal_ = 0;
    for (size_t i = 0; i < folds_.size(); ++i) {               // folds_ is sorted by start, so are the intervals
        const size_t a = folds_[i].start + 1, b = folds_[i].last;
        if (!he_.empty() && a <= he_[he_.size() - 1] + 1) {
            if (b > he_[he_.size() - 1]) { hiddenTotal_ += b - he_[he_.size() - 1]; he_[he_.size() - 1] = b; }
            continue;
        }
        before_.push_back(hiddenTotal_);
        hs_.push_back(a);
        he_.push_back(b);
        hiddenTotal_ += b - a + 1;
    }
}

size_t FoldSet::intervalsAtOrBefore(size_t line) const {
    size_t lo = 0, hi = hs_.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (hs_[mid] <= line) lo = mid + 1; else hi = mid;
    }
    return lo;
}

bool FoldSet::isHidden(size_t line) const {
    const size_t k = intervalsAtOrBefore(line);
    return k > 0 && line <= he_[k - 1];
}

size_t FoldSet::rowOf(size_t line) const {
    const size_t k = intervalsAtOrBefore(line);
    if (k == 0) return line;
    if (line <= he_[k - 1]) line = hs_[k - 1] - 1;            // hidden: the header above the block
    const size_t hiddenBefore = (line < hs_[k - 1]) ? before_[k - 1] : before_[k - 1] + (he_[k - 1] - hs_[k - 1] + 1);
    return line - hiddenBefore;
}

size_t FoldSet::lineAt(size_t row) const {
    // interval k starts after (hs_[k] - before_[k]) visible rows; find the last one that starts at or before 'row'
    size_t lo = 0, hi = hs_.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (hs_[mid] - before_[mid] <= row) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return row;
    return row + before_[lo - 1] + (he_[lo - 1] - hs_[lo - 1] + 1);
}

size_t FoldSet::nextVisible(size_t line, size_t lineCount) const {
    size_t next = lineAt(rowOf(line) + 1);
    return (next < lineCount && next > line) ? next : line;
}

size_t FoldSet::prevVisible(size_t line) const {
    const size_t r = rowOf(line);
    return r == 0 ? lineAt(0) : lineAt(r - 1);
}

bool FoldSet::reveal(size_t line) {
    bool changed = false;
    for (size_t i = folds_.size(); i-- > 0;) {
        if (folds_[i].start < line && line <= folds_[i].last) { folds_.erase(i); changed = true; }
    }
    if (changed) rebuild();
    return changed;
}

void FoldSet::linesChanged(size_t fromLine, long delta) {
    if (delta == 0 || folds_.empty()) return;
    DynArray<FoldRange> keep;
    for (size_t i = 0; i < folds_.size(); ++i) {
        FoldRange r = folds_[i];
        if (r.start > fromLine) {                                       // below the edit: moves with the text
            const long s = static_cast<long>(r.start) + delta;
            if (s <= static_cast<long>(fromLine)) continue;             // its header was deleted
            r.start = static_cast<size_t>(s);
            r.last = static_cast<size_t>(static_cast<long>(r.last) + delta);
        } else if (r.last >= fromLine) {                                // the edit is inside it (or on its header)
            const long l = static_cast<long>(r.last) + delta;
            if (l <= static_cast<long>(r.start)) continue;
            r.last = static_cast<size_t>(l);
        }
        keep.push_back(r);
    }
    folds_ = keep;
    rebuild();
}

void FoldSet::keepOnly(const DynArray<FoldRange>& valid) {
    DynArray<FoldRange> keep;
    for (size_t i = 0; i < folds_.size(); ++i)
        if (const FoldRange* v = foldRangeAt(valid, folds_[i].start)) keep.push_back(*v);
    folds_ = keep;
    rebuild();
}

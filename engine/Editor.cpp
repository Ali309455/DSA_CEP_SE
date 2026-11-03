#include "Editor.h"
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------- recording

void Editor::record(CmdType type, Pos start, Pos end, const std::string& text, const char* label) {
    Command c;
    c.type = type;
    c.start = start;
    c.end = end;
    c.text = text;
    if (depth_ > 0) {
        pending_.cmds.push_back(c);          // part of a bigger step
    } else {
        Group g;
        g.label = label;
        g.cmds.push_back(c);
        um_.record(g);                       // pushes on undo stack, clears redo stack
    }
}

void Editor::beginGroup(const std::string& label) {
    if (depth_ == 0) {
        pending_.label = label;
        pending_.cmds.clear();
    }
    ++depth_;
}

void Editor::endGroup() {
    if (depth_ == 0) return;
    --depth_;
    if (depth_ == 0 && !pending_.cmds.empty()) {
        um_.record(pending_);
        pending_.cmds.clear();
    }
}

// ---------------------------------------------------------------- editing

Pos Editor::insertText(Pos at, const std::string& text) {
    Pos end = doc_.insertText(at, text);     // throws if 'at' is invalid: nothing recorded
    if (end != at) record(CmdType::Insert, at, end, text, "insert");
    return end;
}

std::string Editor::deleteRange(Pos a, Pos b) {
    if (b < a) { Pos t = a; a = b; b = t; }
    std::string removed = doc_.deleteRange(a, b);
    if (!removed.empty()) record(CmdType::Delete, a, b, removed, "delete");
    return removed;
}

Pos Editor::insertNewLine(Pos at) {
    Pos end = doc_.insertText(at, "\n");
    record(CmdType::Insert, at, end, "\n", "new line");
    return end;
}

void Editor::insertLineBefore(size_t line, const std::string& text) {
    if (line > doc_.lineCount()) throw std::out_of_range("line number out of range");
    Pos at;
    std::string s;
    if (line == doc_.lineCount()) {
        size_t last = doc_.lineCount() - 1;
        at = Pos{last, doc_.lineLength(last)};
        s = "\n" + text;
    } else {
        at = Pos{line, 0};
        s = text + "\n";
    }
    Pos end = doc_.insertText(at, s);
    record(CmdType::Insert, at, end, s, "add line");
}

std::string Editor::deleteChars(Pos at, size_t count) {
    std::string removed = doc_.deleteChars(at, count);   // throws on bad input
    record(CmdType::Delete, at, Pos{at.line, at.col + removed.size()}, removed, "delete characters");
    return removed;
}

std::string Editor::deleteWord(Pos at) {
    Pos a, b;
    doc_.wordRange(at, a, b);                            // throws if there is no word
    std::string removed = doc_.deleteRange(a, b);
    record(CmdType::Delete, a, b, removed, "delete word");
    return removed;
}

std::string Editor::deleteLines(size_t from, size_t to) {
    Pos a, b;
    doc_.lineSpan(from, to, a, b);                       // throws on a bad range
    std::string removed = doc_.deleteRange(a, b);
    if (!removed.empty()) record(CmdType::Delete, a, b, removed, "delete lines");
    return removed;
}

Pos Editor::replaceRange(Pos a, Pos b, const std::string& text) {
    if (b < a) { Pos t = a; a = b; b = t; }
    EditGroup g(*this, "replace");
    deleteRange(a, b);                                   // validates both positions first
    return insertText(a, text);
}

// ---------------------------------------------------------------- search and replace

ReplaceResult Editor::replaceAll(const std::string& pattern, const std::string& replacement,
                                 const SearchOptions& opt) {
    ReplaceResult result;
    // Non-overlapping matches, in document order. Throws on an empty search text
    // before anything is touched.
    DynArray<Match> hits = findAll(doc_, pattern, opt, false);
    result.found = hits.size();
    if (hits.empty()) return result;

    // Work from the LAST match to the first: a change only moves text that comes
    // after it, so the positions of the matches still to do stay valid.
    beginGroup("replace all");
    size_t mark = pending_.cmds.size();     // commands before 'mark' belong to an outer group
    try {
        for (size_t i = hits.size(); i > 0; --i) {
            const Match& m = hits[i - 1];
            if (failAfter_ != 0 && result.changed == failAfter_) {
                failAfter_ = 0;
                throw std::runtime_error("injected failure");
            }
            if (doc_.getRange(m.start, m.end) == replacement) continue;   // nothing would change
            replaceRange(m.start, m.end, replacement);
            ++result.changed;
        }
    } catch (...) {
        // Put the text back, newest command first. A command leaves the list only
        // after it was reversed, so if even this fails the history still matches the text.
        try {
            while (pending_.cmds.size() > mark) {
                reverse(pending_.cmds.back());
                pending_.cmds.pop_back();
            }
        } catch (...) {
        }
        endGroup();
        throw;
    }
    endGroup();                              // records ONE step (nothing if nothing changed)
    return result;
}

Match Editor::replaceNth(const std::string& pattern, const std::string& replacement, size_t n,
                         const SearchOptions& opt) {
    DynArray<Match> hits = findAll(doc_, pattern, opt, true);
    if (n == 0) throw std::out_of_range("Occurrence numbers start at 1.");
    if (n > hits.size()) {
        if (hits.empty()) throw std::out_of_range("The text was not found, nothing to replace.");
        throw std::out_of_range("Occurrence " + std::to_string(n) + " does not exist: the text occurs " +
                                std::to_string(hits.size()) + " time(s).");
    }
    Match m = hits[n - 1];
    replaceRange(m.start, m.end, replacement);
    return m;
}

bool Editor::replaceNext(Pos from, const std::string& pattern, const std::string& replacement,
                         const SearchOptions& opt, Pos* after, bool wrap) {
    Match m;
    if (!findNext(doc_, from, pattern, opt, m, wrap)) return false;
    Pos end = replaceRange(m.start, m.end, replacement);
    if (after) *after = end;
    return true;
}

// ---------------------------------------------------------------- undo / redo

void Editor::apply(const Command& c) {
    if (c.type == CmdType::Insert) doc_.insertText(c.start, c.text);
    else doc_.deleteRange(c.start, c.end);
}

void Editor::reverse(const Command& c) {
    if (c.type == CmdType::Insert) doc_.deleteRange(c.start, c.end);
    else doc_.insertText(c.start, c.text);
}

bool Editor::undo(std::string* label) {
    if (depth_ > 0 || !um_.canUndo()) return false;
    Group g = um_.takeUndo();
    for (size_t i = g.cmds.size(); i > 0; --i) reverse(g.cmds[i - 1]);   // newest command first
    if (label) *label = g.label;
    um_.putRedo(g);
    return true;
}

bool Editor::redo(std::string* label) {
    if (depth_ > 0 || !um_.canRedo()) return false;
    Group g = um_.takeRedo();
    for (size_t i = 0; i < g.cmds.size(); ++i) apply(g.cmds[i]);         // oldest command first
    if (label) *label = g.label;
    um_.putUndo(g);
    return true;
}

// ---------------------------------------------------------------- files

void Editor::newDocument() {
    doc_.clear();
    um_.clear();
    depth_ = 0;
    pending_.cmds.clear();
}

IOResult Editor::open(const std::string& path) {
    IOResult r = openFile(doc_, path);
    if (r.ok) um_.clear();            // old steps refer to the old text
    return r;
}

IOResult Editor::save(const std::string& path) {
    return saveFile(doc_, path);      // saving does not touch the history
}

#include "Editor.h"
#include "TextUtil.h"
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------- recording

void Editor::record(CmdType type, Pos start, Pos end, const std::string& text, const char* label) {
    run_ = Run::None;                        // any ordinary edit ends a typing run
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
    orderPos(a, b);
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
    orderPos(a, b);
    EditGroup g(*this, "replace");
    deleteRange(a, b);                                   // validates both positions first
    return insertText(a, text);
}

// ---------------------------------------------------------------- typing runs

// 0 = letter / digit / '_' (bytes >= 0x80 are parts of UTF-8 letters), 1 = blank, 2 = other.
static int charClass(char c) {
    if (isWordChar(c) || static_cast<unsigned char>(c) >= 0x80) return 0;
    if (c == ' ' || c == '\t') return 1;
    return 2;
}

// The one class shared by every character of 'text', or -1 if mixed / has a line break.
static int uniformClass(const std::string& text) {
    if (text.empty()) return -1;
    int cls = charClass(text[0]);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' || charClass(text[i]) != cls) return -1;
    }
    return cls;
}

Pos Editor::typeText(Pos at, const std::string& text) {
    if (text.empty()) return at;
    const int cls = uniformClass(text);
    const bool merge = depth_ == 0 && cls >= 0 && run_ == Run::Typing && um_.canUndo() &&
                       um_.undoCount() == runCount_ && at == runPos_ && cls == runClass_;
    Pos end = doc_.insertText(at, text);     // throws if 'at' is invalid: nothing changed, nothing recorded
    if (end == at) return end;
    if (merge) {
        Command& c = um_.topUndo().cmds[0];  // the run's single Insert command
        c.text += text;
        c.end = end;
        um_.clearRedo();
    } else {
        record(CmdType::Insert, at, end, text, "typing");
    }
    if (depth_ == 0 && cls >= 0) {
        run_ = Run::Typing;
        runPos_ = end;
        runClass_ = cls;
        runCount_ = um_.undoCount();
    } else {
        run_ = Run::None;
    }
    return end;
}

std::string Editor::eraseCoalescing(Pos a, Pos b) {
    orderPos(a, b);
    const bool single = (a.line == b.line);
    const bool chain = depth_ == 0 && single && run_ == Run::Erase && um_.canUndo() && um_.undoCount() == runCount_;
    std::string removed = doc_.deleteRange(a, b);   // throws on a bad range: nothing recorded
    if (removed.empty()) return removed;
    bool merged = false;
    if (chain) {
        Command& c = um_.topUndo().cmds[0];          // the run's single Delete command
        if (b == c.start) {                          // Backspace run: new text lies just before the old
            c.start = a;
            c.text = removed + c.text;
            merged = true;
        } else if (a == c.start) {                   // Delete run: new text was just after the old
            c.end.col += removed.size();
            c.text += removed;
            merged = true;
        }
        if (merged) um_.clearRedo();
    }
    if (!merged) record(CmdType::Delete, a, b, removed, "delete");
    if (depth_ == 0 && single) {
        run_ = Run::Erase;
        runPos_ = a;
        runCount_ = um_.undoCount();
    } else {
        run_ = Run::None;
    }
    return removed;
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

Pos Editor::apply(const Command& c) {
    if (c.type == CmdType::Insert) return doc_.insertText(c.start, c.text);   // cursor after the new text
    doc_.deleteRange(c.start, c.end);
    return c.start;
}

Pos Editor::reverse(const Command& c) {
    if (c.type == CmdType::Insert) {
        doc_.deleteRange(c.start, c.end);
        return c.start;
    }
    return doc_.insertText(c.start, c.text);                                   // cursor after the restored text
}

bool Editor::undo(std::string* label, Pos* where) {
    if (depth_ > 0 || !um_.canUndo()) return false;
    run_ = Run::None;
    Group g = um_.takeUndo();
    Pos last;
    for (size_t i = g.cmds.size(); i > 0; --i) last = reverse(g.cmds[i - 1]);   // newest command first
    if (label) *label = g.label;
    if (where) *where = last;
    um_.putRedo(g);
    return true;
}

bool Editor::redo(std::string* label, Pos* where) {
    if (depth_ > 0 || !um_.canRedo()) return false;
    run_ = Run::None;
    Group g = um_.takeRedo();
    Pos last;
    for (size_t i = 0; i < g.cmds.size(); ++i) last = apply(g.cmds[i]);         // oldest command first
    if (label) *label = g.label;
    if (where) *where = last;
    um_.putUndo(g);
    return true;
}

size_t Editor::jumpToHistory(size_t undoSteps, Pos* where) {
    size_t moved = 0;
    Pos w;
    while (um_.undoCount() > undoSteps && undo(nullptr, &w)) ++moved;
    while (um_.undoCount() < undoSteps && redo(nullptr, &w)) ++moved;
    if (moved && where) *where = w;
    return moved;
}

std::string Editor::describeStep(const Group& g) {
    std::string out = g.label.empty() ? std::string("edit") : g.label;
    if (g.cmds.empty()) return out;
    // the text of a single insert / delete, shortened and on one line
    const Command& c = g.cmds[g.cmds.size() - 1];
    if (g.cmds.size() <= 2) {
        std::string t;
        for (size_t i = 0; i < c.text.size() && t.size() < 28; ++i) {
            const char ch = c.text[i];
            if (ch == '\n') t += "\xE2\x86\xB5";                 // the "return" arrow
            else if (ch == '\t') t += ' ';
            else t += ch;
        }
        if (c.text.size() > 28) t += "...";
        bool blank = true;
        for (char ch : t) if (ch != ' ') { blank = false; break; }
        if (!t.empty() && !blank) out += " \"" + t + "\"";
    } else {
        out += " (" + std::to_string(g.cmds.size()) + " changes)";
    }
    out += "  line " + std::to_string(g.cmds[0].start.line + 1);
    return out;
}

// ---------------------------------------------------------------- files

void Editor::newDocument() {
    doc_.clear();
    um_.clear();
    run_ = Run::None;
    depth_ = 0;
    pending_.cmds.clear();
}

IOResult Editor::open(const std::string& path) {
    IOResult r = openFile(doc_, path);
    if (r.ok) { um_.clear(); run_ = Run::None; }   // old steps refer to the old text
    return r;
}

IOResult Editor::renameFile(const std::string& newName) {
    if (doc_.path().empty()) return IOResult{false, "This document has no file yet. Save it first, then rename it."};
    std::string newPath;
    IOResult r = ::renameFile(doc_.path(), newName, &newPath);
    if (r.ok) doc_.setPath(newPath);
    return r;
}

IOResult Editor::save(const std::string& path) {
    return saveFile(doc_, path);      // saving does not touch the history
}

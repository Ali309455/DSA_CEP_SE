// Editor.h - the Document plus its Undo/Redo history.
//
// The front end (console or GUI) talks to Editor, not to Document, for every
// change. Editor performs the change on the Document and records it, so Undo
// and Redo always work. Reading (lines, counts, file name) goes through doc().
//
// If an edit fails (bad position etc.) it throws BEFORE anything is changed or
// recorded, so the document and the history stay consistent.
#pragma once
#include <cstddef>
#include <string>
#include "Document.h"
#include "FileIO.h"
#include "Search.h"
#include "UndoManager.h"

// What replaceAll did: 'found' occurrences matched, 'changed' of them actually
// altered the text (an occurrence already equal to the replacement is skipped).
struct ReplaceResult {
    size_t found = 0;
    size_t changed = 0;
};

class Editor {
public:
    const Document& doc() const { return doc_; }

    // ---- editing (each call is ONE undo step, unless inside a group) ----
    Pos insertText(Pos at, const std::string& text);
    std::string deleteRange(Pos a, Pos b);
    Pos insertNewLine(Pos at);
    void insertLineBefore(size_t line, const std::string& text);
    std::string deleteChars(Pos at, size_t count);
    std::string deleteWord(Pos at);
    std::string deleteLines(size_t from, size_t to);
    // Replace the text between a and b with 'text'. Returns the end of the new text.
    // One undo step (a delete and an insert grouped together).
    Pos replaceRange(Pos a, Pos b, const std::string& text);

    // ---- search and replace (KMP, see Search.h) ----
    // Reading only: use findAll / findNext from Search.h with doc().
    //
    // Replace EVERY occurrence (left to right, non-overlapping) in ONE undo step.
    // All-or-nothing: if something fails half way, the text is put back and the
    // history is left as it was. Throws std::invalid_argument for an empty search
    // text. No matches is not an error (found == 0, nothing changes, nothing recorded).
    ReplaceResult replaceAll(const std::string& pattern, const std::string& replacement,
                             const SearchOptions& opt = SearchOptions());

    // Replace the n-th occurrence (1-based, numbered like findAll's list, i.e.
    // overlapping occurrences count). One undo step. Returns where the old text
    // was. Throws std::out_of_range if n is 0 or larger than the number of
    // occurrences, std::invalid_argument for an empty search text.
    Match replaceNth(const std::string& pattern, const std::string& replacement, size_t n,
                     const SearchOptions& opt = SearchOptions());

    // Replace the first occurrence at or after 'from' (wrapping to the top when
    // 'wrap' is true). Returns false if there is none. 'after' receives the end of
    // the new text, ready to be used as 'from' for the next call. One undo step.
    bool replaceNext(Pos from, const std::string& pattern, const std::string& replacement,
                     const SearchOptions& opt = SearchOptions(), Pos* after = nullptr, bool wrap = true);

    // For tests only: make replaceAll throw after this many replacements, to
    // prove that a failure half way is rolled back. 0 turns it off.
    void injectFailureForTesting(size_t afterReplacements) { failAfter_ = afterReplacements; }

    // ---- grouping: everything between begin and end becomes ONE undo step ----
    // (used for "replace all"). Groups can be nested; the outermost label wins.
    void beginGroup(const std::string& label);
    void endGroup();

    // ---- undo / redo ----
    // Return false when there is nothing to undo / redo. If 'label' is given it
    // receives the name of the step that was undone / redone.
    bool undo(std::string* label = nullptr);
    bool redo(std::string* label = nullptr);
    bool canUndo() const { return um_.canUndo(); }
    bool canRedo() const { return um_.canRedo(); }
    size_t undoCount() const { return um_.undoCount(); }
    size_t redoCount() const { return um_.redoCount(); }
    std::string nextUndoLabel() const { return um_.canUndo() ? um_.nextUndoLabel() : std::string(); }
    std::string nextRedoLabel() const { return um_.canRedo() ? um_.nextRedoLabel() : std::string(); }
    void setHistoryLimit(size_t steps) { um_.setLimit(steps); }
    size_t historyLimit() const { return um_.limit(); }

    // ---- files (history is cleared whenever the whole document is replaced) ----
    void newDocument();
    IOResult open(const std::string& path);
    IOResult save(const std::string& path);

private:
    Document doc_;
    UndoManager um_;
    int depth_ = 0;     // > 0 while inside beginGroup / endGroup
    Group pending_;     // the group being collected
    size_t failAfter_ = 0;   // test hook, see injectFailureForTesting

    void record(CmdType type, Pos start, Pos end, const std::string& text, const char* label);
    void apply(const Command& c);     // do the command
    void reverse(const Command& c);   // do its opposite
};

// Opens a group in the constructor and closes it in the destructor, so the
// group is closed even if an exception is thrown in between.
class EditGroup {
public:
    EditGroup(Editor& e, const std::string& label) : e_(e) { e_.beginGroup(label); }
    ~EditGroup() { e_.endGroup(); }
    EditGroup(const EditGroup&) = delete;
    EditGroup& operator=(const EditGroup&) = delete;
private:
    Editor& e_;
};

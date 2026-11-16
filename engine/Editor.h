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

    // ---- typing runs (phase 5) ----
    // A GUI sends one call per key press. If every key press were its own undo step,
    // "Undo" would remove one letter at a time and the 100-step history would hold
    // only a few words. These two merge a run of key presses into ONE step:
    //
    //   typeText      : consecutive insertions merge while each one starts exactly where
    //                   the previous ended AND is the same kind of character
    //                   (letters/digits/_ , blanks, other). So "hello world" = 3 steps.
    //   eraseCoalescing: consecutive single-line deletions merge when each one touches the
    //                   previous (Backspace runs and Delete runs).
    //
    // Any other edit, undo, redo, open or breakTyping() ends the run. Use breakTyping()
    // when the cursor is moved by the user. The merged step is still one Command, so
    // memory stays O(characters typed) and Undo stays one stack move.
    Pos typeText(Pos at, const std::string& text);
    std::string eraseCoalescing(Pos a, Pos b);
    void breakTyping() { run_ = Run::None; }

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
    // receives the name of the step that was undone / redone. If 'where' is given it
    // receives the position where the cursor belongs afterwards (the end of the text
    // that was put back, or the place where inserted text was removed / re-inserted).
    bool undo(std::string* label = nullptr, Pos* where = nullptr);
    bool redo(std::string* label = nullptr, Pos* where = nullptr);
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
    // Rename the file this document was opened from / saved to (a new NAME in the same
    // folder). The text and the history are untouched. Fails, changing nothing, if the
    // document has no file yet, the name is invalid, or the target exists (see FileIO.h).
    IOResult renameFile(const std::string& newName);
    // The file was renamed or moved by someone else (the Explorer): only the stored path changes,
    // the text, the modified flag and the undo history stay.
    void setPath(const std::string& path) { doc_.setPath(path); }

private:
    Document doc_;
    UndoManager um_;
    int depth_ = 0;     // > 0 while inside beginGroup / endGroup
    Group pending_;     // the group being collected
    size_t failAfter_ = 0;   // test hook, see injectFailureForTesting

    // State of the current typing run (see typeText / eraseCoalescing).
    enum class Run { None, Typing, Erase };
    Run run_ = Run::None;
    Pos runPos_;              // Typing: where the next key press would go. Erase: start of the merged deletion.
    int runClass_ = 0;        // Typing: kind of the last typed character
    size_t runCount_ = 0;     // undo steps when the run was last extended (safety check)

    void record(CmdType type, Pos start, Pos end, const std::string& text, const char* label);
    Pos apply(const Command& c);      // do the command; returns where the cursor belongs afterwards
    Pos reverse(const Command& c);    // do its opposite; same
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

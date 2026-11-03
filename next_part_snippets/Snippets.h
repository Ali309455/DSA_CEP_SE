// Snippets.h - code snippets stored in our own HashTable (phase 4).
//
// A snippet is a short trigger word ("for") and a body of text. Typing the
// trigger and pressing the snippet key replaces it with the body. Looking up the
// trigger in the hash table is O(1) expected, which is why a table is the right
// structure here (no ordering is needed, only "is this word a snippet?").
//
// Body syntax (kept deliberately tiny):
//     $0   where the cursor goes after expansion (at most once; default = end of the text)
//     $$   a literal dollar sign
//     any other '$' is just a dollar sign
//   Line breaks and tabs are real characters in the body. When a snippet is
//   expanded, every line after the first gets the indentation of the line the
//   trigger was typed on, so snippets nest correctly inside blocks.
//
// Rules / errors (nothing is changed when one of these throws):
//   * trigger: 1..32 characters, only letters, digits and '_' (so it can be read back
//     from the text before the cursor), not starting with a digit
//     -> std::invalid_argument otherwise
//   * body: not empty, at most 4000 characters, no '\r' or NUL, at most one $0
//     -> std::invalid_argument otherwise
//   * at most 500 snippets -> std::length_error
#pragma once
#include <cstddef>
#include <string>
#include "DynArray.h"
#include "FileIO.h"
#include "HashTable.h"

struct Snippet {
    std::string trigger;
    std::string body;
};

// The result of expanding a body at some indentation.
struct ExpandedSnippet {
    std::string text;      // body with $0 removed and indentation added
    size_t cursorLine;     // where the cursor goes, relative to the start of 'text':
    size_t cursorCol;      //   line offset (0 = first line) and column on that line
    bool hasCursor;        // false: no $0, cursor goes to the end of the text
};

class SnippetTable {
public:
    static const size_t kMaxSnippets = 500;
    static const size_t kMaxTrigger = 32;
    static const size_t kMaxBody = 4000;

    SnippetTable() {}

    // Add a snippet, or replace the one with this trigger. Returns true if it was new.
    // Throws as described above.
    bool add(const std::string& trigger, const std::string& body);

    bool remove(const std::string& trigger);                 // false if there is no such snippet
    const Snippet* find(const std::string& trigger) const;   // nullptr if none
    size_t size() const { return table_.size(); }
    void clear() { table_.clear(); }

    // All triggers, alphabetical.
    DynArray<std::string> triggers() const;

    // Put the built-in C++ snippets in (for, while, if, ifelse, switch, main, class, ...).
    // Existing snippets with the same trigger are replaced.
    void loadDefaults();

    // One snippet per line:  trigger <TAB> body   with \n \t \\ escapes in the body.
    // Lines that are empty or start with '#' are ignored; a bad line is skipped and
    // counted in the message (the rest still loads).
    IOResult loadFromFile(const std::string& path);
    IOResult saveToFile(const std::string& path) const;

    // Statistics of the hash table, for the report.
    size_t bucketCount() const { return table_.bucketCount(); }
    size_t longestChain() const { return table_.longestChain(); }

    static bool validTrigger(const std::string& t);
    // Returns "" when OK, otherwise the reason (used by add and by the console).
    static std::string bodyProblem(const std::string& body);

private:
    HashTable<std::string, Snippet> table_;
};

// Pure text work, no document needed. 'indent' is added after every '\n' of the body.
// Throws std::invalid_argument if the body is not valid (see above).
ExpandedSnippet expandBody(const std::string& body, const std::string& indent);

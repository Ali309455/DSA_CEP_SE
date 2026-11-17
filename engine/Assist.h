// Assist.h - code assistance that ties phase 4 together (no GUI code):
//
//   Autocomplete  - Trie of C++ keywords / std names  +  Trie of the words in the
//                   open document. Ranks by how often a word is used.
//   applyCompletion - changes the text THROUGH THE EDITOR, so it is exactly one undo step.
//   applySnippet  - the word before the cursor is looked up in the snippet HashTable and
//                   replaced by its body (one undo step, cursor placed at $0).
//
// Word characters are letters, digits and '_'. The "prefix" is the run of word
// characters immediately BEFORE the cursor on its line.
#pragma once
#include <cstddef>
#include <string>
#include "Brackets.h"
#include "Document.h"
#include "DynArray.h"
#include "Editor.h"
#include "Snippets.h"
#include "Trie.h"

bool isWordChar(char c);

// The word characters just before 'cursor' (may be empty). 'start' receives where
// they begin. Throws std::out_of_range if the cursor is not a valid position.
std::string prefixBefore(const Document& doc, Pos cursor, Pos* start = nullptr);

class Autocomplete {
public:
    Autocomplete();    // loads the built-in C++ dictionary

    // Re-read the words of the document into the document trie (the old ones are
    // dropped, so counts never inflate). Words shorter than 2 characters, numbers
    // and words longer than Trie::kMaxWordLength are ignored. O(N).
    void refresh(const Document& doc);

    // Suggestions for a prefix, best first, never more than 'maxResults'. The
    // prefix itself is not suggested. Empty or invalid prefix -> nothing.
    // Rank = (uses in the document) + (uses of the dictionary entry, which
    // grows when the user accepts it).
    DynArray<std::string> suggest(const std::string& prefix, size_t maxResults = 8) const;

    // suggest() for the word before the cursor.
    DynArray<std::string> suggestAt(const Document& doc, Pos cursor, size_t maxResults = 8) const;

    // The user picked this word: it ranks higher next time.
    void accepted(const std::string& word);

    // User dictionary (kept in the dictionary trie). Return false if the word is
    // not acceptable (empty, too long, spaces/control characters).
    bool addWord(const std::string& word);
    bool removeWord(const std::string& word);     // only dictionary words; false if absent

    size_t dictionarySize() const { return dict_.wordCount(); }
    size_t documentWordCount() const { return docWords_.wordCount(); }

private:
    Trie dict_;
    Trie docWords_;
};

// Complete the word before the cursor to 'word' by inserting the missing tail at the
// cursor (one undo step, label "autocomplete"). Text after the cursor is not touched.
// Returns the new cursor, just after the inserted text.
// Throws std::invalid_argument if 'word' is not a valid word, or if it does not
// start with the prefix already typed (so a stale suggestion can't wreck the text);
// std::out_of_range for a bad cursor. Nothing changes when it throws.
Pos applyCompletion(Editor& ed, Pos cursor, const std::string& word);

// What applySnippet did. Not finding a snippet is a normal answer, not an error.
struct SnippetExpansion {
    bool expanded = false;
    Pos cursor;              // where the cursor goes (valid when expanded; else the cursor you passed)
    std::string trigger;     // the word before the cursor (may be empty)
    std::string reason;      // when !expanded: why (for a status bar / console message)
};

// Expand the snippet whose trigger is the word just before 'cursor': the trigger is
// replaced by the body, every line after the first gets the indentation of the line
// the trigger was typed on, and the cursor lands on $0 (or at the end of the text).
// ONE undo step (label "snippet"). The whole word must be the trigger ("xfor" does not
// expand "for"), and the cursor must not be in the middle of a word.
// Returns expanded == false (nothing changed, nothing recorded) when there is no word,
// the word is not a snippet, or the cursor is inside a word.
// Throws std::out_of_range for a bad cursor.
SnippetExpansion applySnippet(Editor& ed, const SnippetTable& snippets, Pos cursor);

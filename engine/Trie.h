// Trie.h - prefix tree for autocomplete (phase 4). Own implementation, no std::map.
//
// Each node stands for one prefix; the path root -> node spells it. A node knows
// whether a word ENDS there and how often it was used ('weight'). Finding every
// word that starts with a prefix = walk down the prefix (O(p)), then collect the
// words in that subtree.
//
// Children of a node are kept in a small array sorted by character (binary
// search), so memory is proportional to the characters actually used rather
// than 256 pointers per node, and results come out in alphabetical order.
//
// Nodes live in one DynArray and refer to each other by INDEX (not pointers),
// so growing the array can never leave a dangling pointer. Removed words leave
// their now-useless nodes in place (they are recycled by compact()).
//
//   insert / contains / remove / weight ... O(L log s)   L = word length, s = alphabet in use (<=256)
//   complete(prefix, k)  ... O(p log s + S*k)  S = nodes under the prefix, k = result size
//                            (top-k kept in a small sorted array, no full sort)
//
// Rules / errors:
//   * Words are case sensitive (C++ is). Characters are bytes; any byte except
//     '\0' and whitespace/control characters is allowed.
//   * An empty word, a word with a forbidden character or longer than kMaxWordLength
//     is rejected (insert returns false); it never throws and never changes the trie.
//   * complete() with an empty prefix returns nothing (use 'maxResults' to ask for
//     top words overall via topWords()).
#pragma once
#include <cstddef>
#include <string>
#include "DynArray.h"

class Trie {
public:
    static const size_t kMaxWordLength = 64;

    Trie();

    // Add a word (or raise its weight by 'weight' if it is already there).
    // Returns false (and changes nothing) if the word is not acceptable.
    bool insert(const std::string& word, unsigned weight = 1);

    bool contains(const std::string& word) const;
    unsigned weightOf(const std::string& word) const;   // 0 if not a word

    // Remove a word. Returns false if it was not in the trie.
    bool remove(const std::string& word);

    // Words that start with 'prefix' (the prefix itself included if it is a word),
    // best first: higher weight first, ties alphabetical. At most maxResults
    // (0 means no words are returned). Empty / invalid prefix -> no results.
    DynArray<std::string> complete(const std::string& prefix, size_t maxResults = 10) const;

    // Same idea without a prefix: the heaviest words overall.
    DynArray<std::string> topWords(size_t maxResults) const;

    size_t countWithPrefix(const std::string& prefix) const;   // words (not nodes)
    size_t wordCount() const { return words_; }
    size_t nodeCount() const { return nodes_.size(); }          // incl. unused ones until compact()

    void clear();
    // Rebuild without the nodes left over from remove(). O(total characters).
    void compact();

    static bool validWord(const std::string& w);

private:
    struct Edge {
        char ch;
        size_t child;
    };
    struct Node {
        DynArray<Edge> edges;     // sorted by ch
        bool isWord = false;
        unsigned weight = 0;
    };
    DynArray<Node> nodes_;        // nodes_[0] is the root
    size_t words_;

    // index of node for 'prefix', or npos
    static const size_t npos = static_cast<size_t>(-1);
    size_t find(const std::string& prefix) const;
    size_t childOf(size_t node, char ch) const;            // npos if none
    size_t addChild(size_t node, char ch);
    void collect(size_t node, std::string& path, size_t maxResults,
                 DynArray<std::string>& outWords, DynArray<unsigned>& outWeights) const;
};

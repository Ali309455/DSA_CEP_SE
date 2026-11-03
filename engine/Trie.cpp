// Trie.cpp - see Trie.h.
#include "Trie.h"
#include <stdexcept>

Trie::Trie() : words_(0) { nodes_.push_back(Node()); }

bool Trie::validWord(const std::string& w) {
    if (w.empty() || w.size() > kMaxWordLength) return false;
    for (size_t i = 0; i < w.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(w[i]);
        if (c <= 32 || c == 127) return false;     // NUL, control characters, space
    }
    return true;
}

size_t Trie::childOf(size_t node, char ch) const {
    const DynArray<Edge>& e = nodes_[node].edges;
    size_t lo = 0, hi = e.size();
    while (lo < hi) {                                // binary search on the sorted edges
        size_t mid = lo + (hi - lo) / 2;
        if (e[mid].ch == ch) return e[mid].child;
        if (static_cast<unsigned char>(e[mid].ch) < static_cast<unsigned char>(ch)) lo = mid + 1;
        else hi = mid;
    }
    return npos;
}

size_t Trie::addChild(size_t node, char ch) {
    size_t fresh = nodes_.size();
    nodes_.push_back(Node());                        // may move nodes_: take no references before this
    DynArray<Edge>& e = nodes_[node].edges;
    size_t pos = 0;
    while (pos < e.size() && static_cast<unsigned char>(e[pos].ch) < static_cast<unsigned char>(ch)) ++pos;
    e.insert(pos, Edge{ch, fresh});
    return fresh;
}

size_t Trie::find(const std::string& prefix) const {
    size_t cur = 0;
    for (size_t i = 0; i < prefix.size(); ++i) {
        cur = childOf(cur, prefix[i]);
        if (cur == npos) return npos;
    }
    return cur;
}

bool Trie::insert(const std::string& word, unsigned weight) {
    if (!validWord(word)) return false;
    size_t cur = 0;
    for (size_t i = 0; i < word.size(); ++i) {
        size_t next = childOf(cur, word[i]);
        if (next == npos) next = addChild(cur, word[i]);
        cur = next;
    }
    Node& n = nodes_[cur];
    if (!n.isWord) { n.isWord = true; n.weight = 0; ++words_; }
    unsigned long long w = static_cast<unsigned long long>(n.weight) + weight;
    n.weight = w > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<unsigned>(w);   // saturate, never wrap
    return true;
}

bool Trie::contains(const std::string& word) const {
    if (!validWord(word)) return false;
    size_t n = find(word);
    return n != npos && nodes_[n].isWord;
}

unsigned Trie::weightOf(const std::string& word) const {
    if (!validWord(word)) return 0;
    size_t n = find(word);
    return (n != npos && nodes_[n].isWord) ? nodes_[n].weight : 0;
}

bool Trie::remove(const std::string& word) {
    if (!validWord(word)) return false;
    size_t n = find(word);
    if (n == npos || !nodes_[n].isWord) return false;
    nodes_[n].isWord = false;
    nodes_[n].weight = 0;
    --words_;
    return true;
}

void Trie::clear() {
    nodes_.clear();
    nodes_.push_back(Node());
    words_ = 0;
}

// Keep the best 'maxResults' (weight desc, then alphabetical) in a sorted array.
// Insertion into a bounded array: O(k) per candidate.
void Trie::collect(size_t start, std::string& path, size_t maxResults,
                   DynArray<std::string>& outWords, DynArray<unsigned>& outWeights) const {
    // iterative DFS with an explicit stack of (node, path length) - no recursion, so a
    // 64-character word can never be a stack problem and the order is alphabetical.
    struct Frame { size_t node; size_t pathLen; size_t nextEdge; };
    DynArray<Frame> st;
    st.push_back(Frame{start, path.size(), 0});
    while (!st.empty()) {
        Frame& f = st.back();
        const Node& nd = nodes_[f.node];
        if (f.nextEdge == 0) {          // entering this node: offer the word that ends here
            path.resize(f.pathLen);
            if (nd.isWord) {
                // position among current results
                size_t pos = outWords.size();
                while (pos > 0) {
                    unsigned pw = outWeights[pos - 1];
                    if (pw > nd.weight || (pw == nd.weight && outWords[pos - 1] < path)) break;
                    --pos;
                }
                if (pos < maxResults) {
                    outWords.insert(pos, path);
                    outWeights.insert(pos, nd.weight);
                    if (outWords.size() > maxResults) { outWords.pop_back(); outWeights.pop_back(); }
                }
            }
        }
        if (f.nextEdge < nd.edges.size()) {
            const Edge& e = nd.edges[f.nextEdge];
            ++f.nextEdge;
            path.resize(f.pathLen);
            path.push_back(e.ch);
            size_t child = e.child;
            size_t len = path.size();
            st.push_back(Frame{child, len, 0});      // may move 'st': f is not used after this
        } else {
            st.pop_back();
        }
    }
}

DynArray<std::string> Trie::complete(const std::string& prefix, size_t maxResults) const {
    DynArray<std::string> words;
    DynArray<unsigned> weights;
    if (maxResults == 0 || !validWord(prefix)) return words;
    size_t n = find(prefix);
    if (n == npos) return words;
    std::string path = prefix;
    collect(n, path, maxResults, words, weights);
    return words;
}

DynArray<std::string> Trie::topWords(size_t maxResults) const {
    DynArray<std::string> words;
    DynArray<unsigned> weights;
    if (maxResults == 0) return words;
    std::string path;
    collect(0, path, maxResults, words, weights);
    return words;
}

size_t Trie::countWithPrefix(const std::string& prefix) const {
    if (!validWord(prefix)) return 0;
    size_t n = find(prefix);
    if (n == npos) return 0;
    size_t count = 0;
    DynArray<size_t> st;
    st.push_back(n);
    while (!st.empty()) {
        size_t cur = st.back();
        st.pop_back();
        if (nodes_[cur].isWord) ++count;
        for (size_t i = 0; i < nodes_[cur].edges.size(); ++i) st.push_back(nodes_[cur].edges[i].child);
    }
    return count;
}

void Trie::compact() {
    // Walk the whole tree (alphabetical DFS), remember every word with its weight,
    // then rebuild from scratch. O(total characters), no sorting.
    DynArray<std::string> words;
    DynArray<unsigned> weights;
    struct Frame { size_t node; size_t pathLen; size_t nextEdge; };
    DynArray<Frame> st;
    std::string path;
    st.push_back(Frame{0, 0, 0});
    while (!st.empty()) {
        Frame& f = st.back();
        const Node& nd = nodes_[f.node];
        if (f.nextEdge == 0 && nd.isWord) { words.push_back(path); weights.push_back(nd.weight); }
        if (f.nextEdge < nd.edges.size()) {
            const Edge& e = nd.edges[f.nextEdge];
            ++f.nextEdge;
            path.resize(f.pathLen);
            path.push_back(e.ch);
            st.push_back(Frame{e.child, path.size(), 0});
        } else {
            st.pop_back();
        }
    }
    clear();
    for (size_t i = 0; i < words.size(); ++i) insert(words[i], weights[i]);
}

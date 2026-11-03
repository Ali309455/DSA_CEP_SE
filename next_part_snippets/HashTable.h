// HashTable.h - our own hash table (phase 4). Replaces std::unordered_map.
//
// Separate chaining: an array of buckets, each bucket a singly linked list of
// (key, value) nodes. The bucket of a key is  hash(key) % bucketCount.
// When the load factor (entries / buckets) passes 0.75 the bucket array doubles
// and every node is relinked ("rehash"), so chains stay short:
//
//     put / get / erase / contains  O(1) expected, O(n) worst case (all keys in one chain)
//     rehash                        O(n), but only every doubling -> O(1) amortized per put
//     memory                        O(n + buckets)
//
// StringHash is FNV-1a (64 bit): xor each byte, multiply by a large prime. It is
// fast, spreads similar strings ("for", "fori", "form") well and is easy to explain.
//
// Not copyable (it owns raw nodes). Used for snippet triggers; any key type
// works if you give a Hash functor and operator==.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "DynArray.h"

struct StringHash {
    size_t operator()(const std::string& s) const {
        std::uint64_t h = 1469598103934665603ULL;          // FNV offset basis
        for (size_t i = 0; i < s.size(); ++i) {
            h ^= static_cast<unsigned char>(s[i]);
            h *= 1099511628211ULL;                         // FNV prime
        }
        return static_cast<size_t>(h);
    }
};

template <typename K, typename V, typename Hash = StringHash>
class HashTable {
    struct Node {
        K key;
        V value;
        Node* next;
        Node(const K& k, const V& v, Node* n) : key(k), value(v), next(n) {}
    };

public:
    HashTable() : size_(0) { buckets_.resize(kInitialBuckets, nullptr); }
    ~HashTable() { clear(); }
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_t bucketCount() const { return buckets_.size(); }
    double loadFactor() const { return static_cast<double>(size_) / static_cast<double>(buckets_.size()); }

    // Insert or overwrite. Returns true if the key was NEW.
    bool put(const K& key, const V& value) {
        Node* n = findNode(key);
        if (n) { n->value = value; return false; }
        if (static_cast<double>(size_ + 1) > kMaxLoad * static_cast<double>(buckets_.size())) grow();
        size_t b = hash_(key) % buckets_.size();
        buckets_[b] = new Node(key, value, buckets_[b]);   // push at the head of the chain: O(1)
        ++size_;
        return true;
    }

    // Pointer to the value, or nullptr. Valid until the next put/erase/clear.
    V* get(const K& key) {
        Node* n = findNode(key);
        return n ? &n->value : nullptr;
    }
    const V* get(const K& key) const {
        const Node* n = findNode(key);
        return n ? &n->value : nullptr;
    }

    bool contains(const K& key) const { return findNode(key) != nullptr; }

    bool erase(const K& key) {
        size_t b = hash_(key) % buckets_.size();
        Node* prev = nullptr;
        for (Node* n = buckets_[b]; n; prev = n, n = n->next) {
            if (n->key == key) {
                if (prev) prev->next = n->next; else buckets_[b] = n->next;
                delete n;
                --size_;
                return true;
            }
        }
        return false;
    }

    void clear() {
        for (size_t b = 0; b < buckets_.size(); ++b) {
            Node* n = buckets_[b];
            while (n) { Node* nx = n->next; delete n; n = nx; }
            buckets_[b] = nullptr;
        }
        size_ = 0;
    }

    // Visit every entry (unspecified order): f(key, value).
    template <typename F>
    void forEach(F f) const {
        for (size_t b = 0; b < buckets_.size(); ++b)
            for (const Node* n = buckets_[b]; n; n = n->next) f(n->key, n->value);
    }

    // For the report: the longest chain (1 = perfect, no collisions beyond that).
    size_t longestChain() const {
        size_t best = 0;
        for (size_t b = 0; b < buckets_.size(); ++b) {
            size_t len = 0;
            for (const Node* n = buckets_[b]; n; n = n->next) ++len;
            if (len > best) best = len;
        }
        return best;
    }

private:
    static const size_t kInitialBuckets = 16;
    static constexpr double kMaxLoad = 0.75;

    DynArray<Node*> buckets_;
    size_t size_;
    Hash hash_;

    Node* findNode(const K& key) const {
        size_t b = hash_(key) % buckets_.size();
        for (Node* n = buckets_[b]; n; n = n->next)
            if (n->key == key) return n;
        return nullptr;
    }

    void grow() {
        DynArray<Node*> fresh;
        fresh.resize(buckets_.size() * 2, nullptr);
        for (size_t b = 0; b < buckets_.size(); ++b) {
            Node* n = buckets_[b];
            while (n) {                                     // relink, no allocation: cannot fail half way
                Node* nx = n->next;
                size_t nb = hash_(n->key) % fresh.size();
                n->next = fresh[nb];
                fresh[nb] = n;
                n = nx;
            }
        }
        buckets_.swap(fresh);
    }
};

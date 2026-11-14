// Assist.cpp - see Assist.h.
#include "Assist.h"
#include <stdexcept>

bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::string prefixBefore(const Document& doc, Pos cursor, Pos* start) {
    if (cursor.line >= doc.lineCount()) throw std::out_of_range("Line " + std::to_string(cursor.line + 1) + " does not exist.");
    const std::string line = doc.getLine(cursor.line);
    if (cursor.col > line.size()) throw std::out_of_range("Column " + std::to_string(cursor.col + 1) + " is past the end of the line.");
    size_t b = cursor.col;
    while (b > 0 && isWordChar(line[b - 1])) --b;
    if (start) *start = Pos{cursor.line, b};
    return line.substr(b, cursor.col - b);
}

// ---------------------------------------------------------------- Autocomplete

static const char* const kDictionary[] = {
    // keywords
    "alignas", "alignof", "auto", "bool", "break", "case", "catch", "char", "class", "const",
    "constexpr", "const_cast", "continue", "decltype", "default", "delete", "do", "double",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "final", "float",
    "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
    "noexcept", "nullptr", "operator", "override", "private", "protected", "public", "register",
    "reinterpret_cast", "return", "short", "signed", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "template", "this", "throw", "true", "try", "typedef",
    "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while",
    // preprocessor
    "include", "define", "ifdef", "ifndef", "endif", "pragma", "once", "elif", "undef",
    // common standard library
    "std", "string", "vector", "map", "set", "unordered_map", "unordered_set", "list", "deque",
    "array", "pair", "tuple", "optional", "variant", "unique_ptr", "shared_ptr", "make_unique",
    "make_shared", "cout", "cin", "cerr", "endl", "ifstream", "ofstream", "istream", "ostream",
    "size_t", "push_back", "pop_back", "emplace_back", "begin", "end", "size", "empty", "clear",
    "erase", "insert", "find", "sort", "min", "max", "swap", "move", "getline", "to_string",
    "stoi", "stod", "iostream", "fstream", "sstream", "algorithm", "cstdio", "cstdlib",
    "cstring", "cmath", "exception", "runtime_error", "invalid_argument", "out_of_range",
    "printf", "scanf", "malloc", "free", "main", "argc", "argv", "nullptr_t", "int64_t", "uint32_t"
};

Autocomplete::Autocomplete() {
    for (size_t i = 0; i < sizeof(kDictionary) / sizeof(kDictionary[0]); ++i) dict_.insert(kDictionary[i], 1);
}

void Autocomplete::refresh(const Document& doc) {
    docWords_.clear();
    const size_t n = doc.lineCount();
    for (size_t ln = 0; ln < n; ++ln) {
        const std::string s = doc.getLine(ln);
        size_t i = 0;
        while (i < s.size()) {
            if (!isWordChar(s[i])) { ++i; continue; }
            size_t j = i;
            while (j < s.size() && isWordChar(s[j])) ++j;
            const bool startsWithDigit = s[i] >= '0' && s[i] <= '9';    // numbers and 0x1F, 3rd, ...
            if (!startsWithDigit && j - i >= 2 && j - i <= Trie::kMaxWordLength) docWords_.insert(s.substr(i, j - i), 1);
            i = j;
        }
    }
}

DynArray<std::string> Autocomplete::suggest(const std::string& prefix, size_t maxResults) const {
    DynArray<std::string> out;
    if (maxResults == 0 || !Trie::validWord(prefix)) return out;
    // Score of a word = weight in the dictionary + weight in the document. Each trie only
    // hands out its own best m words, so a word that is mediocre in both lists but strong
    // in total could be missed. Threshold rule: any word NOT in either top-m list scores at
    // most (m-th weight of A) + (m-th weight of B). Once our k-th best score is strictly
    // above that bound the answer is final; otherwise m doubles. Ends because a list shorter
    // than m is exhausted (its bound is 0). Cost O(log(total) * trie work) in the worst case.
    size_t m = maxResults + 1;                        // +1: the prefix itself may use a slot
    while (true) {
        DynArray<std::string> a = dict_.complete(prefix, m);
        DynArray<std::string> b = docWords_.complete(prefix, m);
        const bool aFull = a.size() == m, bFull = b.size() == m;
        const unsigned long long bound =
            (aFull ? dict_.weightOf(a[m - 1]) : 0ull) + (bFull ? docWords_.weightOf(b[m - 1]) : 0ull);

        DynArray<std::string> cand;
        DynArray<unsigned long long> score;
        auto consider = [&](const std::string& w) {
            if (w == prefix) return;
            for (size_t i = 0; i < cand.size(); ++i) if (cand[i] == w) return;   // seen (<= 2m items)
            cand.push_back(w);
            score.push_back(static_cast<unsigned long long>(dict_.weightOf(w)) + docWords_.weightOf(w));
        };
        for (size_t i = 0; i < a.size(); ++i) consider(a[i]);
        for (size_t i = 0; i < b.size(); ++i) consider(b[i]);

        out.clear();
        unsigned long long kth = 0;
        while (out.size() < maxResults && !cand.empty()) {       // selection: best first, ties alphabetical
            size_t best = 0;
            for (size_t i = 1; i < cand.size(); ++i)
                if (score[i] > score[best] || (score[i] == score[best] && cand[i] < cand[best])) best = i;
            out.push_back(cand[best]);
            kth = score[best];
            cand.erase(best);
            score.erase(best);
        }
        const bool exhausted = !aFull && !bFull;
        if (exhausted || (out.size() == maxResults && kth > bound)) return out;
        m *= 2;
    }
}

DynArray<std::string> Autocomplete::suggestAt(const Document& doc, Pos cursor, size_t maxResults) const {
    return suggest(prefixBefore(doc, cursor), maxResults);
}

void Autocomplete::accepted(const std::string& word) { dict_.insert(word, 1); }

bool Autocomplete::addWord(const std::string& word) { return dict_.insert(word, 1); }

bool Autocomplete::removeWord(const std::string& word) { return dict_.remove(word); }

// ---------------------------------------------------------------- editing helpers

Pos applyCompletion(Editor& ed, Pos cursor, const std::string& word) {
    if (!Trie::validWord(word)) throw std::invalid_argument("'" + word + "' is not a valid word to insert.");
    Pos start;
    std::string prefix = prefixBefore(ed.doc(), cursor, &start);     // validates the cursor
    if (word.compare(0, prefix.size(), prefix) != 0 || word.size() < prefix.size())
        throw std::invalid_argument("'" + word + "' does not start with what is typed ('" + prefix + "').");
    (void)start;
    if (word.size() == prefix.size()) return cursor;                 // already complete: nothing to do
    EditGroup g(ed, "autocomplete");
    return ed.insertText(cursor, word.substr(prefix.size()));
}

SnippetExpansion applySnippet(Editor& ed, const SnippetTable& snippets, Pos cursor) {
    SnippetExpansion r;
    r.cursor = cursor;
    Pos start;
    const std::string line = ed.doc().getLine(cursor.line < ed.doc().lineCount() ? cursor.line : 0);
    r.trigger = prefixBefore(ed.doc(), cursor, &start);               // validates the cursor
    if (r.trigger.empty()) { r.reason = "There is no word before the cursor to expand."; return r; }
    if (cursor.col < line.size() && isWordChar(line[cursor.col])) {
        r.reason = "The cursor is in the middle of a word; move it to the end of \"" + r.trigger + "\" first.";
        return r;
    }
    const Snippet* sn = r.trigger.size() <= SnippetTable::kMaxTrigger ? snippets.find(r.trigger) : nullptr;
    if (!sn) {
        r.reason = r.trigger.size() <= SnippetTable::kMaxTrigger
                       ? "\"" + r.trigger + "\" is not a snippet."
                       : "The word before the cursor is too long to be a snippet.";
        return r;
    }
    // Indentation of the current line (only the part left of the trigger).
    size_t ind = 0;
    while (ind < start.col && (line[ind] == ' ' || line[ind] == '\t')) ++ind;
    ExpandedSnippet ex = expandBody(sn->body, line.substr(0, ind));   // throws only if a stored body were invalid
    {
        EditGroup g(ed, "snippet");
        ed.replaceRange(start, cursor, ex.text);
    }
    r.cursor.line = start.line + ex.cursorLine;
    r.cursor.col = ex.cursorLine == 0 ? start.col + ex.cursorCol : ex.cursorCol;
    r.expanded = true;
    return r;
}

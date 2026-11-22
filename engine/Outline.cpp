#include "Outline.h"
#include "Stack.h"

namespace {

bool isIdent(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

// "a   b\n c" -> "a b c"
std::string squeeze(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (isSpace(c)) { space = true; continue; }
        if (space && !out.empty()) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

// The line without a trailing // comment (a "//" inside a string is rare in a header; good enough here).
std::string codePart(const std::string& line) {
    const size_t c = line.find("//");
    return c == std::string::npos ? line : line.substr(0, c);
}

bool startsWithWord(const std::string& s, size_t at, const char* w) {
    size_t i = 0;
    for (; w[i]; ++i) if (at + i >= s.size() || s[at + i] != w[i]) return false;
    if (at > 0 && isIdent(s[at - 1])) return false;
    return at + i >= s.size() || !isIdent(s[at + i]);
}

// The header of the '{' at 'open': its line up to the brace, plus earlier lines while a ')' has no
// '(' yet (parameters over several lines) or while nothing but blanks was found (Allman braces).
// 'firstLine' receives the line the header starts on; lines are joined with '\n'.
std::string headerOf(const Document& doc, Pos open, size_t& firstLine) {
    std::string h = doc.getLine(open.line).substr(0, open.col);
    firstLine = open.line;
    for (int walk = 0; walk < 6 && firstLine > 0; ++walk) {
        int balance = 0;
        for (char c : h) { if (c == '(') --balance; else if (c == ')') ++balance; }
        const std::string ht = trim(h);
        const bool blank = ht.empty();
        const std::string prev = codePart(doc.getLine(firstLine - 1));
        const std::string t = trim(prev);
        // more to read: an unopened ')', a brace on its own line, or a constructor's ": a(1), b(2)" list
        const bool more = blank || balance > 0 || ht[0] == ':' || ht[0] == ',' || (!t.empty() && t[t.size() - 1] == ',');
        if (!more) break;
        if (t.empty() && !blank) break;
        if (!t.empty() && (t[t.size() - 1] == ';' || t[t.size() - 1] == '{' || t[t.size() - 1] == '}' || t[0] == '#')) break;
        h = prev + "\n" + h;
        --firstLine;
    }
    return h;
}

// Offset in the joined header -> position in the document.
Pos posInHeader(const std::string& h, size_t firstLine, size_t offset) {
    Pos p{firstLine, 0};
    for (size_t i = 0; i < offset && i < h.size(); ++i) {
        if (h[i] == '\n') { ++p.line; p.col = 0; } else ++p.col;
    }
    return p;
}

struct Candidate {
    Symbol sym;
    bool needsClass = false;      // no return type: only a constructor / destructor of the enclosing class
};

const char* const kNotFunctions[] = {"if", "for", "while", "switch", "catch", "return", "sizeof", "do", "else",
                                     "decltype", "alignof", "static_assert", "__attribute__", "defined", "new",
                                     "delete", "throw", "typeid", "noexcept", "requires", "foreach", "Q_FOREACH",
                                     "forever", "elif", "with", "lock", "using", "fixed", nullptr};

bool notAFunction(const std::string& name) {
    for (int i = 0; kNotFunctions[i]; ++i) if (name == kNotFunctions[i]) return true;
    return false;
}

// "class Foo : public Bar", "template <typename T> struct X", "enum class Color : int", "namespace a::b"
bool classLike(const std::string& h, size_t firstLine, Candidate& out) {
    size_t i = 0;
    const std::string t = h;
    size_t start = 0;
    while (start < t.size() && isSpace(t[start])) ++start;
    if (startsWithWord(t, start, "template")) {                         // skip template <...>
        size_t k = t.find('<', start);
        int depth = 0;
        for (; k != std::string::npos && k < t.size(); ++k) {
            if (t[k] == '<') ++depth;
            else if (t[k] == '>' && --depth == 0) { ++k; break; }
        }
        if (k == std::string::npos || depth != 0) return false;
        start = k;
    }
    struct Kw { const char* word; SymbolKind kind; };
    const Kw kws[] = {{"namespace", SymbolKind::Namespace}, {"class", SymbolKind::Class}, {"struct", SymbolKind::Struct},
                      {"union", SymbolKind::Union}, {"enum", SymbolKind::Enum}};
    int paren = 0;
    for (i = start; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '(') ++paren;
        else if (c == ')') --paren;
        else if (c == '=' || c == ';') return false;                    // an initializer, not a definition
        if (paren != 0 || !isIdent(c) || (i > 0 && isIdent(t[i - 1]))) continue;
        for (const Kw& kw : kws) {
            if (!startsWithWord(t, i, kw.word)) continue;
            size_t j = i + std::string(kw.word).size();
            out.sym.kind = kw.kind;
            // the name: the last identifier before ':' (base classes / underlying type) or the end
            std::string name;
            size_t nameAt = j;
            while (j < t.size()) {
                if (isSpace(t[j])) { ++j; continue; }
                if (t[j] == ':' && !(j + 1 < t.size() && t[j + 1] == ':')) break;   // base classes / underlying type
                const size_t a = j;
                while (j < t.size() && (isIdent(t[j]) || (t[j] == ':' && j + 1 < t.size() && t[j + 1] == ':')))
                    j += (t[j] == ':') ? 2 : 1;                         // "a::b" stays one name
                if (j == a) { ++j; continue; }                          // some other character: skip it
                const std::string w = t.substr(a, j - a);
                if (w == "final" || ((kw.kind == SymbolKind::Enum) && (w == "class" || w == "struct"))) continue;
                name = w;
                nameAt = a;
            }
            out.sym.name = name.empty() ? std::string("(anonymous)") : name;
            out.sym.pos = posInHeader(t, firstLine, name.empty() ? i : nameAt);
            return true;
        }
    }
    return false;
}

// "int Foo::size(int a) const", "Foo::Foo(int x) : a(x)", "~Foo()", "bool operator==(const A& o) const"
bool functionLike(const std::string& t, size_t firstLine, Candidate& out) {
    int depth = 0;
    size_t open = std::string::npos;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '"' || t[i] == '\'') return false;                  // string literals do not appear in headers
        if (t[i] == '(' && depth == 0) { open = i; break; }
        if (t[i] == '<') ++depth;
        else if (t[i] == '>' && depth > 0) --depth;
    }
    if (open == std::string::npos) return false;

    // the name in front of the '('
    size_t e = open;
    while (e > 0 && isSpace(t[e - 1])) --e;
    size_t a = e;
    while (a > 0 && (isIdent(t[a - 1]) || t[a - 1] == ':' || t[a - 1] == '~')) --a;
    std::string name = t.substr(a, e - a);
    const size_t op = t.rfind("operator", open);
    if (op != std::string::npos && op < open && startsWithWord(t, op, "operator") &&
        trim(t.substr(op + 8, open - op - 8)).find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_") == std::string::npos) {
        // operator== / operator() / operator[] ...: the name runs from "operator" to the parameter list
        a = op;
        while (a > 0 && (isIdent(t[a - 1]) || t[a - 1] == ':' || t[a - 1] == '~')) --a;
        if (t.compare(open, 2, "()") == 0) {                            // operator()(...): the second '(' has the parameters
            size_t k = open + 2;
            while (k < t.size() && isSpace(t[k])) ++k;
            if (k < t.size() && t[k] == '(') open = k;
        }
        name = squeeze(t.substr(a, open - a));
    }
    while (!name.empty() && name[0] == ':') name.erase(0, 1);
    if (name.empty() || name == "operator" || notAFunction(name) || (name[0] >= '0' && name[0] <= '9')) return false;

    // what comes before the name must look like a return type: identifiers, ::, <>, *, &, [[...]]
    const std::string before = trim(t.substr(0, a));
    for (char c : before)
        if (!(isIdent(c) || isSpace(c) || c == ':' || c == '<' || c == '>' || c == '*' || c == '&' || c == ',' || c == '[' || c == ']'))
            return false;
    if (!before.empty()) {
        const char last = before[before.size() - 1];
        if (last == ',' || last == '<') return false;
        if (startsWithWord(before, 0, "return") || startsWithWord(before, 0, "else") || startsWithWord(before, 0, "new")) return false;
    }

    // the parameter list must close before the '{' (otherwise the brace is a lambda inside a call)
    int p = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < t.size(); ++i) {
        if (t[i] == '(') ++p;
        else if (t[i] == ')' && --p == 0) { close = i; break; }
    }
    if (close == std::string::npos) return false;
    const std::string rest = trim(t.substr(close + 1));
    if (rest.find(';') != std::string::npos || rest.find('=') != std::string::npos) return false;
    if (!rest.empty()) {
        static const char* const ok[] = {"const", "override", "final", "noexcept", "->", ":", "volatile", "&", "throw", "try", "requires", "mutable", nullptr};
        bool fine = false;
        for (int k = 0; ok[k] && !fine; ++k) fine = rest.compare(0, std::string(ok[k]).size(), ok[k]) == 0;
        if (!fine) return false;
    }

    out.sym.kind = SymbolKind::Function;
    out.sym.name = name;
    std::string params = squeeze(t.substr(open, close - open + 1));
    if (params.size() > 48) params = params.substr(0, 45) + "...)";
    out.sym.detail = params;
    out.sym.pos = posInHeader(t, firstLine, a);
    out.needsClass = before.empty() || before == "explicit" || before == "inline" || before == "virtual" || before == "constexpr";
    if (name.find("::") != std::string::npos) out.needsClass = false;   // Foo::Foo(...) out of the class
    return true;
}

std::string lastSegment(const std::string& n) {
    const size_t c = n.rfind("::");
    return c == std::string::npos ? n : n.substr(c + 2);
}

}  // namespace

const char* symbolKindName(SymbolKind k) {
    switch (k) {
        case SymbolKind::Namespace: return "namespace";
        case SymbolKind::Class: return "class";
        case SymbolKind::Struct: return "struct";
        case SymbolKind::Union: return "union";
        case SymbolKind::Enum: return "enum";
        case SymbolKind::Function: return "function";
    }
    return "symbol";
}

DynArray<Symbol> outlineSymbols(const Document& doc, const BracketReport& report) {
    DynArray<Symbol> out;
    const size_t n = doc.lineCount();

    // the '{' pairs in order of their opening bracket: counting sort by line, insertion by column
    DynArray<size_t> startOf;                       // startOf[l] .. startOf[l + 1]: the pairs opening on line l
    startOf.resize(n + 1, 0);
    for (size_t i = 0; i < report.pairs.size(); ++i)
        if (report.pairs[i].type == '{' && report.pairs[i].open.line < n) ++startOf[report.pairs[i].open.line + 1];
    for (size_t l = 0; l < n; ++l) startOf[l + 1] += startOf[l];
    DynArray<size_t> order, fill;
    order.resize(startOf[n], 0);
    fill = startOf;
    for (size_t i = 0; i < report.pairs.size(); ++i) {
        const BracketPair& bp = report.pairs[i];
        if (bp.type != '{' || bp.open.line >= n) continue;
        size_t at = fill[bp.open.line]++;
        while (at > startOf[bp.open.line] && report.pairs[order[at - 1]].open.col > bp.open.col) { order[at] = order[at - 1]; --at; }
        order[at] = i;
    }

    Stack<size_t> open;                             // indexes into 'out' of the symbols still open
    DynArray<Pos> closeOf;                          // closeOf[k]: the '}' of out[k]
    for (size_t k = 0; k < order.size(); ++k) {
        const BracketPair& bp = report.pairs[order[k]];
        while (!open.empty() && closeOf[open.top()] < bp.open) open.pop();
        size_t firstLine = 0;
        const std::string h = headerOf(doc, bp.open, firstLine);
        if (trim(h).empty()) continue;
        Candidate c;
        if (!classLike(h, firstLine, c) && !functionLike(h, firstLine, c)) continue;
        if (c.needsClass) {                          // a constructor / destructor written inside its class
            if (open.empty()) continue;
            const Symbol& owner = out[open.top()];
            if (owner.kind != SymbolKind::Class && owner.kind != SymbolKind::Struct) continue;
            const std::string cls = lastSegment(owner.name);
            if (c.sym.name != cls && c.sym.name != "~" + cls) continue;
        }
        c.sym.endLine = bp.close.line;
        c.sym.depth = open.size();
        out.push_back(c.sym);
        closeOf.push_back(bp.close);
        open.push(out.size() - 1);
    }
    return out;
}

DynArray<Symbol> outlineScriptSymbols(const Document& doc, size_t tabWidth) {
    if (tabWidth == 0) tabWidth = 4;
    DynArray<Symbol> out;
    struct Open { size_t indent; size_t index; };
    Stack<Open> open;
    size_t lastNonBlank = 0;
    for (size_t l = 0; l < doc.lineCount(); ++l) {
        const std::string s = doc.getLine(l);
        size_t ind = 0, i = 0;
        for (; i < s.size() && (s[i] == ' ' || s[i] == '\t'); ++i) ind += (s[i] == '\t') ? tabWidth - ind % tabWidth : 1;
        if (i == s.size() || s[i] == '#') continue;
        while (!open.empty() && open.top().indent >= ind) out[open.pop().index].endLine = lastNonBlank;
        lastNonBlank = l;

        SymbolKind kind = SymbolKind::Function;
        size_t j = i;
        if (startsWithWord(s, j, "async")) { j += 5; while (j < s.size() && isSpace(s[j])) ++j; }
        if (startsWithWord(s, j, "def")) j += 3;
        else if (startsWithWord(s, j, "function")) j += 8;
        else if (startsWithWord(s, j, "class")) { j += 5; kind = SymbolKind::Class; }
        else continue;
        while (j < s.size() && isSpace(s[j])) ++j;
        const size_t a = j;
        while (j < s.size() && isIdent(s[j])) ++j;
        if (j == a) continue;
        Symbol sym;
        sym.kind = kind;
        sym.name = s.substr(a, j - a);
        sym.pos = Pos{l, a};
        sym.endLine = l;
        sym.depth = open.size();
        if (kind == SymbolKind::Function) {
            const size_t p = s.find('(', j), q = s.find(')', j);
            if (p != std::string::npos && q != std::string::npos && q > p) {
                sym.detail = squeeze(s.substr(p, q - p + 1));
                if (sym.detail.size() > 48) sym.detail = sym.detail.substr(0, 45) + "...)";
            }
        }
        out.push_back(sym);
        open.push(Open{ind, out.size() - 1});
    }
    while (!open.empty()) out[open.pop().index].endLine = lastNonBlank;
    return out;
}

long symbolAtLine(const DynArray<Symbol>& symbols, size_t line) {
    long best = -1;
    for (size_t i = 0; i < symbols.size(); ++i) {
        const Symbol& s = symbols[i];
        if (s.pos.line <= line && line <= s.endLine && (best < 0 || s.depth >= symbols[static_cast<size_t>(best)].depth)) best = static_cast<long>(i);
    }
    return best;
}

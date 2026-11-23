#include "Formatter.h"
#include <cstring>
#include "Editor.h"
#include "Stack.h"
#include "TextUtil.h"

namespace {

// ---------------------------------------------------------------- tokens

enum class Kind { Word, Number, String, Comment, Punct };

struct Tok {
    Kind kind = Kind::Punct;
    std::string text;
    std::string gap;        // the blanks in front of it in the original line ("" = touching)
};

// What is still open at the end of a line, carried to the next one.
struct LexState {
    bool inBlock = false;   // inside /* ... */
    bool inRaw = false;     // inside R"delim( ... )delim"
    std::string rawEnd;     // )delim"
    bool inString = false;  // a "..." or '...' continued with a backslash at the end of the line
    char quote = 0;
};


bool isIdStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
bool isIdChar(char c) { return isIdStart(c) || isDigitChar(c); }
bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

bool inList(const std::string& w, const char* const* list) {
    for (int i = 0; list[i]; ++i) if (w == list[i]) return true;
    return false;
}

const char* const kStringPrefixes[] = {"R", "u8", "u", "U", "L", "u8R", "uR", "UR", "LR", nullptr};
// longest first, so ">>=" is one token, not ">" ">" "="
const char* const kPuncts[] = {">>=", "<<=", "->*", "...", "<=>", "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
                               "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##", ".*", nullptr};

// From 'from' (just after the opening quote) to just after the closing quote. A backslash at the very
// end of the line continues the literal on the next line.
size_t scanString(const std::string& s, size_t from, char quote, LexState& st) {
    size_t j = from;
    while (j < s.size()) {
        if (s[j] == '\\') {
            if (j + 1 == s.size()) { st.inString = true; st.quote = quote; return s.size(); }
            j += 2;
            continue;
        }
        if (s[j] == quote) return j + 1;
        ++j;
    }
    return s.size();                                       // unterminated on this line: the compiler will say so
}

// One line into tokens; 'st' carries comments and strings that continue on the next line.
void lexLine(const std::string& s, LexState& st, DynArray<Tok>& out) {
    const size_t n = s.size();
    size_t i = 0;
    std::string gap;
    auto push = [&](Kind k, size_t a, size_t b) {
        Tok t;
        t.kind = k;
        t.text = s.substr(a, b - a);
        t.gap = gap;
        out.push_back(t);
        gap.clear();
    };
    if (st.inBlock) {                                      // the line starts inside /* ... */
        const size_t e = s.find("*/");
        if (e == std::string::npos) { push(Kind::Comment, 0, n); return; }
        push(Kind::Comment, 0, e + 2);
        st.inBlock = false;
        i = e + 2;
    } else if (st.inRaw) {                                 // ... inside a raw string
        const size_t e = s.find(st.rawEnd);
        if (e == std::string::npos) { push(Kind::String, 0, n); return; }
        push(Kind::String, 0, e + st.rawEnd.size());
        st.inRaw = false;
        i = e + st.rawEnd.size();
    } else if (st.inString) {                              // ... inside a string continued with '\'
        st.inString = false;
        const size_t j = scanString(s, 0, st.quote, st);
        push(Kind::String, 0, j);
        if (st.inString) return;
        i = j;
    }
    while (i < n) {
        const char c = s[i];
        if (isBlank(c)) { gap.push_back(c); ++i; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { push(Kind::Comment, i, n); return; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            const size_t e = s.find("*/", i + 2);
            if (e == std::string::npos) { push(Kind::Comment, i, n); st.inBlock = true; return; }
            push(Kind::Comment, i, e + 2);
            i = e + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            const size_t j = scanString(s, i + 1, c, st);
            push(Kind::String, i, j);
            i = j;
            continue;
        }
        if (isDigitChar(c) || (c == '.' && i + 1 < n && isDigitChar(s[i + 1]))) {   // 42, 3.14, 1e-5, 0xFF, 1'000'000, 2.5f
            const bool hex = c == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X');
            size_t j = i + 1;
            while (j < n) {
                const char d = s[j];
                if (isIdChar(d) || d == '.') { ++j; continue; }
                if (d == '\'' && j + 1 < n && isIdChar(s[j + 1])) { j += 2; continue; }
                if ((d == '+' || d == '-') && ((!hex && (s[j - 1] == 'e' || s[j - 1] == 'E')) || s[j - 1] == 'p' || s[j - 1] == 'P')) { ++j; continue; }
                break;
            }
            push(Kind::Number, i, j);
            i = j;
            continue;
        }
        if (isIdStart(c)) {
            size_t j = i + 1;
            while (j < n && isIdChar(s[j])) ++j;
            const std::string w = s.substr(i, j - i);
            if (j < n && (s[j] == '"' || s[j] == '\'') && inList(w, kStringPrefixes)) {   // L"x", u8"x", R"(x)"
                if (w[w.size() - 1] == 'R' && s[j] == '"') {
                    const size_t open = s.find('(', j + 1);
                    if (open == std::string::npos) { push(Kind::String, i, n); return; }
                    const std::string end = ")" + s.substr(j + 1, open - j - 1) + "\"";
                    const size_t e = s.find(end, open + 1);
                    if (e == std::string::npos) { push(Kind::String, i, n); st.inRaw = true; st.rawEnd = end; return; }
                    push(Kind::String, i, e + end.size());
                    i = e + end.size();
                    continue;
                }
                const size_t k = scanString(s, j + 1, s[j], st);
                push(Kind::String, i, k);
                i = k;
                continue;
            }
            push(Kind::Word, i, j);
            i = j;
            continue;
        }
        size_t len = 1;
        for (int p = 0; kPuncts[p]; ++p) {
            const size_t l = std::strlen(kPuncts[p]);
            if (s.compare(i, l, kPuncts[p]) == 0) { len = l; break; }
        }
        push(Kind::Punct, i, i + len);
        i += len;
    }
}

// ---------------------------------------------------------------- spacing rules

// words after which a following + - is a sign, not a subtraction ("return -1")
const char* const kNotOperands[] = {"return", "case", "throw", "new", "delete", "else", "do", "goto", "co_return", "co_yield",
                                    "co_await", "sizeof", "alignof", "decltype", "typeid", "not", "and", "or", "if", "for",
                                    "while", "switch", "catch", "operator", "default", nullptr};
// words that are never function names, so "int (*p)" or "return (x)" keep their blank before '('
const char* const kKeywords[] = {"return", "case", "throw", "new", "delete", "else", "do", "goto", "sizeof", "alignof",
    "decltype", "typeid", "not", "and", "or", "if", "for", "while", "switch", "catch", "operator", "default", "int", "char",
    "void", "bool", "double", "float", "long", "short", "unsigned", "signed", "auto", "const", "volatile", "static",
    "constexpr", "consteval", "constinit", "inline", "virtual", "explicit", "struct", "class", "union", "enum", "typename",
    "template", "using", "namespace", "public", "private", "protected", "friend", "noexcept", "override", "final", "this",
    "nullptr", "true", "false", "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast", "alignas", "static_assert",
    "thread_local", "mutable", "register", "extern", "typedef", "asm", "requires", "concept", "co_await", "co_return",
    "co_yield", "wchar_t", "char8_t", "char16_t", "char32_t", nullptr};
const char* const kControl[] = {"if", "for", "while", "switch", "catch", nullptr};
// words that follow a parameter list: "f() const", "g() override {"
const char* const kQualifiers[] = {"const", "override", "noexcept", "final", "volatile", "mutable", nullptr};
// always one blank on each side (between two operands)
const char* const kSpacedOps[] = {"=", "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "||", "<=>", nullptr};
// one blank on each side only when both neighbours are clearly operands ("a+b", not "-1" or "*p")
const char* const kBinaryOps[] = {"+", "-", "/", "%", "<<", nullptr};

bool isP(const Tok& t, const char* p) { return t.kind == Kind::Punct && t.text == p; }

bool leftOperand(const Tok& t) {
    if (t.kind == Kind::Word) return !inList(t.text, kNotOperands);
    if (t.kind == Kind::Number || t.kind == Kind::String) return true;
    return isP(t, ")") || isP(t, "]");
}
bool rightOperand(const Tok* t) {
    return t && (t->kind == Kind::Word || t->kind == Kind::Number || t->kind == Kind::String || isP(*t, "("));
}
bool notAnEnd(const Tok* t) {
    return t && !isP(*t, ")") && !isP(*t, "]") && !isP(*t, ",") && !isP(*t, ";");
}

// The line rebuilt from its tokens with normalised blanks between them.
std::string joinTokens(const DynArray<Tok>& t, bool spacing) {
    const size_t n = t.size();
    size_t lastCode = n;                                   // the last token that is not a comment
    for (size_t i = n; i-- > 0;) if (t[i].kind != Kind::Comment) { lastCode = i; break; }
    // operators that get a blank on both sides
    DynArray<char> spaced;
    spaced.resize(n, 0);
    for (size_t i = 1; spacing && i < n; ++i) {
        if (t[i].kind != Kind::Punct) continue;
        const Tok* next = (i + 1 < n && t[i + 1].kind != Kind::Comment) ? &t[i + 1] : nullptr;
        if (inList(t[i].text, kSpacedOps) && leftOperand(t[i - 1]) && notAnEnd(next)) spaced[i] = 1;
        else if (inList(t[i].text, kBinaryOps) && leftOperand(t[i - 1]) && rightOperand(next)) spaced[i] = 1;
    }
    std::string out = t.empty() ? std::string() : t[0].text;
    for (size_t i = 1; i < n; ++i) {
        const Tok& prev = t[i - 1];
        const Tok& cur = t[i];
        const bool hadGap = !cur.gap.empty();
        std::string sep;
        if (!spacing) sep = cur.gap;
        else if (cur.kind == Kind::Comment) sep = hadGap ? cur.gap : " ";           // keep trailing comments aligned
        else if (prev.kind == Kind::Comment) sep = hadGap ? " " : "";
        else if (isP(cur, ",") || isP(cur, ";")) sep = "";
        else if (isP(prev, "(") || isP(prev, "[")) sep = "";
        else if (isP(cur, ")") || isP(cur, "]")) sep = "";
        else if (isP(prev, ",") || (isP(prev, ";") && !isP(cur, "}"))) sep = " ";   // "{ return a;}" keeps its own gap
        else if (spaced[i] || spaced[i - 1]) sep = " ";
        else if (isP(cur, "(") && prev.kind == Kind::Word && inList(prev.text, kControl)) sep = " ";     // if (
        else if (isP(cur, "(") && prev.kind == Kind::Word && !inList(prev.text, kKeywords)) sep = "";   // f(x)
        else if (isP(cur, "{") && (isP(prev, ")") || (i == lastCode && prev.kind == Kind::Word) ||
                                   (prev.kind == Kind::Word && inList(prev.text, kQualifiers)))) sep = " ";  // ) {   class X {   const {
        else if (isP(prev, ")") && cur.kind == Kind::Word && inList(cur.text, kQualifiers)) sep = " ";        // f() const
        else if (isP(prev, "}") && cur.kind == Kind::Word && (cur.text == "else" || cur.text == "while" || cur.text == "catch")) sep = " ";
        else sep = hadGap ? " " : "";
        out += sep;
        out += cur.text;
    }
    while (!out.empty() && isBlank(out[out.size() - 1])) out.erase(out.size() - 1);
    return out;
}

// ---------------------------------------------------------------- indentation

struct Open {
    char ch = '{';
    long openerCols = 0;     // indentation of the line that opened it (a closer lines up with it)
    long delta = 0;          // how far that line moved (lines inside an open ( or [ move the same)
    long contentCols = 0;    // indentation of the lines inside a { }
    bool isSwitch = false;
    bool caseOpen = false;   // a case label was seen: statements go one level further in
};

long indentCols(const std::string& s, size_t tabWidth) {
    long cols = 0;
    for (char c : s) {
        if (c == ' ') ++cols;
        else if (c == '\t') cols += static_cast<long>(tabWidth) - cols % static_cast<long>(tabWidth);
        else break;
    }
    return cols;
}

size_t firstNonBlank(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && isBlank(s[i])) ++i;
    return i;
}

std::string rtrim(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && isBlank(s[e - 1])) --e;
    return s.substr(0, e);
}

bool isCaseLabel(const DynArray<const Tok*>& code) {
    if (code.empty() || code[0]->kind != Kind::Word || (code[0]->text != "case" && code[0]->text != "default")) return false;
    for (size_t i = 1; i < code.size(); ++i) if (isP(*code[i], ":")) return true;
    return false;
}

bool isAccessSpecifier(const DynArray<const Tok*>& code) {
    if (code.size() < 2 || code.size() > 3 || !isP(*code[code.size() - 1], ":") || code[0]->kind != Kind::Word) return false;
    const std::string& w = code[0]->text;
    if (w != "public" && w != "private" && w != "protected" && w != "signals" && w != "Q_SIGNALS" && w != "slots" && w != "Q_SLOTS") return false;
    return code.size() == 2 || code[1]->kind == Kind::Word;                 // "public slots:"
}

bool isPlainLabel(const DynArray<const Tok*>& code) {                       // "retry:" (goto label)
    return code.size() == 2 && code[0]->kind == Kind::Word && isP(*code[1], ":");
}

// What the last line of code looked like: decides whether the next line continues it.
struct PrevCode {
    bool exists = false;
    std::string last;                // its last token
    bool lastIsWord = false;
    bool label = false;              // case / access specifier / goto label
    DynArray<std::string> words;     // its words, for a '{' alone on the next line (Allman style)

    // A line that ended with 'lastTok' (label and words are filled in by the caller if needed).
    void set(const std::string& lastTok, bool isWord) {
        exists = true;
        last = lastTok;
        lastIsWord = isWord;
        label = false;
        words.clear();
    }
};

// "if (x)", "for (...)", "else", "do" without a brace: the next line is their one-statement body
bool controlWithoutBrace(const PrevCode& p) {
    static const char* const heads[] = {"if", "for", "while", "else", "do", nullptr};
    return p.exists && !p.words.empty() && inList(p.words[0], heads) && p.last != "{" && p.last != ";";
}

// Everything the indenter knows, so #if / #else branches can each start from the same state.
struct IndentState {
    Stack<Open> stack;
    PrevCode prev;
    long lastAnchorCols = 0, lastAnchorDelta = 0;
};
struct PPFrame {
    IndentState atIf;                // the state where the #if began: every branch starts from it
    IndentState afterFirst;          // the state at the end of the first branch: what follows #endif sees
    bool sawElse = false;
};

// A closing ) ] or }: pop up to and including its opener (stray openers in between go too).
// A closer without an opener is ignored.
void closeBracket(Stack<Open>& stack, const std::string& closer) {
    const char want = closer == "}" ? '{' : closer == ")" ? '(' : '[';
    size_t depth = 0;
    while (depth < stack.size() && stack.peek(depth).ch != want) ++depth;
    if (depth < stack.size())
        for (size_t k = 0; k <= depth; ++k) stack.pop();
}

bool isOpener(const Tok& t) { return t.kind == Kind::Punct && (t.text == "{" || t.text == "(" || t.text == "["); }
bool isCloser(const Tok& t) { return t.kind == Kind::Punct && (t.text == "}" || t.text == ")" || t.text == "]"); }

// #if / #elif / #else / #endif: every branch is indented from the state where the #if began, and what
// follows #endif continues from the end of the first branch. 't' is the line, 'fnb' where '#' is.
void trackPreprocessor(const std::string& t, size_t fnb, IndentState& is, DynArray<PPFrame>& pp) {
    size_t k = fnb + 1;
    while (k < t.size() && isBlank(t[k])) ++k;
    size_t e = k;
    while (e < t.size() && isIdChar(t[e])) ++e;
    const std::string dir = t.substr(k, e - k);
    if (dir == "if" || dir == "ifdef" || dir == "ifndef") {
        PPFrame f;
        f.atIf = is;
        pp.push_back(f);
    } else if ((dir == "elif" || dir == "else" || dir == "elifdef" || dir == "elifndef") && !pp.empty()) {
        PPFrame& f = pp[pp.size() - 1];
        if (!f.sawElse) { f.afterFirst = is; f.sawElse = true; }
        is = f.atIf;
    } else if (dir == "endif" && !pp.empty()) {
        if (pp[pp.size() - 1].sawElse) is = pp[pp.size() - 1].afterFirst;
        pp.pop_back();
    }
}

// The header that owns a '{' at toks[at]: the words before it on its line, or (brace alone on its
// line, Allman style) the words of the line above.
DynArray<std::string> braceHeader(const DynArray<Tok>& toks, size_t at, const PrevCode& prev) {
    DynArray<std::string> words;
    bool codeBefore = false;
    for (size_t k = 0; k < at; ++k) {
        if (toks[k].kind == Kind::Comment) continue;
        codeBefore = true;
        if (toks[k].kind == Kind::Word) words.push_back(toks[k].text);
    }
    return codeBefore ? words : prev.words;
}

// namespace / inline namespace / extern "C" bodies are not indented
bool isNamespaceHeader(const DynArray<std::string>& words) {
    if (words.empty()) return false;
    if (words[0] == "namespace") return true;
    if (words[0] == "inline" && words.size() > 1 && words[1] == "namespace") return true;
    return words[0] == "extern" && words.size() == 1;
}

bool isSwitchHeader(const DynArray<std::string>& words) {
    for (size_t k = 0; k < words.size(); ++k)
        if (words[k] == "switch") return true;
    return false;
}

// The screen column of byte 'at' in 'line' (tabs expanded to 'tabW').
long columnAt(const std::string& line, size_t at, size_t tabW) {
    const long tw = static_cast<long>(tabW);
    long col = 0;
    for (size_t k = 0; k < at; ++k) col += line[k] == '\t' ? tw - col % tw : 1;
    return col;
}

}  // namespace

// ---------------------------------------------------------------- formatLines

DynArray<std::string> formatLines(const DynArray<std::string>& lines, const FormatOptions& opt) {
    const long w = static_cast<long>(opt.indentWidth == 0 ? 4 : opt.indentWidth);
    const size_t tabW = opt.tabWidth == 0 ? 4 : opt.tabWidth;
    DynArray<std::string> out;
    LexState st;
    IndentState is;
    Stack<Open>& stack = is.stack;
    PrevCode& prev = is.prev;
    long& lastAnchorCols = is.lastAnchorCols;
    long& lastAnchorDelta = is.lastAnchorDelta;
    DynArray<PPFrame> pp;              // open #if blocks
    bool preprocCont = false;          // the previous line was a #define ending in '\'
    size_t blankRun = 0;
    long commentDelta = 0;
    long trailingCommentCol = -1;      // column of the previous line's trailing // comment (-1: none)
    long trailingCommentDelta = 0;     // how far that line moved

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        const LexState st0 = st;
        const size_t fnb = firstNonBlank(line);
        const bool blank = fnb == line.size();

        // 1. inside a raw string or a continued string: every character belongs to the literal
        if (st0.inRaw || st0.inString) {
            DynArray<Tok> toks;
            lexLine(line, st, toks);
            out.push_back(line);
            // what follows the end of the literal still counts: brackets for the stack, the last token
            // (usually ';') for deciding whether the next line continues this statement
            for (size_t i = 1; i < toks.size(); ++i) {
                const Tok& t = toks[i];
                if (isOpener(t)) {
                    Open o;
                    o.ch = t.text[0];
                    o.openerCols = lastAnchorCols;
                    o.contentCols = lastAnchorCols + (o.ch == '{' ? w : 0);
                    stack.push(o);
                } else if (isCloser(t)) {
                    closeBracket(stack, t.text);
                }
            }
            if (!st.inRaw && !st.inString && toks.size() > 1) {
                const Tok& lastTok = toks[toks.size() - 1];
                if (lastTok.kind != Kind::Comment) prev.set(lastTok.text, lastTok.kind == Kind::Word);
            }
            continue;
        }
        // 2. empty lines: at most maxBlankLines in a row (inside a comment they are kept)
        if (blank && !preprocCont) {
            if (st0.inBlock) { out.push_back(std::string()); continue; }
            if (++blankRun <= opt.maxBlankLines) out.push_back(std::string());
            continue;
        }
        blankRun = 0;
        const long oldCols = indentCols(line, tabW);
        // 3. preprocessor lines and their '\' continuations: untouched except trailing blanks
        if (!st0.inBlock && (preprocCont || line[fnb] == '#')) {
            const std::string t = rtrim(line);
            out.push_back(t);
            if (!preprocCont) trackPreprocessor(t, fnb, is, pp);
            preprocCont = !t.empty() && t[t.size() - 1] == '\\';
            continue;
        }
        // 4. a line that starts inside /* ... */ moves with the line where the comment began
        DynArray<Tok> toks;
        lexLine(line, st, toks);
        const bool endsInString = st.inRaw || st.inString;
        DynArray<const Tok*> code;
        for (size_t i = 0; i < toks.size(); ++i) if (toks[i].kind != Kind::Comment) code.push_back(&toks[i]);

        long newCols = oldCols;
        bool anchor = false;
        bool caseLine = false;
        bool continuation = false;                                      // statement continued from the line above
        const bool bodyLine = controlWithoutBrace(prev);                 // ... as the body of a brace-less if / for / else
        if (st0.inBlock) {
            newCols = oldCols + commentDelta;
        } else if (code.empty() && oldCols == trailingCommentCol) {
            newCols = oldCols + trailingCommentDelta;                   // continues the comment at the end of the line above
        } else {
            const Tok& first = toks[0];
            const bool startsWithCloser = isP(first, "}") || isP(first, ")") || isP(first, "]");
            if (startsWithCloser) {                                     // lines up with its opener
                if (!stack.empty()) { newCols = stack.top().openerCols; anchor = true; }
            } else if (!stack.empty() && stack.top().ch != '{') {       // inside an open ( or [: keep the alignment
                newCols = oldCols + stack.top().delta;
            } else if (isAccessSpecifier(code)) {                       // public: at the level of its class
                newCols = stack.empty() ? 0 : stack.top().openerCols;
                anchor = true;
            } else if (isCaseLabel(code) && !stack.empty() && stack.top().isSwitch) {
                newCols = stack.top().contentCols;
                anchor = true;
                caseLine = true;
            } else if (!code.empty() && prev.exists && !prev.label && prev.last != ";" && prev.last != "{" &&
                       prev.last != "}" && !(isP(first, "{") && (prev.lastIsWord || prev.last == ")"))) {
                // a statement continued from the line above (a '{' after ")" or a word is the block's own
                // brace on its own line - Allman style - not a continuation; after "," it is a list item)
                newCols = oldCols + lastAnchorDelta;
                continuation = true;
            } else {                                                    // a new statement: one level inside its block
                newCols = stack.empty() ? 0 : stack.top().contentCols + (stack.top().isSwitch && stack.top().caseOpen ? w : 0);
                anchor = true;
            }
        }
        if (newCols < 0) newCols = 0;
        const long delta = newCols - oldCols;
        if (anchor && !code.empty()) { lastAnchorCols = newCols; lastAnchorDelta = delta; }
        if (st.inBlock && !st0.inBlock) commentDelta = delta;          // a comment starts here and goes on

        // 5. the text of the line
        std::string text(static_cast<size_t>(newCols), ' ');
        if (st0.inBlock || endsInString) text += line.substr(fnb);      // comment body / string: as written
        else text += joinTokens(toks, opt.spacing);
        if (!endsInString) text = rtrim(text);
        out.push_back(text);

        // remember a trailing // comment: a comment-only line below that starts in its column continues it
        if (!code.empty() && toks[toks.size() - 1].kind == Kind::Comment && toks[toks.size() - 1].text.compare(0, 2, "//") == 0) {
            trailingCommentCol = columnAt(line, line.size() - toks[toks.size() - 1].text.size(), tabW);
            trailingCommentDelta = delta;
        } else if (!(code.empty() && oldCols == trailingCommentCol)) {
            trailingCommentCol = -1;
        }

        // 6. the bracket stack, from this line's tokens
        if (caseLine && !stack.empty()) stack.top().caseOpen = true;
        for (size_t i = 0; i < toks.size(); ++i) {
            const Tok& t = toks[i];
            if (isOpener(t)) {
                Open o;
                o.ch = t.text[0];
                o.delta = delta;
                if (o.ch == '{') {
                    const DynArray<std::string> words = braceHeader(toks, i, prev);
                    o.isSwitch = isSwitchHeader(words);
                    const bool isNamespace = isNamespaceHeader(words);
                    // a brace on a continuation line belongs to the statement that started above
                    // ("int b) {" ends a header that began on an earlier line) - except the body line of a
                    // brace-less if / for / else, which is a statement of its own ("for (...) {" under "for (...)")
                    // A brace opened while a ( or [ is still open is a lambda / initializer inside a call:
                    // its body follows the line it is on.
                    const bool insideCall = !stack.empty() && stack.top().ch != '{';
                    const bool ownLine = anchor || code.empty() || isP(toks[0], "{") || (continuation && bodyLine) || insideCall;
                    o.openerCols = ownLine ? newCols : lastAnchorCols;
                    if (isP(toks[0], "}")) o.openerCols = newCols;      // "} else {"
                    o.contentCols = o.openerCols + (isNamespace ? 0 : w);
                } else {
                    o.openerCols = newCols;
                }
                stack.push(o);
            } else if (isCloser(t)) {
                closeBracket(stack, t.text);
            }
        }

        // 7. remember this line for the next one
        if (!code.empty() && !st0.inBlock) {
            const Tok& lastCode = *code[code.size() - 1];
            prev.set(lastCode.text, lastCode.kind == Kind::Word);
            prev.label = isCaseLabel(code) || isAccessSpecifier(code) || isPlainLabel(code);
            for (size_t k = 0; k < code.size(); ++k) if (code[k]->kind == Kind::Word) prev.words.push_back(code[k]->text);
        }
    }
    if (out.empty()) out.push_back(std::string());
    return out;
}

std::string formatText(const std::string& text, const FormatOptions& opt) {
    DynArray<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    const bool trailingNewline = !text.empty() && text[text.size() - 1] == '\n';
    if (!trailingNewline) lines.push_back(cur);
    const DynArray<std::string> f = formatLines(lines, opt);
    std::string r;
    for (size_t i = 0; i < f.size(); ++i) {
        r += f[i];
        if (i + 1 < f.size() || trailingNewline) r += '\n';
    }
    return r;
}

// ---------------------------------------------------------------- line diff

DynArray<LineHunk> diffLines(const DynArray<std::string>& a, const DynArray<std::string>& b) {
    DynArray<LineHunk> out;
    const size_t n = a.size(), m = b.size();
    size_t p = 0;                                          // common first lines
    while (p < n && p < m && a[p] == b[p]) ++p;
    size_t s = 0;                                          // common last lines
    while (s < n - p && s < m - p && a[n - 1 - s] == b[m - 1 - s]) ++s;
    const size_t n2 = n - p - s, m2 = m - p - s;
    auto add = [&](size_t i0, size_t i1, size_t j0, size_t j1) {
        LineHunk h;
        h.oldStart = p + i0; h.oldEnd = p + i1;
        h.newStart = p + j0; h.newEnd = p + j1;
        out.push_back(h);
    };
    if (n2 == 0 && m2 == 0) return out;
    if (n2 == m2) {                                        // same number of lines: compare them pair by pair
        size_t i = 0;
        while (i < n2) {
            if (a[p + i] == b[p + i]) { ++i; continue; }
            size_t j = i;
            while (j < n2 && a[p + j] != b[p + j]) ++j;
            add(i, j, i, j);
            i = j;
        }
        return out;
    }
    if (n2 == 0 || m2 == 0 || (n2 + 1) * (m2 + 1) > kMaxDiffCells) { add(0, n2, 0, m2); return out; }

    // longest common subsequence of the middle lines: L[i][j] = LCS of a[i..] and b[j..].
    // (n2 + 1)(m2 + 1) <= 4,000,000 means min(n2, m2) <= 2000, so the lengths fit in 16 bits.
    const size_t W = m2 + 1;
    DynArray<unsigned short> L;
    L.resize((n2 + 1) * W, 0);
    for (size_t i = n2; i-- > 0;)
        for (size_t j = m2; j-- > 0;) {
            if (a[p + i] == b[p + j]) L[i * W + j] = static_cast<unsigned short>(L[(i + 1) * W + j + 1] + 1);
            else {
                const unsigned short down = L[(i + 1) * W + j], right = L[i * W + j + 1];
                L[i * W + j] = down >= right ? down : right;
            }
        }
    // walk the table: equal lines are kept, the rest is collected into blocks
    size_t i = 0, j = 0, hi = 0, hj = 0;
    bool open = false;
    while (i < n2 || j < m2) {
        if (i < n2 && j < m2 && a[p + i] == b[p + j]) {
            if (open) { add(hi, i, hj, j); open = false; }
            ++i; ++j;
            continue;
        }
        if (!open) { open = true; hi = i; hj = j; }
        if (j >= m2 || (i < n2 && L[(i + 1) * W + j] >= L[i * W + j + 1])) ++i;   // drop a[i]
        else ++j;                                                                 // take b[j]
    }
    if (open) add(hi, n2, hj, m2);
    return out;
}

// ---------------------------------------------------------------- applying it

static std::string joinRange(const DynArray<std::string>& v, size_t a, size_t b) {
    std::string s;
    for (size_t k = a; k < b; ++k) {
        if (k > a) s += '\n';
        s += v[k];
    }
    return s;
}

size_t formatDocument(Editor& ed, const FormatOptions& opt) {
    const Document& d = ed.doc();
    DynArray<std::string> old;
    for (size_t l = 0; l < d.lineCount(); ++l) old.push_back(d.getLine(l));
    const DynArray<std::string> neu = formatLines(old, opt);
    const DynArray<LineHunk> hunks = diffLines(old, neu);
    if (hunks.empty()) return 0;

    size_t changed = 0;
    EditGroup group(ed, "format document");
    for (size_t k = hunks.size(); k-- > 0;) {              // bottom to top: the blocks above keep their line numbers
        const LineHunk& h = hunks[k];
        const size_t oldLen = h.oldEnd - h.oldStart, newLen = h.newEnd - h.newStart;
        changed += oldLen > newLen ? oldLen : newLen;
        const size_t n = d.lineCount();
        const bool hasNew = newLen > 0;
        const std::string text = joinRange(neu, h.newStart, h.newEnd);
        if (h.oldEnd < n) {                                // a line follows the block
            ed.replaceRange(Pos{h.oldStart, 0}, Pos{h.oldEnd, 0}, hasNew ? text + "\n" : std::string());
        } else if (h.oldStart < n) {                       // the block reaches the end of the document
            if (hasNew) ed.replaceRange(Pos{h.oldStart, 0}, Pos{n - 1, d.lineLength(n - 1)}, text);
            else if (h.oldStart > 0)
                ed.replaceRange(Pos{h.oldStart - 1, d.lineLength(h.oldStart - 1)}, Pos{n - 1, d.lineLength(n - 1)}, std::string());
            else ed.replaceRange(Pos{0, 0}, Pos{n - 1, d.lineLength(n - 1)}, std::string());
        } else {                                           // new lines after the last one
            ed.insertText(Pos{n - 1, d.lineLength(n - 1)}, "\n" + text);
        }
    }
    return changed;
}

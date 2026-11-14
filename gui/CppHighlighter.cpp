#include "CppHighlighter.h"

static bool isIdStart(QChar c) { return c.isLetter() || c == QChar('_'); }
static bool isIdChar(QChar c) { return c.isLetterOrNumber() || c == QChar('_'); }

CppHighlighter::CppHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {
    const char* kw[] = {"alignas", "alignof", "auto", "break", "case", "catch", "class", "const", "constexpr",
                        "const_cast", "continue", "decltype", "default", "delete", "do", "dynamic_cast", "else",
                        "enum", "explicit", "export", "extern", "false", "final", "for", "friend", "goto", "if",
                        "inline", "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override",
                        "private", "protected", "public", "register", "reinterpret_cast", "return", "sizeof",
                        "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "throw",
                        "true", "try", "typedef", "typeid", "typename", "union", "using", "virtual", "volatile",
                        "while"};
    const char* ty[] = {"bool", "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void",
                        "size_t", "string", "vector", "map", "set", "unordered_map", "unordered_set", "list",
                        "deque", "array", "pair", "tuple", "optional", "variant", "unique_ptr", "shared_ptr",
                        "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t"};
    for (const char* k : kw) keywords_.insert(QString::fromLatin1(k));
    for (const char* k : ty) types_.insert(QString::fromLatin1(k));
    setTheme(Theme::dark_());
}

void CppHighlighter::setTheme(const Theme& t) {
    theme_ = t;
    fKeyword_.setForeground(t.keyword);   fKeyword_.setFontWeight(QFont::DemiBold);
    fType_.setForeground(t.type);
    fString_.setForeground(t.string);
    fComment_.setForeground(t.comment);   fComment_.setFontItalic(true);
    fNumber_.setForeground(t.number);
    fPreproc_.setForeground(t.preproc);
    fFunction_.setForeground(t.function);
    rehighlight();
}

// Block state: 0 = normal, 1 = inside a /* ... */ comment that started on an earlier line.
void CppHighlighter::highlightBlock(const QString& s) {
    const int n = s.size();
    int i = 0;
    int state = previousBlockState() == 1 ? 1 : 0;
    bool lineStart = true;      // only blanks seen so far (for '#')

    while (i < n) {
        if (state == 1) {
            const int end = s.indexOf(QLatin1String("*/"), i);
            if (end < 0) { setFormat(i, n - i, fComment_); i = n; break; }
            setFormat(i, end + 2 - i, fComment_);
            i = end + 2;
            state = 0;
            lineStart = false;
            continue;
        }
        const QChar c = s.at(i);
        if (c.isSpace()) { ++i; continue; }

        if (c == QChar('/') && i + 1 < n && s.at(i + 1) == QChar('/')) { setFormat(i, n - i, fComment_); i = n; break; }
        if (c == QChar('/') && i + 1 < n && s.at(i + 1) == QChar('*')) { state = 1; setFormat(i, 2, fComment_); i += 2; lineStart = false; continue; }

        if (c == QChar('#') && lineStart) {
            int j = i + 1;
            while (j < n && isIdChar(s.at(j)) ) ++j;
            setFormat(i, j - i, fPreproc_);
            i = j;
            lineStart = false;
            // <header> / "header" after #include
            while (i < n && s.at(i).isSpace()) ++i;
            if (i < n && s.at(i) == QChar('<')) {
                const int e = s.indexOf(QChar('>'), i);
                if (e > 0) { setFormat(i, e + 1 - i, fString_); i = e + 1; }
            }
            continue;
        }
        lineStart = false;

        if (c == QChar('"') || (c == QChar('\'') && !(i > 0 && s.at(i - 1).isLetterOrNumber()))) {
            int j = i + 1;
            while (j < n && s.at(j) != c) { if (s.at(j) == QChar('\\')) ++j; ++j; }
            j = qMin(j + 1, n);
            setFormat(i, j - i, fString_);
            i = j;
            continue;
        }
        if (c.isDigit() || (c == QChar('.') && i + 1 < n && s.at(i + 1).isDigit())) {
            int j = i + 1;
            while (j < n && (s.at(j).isLetterOrNumber() || s.at(j) == QChar('.') || s.at(j) == QChar('\''))) ++j;
            setFormat(i, j - i, fNumber_);
            i = j;
            continue;
        }
        if (isIdStart(c)) {
            int j = i + 1;
            while (j < n && isIdChar(s.at(j))) ++j;
            const QString w = s.mid(i, j - i);
            if (keywords_.contains(w)) setFormat(i, j - i, fKeyword_);
            else if (types_.contains(w)) setFormat(i, j - i, fType_);
            else {
                int k = j;
                while (k < n && s.at(k) == QChar(' ')) ++k;
                if (k < n && s.at(k) == QChar('(')) setFormat(i, j - i, fFunction_);
            }
            i = j;
            continue;
        }
        ++i;
    }
    setCurrentBlockState(state);
}

// TextUtil.h - tiny character and string helpers shared by the whole engine.
//
// One definition, so search, autocomplete, brackets, the outline, snippets and the
// diagnostics all agree on what a "word" or a "space" is.
#pragma once
#include <cstddef>
#include <string>

// Letters, digits and '_' (ASCII only): the characters of a C/C++ identifier or number.
inline bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

inline bool isDigitChar(char c) { return c >= '0' && c <= '9'; }
inline bool isLetterChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Space, tab, carriage return or newline.
inline bool isSpaceChar(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// 's' without the spaces, tabs and line-end characters at both ends.
inline std::string trimSpaces(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpaceChar(s[a])) ++a;
    while (b > a && isSpaceChar(s[b - 1])) --b;
    return s.substr(a, b - a);
}

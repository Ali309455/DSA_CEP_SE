// Outline.h - the symbols of a file (namespaces, classes, structs, enums, functions) for the Outline panel.
//
// C-like code: every '{' ... '}' pair from the bracket checker (Brackets.h, so braces in strings and
// comments are already ignored) is looked at from its "header", the text in front of the '{'
// (joined with earlier lines while a '(' ... ')' is still open, or when the brace sits on its own line):
//
//     class Foo : public Bar {          -> Class     "Foo"
//     enum class Color : int {          -> Enum      "Color"
//     namespace {                       -> Namespace "(anonymous)"
//     int Foo::size(int a) const {      -> Function  "Foo::size"   detail "(int a)"
//     if (x) {   while (..) {   [&](){  -> nothing (keywords, lambdas)
//     connect(a, b, [this]() {          -> nothing: the '{' is inside the call's parentheses
//
// Python / scripts: "def name(...)" and "class Name" lines, nested by indentation.
//
// Nesting depth comes from a stack of the symbols that are still open (Stack.h). The pairs are put in
// order of their opening bracket with a counting sort by line. O(N + P), N characters, P pairs.
#pragma once
#include <cstddef>
#include <string>
#include "Brackets.h"
#include "Document.h"
#include "DynArray.h"

enum class SymbolKind { Namespace, Class, Struct, Union, Enum, Function };

struct Symbol {
    std::string name;      // "Foo", "Foo::size", "(anonymous)"
    std::string detail;    // functions: the parameter list, shortened; otherwise empty
    SymbolKind kind = SymbolKind::Function;
    Pos pos;               // where the name is
    size_t endLine = 0;    // the line of the closing '}' (scripts: the last line of the block)
    size_t depth = 0;      // 0 = top level, 1 = inside one symbol, ...
};

// In document order (by position of the name).
DynArray<Symbol> outlineSymbols(const Document& doc, const BracketReport& report);
DynArray<Symbol> outlineScriptSymbols(const Document& doc, size_t tabWidth = 4);

const char* symbolKindName(SymbolKind k);     // "namespace", "class", ...

// The innermost symbol whose range contains 'line' (for "you are here"), or -1.
long symbolAtLine(const DynArray<Symbol>& symbols, size_t line);

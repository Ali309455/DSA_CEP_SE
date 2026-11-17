// Diagnostics.h - one list of "problems" for the GUI's Problems tab, the red squiggles and
// the console 'check' command (no GUI code in here).
//
// Two sources feed it:
//   * the bracket checker (Brackets.h)      -> fromBrackets()        instant, runs while typing
//   * the compiler (g++ / gcc -fsyntax-only) -> runSyntaxCheck()      real syntax / type errors
//     and parseCompilerOutput() for the text the compiler printed (the GUI starts the compiler
//     itself, asynchronously, and only uses the parser).
//
// parseCompilerOutput understands the GCC / Clang format
//     path:line:col: error: message        path:line: warning: message
//     path:line:col: fatal error: ...      path:line:col: note: ...
// including Windows paths (C:\dir\file.cpp:3:5: ...), colour escape codes, the "In function"
// and source-excerpt lines (ignored), linker "undefined reference" lines and location-less
// "g++: error: no such file" lines (both reported with line == 0).
//
//   time O(size of the output), memory O(number of diagnostics). The list is capped at
//   kMaxDiagnostics so a runaway template error cannot flood the screen.
#pragma once
#include <cstddef>
#include <string>
#include "Brackets.h"
#include "DynArray.h"

enum class Severity { Error, Warning, Note };

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string file;      // as printed by the compiler; "" for a problem in the current document
    size_t line = 0;       // 1-based; 0 = the message has no location (linker errors, bad command)
    size_t col = 0;        // 1-based; 0 = unknown
    std::string message;   // one line, without the "error:" prefix
    std::string source;    // "g++", "gcc", "clang" or "brackets"
};

const size_t kMaxDiagnostics = 500;

// "error" / "warning" / "note" - for printing.
const char* severityName(Severity s);

// Parse the text a C/C++ compiler printed (stdout + stderr together). Never throws; text that
// is not a diagnostic is skipped. 'source' is stored in every entry.
DynArray<Diagnostic> parseCompilerOutput(const std::string& text, const std::string& source = "g++");

// The bracket checker's issues as diagnostics (all Errors, file == ""). Positions become 1-based.
DynArray<Diagnostic> fromBrackets(const BracketReport& report);

// How many characters to underline for a diagnostic that starts at 'col0' (0-based) of 'line':
// the whole identifier / number at that spot, the whole "string" if it starts with a quote,
// otherwise one character. Always at least 1, and never past the end of the line (a column
// past the end of the line gives 1: the GUI underlines the end-of-line spot).
size_t squiggleLength(const std::string& line, size_t col0);

// Number of diagnostics of one severity.
size_t countSeverity(const DynArray<Diagnostic>& list, Severity s);

// ---------------------------------------------------------------- running the compiler

struct SyntaxCheckOptions {
    std::string compiler;      // "" = pick by extension: gcc for .c, g++ otherwise
    std::string includeDir;    // -I folder (the folder of the real file, when checking a temporary copy)
    bool warnings = true;      // -Wall -Wextra
};

// The compiler's arguments (no shell quoting), e.g. {"-std=c++17", "-fsyntax-only", ..., path}.
// Both the console (through the shell) and the GUI (QProcess) use this list.
DynArray<std::string> syntaxCheckArgs(const std::string& sourcePath, const SyntaxCheckOptions& opt = SyntaxCheckOptions());

// The program for syntaxCheckArgs: opt.compiler, or gcc / g++ by the extension of 'sourcePath'.
std::string syntaxCheckCompiler(const std::string& sourcePath, const SyntaxCheckOptions& opt = SyntaxCheckOptions());

// Is this a file the compiler check applies to (.c .cc .cpp .cxx .h .hpp .hh .hxx)?
bool isCompilableSource(const std::string& path);

struct SyntaxCheckResult {
    bool ran = false;                 // the compiler was started and finished
    bool compilerFound = true;        // false: g++ / gcc is not installed or not on PATH
    int exitCode = 0;
    std::string message;              // when !ran: why ("g++ was not found ...")
    std::string output;               // everything the compiler printed
    DynArray<Diagnostic> diagnostics;
};

// Run "<compiler> -fsyntax-only ..." on a file and parse what it prints. Blocks until the
// compiler ends (it does not compile or link: usually well under a second). Never throws.
SyntaxCheckResult runSyntaxCheck(const std::string& sourcePath, const SyntaxCheckOptions& opt = SyntaxCheckOptions());

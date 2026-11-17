// Diagnostics.cpp - see Diagnostics.h.
#include "Diagnostics.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifndef _WIN32
#include <sys/wait.h>
#endif

const char* severityName(Severity s) {
    switch (s) {
        case Severity::Error: return "error";
        case Severity::Warning: return "warning";
        default: return "note";
    }
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }
static bool isWordCh(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '_';
}

// Remove ANSI colour / cursor escape sequences (ESC [ ... letter).
static std::string stripAnsi(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !((s[i] >= '@' && s[i] <= '~'))) ++i;   // up to the final byte
            continue;
        }
        o.push_back(s[i]);
    }
    return o;
}

static bool startsWith(const std::string& s, size_t at, const char* w) {
    for (size_t i = 0; w[i]; ++i)
        if (at + i >= s.size() || s[at + i] != w[i]) return false;
    return true;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// After "error: " style word: which severity does the text at 'at' start with?
// On success 'len' is the length of "error: " etc.
static bool severityAt(const std::string& s, size_t at, Severity& sev, size_t& len) {
    if (startsWith(s, at, "fatal error: ")) { sev = Severity::Error; len = 13; return true; }
    if (startsWith(s, at, "error: "))       { sev = Severity::Error; len = 7; return true; }
    if (startsWith(s, at, "warning: "))     { sev = Severity::Warning; len = 9; return true; }
    if (startsWith(s, at, "note: "))        { sev = Severity::Note; len = 6; return true; }
    return false;
}

// Try "<path>:<line>[:<col>]: <severity>: <message>". Windows drive letters ("C:\") are skipped
// when looking for the first colon.
static bool parseLocated(const std::string& s, Diagnostic& d) {
    size_t from = 0;
    if (s.size() > 2 && ((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')) && s[1] == ':' &&
        (s[2] == '\\' || s[2] == '/'))
        from = 2;
    // Scan colons: the location is the first ':' that is followed by digits and another ':'.
    for (size_t p = s.find(':', from); p != std::string::npos; p = s.find(':', p + 1)) {
        size_t i = p + 1;
        size_t lineStart = i;
        while (i < s.size() && isDigit(s[i])) ++i;
        if (i == lineStart || i >= s.size() || s[i] != ':') continue;
        size_t lineNo = static_cast<size_t>(std::strtoul(s.substr(lineStart, i - lineStart).c_str(), nullptr, 10));
        size_t colNo = 0;
        size_t j = i + 1;
        size_t colStart = j;
        while (j < s.size() && isDigit(s[j])) ++j;
        if (j > colStart && j < s.size() && s[j] == ':') {
            colNo = static_cast<size_t>(std::strtoul(s.substr(colStart, j - colStart).c_str(), nullptr, 10));
            i = j;                                           // "line:col:" form
        }
        size_t k = i + 1;
        while (k < s.size() && s[k] == ' ') ++k;
        Severity sev;
        size_t len;
        if (!severityAt(s, k, sev, len)) return false;       // "path:3:5:   required from here" etc.
        d.file = s.substr(0, p);
        d.line = lineNo;
        d.col = colNo;
        d.severity = sev;
        d.message = trim(s.substr(k + len));
        return true;
    }
    return false;
}

// "g++: error: foo.cpp: No such file or directory"  (no line number)
static bool parseUnlocated(const std::string& s, Diagnostic& d) {
    size_t i = 0;
    while (i < s.size() && (isWordCh(s[i]) || s[i] == '+' || s[i] == '-' || s[i] == '.')) ++i;
    if (i == 0 || i + 1 >= s.size() || s[i] != ':' || s[i + 1] != ' ') return false;
    Severity sev;
    size_t len;
    if (!severityAt(s, i + 2, sev, len)) return false;
    std::string msg = trim(s.substr(i + 2 + len));
    if (msg.compare(0, 11, "ld returned") == 0) return false;    // the real cause was reported already
    d.file = "";
    d.line = 0;
    d.col = 0;
    d.severity = sev;
    d.message = msg;
    return true;
}

DynArray<Diagnostic> parseCompilerOutput(const std::string& text, const std::string& source) {
    DynArray<Diagnostic> out;
    size_t pos = 0;
    while (pos <= text.size() && out.size() < kMaxDiagnostics) {
        size_t nl = text.find('\n', pos);
        std::string raw = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        std::string line = stripAnsi(raw);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;

        Diagnostic d;
        d.source = source;
        if (parseLocated(line, d)) { out.push_back(d); continue; }

        size_t ur = line.find("undefined reference to");
        if (ur != std::string::npos) {
            d.severity = Severity::Error;
            d.message = trim(line.substr(ur));
            out.push_back(d);
            continue;
        }
        if (parseUnlocated(line, d)) out.push_back(d);
    }
    return out;
}

DynArray<Diagnostic> fromBrackets(const BracketReport& report) {
    DynArray<Diagnostic> out;
    for (size_t i = 0; i < report.issues.size() && out.size() < kMaxDiagnostics; ++i) {
        const BracketIssue& b = report.issues[i];
        Diagnostic d;
        d.severity = Severity::Error;
        d.line = b.pos.line + 1;
        d.col = b.pos.col + 1;
        d.source = "brackets";
        std::string m = b.message;      // already says where: "Mismatch at 3:7: ..."
        d.message = m;
        out.push_back(d);
    }
    return out;
}

size_t squiggleLength(const std::string& line, size_t col0) {
    if (col0 >= line.size()) return 1;
    char c = line[col0];
    if (isWordCh(c)) {
        size_t e = col0;
        while (e < line.size() && isWordCh(line[e])) ++e;
        return e - col0;
    }
    if (c == '"') {
        for (size_t e = col0 + 1; e < line.size(); ++e) {
            if (line[e] == '\\') { ++e; continue; }
            if (line[e] == '"') return e - col0 + 1;
        }
        return line.size() - col0;
    }
    return 1;
}

size_t countSeverity(const DynArray<Diagnostic>& list, Severity s) {
    size_t n = 0;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].severity == s) ++n;
    return n;
}

// ---------------------------------------------------------------- running the compiler

static std::string lowerExt(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string e = path.substr(dot + 1);
    for (size_t i = 0; i < e.size(); ++i)
        if (e[i] >= 'A' && e[i] <= 'Z') e[i] = static_cast<char>(e[i] - 'A' + 'a');
    return e;
}

bool isCompilableSource(const std::string& path) {
    std::string e = lowerExt(path);
    return e == "c" || e == "cc" || e == "cpp" || e == "cxx" || e == "h" || e == "hpp" || e == "hh" || e == "hxx";
}

std::string syntaxCheckCompiler(const std::string& sourcePath, const SyntaxCheckOptions& opt) {
    if (!opt.compiler.empty()) return opt.compiler;
    return lowerExt(sourcePath) == "c" ? "gcc" : "g++";
}

DynArray<std::string> syntaxCheckArgs(const std::string& sourcePath, const SyntaxCheckOptions& opt) {
    DynArray<std::string> a;
    const bool isC = lowerExt(sourcePath) == "c";
    a.push_back(isC ? "-std=c11" : "-std=c++17");
    a.push_back("-fsyntax-only");
    a.push_back("-fno-diagnostics-color");
    if (opt.warnings) { a.push_back("-Wall"); a.push_back("-Wextra"); }
    if (!opt.includeDir.empty()) { a.push_back("-I"); a.push_back(opt.includeDir); }
    const std::string e = lowerExt(sourcePath);
    if (e == "h" || e == "hpp" || e == "hh" || e == "hxx") { a.push_back("-x"); a.push_back(isC ? "c" : "c++"); }
    a.push_back(sourcePath);
    return a;
}

// Quote one argument for the shell that popen uses.
static std::string shellQuote(const std::string& s) {
#ifdef _WIN32
    std::string o = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') o += "\\\"";
        else o.push_back(s[i]);
    }
    return o + "\"";
#else
    std::string o = "'";
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\'') o += "'\\''";
        else o.push_back(s[i]);
    }
    return o + "'";
#endif
}

SyntaxCheckResult runSyntaxCheck(const std::string& sourcePath, const SyntaxCheckOptions& opt) {
    SyntaxCheckResult r;
    if (sourcePath.empty()) { r.message = "No file to check."; return r; }
    const std::string compiler = syntaxCheckCompiler(sourcePath, opt);
    DynArray<std::string> args = syntaxCheckArgs(sourcePath, opt);
    std::string cmd;
#ifndef _WIN32
    cmd = "LC_ALL=C ";                         // English messages, whatever the system language
#endif
    cmd += compiler;
    for (size_t i = 0; i < args.size(); ++i) cmd += " " + shellQuote(args[i]);
    cmd += " 2>&1";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";                   // cmd /c strips one pair of outer quotes
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) { r.compilerFound = false; r.message = "Could not start '" + compiler + "'."; return r; }
    char buf[4096];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, pipe)) > 0) r.output.append(buf, got);
#ifdef _WIN32
    int status = _pclose(pipe);
    r.exitCode = status;
#else
    int status = pclose(pipe);
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    const bool missing = r.exitCode == 127 || r.exitCode == 9009 ||
                         r.output.find("not recognized as an internal or external command") != std::string::npos ||
                         r.output.find("command not found") != std::string::npos;
    if (missing) {
        r.compilerFound = false;
        r.message = "'" + compiler + "' was not found. Install it (MinGW-w64 / GCC) and put it on PATH.";
        return r;
    }
    r.ran = true;
    r.diagnostics = parseCompilerOutput(r.output, compiler);
    return r;
}

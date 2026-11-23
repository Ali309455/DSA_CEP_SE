// console_main.cpp - a command-line front end for the engine (phases 1-4).
// Lines and columns are 1-based here (as a user would count them).

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>     // GetModuleFileNameA: where the .exe is
#endif

#include "Assist.h"
#include "Brackets.h"
#include "Diagnostics.h"
#include "DynArray.h"
#include "Document.h"
#include "Editor.h"
#include "FileIO.h"
#include "Formatter.h"
#include "Search.h"

static void printHelp() {
    std::cout <<
        "Commands (line and column numbers start at 1).\n"
        "Words in <angle brackets> are placeholders: type the value WITHOUT the < >.\n"
        "  File:\n"
        "    new                         start an empty document\n"
        "    open <file>                 open an existing text file (a plain name is looked up in files/ first)\n"
        "    save                        save to the current file\n"
        "    saveas <file>               save under a new name; a plain name is saved in the project's\n"
        "                                files/ folder (created if needed), a full path is used as typed\n"
        "    quit                        exit\n"
        "  View:\n"
        "    show [from [to]]            show lines with numbers\n"
        "    info                        file name, line and character counts\n"
        "  Insert:\n"
        "    append <text>               add a new line at the end\n"
        "    addline <line> <text>       insert a new line before <line>\n"
        "    ins <line> <col> <text>     insert text at a position\n"
        "    nl <line> <col>             split the line at a position (new line)\n"
        "  Delete:\n"
        "    del <line> <col> [count]    delete characters (default 1)\n"
        "    delword <line> <col>        delete the word at a position\n"
        "    delrange <l1> <c1> <l2> <c2>  delete from (l1,c1) up to, not including, (l2,c2)\n"
        "    dellines <from> [to]        delete one line or a range of lines\n"
        "  Replace:\n"
        "    replace <l1> <c1> <l2> <c2> <text>  replace a range of text with <text>\n"
        "  Undo / Redo:\n"
        "    undo                        reverse the last edit\n"
        "    redo                        apply an undone edit again\n"
        "    history                     how many steps can be undone / redone\n"
        "  Search / replace by text (KMP):\n"
        "    find [-i] [-w] <text>       list every place <text> occurs (line:column)\n"
        "    replaceall [-i] [-w] <old> <new>      replace every occurrence (one undo step)\n"
        "    replacenth [-i] [-w] <n> <old> <new>  replace the n-th occurrence (n as in 'find')\n"
        "      -i ignore upper/lower case     -w whole words only\n"
        "      Put text in \"double quotes\" if it has spaces or is empty (\"\"); inside quotes\n"
        "      \\n is a line break, \\t a tab, \\\" a quote, \\\\ a backslash.\n"
        "  Code help (phase 4):\n"
        "    check                       compiler check (g++ -fsyntax-only) + brackets: every error / warning with line:column\n"
        "    format                      tidy indentation and spacing (C / C++ style, one undo step)\n"
        "    brackets                    check ( ) [ ] { } in the document (ignores strings and comments)\n"
        "    match <line> <col>          show the partner of the bracket at / just before a position\n"
        "    complete <prefix>           suggest words (C++ keywords + words in this document)\n"
        "    complete <line> <col>       suggest for the word just before a position\n"
        "    accept <line> <col> <word>  finish the word before a position with <word> (one undo step)\n"
        "    dictadd <word>              add a word to the autocomplete dictionary\n"
        "    dictdel <word>              remove a word from the dictionary\n"
        "  Snippets (hash table, phase 4):\n"
        "    expand <line> <col>         replace the snippet word just before a position with its body\n"
        "                                (one undo step; the new cursor position is printed)\n"
        "    snippet list                all triggers, alphabetical\n"
        "    snippet show <trigger>      print one snippet\n"
        "    snippet add <trigger> <body>  add / replace a snippet; quote the body: \"if ($0) {\\n    \\n}\"\n"
        "                                ($0 = where the cursor goes, $$ = a real dollar sign)\n"
        "    snippet del <trigger>       remove a snippet\n"
        "    snippet load <file>         add the snippets of a file (trigger<TAB>body per line)\n"
        "    snippet save <file>         write all snippets to a file\n"
        "    snippet defaults            (re)load the built-in C++ snippets\n"
        "    snippet stats               hash table size, buckets, load factor, longest chain\n"
        "  help                          show this list\n";
}

static bool parseNum(const std::string& s, size_t& out) {
    if (s.empty() || s[0] == '-' || s[0] == '+') return false;
    char* end = nullptr;
    errno = 0;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (*end != '\0' || errno != 0) return false;
    out = static_cast<size_t>(v);
    return true;
}

// Take up to k whitespace-separated words from 'in'; whatever is left (minus
// leading blanks) goes to 'rest'. Returns how many words were found.
static size_t takeWords(const std::string& in, size_t k, DynArray<std::string>& words, std::string& rest) {
    words.clear();
    size_t i = 0;
    while (words.size() < k) {
        while (i < in.size() && (in[i] == ' ' || in[i] == '\t')) ++i;
        if (i >= in.size()) break;
        size_t start = i;
        while (i < in.size() && in[i] != ' ' && in[i] != '\t') ++i;
        words.push_back(in.substr(start, i - start));
    }
    while (i < in.size() && (in[i] == ' ' || in[i] == '\t')) ++i;
    rest = in.substr(i);
    return words.size();
}

static std::string preview(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size() && out.size() < 60; ++i) {
        if (s[i] == '\n') out += "\\n"; else out.push_back(s[i]);
    }
    if (s.size() > out.size()) out += "...";
    return out;
}

static std::string toLower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = static_cast<char>(s[i] - 'A' + 'a');
    return s;
}

// ---------------------------------------------------------------- where files are saved
// Plain file names (saveas notes.txt) go into a folder called "files" in the project root, not
// into whatever folder the program happens to run from (usually build/). The project root is
// found by walking up from the .exe to the folder that holds CMakeLists.txt and engine/; if the
// .exe was copied somewhere else, "files" is created next to the .exe instead.

static std::filesystem::path exeFolder() {
    namespace fs = std::filesystem;
    std::error_code ec;
#ifdef _WIN32
    char buf[4096];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) return fs::path(std::string(buf, len)).parent_path();
#else
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
#endif
    return fs::current_path(ec);
}

static const std::filesystem::path& filesFolder() {
    namespace fs = std::filesystem;
    static const fs::path folder = []() {
        std::error_code ec;
        const fs::path start = exeFolder();
        for (fs::path p = start; !p.empty(); p = p.parent_path()) {
            if (fs::exists(p / "CMakeLists.txt", ec) && fs::is_directory(p / "engine", ec)) return p / "files";
            if (p == p.parent_path()) break;            // reached the drive root
        }
        return start / "files";
    }();
    return folder;
}

// For saving: a relative name is placed inside files/ (the folder is created if needed);
// an absolute path is used as it is.
static std::string pathForSave(const std::string& name) {
    namespace fs = std::filesystem;
    fs::path p(name);
    if (p.is_absolute()) return name;
    fs::path full = filesFolder() / p;
    std::error_code ec;
    fs::create_directories(full.parent_path(), ec);   // if this fails, saving reports the problem
    return full.make_preferred().string();
}

// For opening: a relative name is looked up in files/ first, then in the current folder.
static std::string pathForOpen(const std::string& name) {
    namespace fs = std::filesystem;
    fs::path p(name);
    if (p.is_absolute()) return name;
    std::error_code ec;
    fs::path inFiles = filesFolder() / p;
    if (fs::is_regular_file(inFiles, ec)) return inFiles.make_preferred().string();
    return name;
}

// "Saved 'notes.txt' at: D:\...\files\notes.txt"
static void printSaved(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path full = fs::absolute(path, ec);
    std::cout << "Saved '" << fs::path(path).filename().string() << "' at: "
              << (ec ? path : full.make_preferred().string()) << "\n";
}

static bool ask(const std::string& question) {
    std::cout << question << " (y/n): " << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) return false;
    return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
}

static bool okToDiscard(const Document& doc) {
    return !doc.modified() || ask("There are unsaved changes. Discard them?");
}

static void show(const Document& doc, size_t from, size_t to) {
    if (doc.isEmpty()) { std::cout << "(document is empty)\n"; return; }
    size_t width = std::to_string(to + 1).size();
    for (size_t i = from; i <= to; ++i) {
        std::string num = std::to_string(i + 1);
        std::cout << std::string(width - num.size(), ' ') << num << " | " << doc.getLine(i) << "\n";
    }
}

static void info(const Document& doc) {
    std::cout << "File      : " << (doc.path().empty() ? "(not saved yet)" : doc.path()) << "\n"
              << "Lines     : " << doc.lineCount() << "\n"
              << "Characters: " << doc.charCount() << "\n"
              << "Modified  : " << (doc.modified() ? "yes" : "no") << "\n";
}

// Convert user (1-based) numbers to a checked engine Pos. Throws with a friendly message.
static Pos userPos(const Document& doc, size_t line1, size_t col1) {
    if (line1 < 1 || line1 > doc.lineCount())
        throw std::out_of_range("Line " + std::to_string(line1) + " does not exist (the document has " +
                                std::to_string(doc.lineCount()) + " line(s)).");
    size_t len = doc.lineLength(line1 - 1);
    if (col1 < 1 || col1 > len + 1)
        throw std::out_of_range("Column " + std::to_string(col1) + " is not valid on line " +
                                std::to_string(line1) + " (valid: 1 to " + std::to_string(len + 1) + ").");
    return Pos{line1 - 1, col1 - 1};
}

static void needNumbers(const DynArray<std::string>& w, size_t count, size_t* out, const char* usage) {
    if (w.size() < count) throw std::invalid_argument(std::string("Usage: ") + usage);
    for (size_t i = 0; i < count; ++i)
        if (!parseNum(w[i], out[i])) throw std::invalid_argument(std::string("'") + w[i] + "' is not a valid number. Usage: " + usage);
}

static void requireNotEmpty(const Document& doc, const char* verb = "delete") {
    if (doc.isEmpty()) throw std::logic_error(std::string("The document is empty, there is nothing to ") + verb + ".");
}

static bool isBlank(char c) { return c == ' ' || c == '\t'; }

// Read one argument from 'in' starting at 'i': either "text in quotes" (with the
// escapes \n \t \" \\) or a bare word. Returns false when only blanks are left.
static bool takeArg(const std::string& in, size_t& i, std::string& out) {
    while (i < in.size() && isBlank(in[i])) ++i;
    if (i >= in.size()) return false;
    out.clear();
    if (in[i] == '"') {
        ++i;
        bool closed = false;
        while (i < in.size()) {
            char c = in[i++];
            if (c == '"') { closed = true; break; }
            if (c == '\\' && i < in.size()) {
                char e = in[i++];
                if (e == 'n') out.push_back('\n');
                else if (e == 't') out.push_back('\t');
                else if (e == '"' || e == '\\') out.push_back(e);
                else { out.push_back('\\'); out.push_back(e); }
            } else {
                out.push_back(c);
            }
        }
        if (!closed) throw std::invalid_argument("Missing closing quote (\").");
        if (i < in.size() && !isBlank(in[i])) throw std::invalid_argument("Unexpected text right after the closing quote.");
    } else {
        while (i < in.size() && !isBlank(in[i])) out.push_back(in[i++]);
    }
    return true;
}

// Leading options: -i  -w  -iw  (and -- to stop). Moves 'i' past them.
static void takeFlags(const std::string& in, size_t& i, SearchOptions& opt) {
    while (true) {
        size_t j = i;
        while (j < in.size() && isBlank(in[j])) ++j;
        if (j >= in.size() || in[j] != '-') return;
        size_t k = j + 1;
        if (k < in.size() && in[k] == '-' && (k + 1 >= in.size() || isBlank(in[k + 1]))) { i = k + 1; return; }
        bool any = false, valid = true;
        SearchOptions next = opt;
        while (k < in.size() && !isBlank(in[k])) {
            if (in[k] == 'i') next.matchCase = false;
            else if (in[k] == 'w') next.wholeWord = true;
            else { valid = false; break; }
            any = true;
            ++k;
        }
        if (!any || !valid) return;          // not a flag: it is part of the text (e.g. "-x" or "-5")
        opt = next;
        i = k;
    }
}

static std::string describe(const SearchOptions& o) {
    std::string s = o.matchCase ? "case sensitive" : "ignoring case";
    if (o.wholeWord) s += ", whole words";
    return s;
}

// Returns false when the user wants to quit.
static void snippetCommand(SnippetTable& sn, const std::string& args) {
    const char* usage = "Usage: snippet list | show <trigger> | add <trigger> <body> | del <trigger> | load <file> | save <file> | defaults | stats";
    size_t i = 0;
    std::string sub;
    if (!takeArg(args, i, sub)) throw std::invalid_argument(usage);
    sub = toLower(sub);
    std::string a1, a2, extra;
    if (sub == "list") {
        if (takeArg(args, i, extra)) throw std::invalid_argument("Usage: snippet list");
        DynArray<std::string> keys = sn.triggers();
        if (keys.empty()) { std::cout << "There are no snippets (try 'snippet defaults').\n"; return; }
        std::cout << keys.size() << " snippet(s):\n";
        for (size_t k = 0; k < keys.size(); ++k)
            std::cout << "  " << keys[k] << "   " << preview(sn.find(keys[k])->body) << "\n";
    } else if (sub == "show") {
        if (!takeArg(args, i, a1) || takeArg(args, i, extra)) throw std::invalid_argument("Usage: snippet show <trigger>");
        const Snippet* s = sn.find(a1);
        if (!s) throw std::invalid_argument("There is no snippet \"" + preview(a1) + "\".");
        std::cout << s->trigger << " =\n" << s->body << "\n";
    } else if (sub == "add") {
        if (!takeArg(args, i, a1) || !takeArg(args, i, a2) || takeArg(args, i, extra))
            throw std::invalid_argument("Usage: snippet add <trigger> <body>   (put the body in \"quotes\"; \\n is a line break)");
        bool fresh = sn.add(a1, a2);                 // throws with the reason when invalid
        std::cout << (fresh ? "Added" : "Replaced") << " snippet \"" << a1 << "\" (" << sn.size() << " in total).\n";
    } else if (sub == "del" || sub == "remove") {
        if (!takeArg(args, i, a1) || takeArg(args, i, extra)) throw std::invalid_argument("Usage: snippet del <trigger>");
        if (!sn.remove(a1)) throw std::invalid_argument("There is no snippet \"" + preview(a1) + "\".");
        std::cout << "Removed \"" << a1 << "\" (" << sn.size() << " left).\n";
    } else if (sub == "load" || sub == "save") {
        if (!takeArg(args, i, a1) || takeArg(args, i, extra)) throw std::invalid_argument("Usage: snippet " + sub + " <file>");
        const std::string path = (sub == "load") ? pathForOpen(a1) : pathForSave(a1);
        IOResult r = (sub == "load") ? sn.loadFromFile(path) : sn.saveToFile(path);
        std::cout << (r.ok ? "" : "Error: ") << r.message << "\n";
    } else if (sub == "defaults") {
        if (takeArg(args, i, extra)) throw std::invalid_argument("Usage: snippet defaults");
        sn.loadDefaults();
        std::cout << "Built-in snippets loaded (" << sn.size() << " snippets).\n";
    } else if (sub == "stats") {
        if (takeArg(args, i, extra)) throw std::invalid_argument("Usage: snippet stats");
        std::cout << sn.size() << " snippet(s) in " << sn.bucketCount() << " buckets, longest chain "
                  << sn.longestChain() << ".\n";
    } else {
        throw std::invalid_argument("Unknown snippet command '" + preview(sub) + "'. " + usage);
    }
}

// check: brackets (instant) + the compiler's syntax check, one list like the GUI's Problems tab.
static void checkCommand(const Document& doc) {
    DynArray<Diagnostic> all = fromBrackets(checkBrackets(doc));
    std::string note;
    // The compiler needs a file: a temporary copy of what is on screen, so unsaved edits are checked too.
    std::string ext = ".cpp";
    if (!doc.path().empty()) {
        std::string p = doc.path();
        size_t dot = p.find_last_of('.');
        if (dot != std::string::npos && isCompilableSource(p)) ext = p.substr(dot);
        else if (!isCompilableSource(p)) note = "(file is not C/C++: compiler check skipped)";
    }
    if (note.empty()) {
        std::error_code ec;
        std::filesystem::path tmp = std::filesystem::temp_directory_path(ec) / ("console_check_tmp" + ext);
        if (ec) {
            note = "(no temporary folder: compiler check skipped)";
        } else {
            std::ofstream out(tmp.string().c_str(), std::ios::binary | std::ios::trunc);
            out << doc.toString();
            out.close();
            SyntaxCheckOptions opt;
            if (!doc.path().empty()) opt.includeDir = std::filesystem::path(doc.path()).parent_path().string();
            SyntaxCheckResult r = runSyntaxCheck(tmp.string(), opt);
            std::filesystem::remove(tmp, ec);
            if (!r.ran) note = "(compiler check skipped: " + r.message + ")";
            else for (size_t i = 0; i < r.diagnostics.size(); ++i) all.push_back(r.diagnostics[i]);
        }
    }
    const size_t e = countSeverity(all, Severity::Error), w = countSeverity(all, Severity::Warning);
    if (all.empty()) std::cout << "No problems found.";
    else std::cout << e << " error(s), " << w << " warning(s):\n";
    for (size_t i = 0; i < all.size(); ++i) {
        const Diagnostic& d = all[i];
        if (d.severity == Severity::Note) continue;
        std::cout << "  ";
        if (d.line) std::cout << d.line << ":" << (d.col ? d.col : 1) << "  "; else std::cout << "-    ";
        std::cout << severityName(d.severity) << " [" << d.source << "]  " << d.message << "\n";
    }
    if (!note.empty()) std::cout << (all.empty() ? " " : "") << note << "\n";
    else if (all.empty()) std::cout << "\n";
}

// ---------------------------------------------------------------- commands
//
// Every command is one small function: it gets the session and the text after the command
// word ('rest'), and returns false only to end the program (quit). Errors are thrown and
// printed by main(). The table below maps command names to these functions.

struct Session {
    Editor ed;
    Autocomplete ac;
    SnippetTable snippets;
    const Document& doc() const { return ed.doc(); }
};

using Handler = bool (*)(Session&, std::string& rest);

static void throwBadLineRange(const Document& doc, size_t from, size_t to) {
    throw std::out_of_range("Line range " + std::to_string(from) + "-" + std::to_string(to) +
                            " is not valid (the document has " + std::to_string(doc.lineCount()) + " line(s)).");
}

// ---- files

static bool cmdHelp(Session&, std::string&) { printHelp(); return true; }

static bool cmdQuit(Session& s, std::string&) { return !okToDiscard(s.doc()); }

static bool cmdNew(Session& s, std::string&) {
    if (okToDiscard(s.doc())) { s.ed.newDocument(); std::cout << "New empty document.\n"; }
    return true;
}

static bool cmdOpen(Session& s, std::string& rest) {
    if (rest.empty()) throw std::invalid_argument("Usage: open <file>");
    if (!okToDiscard(s.doc())) return true;
    IOResult r = s.ed.open(pathForOpen(rest));
    std::cout << r.message;
    if (r.ok) std::cout << " (" << s.doc().lineCount() << " lines, " << s.doc().charCount() << " characters)";
    std::cout << "\n";
    return true;
}

static bool cmdSave(Session& s, std::string&) {
    const std::string path = s.doc().path();
    if (path.empty()) throw std::logic_error("This document has no filename yet. Use: saveas <file>");
    IOResult r = s.ed.save(path);
    if (r.ok) printSaved(path); else std::cout << r.message << "\n";
    return true;
}

static bool cmdSaveAs(Session& s, std::string& rest) {
    if (rest.empty()) throw std::invalid_argument("Usage: saveas <file>");
    IOResult r = s.ed.save(pathForSave(rest));
    if (r.ok) printSaved(s.doc().path()); else std::cout << r.message << "\n";
    return true;
}

static bool cmdInfo(Session& s, std::string&) { info(s.doc()); return true; }

static bool cmdShow(Session& s, std::string& rest) {
    const Document& doc = s.doc();
    DynArray<std::string> w;
    const size_t got = takeWords(rest, 2, w, rest);
    size_t from = 1, to = doc.lineCount();
    if (got >= 1 && !parseNum(w[0], from)) throw std::invalid_argument("Usage: show [from [to]]");
    if (got >= 2 && !parseNum(w[1], to)) throw std::invalid_argument("Usage: show [from [to]]");
    if (got == 1) to = from;
    if (from < 1 || to > doc.lineCount() || from > to) throwBadLineRange(doc, from, to);
    show(doc, from - 1, to - 1);
    return true;
}

// ---- editing

static bool cmdAppend(Session& s, std::string& rest) {
    if (s.doc().isEmpty()) s.ed.insertText(Pos{0, 0}, rest);
    else s.ed.insertLineBefore(s.doc().lineCount(), rest);
    std::cout << "Added at line " << s.doc().lineCount() << ".\n";
    return true;
}

static bool cmdAddLine(Session& s, std::string& rest) {
    DynArray<std::string> w;
    size_t n[1];
    takeWords(rest, 1, w, rest);
    needNumbers(w, 1, n, "addline <line> <text>");
    if (n[0] < 1 || n[0] > s.doc().lineCount() + 1)
        throw std::out_of_range("Line " + std::to_string(n[0]) + " is not valid (use 1 to " +
                                std::to_string(s.doc().lineCount() + 1) + ").");
    s.ed.insertLineBefore(n[0] - 1, rest);
    std::cout << "Inserted new line " << n[0] << ".\n";
    return true;
}

static bool cmdIns(Session& s, std::string& rest) {
    DynArray<std::string> w;
    size_t n[2];
    takeWords(rest, 2, w, rest);
    needNumbers(w, 2, n, "ins <line> <col> <text>");
    if (rest.empty()) throw std::invalid_argument("Nothing to insert. Usage: ins <line> <col> <text>");
    s.ed.insertText(userPos(s.doc(), n[0], n[1]), rest);
    std::cout << "Inserted " << rest.size() << " character(s) at " << n[0] << ":" << n[1] << ".\n";
    return true;
}

static bool cmdNewLine(Session& s, std::string& rest) {
    DynArray<std::string> w;
    size_t n[2];
    takeWords(rest, 2, w, rest);
    needNumbers(w, 2, n, "nl <line> <col>");
    s.ed.insertNewLine(userPos(s.doc(), n[0], n[1]));
    std::cout << "Line split at " << n[0] << ":" << n[1] << ".\n";
    return true;
}

static bool cmdDel(Session& s, std::string& rest) {
    requireNotEmpty(s.doc());
    DynArray<std::string> w;
    size_t n[2];
    const size_t got = takeWords(rest, 3, w, rest);
    needNumbers(w, 2, n, "del <line> <col> [count]");
    size_t count = 1;
    if (got >= 3 && (!parseNum(w[2], count) || count == 0))
        throw std::invalid_argument("Usage: del <line> <col> [count]   (count must be 1 or more)");
    const std::string gone = s.ed.deleteChars(userPos(s.doc(), n[0], n[1]), count);
    std::cout << "Deleted \"" << preview(gone) << "\".\n";
    return true;
}

static bool cmdDelWord(Session& s, std::string& rest) {
    requireNotEmpty(s.doc());
    DynArray<std::string> w;
    size_t n[2];
    takeWords(rest, 2, w, rest);
    needNumbers(w, 2, n, "delword <line> <col>");
    const std::string gone = s.ed.deleteWord(userPos(s.doc(), n[0], n[1]));
    std::cout << "Deleted word \"" << preview(gone) << "\".\n";
    return true;
}

static bool cmdDelRange(Session& s, std::string& rest) {
    requireNotEmpty(s.doc());
    DynArray<std::string> w;
    size_t n[4];
    takeWords(rest, 4, w, rest);
    needNumbers(w, 4, n, "delrange <l1> <c1> <l2> <c2>");
    const std::string gone = s.ed.deleteRange(userPos(s.doc(), n[0], n[1]), userPos(s.doc(), n[2], n[3]));
    std::cout << "Deleted " << gone.size() << " character(s): \"" << preview(gone) << "\".\n";
    return true;
}

static bool cmdDelLines(Session& s, std::string& rest) {
    requireNotEmpty(s.doc());
    DynArray<std::string> w;
    size_t n[1];
    const size_t got = takeWords(rest, 2, w, rest);
    needNumbers(w, 1, n, "dellines <from> [to]");
    size_t from = n[0], to = from;
    if (got >= 2 && !parseNum(w[1], to)) throw std::invalid_argument("Usage: dellines <from> [to]");
    if (from < 1 || to < from || to > s.doc().lineCount()) throwBadLineRange(s.doc(), from, to);
    s.ed.deleteLines(from - 1, to - 1);
    std::cout << "Deleted line(s) " << from << (to > from ? "-" + std::to_string(to) : std::string()) << ".\n";
    return true;
}

static bool cmdReplaceRange(Session& s, std::string& rest) {
    requireNotEmpty(s.doc());
    DynArray<std::string> w;
    size_t n[4];
    takeWords(rest, 4, w, rest);
    needNumbers(w, 4, n, "replace <l1> <c1> <l2> <c2> <text>");
    const Pos a = userPos(s.doc(), n[0], n[1]);
    const Pos b = userPos(s.doc(), n[2], n[3]);
    const std::string before = s.doc().getRange(a, b);
    s.ed.replaceRange(a, b, rest);
    std::cout << "Replaced \"" << preview(before) << "\" with \"" << preview(rest) << "\".\n";
    return true;
}

// ---- search and replace

static bool cmdFind(Session& s, std::string& rest) {
    const Document& doc = s.doc();
    requireNotEmpty(doc, "search");
    SearchOptions opt;
    size_t i = 0;
    takeFlags(rest, i, opt);
    std::string what;
    while (i < rest.size() && isBlank(rest[i])) ++i;
    if (i < rest.size() && rest[i] == '"') {                     // quoted: exact text, spaces kept
        if (!takeArg(rest, i, what)) what.clear();
        while (i < rest.size() && isBlank(rest[i])) ++i;
        if (i < rest.size()) throw std::invalid_argument("Unexpected text after the closing quote.");
    } else {
        what = rest.substr(i);                                    // the whole rest of the line
    }
    if (what.empty()) throw std::invalid_argument("Usage: find [-i] [-w] <text>   (the search text is empty)");
    DynArray<Match> hits = findAll(doc, what, opt);
    if (hits.empty()) {
        std::cout << "\"" << preview(what) << "\" was not found (" << describe(opt) << ").\n";
        return true;
    }
    std::cout << "Found " << hits.size() << " occurrence(s) of \"" << preview(what) << "\" (" << describe(opt) << "):\n";
    const size_t shown = hits.size() < 50 ? hits.size() : 50;
    for (size_t k = 0; k < shown; ++k)
        std::cout << "  " << (k + 1) << ")  " << (hits[k].start.line + 1) << ":" << (hits[k].start.col + 1)
                  << "  | " << preview(doc.getLine(hits[k].start.line)) << "\n";
    if (hits.size() > shown) std::cout << "  ... and " << (hits.size() - shown) << " more.\n";
    return true;
}

// replaceall [-i] [-w] <old> <new>   and   replacenth [-i] [-w] <n> <old> <new>
static bool replaceCommand(Session& s, std::string& rest, bool nth) {
    const char* usage = nth ? "replacenth [-i] [-w] <n> <old> <new>" : "replaceall [-i] [-w] <old> <new>";
    requireNotEmpty(s.doc(), "replace");
    SearchOptions opt;
    size_t i = 0;
    takeFlags(rest, i, opt);
    std::string a, b;
    size_t which = 0;
    if (nth) {
        std::string num;
        if (!takeArg(rest, i, num) || !parseNum(num, which) || which == 0)
            throw std::invalid_argument(std::string("Usage: ") + usage + "   (n must be 1 or more)");
    }
    if (!takeArg(rest, i, a)) throw std::invalid_argument(std::string("Usage: ") + usage);
    if (!takeArg(rest, i, b)) throw std::invalid_argument(std::string("Usage: ") + usage + "   (use \"\" for an empty replacement)");
    while (i < rest.size() && isBlank(rest[i])) ++i;
    if (i < rest.size())
        throw std::invalid_argument(std::string("Too many arguments. Usage: ") + usage + "   (put text with spaces in \"quotes\")");
    if (a.empty()) throw std::invalid_argument("The search text is empty.");
    if (nth) {
        const Match m = s.ed.replaceNth(a, b, which, opt);
        std::cout << "Replaced occurrence " << which << " (at " << (m.start.line + 1) << ":" << (m.start.col + 1)
                  << ") with \"" << preview(b) << "\".\n";
        return true;
    }
    const ReplaceResult r = s.ed.replaceAll(a, b, opt);
    if (r.found == 0) std::cout << "\"" << preview(a) << "\" was not found; nothing changed.\n";
    else if (r.changed == 0) std::cout << "Found " << r.found << " occurrence(s), but they already equal the replacement; nothing changed.\n";
    else std::cout << "Replaced " << r.changed << " occurrence(s) of \"" << preview(a) << "\" with \""
                   << preview(b) << "\". One 'undo' reverses all of them.\n";
    return true;
}

static bool cmdReplaceAll(Session& s, std::string& rest) { return replaceCommand(s, rest, false); }
static bool cmdReplaceNth(Session& s, std::string& rest) { return replaceCommand(s, rest, true); }

// ---- history

static bool cmdUndo(Session& s, std::string&) {
    std::string label;
    if (s.ed.undo(&label)) std::cout << "Undid: " << label << ".\n";
    else std::cout << "Nothing to undo.\n";
    return true;
}

static bool cmdRedo(Session& s, std::string&) {
    std::string label;
    if (s.ed.redo(&label)) std::cout << "Redid: " << label << ".\n";
    else std::cout << "Nothing to redo.\n";
    return true;
}

static bool cmdHistory(Session& s, std::string&) {
    const Editor& ed = s.ed;
    std::cout << "Can undo " << ed.undoCount() << " step(s)";
    if (ed.canUndo()) std::cout << " (next: " << ed.nextUndoLabel() << ")";
    std::cout << "; can redo " << ed.redoCount() << " step(s)";
    if (ed.canRedo()) std::cout << " (next: " << ed.nextRedoLabel() << ")";
    std::cout << ". History keeps the latest " << ed.historyLimit() << " steps.\n";
    return true;
}

// ---- code tools

static bool cmdFormat(Session& s, std::string& rest) {
    if (!rest.empty()) throw std::invalid_argument("Usage: format");
    const size_t changed = formatDocument(s.ed);
    if (changed == 0) std::cout << "Already formatted.\n";
    else std::cout << "Formatted: " << changed << " line(s) changed. 'undo' restores the old layout.\n";
    return true;
}

static bool cmdCheck(Session& s, std::string& rest) {
    if (!rest.empty()) throw std::invalid_argument("Usage: check");
    checkCommand(s.doc());
    return true;
}

static bool cmdBrackets(Session& s, std::string&) {
    const BracketReport r = checkBrackets(s.doc());
    if (r.ok()) {
        std::cout << "Brackets OK: " << r.pairs.size() << " pair(s), deepest nesting " << r.maxDepth << ".\n";
        return true;
    }
    std::cout << r.issues.size() << " problem(s) found" << (r.truncated ? " (list cut short)" : "") << ":\n";
    for (size_t k = 0; k < r.issues.size(); ++k) std::cout << "  " << r.issues[k].message << "\n";
    return true;
}

static bool cmdMatch(Session& s, std::string& rest) {
    const Document& doc = s.doc();
    DynArray<std::string> w;
    size_t n[2];
    takeWords(rest, 2, w, rest);
    needNumbers(w, 2, n, "match <line> <col>");
    const Pos at = userPos(doc, n[0], n[1]);
    const BracketReport r = checkBrackets(doc);
    Pos b, o;
    if (matchNearCursor(doc, r, at, b, o))
        std::cout << "'" << doc.getLine(b.line)[b.col] << "' at " << (b.line + 1) << ":" << (b.col + 1)
                  << " pairs with '" << doc.getLine(o.line)[o.col] << "' at " << (o.line + 1) << ":" << (o.col + 1) << ".\n";
    else
        std::cout << "No matched bracket at or just before " << n[0] << ":" << n[1] << ".\n";
    return true;
}

// ---- autocomplete and snippets

static bool cmdComplete(Session& s, std::string& rest) {
    const Document& doc = s.doc();
    DynArray<std::string> w;
    size_t n[2];
    const size_t got = takeWords(rest, 3, w, rest);
    s.ac.refresh(doc);
    std::string pre;
    if (got == 2 && parseNum(w[0], n[0]) && parseNum(w[1], n[1])) pre = prefixBefore(doc, userPos(doc, n[0], n[1]));
    else if (got == 1 && rest.empty()) pre = w[0];
    else throw std::invalid_argument("Usage: complete <prefix>   or   complete <line> <col>");
    const DynArray<std::string> sug = s.ac.suggest(pre, 8);
    if (pre.empty()) throw std::invalid_argument("There is no word before that position to complete.");
    if (!Trie::validWord(pre)) throw std::invalid_argument("'" + pre + "' cannot be completed (too long or has blanks).");
    if (sug.empty()) {
        std::cout << "No suggestions for \"" << pre << "\".\n";
        return true;
    }
    std::cout << "Suggestions for \"" << pre << "\":\n";
    for (size_t k = 0; k < sug.size(); ++k) std::cout << "  " << (k + 1) << ") " << sug[k] << "\n";
    return true;
}

static bool cmdAccept(Session& s, std::string& rest) {
    DynArray<std::string> w;
    size_t n[2];
    takeWords(rest, 2, w, rest);
    needNumbers(w, 2, n, "accept <line> <col> <word>");
    if (rest.empty()) throw std::invalid_argument("Usage: accept <line> <col> <word>");
    const Pos end = applyCompletion(s.ed, userPos(s.doc(), n[0], n[1]), rest);
    s.ac.accepted(rest);
    std::cout << "Completed to \"" << rest << "\"; cursor now at " << (end.line + 1) << ":" << (end.col + 1) << ".\n";
    return true;
}

static bool cmdDictAdd(Session& s, std::string& rest) {
    if (!s.ac.addWord(rest))
        throw std::invalid_argument("'" + preview(rest) + "' is not a valid word (1-" + std::to_string(Trie::kMaxWordLength) + " characters, no blanks).");
    std::cout << "Added \"" << rest << "\" to the dictionary (" << s.ac.dictionarySize() << " words).\n";
    return true;
}

static bool cmdDictDel(Session& s, std::string& rest) {
    if (!s.ac.removeWord(rest)) throw std::invalid_argument("\"" + preview(rest) + "\" is not in the dictionary.");
    std::cout << "Removed \"" << rest << "\" (" << s.ac.dictionarySize() << " words left).\n";
    return true;
}

static bool cmdExpand(Session& s, std::string& rest) {
    DynArray<std::string> w;
    size_t n[2];
    const size_t got = takeWords(rest, 2, w, rest);
    if (got < 2 || !rest.empty()) throw std::invalid_argument("Usage: expand <line> <col>");
    needNumbers(w, 2, n, "expand <line> <col>");
    const SnippetExpansion r = applySnippet(s.ed, s.snippets, userPos(s.doc(), n[0], n[1]));
    if (!r.expanded) std::cout << r.reason << "\n";
    else std::cout << "Expanded \"" << r.trigger << "\"; cursor now at " << (r.cursor.line + 1) << ":" << (r.cursor.col + 1) << ".\n";
    return true;
}

static bool cmdSnippet(Session& s, std::string& rest) { snippetCommand(s.snippets, rest); return true; }

// ---------------------------------------------------------------- the command table

struct ConsoleCommand {
    const char* name;
    Handler run;
};

static const ConsoleCommand kCommands[] = {
    {"help", cmdHelp},         {"?", cmdHelp},
    {"quit", cmdQuit},         {"exit", cmdQuit},
    {"new", cmdNew},           {"open", cmdOpen},           {"save", cmdSave},         {"saveas", cmdSaveAs},
    {"info", cmdInfo},         {"show", cmdShow},
    {"append", cmdAppend},     {"addline", cmdAddLine},     {"ins", cmdIns},           {"nl", cmdNewLine},
    {"del", cmdDel},           {"delword", cmdDelWord},     {"delrange", cmdDelRange}, {"dellines", cmdDelLines},
    {"replace", cmdReplaceRange},
    {"find", cmdFind},         {"replaceall", cmdReplaceAll}, {"replacenth", cmdReplaceNth},
    {"undo", cmdUndo},         {"redo", cmdRedo},           {"history", cmdHistory},
    {"format", cmdFormat},     {"check", cmdCheck},         {"problems", cmdCheck},
    {"brackets", cmdBrackets}, {"match", cmdMatch},
    {"complete", cmdComplete}, {"accept", cmdAccept},       {"dictadd", cmdDictAdd},   {"dictdel", cmdDictDel},
    {"expand", cmdExpand},     {"snippet", cmdSnippet},     {"snippets", cmdSnippet},
};

// Runs one input line. Returns false when the program should end.
static bool execute(Session& s, const std::string& input) {
    DynArray<std::string> w;
    std::string rest;
    if (takeWords(input, 1, w, rest) == 0) return true;      // empty line
    const std::string cmd = toLower(w[0]);
    for (const ConsoleCommand& c : kCommands)
        if (cmd == c.name) return c.run(s, rest);
    std::cout << "Unknown command '" << cmd << "'. Type 'help' for the list.\n";
    return true;
}

int main() {
    Session s;
    s.snippets.loadDefaults();
    std::cout << "Text Editor (console)  -  type 'help' for commands.\n";
    std::string input;
    while (true) {
        std::cout << (s.doc().modified() ? "*" : "") << "> " << std::flush;
        if (!std::getline(std::cin, input)) break;  // end of input
        try {
            if (!execute(s, input)) break;
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "Bye.\n";
    return 0;
}

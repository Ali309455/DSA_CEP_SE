// console_main.cpp - a command-line front end for the engine (phase 1).
// Lines and columns are 1-based here (as a user would count them).

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

#include "DynArray.h"
#include "Document.h"
#include "FileIO.h"

static void printHelp() {
    std::cout <<
        "Commands (line and column numbers start at 1):\n"
        "  File:\n"
        "    new                         start an empty document\n"
        "    open <file>                 open an existing text file\n"
        "    save                        save to the current file\n"
        "    saveas <file>               save under a new name\n"
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
        "  help                          show this list\n"
        "  (search, replace and undo arrive in the next phases)\n";
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

static void requireNotEmpty(const Document& doc) {
    if (doc.isEmpty()) throw std::logic_error("The document is empty, there is nothing to delete.");
}

// Returns false when the user wants to quit.
static bool execute(Document& doc, const std::string& input) {
    DynArray<std::string> w;
    std::string rest;
    if (takeWords(input, 1, w, rest) == 0) return true;
    std::string cmd = toLower(w[0]);
    size_t n[4];

    if (cmd == "help" || cmd == "?") {
        printHelp();
    } else if (cmd == "quit" || cmd == "exit") {
        if (okToDiscard(doc)) return false;
    } else if (cmd == "new") {
        if (okToDiscard(doc)) { doc.clear(); std::cout << "New empty document.\n"; }
    } else if (cmd == "open") {
        if (rest.empty()) throw std::invalid_argument("Usage: open <file>");
        if (!okToDiscard(doc)) return true;
        IOResult r = openFile(doc, rest);
        std::cout << r.message;
        if (r.ok) std::cout << " (" << doc.lineCount() << " lines, " << doc.charCount() << " characters)";
        std::cout << "\n";
    } else if (cmd == "save") {
        if (doc.path().empty()) throw std::logic_error("This document has no filename yet. Use: saveas <file>");
        std::cout << saveFile(doc, doc.path()).message << "\n";
    } else if (cmd == "saveas") {
        if (rest.empty()) throw std::invalid_argument("Usage: saveas <file>");
        std::cout << saveFile(doc, rest).message << "\n";
    } else if (cmd == "info") {
        info(doc);
    } else if (cmd == "show") {
        size_t got = takeWords(rest, 2, w, rest);
        size_t from = 1, to = doc.lineCount();
        if (got >= 1 && !parseNum(w[0], from)) throw std::invalid_argument("Usage: show [from [to]]");
        if (got >= 2 && !parseNum(w[1], to)) throw std::invalid_argument("Usage: show [from [to]]");
        if (got == 1) to = from;
        if (from < 1 || to > doc.lineCount() || from > to)
            throw std::out_of_range("Line range " + std::to_string(from) + "-" + std::to_string(to) +
                                    " is not valid (the document has " + std::to_string(doc.lineCount()) + " line(s)).");
        show(doc, from - 1, to - 1);
    } else if (cmd == "append") {
        if (doc.isEmpty()) doc.insertText(Pos{0, 0}, rest);
        else doc.insertLineBefore(doc.lineCount(), rest);
        std::cout << "Added at line " << doc.lineCount() << ".\n";
    } else if (cmd == "addline") {
        takeWords(rest, 1, w, rest);
        needNumbers(w, 1, n, "addline <line> <text>");
        if (n[0] < 1 || n[0] > doc.lineCount() + 1)
            throw std::out_of_range("Line " + std::to_string(n[0]) + " is not valid (use 1 to " +
                                    std::to_string(doc.lineCount() + 1) + ").");
        doc.insertLineBefore(n[0] - 1, rest);
        std::cout << "Inserted new line " << n[0] << ".\n";
    } else if (cmd == "ins") {
        takeWords(rest, 2, w, rest);
        needNumbers(w, 2, n, "ins <line> <col> <text>");
        if (rest.empty()) throw std::invalid_argument("Nothing to insert. Usage: ins <line> <col> <text>");
        Pos p = userPos(doc, n[0], n[1]);
        doc.insertText(p, rest);
        std::cout << "Inserted " << rest.size() << " character(s) at " << n[0] << ":" << n[1] << ".\n";
    } else if (cmd == "nl") {
        takeWords(rest, 2, w, rest);
        needNumbers(w, 2, n, "nl <line> <col>");
        doc.insertNewLine(userPos(doc, n[0], n[1]));
        std::cout << "Line split at " << n[0] << ":" << n[1] << ".\n";
    } else if (cmd == "del") {
        requireNotEmpty(doc);
        size_t got = takeWords(rest, 3, w, rest);
        needNumbers(w, 2, n, "del <line> <col> [count]");
        size_t count = 1;
        if (got >= 3 && (!parseNum(w[2], count) || count == 0))
            throw std::invalid_argument("Usage: del <line> <col> [count]   (count must be 1 or more)");
        std::string gone = doc.deleteChars(userPos(doc, n[0], n[1]), count);
        std::cout << "Deleted \"" << preview(gone) << "\".\n";
    } else if (cmd == "delword") {
        requireNotEmpty(doc);
        takeWords(rest, 2, w, rest);
        needNumbers(w, 2, n, "delword <line> <col>");
        std::string gone = doc.deleteWord(userPos(doc, n[0], n[1]));
        std::cout << "Deleted word \"" << preview(gone) << "\".\n";
    } else if (cmd == "delrange") {
        requireNotEmpty(doc);
        takeWords(rest, 4, w, rest);
        needNumbers(w, 4, n, "delrange <l1> <c1> <l2> <c2>");
        std::string gone = doc.deleteRange(userPos(doc, n[0], n[1]), userPos(doc, n[2], n[3]));
        std::cout << "Deleted " << gone.size() << " character(s): \"" << preview(gone) << "\".\n";
    } else if (cmd == "dellines") {
        requireNotEmpty(doc);
        size_t got = takeWords(rest, 2, w, rest);
        needNumbers(w, 1, n, "dellines <from> [to]");
        size_t from = n[0], to = from;
        if (got >= 2 && !parseNum(w[1], to)) throw std::invalid_argument("Usage: dellines <from> [to]");
        if (from < 1 || to < from || to > doc.lineCount())
            throw std::out_of_range("Line range " + std::to_string(from) + "-" + std::to_string(to) +
                                    " is not valid (the document has " + std::to_string(doc.lineCount()) + " line(s)).");
        doc.deleteLines(from - 1, to - 1);
        std::cout << "Deleted line(s) " << from << (to > from ? "-" + std::to_string(to) : std::string()) << ".\n";
    } else {
        std::cout << "Unknown command '" << cmd << "'. Type 'help' for the list.\n";
    }
    return true;
}

int main() {
    Document doc;
    std::cout << "Text Editor (console)  -  type 'help' for commands.\n";
    std::string input;
    while (true) {
        std::cout << (doc.modified() ? "*" : "") << "> " << std::flush;
        if (!std::getline(std::cin, input)) break;  // end of input
        try {
            if (!execute(doc, input)) break;
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "Bye.\n";
    return 0;
}

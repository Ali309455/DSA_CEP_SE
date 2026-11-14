// Snippets.cpp - see Snippets.h.
#include "Snippets.h"
#include <fstream>
#include <cstdio>
#include <stdexcept>

static bool isTriggerChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool SnippetTable::validTrigger(const std::string& t) {
    if (t.empty() || t.size() > kMaxTrigger) return false;
    if (t[0] >= '0' && t[0] <= '9') return false;
    for (size_t i = 0; i < t.size(); ++i)
        if (!isTriggerChar(t[i])) return false;
    return true;
}

std::string SnippetTable::bodyProblem(const std::string& body) {
    if (body.empty()) return "The snippet body is empty.";
    if (body.size() > kMaxBody) return "The snippet body is too long (limit " + std::to_string(kMaxBody) + " characters).";
    size_t cursors = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '\r') return "The snippet body must not contain a carriage return (\\r).";
        if (c == '\0') return "The snippet body must not contain a NUL character.";
        if (c == '$' && i + 1 < body.size()) {
            if (body[i + 1] == '$') ++i;                       // "$$" is a literal dollar
            else if (body[i + 1] == '0') { ++cursors; ++i; }
        }
    }
    if (cursors > 1) return "The snippet body has more than one $0 (use it once, or write $$0 for a literal \"$0\").";
    return "";
}

bool SnippetTable::add(const std::string& trigger, const std::string& body) {
    if (!validTrigger(trigger))
        throw std::invalid_argument("Invalid trigger '" + trigger + "': use 1-" + std::to_string(kMaxTrigger) +
                                    " letters, digits or '_', not starting with a digit.");
    std::string problem = bodyProblem(body);
    if (!problem.empty()) throw std::invalid_argument(problem);
    if (!table_.contains(trigger) && table_.size() >= kMaxSnippets)
        throw std::length_error("Too many snippets (limit " + std::to_string(kMaxSnippets) + ").");
    return table_.put(trigger, Snippet{trigger, body});
}

bool SnippetTable::remove(const std::string& trigger) { return table_.erase(trigger); }

const Snippet* SnippetTable::find(const std::string& trigger) const { return table_.get(trigger); }

// Merge sort for strings (stable, O(n log n)); the table has no order of its own.
static void mergeSort(DynArray<std::string>& a, DynArray<std::string>& tmp, size_t lo, size_t hi) {
    if (hi - lo < 2) return;
    size_t mid = lo + (hi - lo) / 2;
    mergeSort(a, tmp, lo, mid);
    mergeSort(a, tmp, mid, hi);
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) tmp[k++] = (a[j] < a[i]) ? a[j++] : a[i++];
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi) tmp[k++] = a[j++];
    for (size_t t = lo; t < hi; ++t) a[t] = tmp[t];
}

DynArray<std::string> SnippetTable::triggers() const {
    DynArray<std::string> out;
    table_.forEach([&out](const std::string& k, const Snippet&) { out.push_back(k); });
    DynArray<std::string> tmp;
    tmp.resize(out.size(), std::string());
    mergeSort(out, tmp, 0, out.size());
    return out;
}

void SnippetTable::loadDefaults() {
    add("for",    "for (int i = 0; i < $0; ++i) {\n    \n}");
    add("forr",   "for (auto& x : $0) {\n    \n}");
    add("while",  "while ($0) {\n    \n}");
    add("if",     "if ($0) {\n    \n}");
    add("ifelse", "if ($0) {\n    \n} else {\n    \n}");
    add("else",   "else {\n    $0\n}");
    add("switch", "switch ($0) {\n    case 0:\n        break;\n    default:\n        break;\n}");
    add("do",     "do {\n    $0\n} while ();");
    add("try",    "try {\n    $0\n} catch (const std::exception& e) {\n    \n}");
    add("main",   "int main() {\n    $0\n    return 0;\n}");
    add("class",  "class $0 {\npublic:\n    \nprivate:\n    \n};");
    add("struct", "struct $0 {\n    \n};");
    add("fn",     "void $0() {\n    \n}");
    add("inc",    "#include <$0>");
    add("incl",   "#include \"$0\"");
    add("guard",  "#pragma once\n$0");
    add("cout",   "std::cout << $0 << \"\\n\";");
    add("cin",    "std::cin >> $0;");
    add("vec",    "std::vector<$0> ");
    add("ns",     "namespace $0 {\n\n}");
    add("tmpl",   "template <typename T>\n$0");
}

// ---------------------------------------------------------------- expansion

ExpandedSnippet expandBody(const std::string& body, const std::string& indent) {
    std::string problem = SnippetTable::bodyProblem(body);
    if (!problem.empty()) throw std::invalid_argument(problem);
    ExpandedSnippet r;
    r.cursorLine = 0;
    r.cursorCol = 0;
    r.hasCursor = false;
    size_t line = 0, col = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '$' && i + 1 < body.size() && body[i + 1] == '$') { r.text.push_back('$'); ++col; ++i; continue; }
        if (c == '$' && i + 1 < body.size() && body[i + 1] == '0') {
            r.hasCursor = true;
            r.cursorLine = line;
            r.cursorCol = col;
            ++i;
            continue;
        }
        r.text.push_back(c);
        if (c == '\n') {
            ++line;
            // Blank lines stay blank (no trailing spaces); every other line is indented.
            const bool blank = (i + 1 >= body.size()) || body[i + 1] == '\n';
            if (!blank) r.text += indent;
            col = blank ? 0 : indent.size();
        } else {
            ++col;
        }
    }
    if (!r.hasCursor) { r.cursorLine = line; r.cursorCol = col; }
    return r;
}

// ---------------------------------------------------------------- files

static std::string escapeBody(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') o += "\\n";
        else if (s[i] == '\t') o += "\\t";
        else if (s[i] == '\\') o += "\\\\";
        else o.push_back(s[i]);
    }
    return o;
}

static bool unescapeBody(const std::string& s, std::string& out) {
    out.clear();
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out.push_back(s[i]); continue; }
        if (i + 1 >= s.size()) return false;
        char e = s[++i];
        if (e == 'n') out.push_back('\n');
        else if (e == 't') out.push_back('\t');
        else if (e == '\\') out.push_back('\\');
        else return false;
    }
    return true;
}

IOResult SnippetTable::saveToFile(const std::string& path) const {
    if (path.empty()) return IOResult{false, "No filename given."};
    std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) return IOResult{false, "Cannot write to '" + path + "': check the folder and permissions."};
    out << "# snippets: trigger<TAB>body  (\\n = new line, \\t = tab, \\\\ = backslash)\n";
    DynArray<std::string> keys = triggers();
    for (size_t i = 0; i < keys.size(); ++i) {
        const Snippet* s = find(keys[i]);
        out << s->trigger << '\t' << escapeBody(s->body) << '\n';
    }
    out.flush();
    if (!out) return IOResult{false, "Error while writing '" + path + "' (disk full?)."};
    return IOResult{true, "Saved " + std::to_string(keys.size()) + " snippet(s) to '" + path + "'."};
}

IOResult SnippetTable::loadFromFile(const std::string& path) {
    if (path.empty()) return IOResult{false, "No filename given."};
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in) return IOResult{false, "Cannot open '" + path + "': file does not exist or is not readable."};

    // Read the whole file first (size-limited), so a folder, a huge file or a read
    // error is reported before the table is touched.
    std::string data;
    char buf[4096];
    while (in.read(buf, sizeof buf) || in.gcount() > 0) {
        data.append(buf, static_cast<size_t>(in.gcount()));
        if (data.size() > kMaxFileBytes)
            return IOResult{false, "'" + path + "' is too large for a snippet file (limit " +
                                       std::to_string(kMaxFileBytes) + " bytes)."};
    }
    if (in.bad() || (!in.eof() && in.fail()))
        return IOResult{false, "Error while reading '" + path + "' (is it a folder?)."};

    size_t pos = 0;
    if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;          // UTF-8 byte-order mark (Notepad)
    size_t loaded = 0, skipped = 0, firstBad = 0, lineNo = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) nl = data.size();
        std::string line = data.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        std::string body;
        bool good = tab != std::string::npos && unescapeBody(line.substr(tab + 1), body);
        if (good) {
            try {
                add(line.substr(0, tab), body);
            } catch (const std::exception&) {
                good = false;                      // bad trigger / body, or the table is full
            }
        }
        if (good) {
            ++loaded;
        } else {
            if (skipped == 0) firstBad = lineNo;
            ++skipped;
        }
    }
    std::string msg = "Loaded " + std::to_string(loaded) + " snippet(s) from '" + path + "'.";
    if (skipped)
        msg += " Skipped " + std::to_string(skipped) + " invalid line(s) (first: line " + std::to_string(firstBad) + ").";
    return IOResult{true, msg};
}

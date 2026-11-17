// test_main.cpp - console test suite for the engine (no GUI needed).
// Build target: engine_tests.   Exit code 0 = all passed.

#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <map>
#include <vector>
#include "Assist.h"
#include "Brackets.h"
#include "Trie.h"
#include "Snippets.h"
#include "Diagnostics.h"
#include "DynArray.h"
#include "Document.h"
#include "EditOps.h"
#include "Editor.h"
#include "FileIO.h"
#include "Stack.h"
#include "Workspace.h"

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failed;                                                              \
            std::cout << "  FAIL  " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                            \
    } while (0)

#define CHECK_THROWS(expr)                      \
    do {                                        \
        ++g_checks;                             \
        bool threw = false;                     \
        try { expr; } catch (const std::exception&) { threw = true; } \
        if (!threw) {                           \
            ++g_failed;                         \
            std::cout << "  FAIL  " << __FILE__ << ":" << __LINE__ << "  expected exception: " #expr "\n"; \
        }                                       \
    } while (0)

// Whole document as text without the trailing newline: lines joined by '\n'.
static std::string text(const Document& d) {
    std::string s;
    for (size_t i = 0; i < d.lineCount(); ++i) {
        if (i) s.push_back('\n');
        s += d.getLine(i);
    }
    return s;
}

static void section(const char* name) { std::cout << "[" << name << "]\n"; }

// ------------------------------------------------------------------ DynArray / Stack

static void testDynArray() {
    section("DynArray");
    DynArray<int> a;
    CHECK(a.empty());
    for (int i = 0; i < 1000; ++i) a.push_back(i);
    CHECK(a.size() == 1000);
    CHECK(a[0] == 0 && a[999] == 999);
    CHECK(a.capacity() >= 1000);

    a.insert(0, -1);
    CHECK(a.size() == 1001 && a[0] == -1 && a[1] == 0);
    a.insert(a.size(), 5000);
    CHECK(a.back() == 5000);
    a.erase(0);
    CHECK(a[0] == 0);
    a.erase(10, 20);
    CHECK(a.size() == 991 && a[10] == 20);

    DynArray<int> b = a;                 // copy is independent
    b[0] = 42;
    CHECK(a[0] == 0 && b[0] == 42);
    DynArray<int> c = std::move(b);      // move steals
    CHECK(c[0] == 42 && b.size() == 0);

    a.truncate(5);
    CHECK(a.size() == 5);
    a.append(a);                         // self-append
    CHECK(a.size() == 10 && a[5] == a[0]);

    CHECK_THROWS(a.at(100));
    CHECK_THROWS(a.insert(100, 1));
    CHECK_THROWS(a.erase(5, 3));
    DynArray<int> empty;
    CHECK_THROWS(empty.pop_back());
    CHECK_THROWS(empty.back());

    DynArray<std::string> s;             // works with non-trivial types too
    s.push_back("hello");
    s.push_back(s[0]);                   // push an element of itself
    CHECK(s.size() == 2 && s[1] == "hello");
}

static void testStack() {
    section("Stack");
    Stack<int> s;
    CHECK(s.empty());
    for (int i = 0; i < 100; ++i) s.push(i);
    CHECK(s.size() == 100 && s.top() == 99);
    for (int i = 99; i >= 0; --i) CHECK(s.pop() == i);   // last in, first out
    CHECK(s.empty());
    CHECK_THROWS(s.pop());
    CHECK_THROWS(s.top());
    s.push(1);
    s.clear();
    CHECK(s.empty());
}

// ------------------------------------------------------------------ Document

static void testDocBasics() {
    section("Document: basics");
    Document d;
    CHECK(d.lineCount() == 1 && d.charCount() == 0 && d.isEmpty());
    CHECK(!d.modified());

    Pos end = d.insertText(Pos{0, 0}, "hello world");
    CHECK(end == (Pos{0, 11}));
    CHECK(text(d) == "hello world" && d.charCount() == 11 && d.modified());

    d.insertText(Pos{0, 5}, ",");
    CHECK(text(d) == "hello, world");
    d.insertText(Pos{0, 0}, ">> ");
    CHECK(text(d) == ">> hello, world");
    d.insertText(Pos{0, d.lineLength(0)}, "!");
    CHECK(text(d) == ">> hello, world!");

    // new line in the middle
    d.insertNewLine(Pos{0, 8});
    CHECK(d.lineCount() == 2 && d.getLine(0) == ">> hello" && d.getLine(1) == ", world!");
    CHECK(d.charCount() == 16);

    // multi-line insert
    Pos e = d.insertText(Pos{1, 0}, "a\nb\nc");
    CHECK(e == (Pos{3, 1}));
    CHECK(text(d) == ">> hello\na\nb\nc, world!");
    CHECK(d.lineCount() == 4);

    // CR is ignored
    Document d2;
    d2.insertText(Pos{0, 0}, "x\r\ny");
    CHECK(text(d2) == "x\ny");

    // insert empty string changes nothing
    Document d3;
    d3.insertText(Pos{0, 0}, "");
    CHECK(!d3.modified() && d3.isEmpty());

    CHECK(d.checkInvariants() && d2.checkInvariants());
}

static void testDocDelete() {
    section("Document: delete");
    Document d;
    d.insertText(Pos{0, 0}, "one two three\nsecond line\nthird line\nfourth");

    // same line
    CHECK(d.deleteRange(Pos{0, 3}, Pos{0, 7}) == " two");
    CHECK(d.getLine(0) == "one three");
    // reversed arguments are fine
    CHECK(d.deleteRange(Pos{0, 7}, Pos{0, 3}) == " thr");  // removes cols 3..6
    d.clear();
    d.insertText(Pos{0, 0}, "one two three\nsecond line\nthird line\nfourth");

    // across lines: from middle of line 0 to middle of line 2
    std::string gone = d.deleteRange(Pos{0, 4}, Pos{2, 6});
    CHECK(gone == "two three\nsecond line\nthird ");
    CHECK(text(d) == "one line\nfourth");
    CHECK(d.lineCount() == 2 && d.checkInvariants());

    // empty range
    CHECK(d.deleteRange(Pos{0, 2}, Pos{0, 2}).empty());

    // join two lines by deleting the break
    CHECK(d.deleteRange(Pos{0, 8}, Pos{1, 0}) == "\n");
    CHECK(text(d) == "one linefourth" && d.lineCount() == 1);

    // characters
    CHECK(d.deleteChars(Pos{0, 0}, 4) == "one ");
    CHECK(d.getLine(0) == "linefourth");
    CHECK(d.deleteChars(Pos{0, 6}, 100) == "urth");    // clamped to end of line
    CHECK(d.getLine(0) == "linefo");
    CHECK_THROWS(d.deleteChars(Pos{0, 6}, 1));           // nothing at end of line
    CHECK_THROWS(d.deleteChars(Pos{0, 0}, 0));           // count 0
    CHECK(d.checkInvariants());

    // words
    d.clear();
    d.insertText(Pos{0, 0}, "foo_bar baz, qux");
    CHECK(d.deleteWord(Pos{0, 2}) == "foo_bar");          // click in the middle of a word
    CHECK(d.getLine(0) == " baz, qux");
    CHECK_THROWS(d.deleteWord(Pos{0, 0}));                // a space is not a word
    CHECK_THROWS(d.deleteWord(Pos{0, 5}));                // a comma is not a word
    CHECK(d.deleteWord(Pos{0, 8}) == "qux");
    CHECK(d.deleteWord(Pos{0, 1}) == "baz");
    CHECK(d.getLine(0) == " , ");
}

static void testDocLines() {
    section("Document: whole lines");
    Document d;
    d.loadFromString("l1\nl2\nl3\nl4\nl5\n");
    CHECK(d.lineCount() == 5);

    CHECK(d.deleteLines(1, 1) == "l2\n");                 // middle
    CHECK(text(d) == "l1\nl3\nl4\nl5");
    CHECK(d.deleteLines(0, 0) == "l1\n");                 // first
    CHECK(text(d) == "l3\nl4\nl5");
    CHECK(d.deleteLines(2, 2) == "\nl5");                 // last (takes the break before it)
    CHECK(text(d) == "l3\nl4" && d.lineCount() == 2);
    CHECK(d.deleteLines(0, 1) == "l3\nl4");               // everything
    CHECK(d.lineCount() == 1 && d.isEmpty());
    CHECK(d.checkInvariants());

    d.loadFromString("a\nb\nc\nd\n");
    d.deleteLines(1, 2);                                  // a range
    CHECK(text(d) == "a\nd");

    CHECK_THROWS(d.deleteLines(0, 5));
    CHECK_THROWS(d.deleteLines(2, 1));
    CHECK_THROWS(d.deleteLines(9, 9));

    // insert whole lines
    d.insertLineBefore(0, "first");
    CHECK(text(d) == "first\na\nd");
    d.insertLineBefore(2, "between");
    CHECK(text(d) == "first\na\nbetween\nd");
    d.insertLineBefore(d.lineCount(), "last");            // append
    CHECK(text(d) == "first\na\nbetween\nd\nlast");
    CHECK_THROWS(d.insertLineBefore(99, "x"));
    CHECK(d.checkInvariants());
}

static void testDocErrors() {
    section("Document: invalid positions");
    Document d;
    d.insertText(Pos{0, 0}, "abc");
    CHECK_THROWS(d.insertText(Pos{1, 0}, "x"));           // no such line
    CHECK_THROWS(d.insertText(Pos{0, 4}, "x"));           // column past end
    CHECK_THROWS(d.deleteRange(Pos{0, 0}, Pos{5, 0}));
    CHECK_THROWS(d.getLine(3));
    CHECK_THROWS(d.getRange(Pos{0, 0}, Pos{0, 9}));
    CHECK(text(d) == "abc");                              // nothing was changed by failed calls
    CHECK(d.checkInvariants());

    Document empty;
    CHECK_THROWS(empty.deleteChars(Pos{0, 0}, 1));        // empty document
    CHECK_THROWS(empty.deleteWord(Pos{0, 0}));
    CHECK(empty.deleteRange(Pos{0, 0}, Pos{0, 0}).empty());
}

static void testLoadSave() {
    section("Document: load/save text");
    Document d;
    const char* samples[] = {"", "a", "a\n", "a\nb", "a\nb\n", "\n", "\n\n", "a\n\n", "\na", "x\r\ny\r\n"};
    // Note: "" and "\n" both load as ONE empty line, which saves as an empty file.
    const char* expect[]  = {"", "a\n", "a\n", "a\nb\n", "a\nb\n", "", "\n\n", "a\n\n", "\na\n", "x\ny\n"};
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        d.loadFromString(samples[i]);
        CHECK(d.checkInvariants());
        CHECK(d.toString() == expect[i]);
        // saving and loading again must give the same document
        Document again;
        again.loadFromString(d.toString());
        CHECK(again.toString() == d.toString());
        CHECK(again.lineCount() == d.lineCount());
    }
    d.loadFromString("one\ntwo\nthree\n");
    CHECK(d.lineCount() == 3 && d.charCount() == 11 && !d.modified());
}

static void testFiles() {
    section("File operations");
    const std::string path = "engine_test_tmp.txt";
    Document d;
    d.insertText(Pos{0, 0}, "first line\nsecond line\n\nlast");
    IOResult r = saveFile(d, path);
    CHECK(r.ok && d.path() == path && !d.modified());

    Document loaded;
    r = openFile(loaded, path);
    CHECK(r.ok && loaded.lineCount() == 4 && loaded.getLine(1) == "second line" && loaded.getLine(3) == "last");
    CHECK(loaded.path() == path && !loaded.modified());

    loaded.insertText(Pos{0, 0}, "x");
    CHECK(loaded.modified());
    r = saveFile(loaded, path);                           // "save" = save to same path
    CHECK(r.ok && !loaded.modified());

    // errors never throw and never damage the document
    Document keep;
    keep.insertText(Pos{0, 0}, "keep me");
    r = openFile(keep, "this_file_does_not_exist_12345.txt");
    CHECK(!r.ok && keep.getLine(0) == "keep me");
    r = openFile(keep, "");
    CHECK(!r.ok);
    r = saveFile(keep, "");
    CHECK(!r.ok);
    r = saveFile(keep, "no_such_folder_12345/x.txt");
    CHECK(!r.ok && keep.getLine(0) == "keep me");

    // Windows line endings survive open + save (the text itself never holds '\r')
    {
        const std::string crlfPath = "engine_test_crlf_tmp.txt";
        { std::ofstream o(crlfPath, std::ios::binary); o << "one\r\ntwo\r\n\r\nfour"; }
        Document w;
        CHECK(openFile(w, crlfPath).ok && w.crlf());
        CHECK(w.lineCount() == 4 && w.getLine(0) == "one" && w.getLine(2).empty());
        CHECK(saveFile(w, crlfPath).ok);
        std::ifstream i(crlfPath, std::ios::binary);
        std::string back((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
        CHECK(back == "one\r\ntwo\r\n\r\nfour\r\n");
        i.close();
        { std::ofstream o(crlfPath, std::ios::binary); o << "a\nb\n"; }
        CHECK(openFile(w, crlfPath).ok && !w.crlf());       // LF file stays LF
        w.setCrlf(true);
        w.clear();
        CHECK(!w.crlf());                                   // a cleared document is LF again
        std::remove(crlfPath.c_str());
    }
    std::remove(path.c_str());
}

// Random edits compared against a plain std::string model.
static Pos offsetToPos(const std::string& s, size_t off) {
    Pos p{0, 0};
    for (size_t i = 0; i < off; ++i) {
        if (s[i] == '\n') { ++p.line; p.col = 0; } else ++p.col;
    }
    return p;
}

static void testRandom() {
    section("Document: random stress vs. model");
    std::srand(12345);
    Document d;
    std::string model;
    const char alphabet[] = "abc \n";
    for (int step = 0; step < 4000; ++step) {
        bool doInsert = model.empty() || (std::rand() % 100) < 55;
        if (doInsert) {
            size_t off = static_cast<size_t>(std::rand()) % (model.size() + 1);
            std::string ins;
            size_t len = 1 + static_cast<size_t>(std::rand()) % 6;
            for (size_t i = 0; i < len; ++i) ins.push_back(alphabet[std::rand() % 5]);
            d.insertText(offsetToPos(model, off), ins);
            model.insert(off, ins);
        } else {
            size_t a = static_cast<size_t>(std::rand()) % (model.size() + 1);
            size_t b = static_cast<size_t>(std::rand()) % (model.size() + 1);
            if (a > b) { size_t t = a; a = b; b = t; }
            std::string gone = d.deleteRange(offsetToPos(model, a), offsetToPos(model, b));
            CHECK(gone == model.substr(a, b - a));
            model.erase(a, b - a);
        }
        if (text(d) != model || !d.checkInvariants()) {
            CHECK(text(d) == model);
            CHECK(d.checkInvariants());
            std::cout << "  stopped at step " << step << "\n";
            return;
        }
    }
    CHECK(text(d) == model);
    CHECK(d.checkInvariants());
}

static void testLarge() {
    section("Document: large file");
    std::string big;
    const size_t N = 100000;
    for (size_t i = 0; i < N; ++i) big += "line number " + std::to_string(i) + "\n";
    Document d;
    d.loadFromString(big);
    CHECK(d.lineCount() == N);
    CHECK(d.getLine(0) == "line number 0");
    CHECK(d.getLine(N - 1) == "line number 99999");
    CHECK(d.getLine(50000) == "line number 50000");
    // sequential access walks one step at a time thanks to the cursor cache
    size_t sum = 0;
    for (size_t i = 0; i < N; ++i) sum += d.lineLength(i);
    CHECK(sum == d.charCount());
    d.deleteLines(10, N - 11);                            // remove 99980 lines in one call
    CHECK(d.lineCount() == 20 && d.checkInvariants());
}


// ------------------------------------------------------------------ Undo / Redo

static void testUndoBasics() {
    section("Undo/Redo: basics");
    Editor e;
    CHECK(!e.canUndo() && !e.canRedo());
    CHECK(!e.undo() && !e.redo());                        // nothing to do: returns false, no crash

    e.insertText(Pos{0, 0}, "hello world");
    CHECK(e.undoCount() == 1 && e.nextUndoLabel() == "insert");
    e.insertText(Pos{0, 5}, ",");
    CHECK(text(e.doc()) == "hello, world");

    std::string label;
    CHECK(e.undo(&label) && label == "insert");
    CHECK(text(e.doc()) == "hello world");
    CHECK(e.canRedo() && e.redoCount() == 1);
    CHECK(e.redo(&label));
    CHECK(text(e.doc()) == "hello, world" && !e.canRedo());

    // a new edit after an undo clears the redo stack
    e.undo();
    CHECK(e.canRedo());
    e.insertText(Pos{0, 0}, ">");
    CHECK(!e.canRedo() && !e.redo());
    CHECK(text(e.doc()) == ">hello world");

    // undo everything
    while (e.undo()) {}
    CHECK(text(e.doc()) == "" && e.doc().isEmpty());
    CHECK(e.doc().checkInvariants());
}

static void testUndoEachKind() {
    section("Undo/Redo: every kind of edit");
    Editor e;
    e.insertText(Pos{0, 0}, "one two three\nsecond line\nthird line\nfourth");
    const std::string base = text(e.doc());

    // delete range across lines
    e.deleteRange(Pos{0, 4}, Pos{2, 6});
    CHECK(text(e.doc()) == "one line\nfourth");
    e.undo();
    CHECK(text(e.doc()) == base);
    e.redo();
    CHECK(text(e.doc()) == "one line\nfourth");
    e.undo();

    // delete chars, word, lines
    e.deleteChars(Pos{0, 0}, 4);
    CHECK(e.nextUndoLabel() == "delete characters");
    e.undo();
    CHECK(text(e.doc()) == base);
    e.deleteWord(Pos{0, 5});
    CHECK(e.nextUndoLabel() == "delete word" && e.doc().getLine(0) == "one  three");
    e.undo();
    CHECK(text(e.doc()) == base);
    e.deleteLines(1, 2);
    CHECK(text(e.doc()) == "one two three\nfourth");
    e.undo();
    CHECK(text(e.doc()) == base);
    e.deleteLines(2, 3);                                   // last lines: takes the break before
    CHECK(text(e.doc()) == "one two three\nsecond line");
    e.undo();
    CHECK(text(e.doc()) == base);
    e.deleteLines(0, 3);                                   // everything
    CHECK(e.doc().isEmpty());
    e.undo();
    CHECK(text(e.doc()) == base);

    // new line, add line
    e.insertNewLine(Pos{0, 3});
    CHECK(e.nextUndoLabel() == "new line" && e.doc().getLine(0) == "one");
    e.undo();
    CHECK(text(e.doc()) == base);
    e.insertLineBefore(1, "inserted");
    CHECK(e.nextUndoLabel() == "add line" && e.doc().getLine(1) == "inserted");
    e.undo();
    CHECK(text(e.doc()) == base);
    e.insertLineBefore(4, "appended");
    CHECK(e.doc().getLine(4) == "appended");
    e.undo();
    CHECK(text(e.doc()) == base);

    CHECK(e.doc().checkInvariants());
}

static void testReplaceAndGroups() {
    section("Undo/Redo: replace and groups");
    Editor e;
    e.insertText(Pos{0, 0}, "cat sat on the cat mat");
    const std::string base = text(e.doc());

    // replace = ONE undo step
    size_t before = e.undoCount();
    Pos end = e.replaceRange(Pos{0, 0}, Pos{0, 3}, "tiger");
    CHECK(end == (Pos{0, 5}));
    CHECK(text(e.doc()) == "tiger sat on the cat mat");
    CHECK(e.undoCount() == before + 1 && e.nextUndoLabel() == "replace");
    e.undo();
    CHECK(text(e.doc()) == base);
    e.redo();
    CHECK(text(e.doc()) == "tiger sat on the cat mat");
    e.undo();

    // replace with nothing (= delete) and with a multi-line text
    e.replaceRange(Pos{0, 0}, Pos{0, 4}, "");
    CHECK(text(e.doc()) == "sat on the cat mat");
    e.undo();
    e.replaceRange(Pos{0, 4}, Pos{0, 7}, "a\nb");
    CHECK(text(e.doc()) == "cat a\nb on the cat mat" && e.doc().lineCount() == 2);
    e.undo();
    CHECK(text(e.doc()) == base);

    // "replace all" style: many edits, right to left, in ONE group
    before = e.undoCount();
    {
        EditGroup g(e, "replace all");
        e.replaceRange(Pos{0, 15}, Pos{0, 18}, "dog");   // second "cat" first (right to left)
        e.replaceRange(Pos{0, 0}, Pos{0, 3}, "dog");
    }
    CHECK(text(e.doc()) == "dog sat on the dog mat");
    CHECK(e.undoCount() == before + 1 && e.nextUndoLabel() == "replace all");
    e.undo();                                              // ONE undo reverts both
    CHECK(text(e.doc()) == base);
    e.redo();
    CHECK(text(e.doc()) == "dog sat on the dog mat");
    e.undo();

    // an empty group records nothing
    before = e.undoCount();
    { EditGroup g(e, "nothing"); }
    CHECK(e.undoCount() == before);
    // a group that fails half-way still closes cleanly
    try {
        EditGroup g(e, "failing");
        e.insertText(Pos{0, 0}, "X");
        e.insertText(Pos{9, 9}, "Y");                      // throws
    } catch (const std::exception&) {}
    CHECK(text(e.doc()) == "Xcat sat on the cat mat");
    e.undo();
    CHECK(text(e.doc()) == base);
    CHECK(e.doc().checkInvariants());
}

static void testUndoErrorsAndLimits() {
    section("Undo/Redo: errors, limit, files");
    Editor e;
    e.insertText(Pos{0, 0}, "abc");
    size_t steps = e.undoCount();
    // failed edits record nothing and change nothing
    CHECK_THROWS(e.insertText(Pos{5, 0}, "x"));
    CHECK_THROWS(e.deleteRange(Pos{0, 0}, Pos{0, 9}));
    CHECK_THROWS(e.deleteChars(Pos{0, 3}, 1));
    CHECK_THROWS(e.deleteWord(Pos{0, 3}));
    CHECK_THROWS(e.deleteLines(0, 4));
    CHECK_THROWS(e.insertLineBefore(9, "x"));
    CHECK_THROWS(e.replaceRange(Pos{0, 0}, Pos{4, 0}, "x"));
    CHECK(e.undoCount() == steps && text(e.doc()) == "abc");
    // edits that do nothing record nothing
    e.insertText(Pos{0, 0}, "");
    e.deleteRange(Pos{0, 1}, Pos{0, 1});
    CHECK(e.undoCount() == steps);

    // history limit drops the OLDEST steps
    Editor lim;
    lim.setHistoryLimit(5);
    for (int i = 0; i < 12; ++i) lim.insertText(Pos{0, lim.doc().lineLength(0)}, std::to_string(i % 10));
    CHECK(lim.undoCount() == 5);
    int undone = 0;
    while (lim.undo()) ++undone;
    CHECK(undone == 5 && text(lim.doc()) == "0123456");   // the first 7 inserts stay
    CHECK(lim.redoCount() == 5);

    // files: save keeps history, open / new clear it
    const std::string path = "engine_undo_tmp.txt";
    Editor f;
    f.insertText(Pos{0, 0}, "saved text");
    CHECK(f.save(path).ok && f.canUndo());
    CHECK(f.open(path).ok && !f.canUndo() && !f.canRedo());
    f.insertText(Pos{0, 0}, "more ");
    CHECK(!f.open("no_such_file_12345.txt").ok && f.canUndo());   // failed open keeps history
    f.newDocument();
    CHECK(!f.canUndo() && f.doc().isEmpty());
    std::remove(path.c_str());

    // file-name check
    Editor g;
    CHECK(!g.save("<firstfile>").ok);
    CHECK(!g.save("a|b.txt").ok);
    CHECK(!g.open("what?.txt").ok);
}

static void testUndoRandom() {
    section("Undo/Redo: random stress (undo all, redo all)");
    std::srand(777);
    Editor e;
    e.setHistoryLimit(1000000);
    DynArray<std::string> snaps;          // snaps[i] = text after i recorded steps
    snaps.push_back(text(e.doc()));
    const char alphabet[] = "abc \n";
    for (int step = 0; step < 1500; ++step) {
        std::string cur = text(e.doc());
        size_t before = e.undoCount();
        int kind = std::rand() % 6;
        size_t a = static_cast<size_t>(std::rand()) % (cur.size() + 1);
        size_t b = static_cast<size_t>(std::rand()) % (cur.size() + 1);
        if (a > b) { size_t t = a; a = b; b = t; }
        std::string ins;
        size_t len = static_cast<size_t>(std::rand()) % 5;
        for (size_t i = 0; i < len; ++i) ins.push_back(alphabet[std::rand() % 5]);
        if (kind <= 1 || cur.empty()) e.insertText(offsetToPos(cur, a), ins);
        else if (kind == 2) e.deleteRange(offsetToPos(cur, a), offsetToPos(cur, b));
        else if (kind == 3) e.replaceRange(offsetToPos(cur, a), offsetToPos(cur, b), ins);
        else if (kind == 4) e.insertNewLine(offsetToPos(cur, a));
        else {
            size_t n = e.doc().lineCount();
            size_t l1 = static_cast<size_t>(std::rand()) % n, l2 = static_cast<size_t>(std::rand()) % n;
            if (l1 > l2) { size_t t = l1; l1 = l2; l2 = t; }
            e.deleteLines(l1, l2);
        }
        if (e.undoCount() == before + 1) snaps.push_back(text(e.doc()));
        else CHECK(e.undoCount() == before);               // no-op edits record nothing
    }
    size_t U = e.undoCount();
    CHECK(U + 1 == snaps.size());
    bool allOk = true;
    for (size_t i = U; i > 0; --i) {                       // undo one step at a time
        e.undo();
        if (text(e.doc()) != snaps[i - 1]) { allOk = false; break; }
    }
    CHECK(allOk);
    CHECK(e.undoCount() == 0 && e.redoCount() == U && e.doc().checkInvariants());
    allOk = true;
    for (size_t i = 1; i <= U; ++i) {                      // redo one step at a time
        e.redo();
        if (text(e.doc()) != snaps[i]) { allOk = false; break; }
    }
    CHECK(allOk);
    CHECK(e.redoCount() == 0 && e.doc().checkInvariants());
    std::cout << "  (" << U << " steps undone and redone)\n";
}


// ------------------------------------------------------------------ phase 3: KMP search / replace

static std::string joinTable(const DynArray<size_t>& f) {
    std::string s;
    for (size_t i = 0; i < f.size(); ++i) { if (i) s.push_back(','); s += std::to_string(f[i]); }
    return s;
}

static std::string joinOffsets(const DynArray<size_t>& f) { return joinTable(f); }

static void testKmpStrings() {
    section("KMP: failure table and string search");
    CHECK(joinTable(buildFailureTable("ababaca")) == "0,0,1,2,3,0,1");
    CHECK(joinTable(buildFailureTable("aaaa")) == "0,1,2,3");
    CHECK(joinTable(buildFailureTable("abcdabd")) == "0,0,0,0,1,2,0");
    CHECK(joinTable(buildFailureTable("a")) == "0");
    CHECK(buildFailureTable("").size() == 0);

    CHECK(joinOffsets(kmpFindAll("aaaaa", "aa")) == "0,1,2,3");               // overlapping
    CHECK(joinOffsets(kmpFindAll("abababab", "abab")) == "0,2,4");
    CHECK(joinOffsets(kmpFindAll("hello world", "o")) == "4,7");
    CHECK(joinOffsets(kmpFindAll("abc", "abc")) == "0");                      // pattern == text
    CHECK(kmpFindAll("abc", "abcd").size() == 0);                             // pattern longer than text
    CHECK(kmpFindAll("", "a").size() == 0);                                   // empty text
    CHECK(kmpFindAll("abc", "x").size() == 0);
    CHECK(joinOffsets(kmpFindAll("Hello HELLO hello", "hello", false)) == "0,6,12");
    CHECK(joinOffsets(kmpFindAll("Hello HELLO hello", "hello", true)) == "12");
    CHECK(joinOffsets(kmpFindAll("a b  c", " ")) == "1,3,4");                 // spaces are ordinary text
    CHECK_THROWS(kmpFindAll("abc", ""));
    CHECK_THROWS(KmpMatcher("", true));

    // The classic worst case for the naive search: must be fast (linear).
    std::string big(2000000, 'a');
    std::string pat(1000, 'a');
    pat.push_back('b');
    CHECK(kmpFindAll(big, pat).size() == 0);
    CHECK(kmpFindAll(big, std::string(1000, 'a')).size() == big.size() - 999);

    // Random strings over a tiny alphabet (many partial matches) vs. std::string::find.
    std::srand(2468);
    bool allOk = true;
    for (int round = 0; round < 3000 && allOk; ++round) {
        std::string s, p;
        size_t n = static_cast<size_t>(std::rand()) % 40, m = 1 + static_cast<size_t>(std::rand()) % 5;
        const char alpha[] = "abAB";
        for (size_t i = 0; i < n; ++i) s.push_back(alpha[std::rand() % 4]);
        for (size_t i = 0; i < m; ++i) p.push_back(alpha[std::rand() % 4]);
        for (int cs = 0; cs < 2; ++cs) {
            auto lower = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return x; };
            std::string hs = cs ? s : lower(s), nd = cs ? p : lower(p);
            std::string expect;
            for (size_t pos = hs.find(nd); pos != std::string::npos; pos = hs.find(nd, pos + 1))
                expect += std::to_string(pos) + ",";
            std::string got = joinOffsets(kmpFindAll(s, p, cs == 1));
            if (!got.empty()) got += ",";
            if (got != expect) allOk = false;
        }
    }
    CHECK(allOk);
}

static std::string matches(const DynArray<Match>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += " ";
        s += std::to_string(v[i].start.line) + ":" + std::to_string(v[i].start.col) + "-" +
             std::to_string(v[i].end.line) + ":" + std::to_string(v[i].end.col);
    }
    return s;
}

static void testDocSearch() {
    section("Search: documents");
    Document d;
    d.loadFromString("The cat sat.\nCat and concat\n\ncat");
    CHECK(matches(findAll(d, "cat")) == "0:4-0:7 1:11-1:14 3:0-3:3");
    SearchOptions ic; ic.matchCase = false;
    CHECK(matches(findAll(d, "cat", ic)) == "0:4-0:7 1:0-1:3 1:11-1:14 3:0-3:3");
    SearchOptions ww; ww.wholeWord = true;
    CHECK(matches(findAll(d, "cat", ww)) == "0:4-0:7 3:0-3:3");                // not "concat"
    SearchOptions both; both.matchCase = false; both.wholeWord = true;
    CHECK(matches(findAll(d, "CAT", both)) == "0:4-0:7 1:0-1:3 3:0-3:3");
    CHECK(findAll(d, "dog").size() == 0);
    CHECK(findAll(d, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx").size() == 0);   // longer than document

    // matches across line breaks
    CHECK(matches(findAll(d, "sat.\nCat")) == "0:8-1:3");
    CHECK(matches(findAll(d, "\n")) == "0:12-1:0 1:14-2:0 2:0-3:0");          // a bare line break
    CHECK(matches(findAll(d, "concat\n\ncat")) == "1:8-3:3");
    CHECK(matches(findAll(d, "t.\n")) == "0:10-1:0");                           // pattern ends with a line break
    CHECK(matches(findAll(d, "\nCat")) == "0:12-1:3");                          // pattern starts with a line break
    CHECK(matches(findAll(d, "t.\r\nC")) == "0:10-1:1");                       // '\r' is dropped: "t.\nC"
    CHECK(matches(findAll(d, " ")) == "0:3-0:4 0:7-0:8 1:3-1:4 1:7-1:8");       // spaces are searchable

    // overlapping vs. not
    Document a;
    a.loadFromString("aaaaa");
    CHECK(matches(findAll(a, "aa")) == "0:0-0:2 0:1-0:3 0:2-0:4 0:3-0:5");
    CHECK(matches(findAll(a, "aa", SearchOptions(), false)) == "0:0-0:2 0:2-0:4");
    Document nlOverlap;
    nlOverlap.loadFromString("a\na\na");
    CHECK(matches(findAll(nlOverlap, "a\na")) == "0:0-1:1 1:0-2:1");
    CHECK(matches(findAll(nlOverlap, "a\na", SearchOptions(), false)) == "0:0-1:1");

    // whole word only checks the sides where the text starts/ends with a word character
    Document w;
    w.loadFromString("(foo) foo_bar foo-bar\nfoo");
    CHECK(matches(findAll(w, "(foo)", ww)) == "0:0-0:5");
    CHECK(matches(findAll(w, "foo", ww)) == "0:1-0:4 0:14-0:17 1:0-1:3");     // not foo_bar
    Document w2;
    w2.loadFromString("x foo\nafoo\nfoo");
    CHECK(matches(findAll(w2, "foo\n", ww)) == "0:2-1:0");                    // right side is a line break: only the left is checked

    // errors and edge cases
    CHECK_THROWS(findAll(d, ""));
    CHECK_THROWS(findAll(d, "\r\r"));                                          // only '\r': empty after cleaning
    Document empty;
    CHECK(findAll(empty, "a").size() == 0);                                    // empty document: no matches, no crash
    CHECK(findAll(empty, "\n").size() == 0);
    Document two;                                                              // two empty lines
    two.loadFromString("\n\n");
    CHECK(two.lineCount() == 2);                                               // a trailing newline ends the last line
    CHECK(matches(findAll(two, "\n")) == "0:0-1:0");

    // findNext
    Match m;
    CHECK(findNext(d, Pos{0, 0}, "cat", SearchOptions(), m) && m.start == (Pos{0, 4}));
    CHECK(findNext(d, Pos{0, 5}, "cat", SearchOptions(), m) && m.start == (Pos{1, 11}));
    CHECK(findNext(d, Pos{0, 4}, "cat", SearchOptions(), m) && m.start == (Pos{0, 4}));      // at 'from' counts
    CHECK(findNext(d, Pos{3, 0}, "cat", SearchOptions(), m) && m.start == (Pos{3, 0}));
    CHECK(findNext(d, Pos{3, 1}, "cat", SearchOptions(), m) && m.start == (Pos{0, 4}));      // wrapped
    CHECK(!findNext(d, Pos{3, 1}, "cat", SearchOptions(), m, false));                        // no wrap
    CHECK(!findNext(d, Pos{0, 0}, "dog", SearchOptions(), m));
    CHECK(findNext(d, Pos{3, 3}, "cat", SearchOptions(), m) && m.start == (Pos{0, 4}));      // 'from' at end of last line
    CHECK_THROWS(findNext(d, Pos{9, 0}, "cat", SearchOptions(), m));
    CHECK_THROWS(findNext(d, Pos{0, 99}, "cat", SearchOptions(), m));
    CHECK_THROWS(findNext(d, Pos{0, 0}, "", SearchOptions(), m));
    CHECK(findNext(empty, Pos{0, 0}, "a", SearchOptions(), m) == false);
    // a match that starts before 'from' is not "next"
    CHECK(findNext(d, Pos{0, 5}, "cat", SearchOptions(), m, false) && m.start == (Pos{1, 11}));
    // searching never changes the document
    CHECK(!d.modified() && d.lineCount() == 4 && d.checkInvariants());
}

// Reference implementation: flatten, naive search, map offsets back to positions.
static std::string flat(const Document& d) { return text(d); }

static bool isWordCh(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

static std::string modelFind(const Document& d, std::string pat, bool matchCase, bool whole, bool overlap) {
    std::string s = flat(d), res;
    auto lower = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return x; };
    std::string hs = matchCase ? s : lower(s), nd = matchCase ? pat : lower(pat);
    size_t lastEnd = 0;
    bool have = false;
    for (size_t pos = hs.find(nd); pos != std::string::npos; pos = hs.find(nd, pos + 1)) {
        size_t end = pos + nd.size();
        if (whole) {
            if (isWordCh(nd.front()) && pos > 0 && isWordCh(s[pos - 1])) continue;
            if (isWordCh(nd.back()) && end < s.size() && isWordCh(s[end])) continue;
        }
        if (!overlap && have && pos < lastEnd) continue;
        Pos a = offsetToPos(s, pos), b = offsetToPos(s, end);
        res += std::to_string(a.line) + ":" + std::to_string(a.col) + "-" + std::to_string(b.line) + ":" +
               std::to_string(b.col) + " ";
        have = true;
        lastEnd = end;
    }
    if (!res.empty()) res.pop_back();
    return res;
}

static void testSearchRandom() {
    section("Search: random documents vs. naive model");
    std::srand(1357);
    bool allOk = true;
    int checked = 0;
    for (int round = 0; round < 1500 && allOk; ++round) {
        std::string body;
        size_t n = static_cast<size_t>(std::rand()) % 50;
        const char alpha[] = "abAB _\n-";
        for (size_t i = 0; i < n; ++i) body.push_back(alpha[std::rand() % 8]);
        Document d;
        d.loadFromString(body);
        std::string pat;
        size_t m = 1 + static_cast<size_t>(std::rand()) % 4;
        for (size_t i = 0; i < m; ++i) pat.push_back(alpha[std::rand() % 8]);
        for (int mode = 0; mode < 8; ++mode) {
            SearchOptions o;
            o.matchCase = (mode & 1) == 0;
            o.wholeWord = (mode & 2) != 0;
            bool overlap = (mode & 4) == 0;
            std::string got = matches(findAll(d, pat, o, overlap));
            std::string want = modelFind(d, pat, o.matchCase, o.wholeWord, overlap);
            ++checked;
            if (got != want) {
                allOk = false;
                std::cout << "  mismatch: doc=\"" << body << "\" pat=\"" << pat << "\" mode=" << mode
                          << "\n    got : " << got << "\n    want: " << want << "\n";
                break;
            }
        }
    }
    CHECK(allOk);
    std::cout << "  (" << checked << " searches compared)\n";
}

static void testSearchLarge() {
    section("Search: large document");
    Document d;
    std::string body;
    for (int i = 0; i < 100000; ++i) body += "the quick brown fox jumps over the lazy dog " + std::to_string(i) + "\n";
    d.loadFromString(body);
    CHECK(findAll(d, "lazy dog").size() == 100000);
    CHECK(findAll(d, "99999").size() >= 1);
    CHECK(findAll(d, "dog 5\nthe").size() == 1);                               // across a line break
    std::string longLine(1000000, 'a');
    Document one;
    one.loadFromString(longLine);
    SearchOptions ww; ww.wholeWord = true;
    CHECK(findAll(one, "aa", SearchOptions(), true).size() == 999999);
    CHECK(findAll(one, "aa", ww, true).size() == 0);                           // many candidates, one long line: stays linear
}

static void testReplaceAll() {
    section("Replace all");
    Editor e;
    e.insertText(Pos{0, 0}, "the cat sat on the mat\nthe end");
    size_t base = e.undoCount();

    ReplaceResult r = e.replaceAll("the", "a");
    CHECK(r.found == 3 && r.changed == 3);
    CHECK(text(e.doc()) == "a cat sat on a mat\na end");
    CHECK(e.undoCount() == base + 1);                                          // ONE undo step
    CHECK(e.nextUndoLabel() == "replace all");
    CHECK(e.undo());
    CHECK(text(e.doc()) == "the cat sat on the mat\nthe end");
    CHECK(e.redo());
    CHECK(text(e.doc()) == "a cat sat on a mat\na end");
    CHECK(e.undo());

    // replacement longer / shorter / empty / with line break / containing the search text
    CHECK(e.replaceAll("cat", "elephant").changed == 1 && text(e.doc()).find("elephant") != std::string::npos);
    e.undo();
    CHECK(e.replaceAll("the ", "").changed == 3 && text(e.doc()) == "cat sat on mat\nend");
    e.undo();
    CHECK(e.replaceAll(" on ", "\n").changed == 1 && text(e.doc()) == "the cat sat\nthe mat\nthe end");
    CHECK(e.doc().lineCount() == 3 && e.doc().checkInvariants());
    e.undo();
    CHECK(text(e.doc()) == "the cat sat on the mat\nthe end");
    CHECK(e.replaceAll("the", "thethe").changed == 3 && text(e.doc()) == "thethe cat sat on thethe mat\nthethe end");
    e.undo();

    // multi-line pattern
    CHECK(e.replaceAll("mat\nthe", "rug ").changed == 1 && text(e.doc()) == "the cat sat on the rug  end");
    CHECK(e.doc().lineCount() == 1);
    e.undo();
    CHECK(e.doc().lineCount() == 2 && text(e.doc()) == "the cat sat on the mat\nthe end");

    // options
    CHECK(e.replaceAll("THE", "x").found == 0);                                // case sensitive by default
    SearchOptions ic; ic.matchCase = false;
    CHECK(e.replaceAll("THE", "x", ic).changed == 3);
    e.undo();
    SearchOptions ww; ww.wholeWord = true;
    e.insertText(Pos{1, 7}, " other");
    CHECK(e.replaceAll("the", "Z", ww).changed == 3);
    CHECK(text(e.doc()) == "Z cat sat on Z mat\nZ end other");
    e.undo(); e.undo();

    // non-overlapping, left to right
    Editor o;
    o.insertText(Pos{0, 0}, "aaaaa");
    CHECK(o.replaceAll("aa", "b").found == 2 && text(o.doc()) == "bba");
    o.undo();
    CHECK(o.replaceAll("aaa", "x").changed == 1 && text(o.doc()) == "xaa");

    // no matches / nothing to change: no history, document untouched
    size_t steps = e.undoCount();
    r = e.replaceAll("zzz", "y");
    CHECK(r.found == 0 && r.changed == 0 && e.undoCount() == steps && e.canUndo());
    r = e.replaceAll("cat", "cat");
    CHECK(r.found == 1 && r.changed == 0 && e.undoCount() == steps);
    CHECK(text(e.doc()) == "the cat sat on the mat\nthe end");

    // errors leave everything as it was
    CHECK_THROWS(e.replaceAll("", "x"));
    CHECK_THROWS(e.replaceAll("\r", "x"));
    CHECK(e.undoCount() == steps && text(e.doc()) == "the cat sat on the mat\nthe end");

    // a new replace clears redo like any other edit
    e.replaceAll("cat", "dog");
    e.undo();
    CHECK(e.canRedo());
    e.replaceAll("sat", "ran");
    CHECK(!e.canRedo());

    // empty document
    Editor empty;
    r = empty.replaceAll("a", "b");
    CHECK(r.found == 0 && empty.undoCount() == 0 && empty.doc().isEmpty());
    CHECK_THROWS(empty.replaceAll("", "b"));
    CHECK_THROWS(empty.replaceNth("a", "b", 1));

    // inside a user group it joins the outer step
    Editor g;
    g.insertText(Pos{0, 0}, "a a a");
    size_t before = g.undoCount();
    g.beginGroup("combo");
    g.replaceAll("a", "b");
    g.insertText(Pos{0, 0}, "!");
    g.endGroup();
    CHECK(g.undoCount() == before + 1 && text(g.doc()) == "!b b b");
    g.undo();
    CHECK(text(g.doc()) == "a a a");
}

static void testReplaceNthAndNext() {
    section("Replace n-th / next");
    Editor e;
    e.insertText(Pos{0, 0}, "cat cat\ncat");
    size_t base = e.undoCount();
    Match m = e.replaceNth("cat", "dog", 2);
    CHECK(m.start == (Pos{0, 4}) && text(e.doc()) == "cat dog\ncat");
    CHECK(e.undoCount() == base + 1);
    e.undo();
    CHECK(text(e.doc()) == "cat cat\ncat");
    e.replaceNth("cat", "", 3);
    CHECK(text(e.doc()) == "cat cat\n");
    e.undo();

    CHECK_THROWS(e.replaceNth("cat", "x", 0));
    CHECK_THROWS(e.replaceNth("cat", "x", 4));
    CHECK_THROWS(e.replaceNth("dog", "x", 1));
    CHECK_THROWS(e.replaceNth("", "x", 1));
    CHECK(e.undoCount() == base && text(e.doc()) == "cat cat\ncat");

    // numbering follows the overlapping list shown by find
    Editor o;
    o.insertText(Pos{0, 0}, "aaa");
    o.replaceNth("aa", "X", 2);
    CHECK(text(o.doc()) == "aX");

    // replaceNext walks through the document and wraps
    Pos from{0, 0}, after;
    CHECK(e.replaceNext(from, "cat", "dog", SearchOptions(), &after));
    CHECK(text(e.doc()) == "dog cat\ncat" && after == (Pos{0, 3}));
    CHECK(e.replaceNext(after, "cat", "dog", SearchOptions(), &after));
    CHECK(text(e.doc()) == "dog dog\ncat" && after == (Pos{0, 7}));
    CHECK(e.replaceNext(after, "cat", "dog", SearchOptions(), &after));
    CHECK(text(e.doc()) == "dog dog\ndog");
    CHECK(!e.replaceNext(after, "cat", "dog", SearchOptions(), &after));       // none left, even after wrapping
    CHECK(text(e.doc()) == "dog dog\ndog");
    CHECK_THROWS(e.replaceNext(Pos{9, 9}, "dog", "x"));
    CHECK_THROWS(e.replaceNext(Pos{0, 0}, "", "x"));
    // wrapping
    Editor w;
    w.insertText(Pos{0, 0}, "ab ab");
    CHECK(w.replaceNext(Pos{0, 4}, "ab", "X", SearchOptions(), nullptr, true));   // wraps to the first one
    CHECK(text(w.doc()) == "X ab");
    CHECK(!w.replaceNext(Pos{0, 2}, "X", "Y", SearchOptions(), nullptr, false));  // 'X' is only before 'from'
    CHECK(text(w.doc()) == "X ab");
    // replacing with a longer text must not make replaceNext loop on its own result
    Editor loop;
    loop.insertText(Pos{0, 0}, "a a");
    Pos p{0, 0};
    int guard = 0;
    while (loop.replaceNext(p, "a", "aa", SearchOptions(), &p, false) && ++guard < 10) {}
    CHECK(guard == 2 && text(loop.doc()) == "aa aa");
}

static void testReplaceRollback() {
    section("Replace all: failure half way is rolled back");
    Editor e;
    e.insertText(Pos{0, 0}, "x1 x2 x3 x4\nx5");
    e.insertNewLine(Pos{1, 2});
    std::string original = text(e.doc());
    size_t steps = e.undoCount();
    e.undo();
    e.redo();                                                                  // leave a redo-free state
    e.injectFailureForTesting(3);                                              // fail after 3 replacements
    bool threw = false;
    try { e.replaceAll("x", "LONGER"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    CHECK(text(e.doc()) == original);                                          // text restored
    CHECK(e.undoCount() == steps && !e.canRedo());                             // history untouched
    CHECK(e.doc().checkInvariants());
    CHECK(e.replaceAll("x", "y").changed == 5);                                // editor still works
    CHECK(e.undoCount() == steps + 1);
    e.undo();
    CHECK(text(e.doc()) == original);

    // failure inside a user group: only this call is rolled back
    e.beginGroup("outer");
    e.insertText(Pos{0, 0}, ">>");
    e.injectFailureForTesting(2);
    threw = false;
    try { e.replaceAll("x", "Q"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    e.endGroup();
    CHECK(text(e.doc()) == ">>" + original);
    e.undo();
    CHECK(text(e.doc()) == original);
}

static void testReplaceRandom() {
    section("Replace all: random stress vs. model (undo / redo)");
    std::srand(97531);
    bool allOk = true;
    int rounds = 0;
    for (int round = 0; round < 1500 && allOk; ++round) {
        std::string body;
        size_t n = static_cast<size_t>(std::rand()) % 60;
        const char alpha[] = "abAB _\n";
        for (size_t i = 0; i < n; ++i) body.push_back(alpha[std::rand() % 7]);
        Editor e;
        e.insertText(Pos{0, 0}, body);
        std::string pat, rep;
        size_t m = 1 + static_cast<size_t>(std::rand()) % 3, rl = static_cast<size_t>(std::rand()) % 4;
        for (size_t i = 0; i < m; ++i) pat.push_back(alpha[std::rand() % 7]);
        for (size_t i = 0; i < rl; ++i) rep.push_back(alpha[std::rand() % 7]);
        SearchOptions o;
        o.matchCase = std::rand() % 2 == 0;
        o.wholeWord = std::rand() % 3 == 0;

        // model: naive non-overlapping search on the flat text, rebuilt left to right
        std::string before = text(e.doc()), expect;
        {
            DynArray<Match> hits = findAll(e.doc(), pat, o, false);        // positions already checked by the search tests
            std::string want = modelFind(e.doc(), pat, o.matchCase, o.wholeWord, false);
            if (matches(hits) != want) { allOk = false; break; }
            size_t cursor = 0;
            for (size_t i = 0; i < hits.size(); ++i) {
                size_t a = 0, b = 0;                                      // offsets of the hit in 'before'
                {
                    size_t line = 0, col = 0, off = 0;
                    for (; off < before.size(); ++off) {
                        if (line == hits[i].start.line && col == hits[i].start.col) break;
                        if (before[off] == '\n') { ++line; col = 0; } else ++col;
                    }
                    a = off;
                    b = a + pat.size();
                }
                expect += before.substr(cursor, a - cursor);
                if (before.substr(a, b - a) != rep) expect += rep; else expect += before.substr(a, b - a);
                cursor = b;
            }
            expect += before.substr(cursor);
        }
        size_t steps = e.undoCount();
        ReplaceResult r = e.replaceAll(pat, rep, o);
        ++rounds;
        if (text(e.doc()) != expect || !e.doc().checkInvariants()) { allOk = false; break; }
        if (r.changed > 0) {
            if (e.undoCount() != steps + 1) { allOk = false; break; }
            std::string after = text(e.doc());
            e.undo();
            if (text(e.doc()) != before) { allOk = false; break; }
            e.redo();
            if (text(e.doc()) != after) { allOk = false; break; }
        } else if (e.undoCount() != steps || text(e.doc()) != before) {
            allOk = false;
        }
    }
    CHECK(allOk);
    std::cout << "  (" << rounds << " random replace-all runs checked, each undone and redone)\n";
}

static void testReplaceLarge() {
    section("Replace all: large document");
    Editor e;
    std::string body;
    for (int i = 0; i < 50000; ++i) body += "int count = 0; // count the items\n";
    e.insertText(Pos{0, 0}, body);
    ReplaceResult r = e.replaceAll("count", "total");
    CHECK(r.found == 100000 && r.changed == 100000);
    CHECK(e.doc().getLine(0) == "int total = 0; // total the items");
    CHECK(e.doc().getLine(49999) == "int total = 0; // total the items");
    CHECK(e.undoCount() >= 1);
    e.undo();
    CHECK(e.doc().getLine(49999) == "int count = 0; // count the items");
    CHECK(e.doc().checkInvariants());
}

// ------------------------------------------------------------------ phase 4

static BracketReport brk(const std::string& src, const BracketOptions& o = BracketOptions()) {
    Document d;
    d.loadFromString(src);
    return checkBrackets(d, o);
}

static void testStackPeek() {
    section("Stack::peek");
    Stack<int> s;
    CHECK_THROWS(s.peek(0));
    s.push(1); s.push(2); s.push(3);
    CHECK(s.peek(0) == 3 && s.peek(1) == 2 && s.peek(2) == 1);
    CHECK_THROWS(s.peek(3));
    CHECK(s.size() == 3);     // peek does not remove
}

static void testBracketsBasic() {
    section("Brackets: basic");
    BracketReport r = brk("");
    CHECK(r.ok() && r.pairs.empty() && r.maxDepth == 0);
    r = brk("int main() { return f(a[1]); }\n");
    CHECK(r.ok());
    CHECK(r.pairs.size() == 4);      // main(), { }, f( ), [ ]
    CHECK(r.maxDepth == 3);
    r = brk("(");
    CHECK(!r.ok() && r.issues.size() == 1 && r.issues[0].kind == BracketIssueKind::Unclosed);
    CHECK(r.issues[0].pos == (Pos{0, 0}) && r.issues[0].expected == ')');
    r = brk(")");
    CHECK(r.issues.size() == 1 && r.issues[0].kind == BracketIssueKind::UnmatchedClose && r.issues[0].found == ')');
    r = brk("(]");
    CHECK(r.issues.size() == 1 && r.issues[0].kind == BracketIssueKind::Mismatch);
    CHECK(r.issues[0].pos == (Pos{0, 1}) && r.issues[0].related == (Pos{0, 0}) && r.issues[0].expected == ')');
    CHECK(r.pairs.empty());
    // message uses 1-based line:column
    r = brk("a\n  )");
    CHECK(r.issues.size() == 1 && r.issues[0].message.find("2:3") != std::string::npos);
    // pairs: depth and ordering
    r = brk("{ ( ) }");
    CHECK(r.pairs.size() == 2 && r.pairs[0].type == '(' && r.pairs[0].depth == 1 && r.pairs[1].type == '{' && r.pairs[1].depth == 0);
    // pair across lines
    r = brk("{\n\n}");
    CHECK(r.ok() && r.pairs.size() == 1 && r.pairs[0].open == (Pos{0, 0}) && r.pairs[0].close == (Pos{2, 0}));
    // other characters (angle brackets, text) are ignored
    r = brk("template<typename T> a < b > c");
    CHECK(r.ok() && r.pairs.empty());
}

static void testBracketsRecovery() {
    section("Brackets: one typo, one message");
    BracketReport r = brk("{ ( }");               // '(' never closed, '}' still pairs with '{'
    CHECK(r.issues.size() == 1 && r.issues[0].kind == BracketIssueKind::Unclosed && r.issues[0].found == '(');
    CHECK(r.pairs.size() == 1 && r.pairs[0].type == '{');
    r = brk("( ) )");
    CHECK(r.issues.size() == 1 && r.issues[0].kind == BracketIssueKind::UnmatchedClose && r.issues[0].pos == (Pos{0, 4}));
    r = brk("{ [ ( ] }");                          // wrong closer: ']' matches '[' below '(' -> '(' unclosed
    CHECK(r.issues.size() == 1 && r.issues[0].found == '(' && r.pairs.size() == 2);
    r = brk("( [ { ");                            // all three unclosed, listed in text order
    CHECK(r.issues.size() == 3 && r.issues[0].found == '(' && r.issues[2].found == '{');
    r = brk("(}");                                // matches nothing below: wrong-kind typo, '(' consumed
    CHECK(r.issues.size() == 1 && r.issues[0].kind == BracketIssueKind::Mismatch);
}

static void testBracketsIgnored() {
    section("Brackets: strings, comments, literals");
    CHECK(brk("s = \")\";").ok());
    CHECK(brk("s = \"\\\"(\";").ok());                    // escaped quote
    CHECK(brk("c = ')'; d = '(';").ok());
    CHECK(brk("c = '\\'' ; ( )").ok());                  // escaped quote in char literal
    CHECK(brk("// ) ] }\nx();").ok());
    CHECK(brk("x(); // :-)").ok());
    CHECK(brk("/* ( [ { */ y();").ok());
    CHECK(brk("/* a\n ( \n b */ ()").ok());              // multi-line block comment
    CHECK(brk("/*/ ( */ ()").ok());                      // '/*/' does not close itself
    CHECK(brk("// one \\\n ( continued\nz();").ok());     // backslash continues a line comment
    CHECK(!brk("// one\n ( not continued").ok());
    CHECK(brk("a = 1'000'000; ( )").ok());               // digit separators are not char literals
    CHECK(brk("a = 0xFF'FF; ( )").ok());
    CHECK(brk("auto s = R\"(a ) b ( c)\"; ( )").ok());     // raw string
    CHECK(brk("auto s = R\"x(a )\" ) b)x\"; ( )").ok());   // raw string with delimiter
    CHECK(brk("auto s = R\"(line1 (\nline2 ))\"; ()").ok());// raw string over lines
    CHECK(brk("#error don't ( break").ok());                 // stray apostrophe: rest of that line is skipped, never an error
}

static void testBracketsIssuesKinds() {
    section("Brackets: unterminated things, options");
    BracketReport r = brk("a /* never ends ( \n b");
    CHECK(r.issues.size() == 1 && r.issues[0].kind == BracketIssueKind::UnterminatedComment && r.issues[0].pos == (Pos{0, 2}));
    r = brk("s = \"abc;\n(");
    CHECK(r.issues.size() == 2 && r.issues[0].kind == BracketIssueKind::UnterminatedString);   // + '(' unclosed
    r = brk("R\"(never ends\n ( ");
    CHECK(r.issues.size() == 1 && r.issues[0].kind == BracketIssueKind::UnterminatedString);
    BracketOptions o;
    o.skipStrings = false;
    CHECK(!brk("s = \")\";", o).ok());
    o = BracketOptions();
    o.skipComments = false;
    CHECK(!brk("// )", o).ok());
    // issue cap
    o = BracketOptions();
    o.maxIssues = 3;
    r = brk(")))))))", o);
    CHECK(r.issues.size() == 3 && r.truncated);
    o.maxIssues = 0;
    r = brk(")))))))", o);
    CHECK(r.issues.size() == 7 && !r.truncated);
}

static void testBracketsCursor() {
    section("Brackets: matching near the cursor");
    Document d;
    d.loadFromString("f(a[1]);\n{ }");
    BracketReport r = checkBrackets(d);
    Pos b, o;
    CHECK(matchNearCursor(d, r, Pos{0, 1}, b, o) && b == (Pos{0, 1}) && o == (Pos{0, 6}));   // on '('
    CHECK(matchNearCursor(d, r, Pos{0, 7}, b, o) && b == (Pos{0, 6}) && o == (Pos{0, 1}));   // just after ')'
    CHECK(!matchNearCursor(d, r, Pos{0, 0}, b, o));                                            // before 'f', none
    CHECK(!matchNearCursor(d, r, Pos{0, 3}, b, o) || b == (Pos{0, 3}));                         // 'a' / '[' neighbour
    CHECK(matchNearCursor(d, r, Pos{1, 2}, b, o) && !(b == (Pos{1, 0})));                // after ' ' -> next is '}'
    CHECK(matchNearCursor(d, r, Pos{1, 3}, b, o) && o == (Pos{1, 0}));                         // end of line, after '}'
    Pos tmp;
    CHECK(r.matchOf(Pos{1, 0}, tmp) && tmp == (Pos{1, 2}));
    CHECK(!r.matchOf(Pos{0, 0}, tmp));
    CHECK_THROWS(matchNearCursor(d, r, Pos{5, 0}, b, o));
    CHECK_THROWS(matchNearCursor(d, r, Pos{0, 99}, b, o));
    // an unmatched bracket has no partner
    d.loadFromString("(");
    r = checkBrackets(d);
    CHECK(!matchNearCursor(d, r, Pos{0, 0}, b, o) && !matchNearCursor(d, r, Pos{0, 1}, b, o));
    CHECK(bracketPartner('(') == ')' && bracketPartner('}') == '{' && bracketPartner('x') == 0);
}

static void testBracketsRandom() {
    section("Brackets: random vs simple model, and fuzz");
    unsigned seed = 12345;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    const char open[] = "([{", close[] = ")]}";
    for (int iter = 0; iter < 400; ++iter) {
        std::string src;
        int len = rnd() % 40;
        for (int i = 0; i < len; ++i) {
            unsigned k = rnd() % 8;
            if (k < 3) src.push_back(open[k]);
            else if (k < 6) src.push_back(close[k - 3]);
            else if (k == 6) src.push_back('\n');
            else src.push_back('x');
        }
        // model: textbook balanced check
        std::vector<char> st;
        bool balanced = true;
        for (size_t i = 0; i < src.size() && balanced; ++i) {
            char c = src[i];
            if (c == '(' || c == '[' || c == '{') st.push_back(c);
            else if (c == ')' || c == ']' || c == '}') {
                if (st.empty() || bracketPartner(st.back()) != c) balanced = false;
                else st.pop_back();
            }
        }
        balanced = balanced && st.empty();
        BracketReport r = brk(src);
        CHECK(r.ok() == balanced);
        if (balanced) {
            size_t opens = 0;
            for (size_t i = 0; i < src.size(); ++i) if (isOpenBracket(src[i])) ++opens;
            CHECK(r.pairs.size() == opens);
        }
        for (size_t i = 0; i < r.pairs.size(); ++i)
            CHECK(r.pairs[i].open < r.pairs[i].close);
    }
    // fuzz with quotes, slashes, stars, backslashes, newlines: never crashes, issues are sane
    const char alphabet[] = "()[]{}\"'/*\\\nR a1_#";
    for (int iter = 0; iter < 600; ++iter) {
        std::string src;
        int len = rnd() % 60;
        for (int i = 0; i < len; ++i) src.push_back(alphabet[rnd() % (sizeof(alphabet) - 1)]);
        BracketReport r = brk(src);
        for (size_t i = 0; i < r.issues.size(); ++i) CHECK(!r.issues[i].message.empty());
        for (size_t i = 0; i < r.pairs.size(); ++i)
            CHECK(r.pairs[i].open < r.pairs[i].close && bracketPartner(r.pairs[i].type) != 0);
    }
}

static void testBracketsLarge() {
    section("Brackets: large input");
    std::string src;
    for (int i = 0; i < 200000; ++i) src += "(";
    for (int i = 0; i < 200000; ++i) src += ")";
    src += "\n";
    BracketReport r = brk(src);                           // very deep nesting: no recursion, no overflow
    CHECK(r.ok() && r.pairs.size() == 200000 && r.maxDepth == 200000);
    std::string big;
    for (int i = 0; i < 100000; ++i) big += "void f" + std::to_string(i) + "() { if (a[1]) { } }\n";
    r = brk(big);
    CHECK(r.ok() && r.pairs.size() == 500000);
}

static void testTrieBasics() {
    section("Trie: basics");
    Trie t;
    CHECK(t.wordCount() == 0 && !t.contains("a") && t.weightOf("a") == 0);
    CHECK(t.insert("car") && t.insert("card") && t.insert("care") && t.insert("cat") && t.insert("dog"));
    CHECK(t.wordCount() == 5);
    CHECK(t.contains("car") && t.contains("card") && !t.contains("ca") && !t.contains("cards"));
    CHECK(t.insert("car", 4) && t.weightOf("car") == 5 && t.wordCount() == 5);
    DynArray<std::string> c = t.complete("ca", 10);
    CHECK(c.size() == 4 && c[0] == "car" && c[1] == "card" && c[2] == "care" && c[3] == "cat");   // weight, then alphabetical
    c = t.complete("ca", 2);
    CHECK(c.size() == 2 && c[0] == "car" && c[1] == "card");
    CHECK(t.complete("car", 10).size() == 3);            // prefix itself included
    CHECK(t.complete("x", 5).empty() && t.complete("ca", 0).empty() && t.complete("", 5).empty());
    CHECK(t.countWithPrefix("ca") == 4 && t.countWithPrefix("") == 0 && t.countWithPrefix("zz") == 0);
    CHECK(t.remove("card") && !t.contains("card") && t.contains("care") && t.wordCount() == 4);
    CHECK(!t.remove("card") && !t.remove("zzz") && !t.remove(""));
    CHECK(t.complete("car", 10).size() == 2);
    DynArray<std::string> top = t.topWords(2);
    CHECK(top.size() == 2 && top[0] == "car");
    CHECK(t.topWords(0).empty());
    size_t before = t.nodeCount();
    t.compact();
    CHECK(t.nodeCount() < before && t.wordCount() == 4 && t.weightOf("car") == 5 && t.contains("dog"));
    t.clear();
    CHECK(t.wordCount() == 0 && t.nodeCount() == 1 && t.complete("c", 5).empty());
}

static void testTrieErrors() {
    section("Trie: invalid input never throws or changes the trie");
    Trie t;
    CHECK(!t.insert("") && !t.insert("a b") && !t.insert("tab\t") && !t.insert("nl\n") && !t.insert(std::string("a\0b", 3)));
    CHECK(!t.insert(std::string(Trie::kMaxWordLength + 1, 'a')));
    CHECK(t.insert(std::string(Trie::kMaxWordLength, 'a')));
    CHECK(t.wordCount() == 1);
    CHECK(!t.contains("") && t.weightOf("") == 0 && t.complete("a b", 5).empty() && t.countWithPrefix("a b") == 0);
    CHECK(t.insert("caf\xc3\xa9"));                       // UTF-8 bytes allowed
    CHECK(t.contains("caf\xc3\xa9"));
    CHECK(t.insert("Hello") && t.insert("hello") && t.wordCount() == 4);   // case sensitive
    CHECK(t.insert("big", 0xFFFFFFFFu) && t.insert("big", 5) && t.weightOf("big") == 0xFFFFFFFFu);   // saturates
}

static void testTrieRandom() {
    section("Trie: random vs std::map model");
    unsigned seed = 777;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    Trie t;
    std::map<std::string, unsigned> model;
    for (int step = 0; step < 6000; ++step) {
        std::string w;
        int len = 1 + rnd() % 4;
        for (int i = 0; i < len; ++i) w.push_back(static_cast<char>('a' + rnd() % 3));
        unsigned op = rnd() % 10;
        if (op < 6) { unsigned wt = rnd() % 4; t.insert(w, wt); model[w] += wt; }
        else if (op < 8) { bool had = model.erase(w) > 0; CHECK(t.remove(w) == had); }
        else if (op == 8) t.compact();
        else {
            std::string pre = w.substr(0, 1 + rnd() % w.size());
            size_t k = 1 + rnd() % 6;
            // model answer: all words with prefix, weight desc then alphabetical, first k
            std::vector<std::pair<std::string, unsigned>> all;
            for (std::map<std::string, unsigned>::const_iterator it = model.begin(); it != model.end(); ++it)
                if (it->first.compare(0, pre.size(), pre) == 0) all.push_back(*it);
            for (size_t i = 0; i < all.size(); ++i)                // selection sort, stable enough: (weight desc, word asc)
                for (size_t j = i + 1; j < all.size(); ++j)
                    if (all[j].second > all[i].second || (all[j].second == all[i].second && all[j].first < all[i].first)) std::swap(all[i], all[j]);
            DynArray<std::string> got = t.complete(pre, k);
            size_t expect = all.size() < k ? all.size() : k;
            CHECK(got.size() == expect);
            for (size_t i = 0; i < got.size() && i < expect; ++i) CHECK(got[i] == all[i].first);
            CHECK(t.countWithPrefix(pre) == all.size());
        }
    }
    CHECK(t.wordCount() == model.size());
}

static void testTrieLarge() {
    section("Trie: large");
    Trie t;
    unsigned seed = 99;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int i = 0; i < 200000; ++i) {
        std::string w;
        int len = 3 + rnd() % 10;
        for (int k = 0; k < len; ++k) w.push_back(static_cast<char>('a' + rnd() % 26));
        t.insert(w);
    }
    CHECK(t.wordCount() > 190000);
    CHECK(t.complete("ab", 8).size() == 8);
    CHECK(t.complete("q", 5).size() == 5);
    CHECK(t.countWithPrefix("a") > 5000);
}

static Document* mkdoc(Document& d, const std::string& s) { d.loadFromString(s); return &d; }

static void testAssist() {
    section("Assist: prefix, dictionary, suggestions");
    Document d;
    mkdoc(d, "int counter = 0;\ncounter++; counter += cou;\nint x9 = 5; 3rd 0x1F\n");
    Pos start;
    CHECK(prefixBefore(d, Pos{1, 0}) == "");
    CHECK(prefixBefore(d, Pos{0, 11}, &start) == "counter" && start == (Pos{0, 4}));
    CHECK(prefixBefore(d, Pos{0, 4}) == "");                           // after a space
    CHECK(prefixBefore(d, Pos{0, 3}) == "int");
    CHECK_THROWS(prefixBefore(d, Pos{9, 0}));
    CHECK_THROWS(prefixBefore(d, Pos{0, 99}));

    Autocomplete ac;
    CHECK(ac.dictionarySize() > 100);
    ac.refresh(d);
    CHECK(ac.documentWordCount() > 0);
    ac.refresh(d);                                   // twice: counts must not inflate
    CHECK(ac.suggest("cou", 5).size() >= 1 && ac.suggest("cou", 5)[0] == "counter");
    CHECK(ac.suggest("counter", 5).empty());          // only itself
    CHECK(ac.suggest("", 5).empty() && ac.suggest("a b", 5).empty() && ac.suggest("zzzq", 5).empty());
    CHECK(ac.suggest("c", 0).empty());
    CHECK(ac.suggest("c", 3).size() == 3);
    CHECK(ac.suggest("3", 5).empty() && ac.suggest("0x", 5).empty());   // numbers are not words
    CHECK(ac.suggest("x", 5).size() >= 1);            // "x9" (a 2-char word) is found from the document
    DynArray<std::string> s = ac.suggestAt(d, Pos{1, 25}, 5);   // after "cou" on line 2
    CHECK(s.size() >= 1 && s[0] == "counter");
    // accepting raises rank
    CHECK(ac.suggest("st", 3).size() == 3);
    DynArray<std::string> before = ac.suggest("st", 20);
    ac.accepted("struct");
    CHECK(ac.suggest("st", 20)[0] == "struct");
    // user dictionary
    CHECK(ac.addWord("my_helper") && !ac.addWord("") && !ac.addWord("a b"));
    CHECK(ac.suggest("my_h", 3).size() == 1);
    CHECK(ac.removeWord("my_helper") && !ac.removeWord("my_helper") && ac.suggest("my_h", 3).empty());
}

static void testAssistRankingThreshold() {
    section("Assist: ranking is exact even when a word is mid-ranked in both tries");
    Autocomplete ac;
    // 10 dictionary words with big dictionary weight, 10 with big document weight, and one
    // word that is only 'medium' in each list but best in total.
    for (int i = 0; i < 10; ++i) {
        std::string a = "zqa" + std::string(1, static_cast<char>('a' + i));
        std::string b = "zqb" + std::string(1, static_cast<char>('a' + i));
        for (int k = 0; k < 60; ++k) ac.accepted(a);
        (void)b;
    }
    Document d;
    std::string text;
    for (int i = 0; i < 10; ++i) {
        std::string b = "zqb" + std::string(1, static_cast<char>('a' + i));
        for (int k = 0; k < 60; ++k) text += b + " ";
        text += "\n";
    }
    for (int k = 0; k < 50; ++k) { ac.accepted("zqmid"); text += "zqmid "; }
    d.loadFromString(text);
    ac.refresh(d);
    DynArray<std::string> s = ac.suggest("zq", 3);
    CHECK(s.size() == 3 && s[0] == "zqmid");           // 51+50 = 101 beats 60..61
}

static void testAssistRandomRanking() {
    section("Assist: suggest() equals brute force");
    unsigned seed = 4242;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int iter = 0; iter < 60; ++iter) {
        Autocomplete ac;
        std::map<std::string, unsigned long long> score;
        std::string text;
        for (int i = 0; i < 300; ++i) {
            std::string w = "w";
            int len = 1 + rnd() % 3;
            for (int k = 0; k < len; ++k) w.push_back(static_cast<char>('a' + rnd() % 3));
            if (rnd() % 2) { ac.accepted(w); score[w] += 1; }
            else { text += w + " "; score[w] += 1; }
        }
        Document d;
        d.loadFromString(text);
        ac.refresh(d);
        // dictionary words that were accepted get +1 on insert (weight 1 per accept), document +1 per use,
        // so model score above is the exact sum of both.
        for (int q = 0; q < 6; ++q) {
            std::string pre = "w";
            int pl = rnd() % 3;
            for (int k = 0; k < pl; ++k) pre.push_back(static_cast<char>('a' + rnd() % 3));
            size_t k = 1 + rnd() % 5;
            std::vector<std::pair<std::string, unsigned long long>> all;
            for (std::map<std::string, unsigned long long>::const_iterator it = score.begin(); it != score.end(); ++it)
                if (it->first != pre && it->first.compare(0, pre.size(), pre) == 0 && it->first.size() >= 2) all.push_back(*it);
            for (size_t i = 0; i < all.size(); ++i)
                for (size_t j = i + 1; j < all.size(); ++j)
                    if (all[j].second > all[i].second || (all[j].second == all[i].second && all[j].first < all[i].first)) std::swap(all[i], all[j]);
            DynArray<std::string> got = ac.suggest(pre, k);
            size_t expect = all.size() < k ? all.size() : k;
            CHECK(got.size() == expect);
            for (size_t i = 0; i < got.size() && i < expect; ++i) CHECK(got[i] == all[i].first);
        }
    }
}

static void testApplyCompletion() {
    section("Assist: applyCompletion is one undo step");
    Editor ed;
    ed.insertText(Pos{0, 0}, "std::cou << x;");
    size_t steps = ed.undoCount();
    Pos end = applyCompletion(ed, Pos{0, 8}, "cout");
    CHECK(ed.doc().getLine(0) == "std::cout << x;" && end == (Pos{0, 9}));
    CHECK(ed.undoCount() == steps + 1 && ed.nextUndoLabel() == "autocomplete");
    CHECK(ed.undo() && ed.doc().getLine(0) == "std::cou << x;");
    CHECK(ed.redo() && ed.doc().getLine(0) == "std::cout << x;");
    // nothing typed to complete -> word must start with the empty prefix: allowed (insert whole word)
    ed.undo();
    // errors change nothing and record nothing
    steps = ed.undoCount();
    std::string snapshot = ed.doc().getLine(0);
    CHECK_THROWS(applyCompletion(ed, Pos{0, 8}, "printf"));        // does not start with 'cou'
    CHECK_THROWS(applyCompletion(ed, Pos{0, 8}, "co"));            // shorter than the prefix
    CHECK_THROWS(applyCompletion(ed, Pos{0, 8}, ""));
    CHECK_THROWS(applyCompletion(ed, Pos{0, 8}, "a b"));
    CHECK_THROWS(applyCompletion(ed, Pos{3, 0}, "cout"));
    CHECK_THROWS(applyCompletion(ed, Pos{0, 99}, "cout"));
    CHECK(ed.doc().getLine(0) == snapshot && ed.undoCount() == steps);
    // already complete: no change, no history
    CHECK(applyCompletion(ed, Pos{0, 8}, "cou") == (Pos{0, 8}) && ed.undoCount() == steps);
    // empty document
    Editor e2;
    CHECK(applyCompletion(e2, Pos{0, 0}, "main") == (Pos{0, 4}) && e2.doc().getLine(0) == "main");
    CHECK(e2.undo() && e2.doc().isEmpty());
    CHECK(ed.doc().checkInvariants());
}


// ------------------------------------------------------------------ phase 5: typing runs

static void testTypingRuns() {
    section("Typing runs: key presses merge into undo steps");
    {
        Editor e;
        Pos p{0, 0};
        const std::string s = "hello world";
        for (size_t i = 0; i < s.size(); ++i) p = e.typeText(p, std::string(1, s[i]));
        CHECK(text(e.doc()) == "hello world");
        CHECK(e.undoCount() == 3);                         // "hello", " ", "world"
        Pos where;
        CHECK(e.undo(nullptr, &where));
        CHECK(text(e.doc()) == "hello " && where == (Pos{0, 6}));
        CHECK(e.undo());
        CHECK(text(e.doc()) == "hello");
        CHECK(e.undo());
        CHECK(text(e.doc()).empty() && !e.canUndo());
        CHECK(e.redo(nullptr, &where));
        CHECK(text(e.doc()) == "hello" && where == (Pos{0, 5}));
        CHECK(e.redo() && e.redo());
        CHECK(text(e.doc()) == "hello world");
    }
    {   // a gap, another kind of edit, or breakTyping() starts a new step
        Editor e;
        Pos p = e.typeText(Pos{0, 0}, "a");
        p = e.typeText(p, "b");
        CHECK(e.undoCount() == 1);
        e.breakTyping();
        p = e.typeText(p, "c");
        CHECK(e.undoCount() == 2 && text(e.doc()) == "abc");
        e.insertText(Pos{0, 3}, "!");                      // an ordinary edit ends the run
        p = e.typeText(Pos{0, 4}, "d");
        CHECK(e.undoCount() == 4);
        p = e.typeText(Pos{0, 0}, "z");                    // not where the last key press ended
        CHECK(e.undoCount() == 5 && text(e.doc()) == "zabc!d");
        p = e.typeText(Pos{0, 1}, "y");                    // contiguous again, same class: merges
        CHECK(e.undoCount() == 5 && text(e.doc()) == "zyabc!d");
        e.undo();
        CHECK(text(e.doc()) == "abc!d");
        e.typeText(Pos{0, 5}, "e");                        // after an undo a run never continues
        CHECK(text(e.doc()) == "abc!de" && e.undoCount() == 5 && !e.canRedo());
        e.undo(); e.undo(); e.undo(); e.undo(); e.undo();
        CHECK(text(e.doc()).empty() && !e.canUndo());
    }
    {   // kinds of characters: letters+digits, blanks, other
        Editor e;
        Pos p{0, 0};
        const std::string s = "ab1  (( x";
        for (size_t i = 0; i < s.size(); ++i) p = e.typeText(p, std::string(1, s[i]));
        CHECK(e.undoCount() == 5);                         // "ab1", "  ", "((", " ", "x"
    }
    {   // multi-character and line-break input
        Editor e;
        Pos p = e.typeText(Pos{0, 0}, "foo");
        p = e.typeText(p, "bar");                          // same class, contiguous: merges
        CHECK(e.undoCount() == 1);
        p = e.typeText(p, "a b");                          // mixed classes: its own step, ends the run
        p = e.typeText(p, "c");
        CHECK(e.undoCount() == 3);
        p = e.typeText(p, "\n");
        CHECK(text(e.doc()) == "foobara bc\n" && e.undoCount() == 4);
        CHECK(e.typeText(p, "").line == p.line);
        CHECK_THROWS(e.typeText(Pos{9, 0}, "x"));
        CHECK(e.undoCount() == 4);
    }
    {   // inside a group nothing is merged
        Editor e;
        Pos p = e.typeText(Pos{0, 0}, "a");
        e.beginGroup("g");
        p = e.typeText(p, "b");
        e.endGroup();
        CHECK(e.undoCount() == 2);
        e.undo();
        CHECK(text(e.doc()) == "a");
    }
    {   // erase runs
        Editor e;
        e.insertText(Pos{0, 0}, "abcdefgh");
        e.breakTyping();
        for (int i = 0; i < 3; ++i) {                      // Backspace x3 from the end
            size_t c = e.doc().lineLength(0);
            e.eraseCoalescing(Pos{0, c - 1}, Pos{0, c});
        }
        CHECK(text(e.doc()) == "abcde" && e.undoCount() == 2);
        Pos where;
        e.undo(nullptr, &where);
        CHECK(text(e.doc()) == "abcdefgh" && where == (Pos{0, 8}));
        e.redo(nullptr, &where);
        CHECK(text(e.doc()) == "abcde" && where == (Pos{0, 5}));
        e.undo();
        for (int i = 0; i < 3; ++i) e.eraseCoalescing(Pos{0, 2}, Pos{0, 3});   // Delete x3 at column 2
        CHECK(text(e.doc()) == "abfgh" && e.undoCount() == 2);
        e.undo();
        CHECK(text(e.doc()) == "abcdefgh");
        e.redo();
        CHECK(text(e.doc()) == "abfgh");
        e.eraseCoalescing(Pos{0, 0}, Pos{0, 1});           // not touching the last deletion: new step
        CHECK(text(e.doc()) == "bfgh" && e.undoCount() == 3);
        CHECK_THROWS(e.eraseCoalescing(Pos{0, 0}, Pos{5, 0}));
        CHECK(text(e.doc()) == "bfgh" && e.undoCount() == 3);
        CHECK(e.eraseCoalescing(Pos{0, 1}, Pos{0, 1}).empty() && e.undoCount() == 3);   // empty range
        // a multi-line delete is its own step and ends the run
        Editor f;
        f.insertText(Pos{0, 0}, "ab\ncd");
        f.eraseCoalescing(Pos{0, 1}, Pos{1, 1});
        CHECK(text(f.doc()) == "ad" && f.undoCount() == 2);
        f.undo();
        CHECK(text(f.doc()) == "ab\ncd");
    }
}

static void testTypingRandom() {
    section("Typing runs: random stress against a model");
    unsigned seed = 4711;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    Editor e;
    e.setHistoryLimit(1000000);
    std::string model;
    size_t cur = 0;
    const char alphabet[] = "ab1 (_";
    for (int step = 0; step < 4000; ++step) {
        unsigned k = rnd() % 10;
        if (k < 5) {                                       // type a character at the cursor
            char c = alphabet[rnd() % 6];
            e.typeText(Pos{0, cur}, std::string(1, c));
            model.insert(cur, 1, c);
            ++cur;
        } else if (k < 7 && cur > 0) {                     // Backspace
            e.eraseCoalescing(Pos{0, cur - 1}, Pos{0, cur});
            model.erase(cur - 1, 1);
            --cur;
        } else if (k < 8 && cur < model.size()) {          // Delete
            e.eraseCoalescing(Pos{0, cur}, Pos{0, cur + 1});
            model.erase(cur, 1);
        } else if (k < 9) {                                // the user moves the cursor
            cur = model.empty() ? 0 : rnd() % (model.size() + 1);
            if (rnd() % 2) e.breakTyping();
        } else {                                           // undo then redo must be a round trip
            std::string before = model;
            Pos w;
            if (e.undo(nullptr, &w)) {
                CHECK(e.redo());
                CHECK(text(e.doc()) == before);
            }
        }
        CHECK(text(e.doc()) == model);
    }
    const std::string finalText = model;
    size_t steps = e.undoCount();
    CHECK(steps < 4000);                                   // runs really were merged
    while (e.undo()) {}
    CHECK(text(e.doc()).empty());
    while (e.redo()) {}
    CHECK(text(e.doc()) == finalText);
    CHECK(e.doc().checkInvariants());
}

static void testUndoWhere() {
    section("Undo / redo report where the cursor belongs");
    Editor e;
    e.insertText(Pos{0, 0}, "one\ntwo\nthree");
    Pos where;
    e.deleteRange(Pos{0, 1}, Pos{2, 2});                   // "oree"
    CHECK(text(e.doc()) == "oree");
    CHECK(e.undo(nullptr, &where));
    CHECK(text(e.doc()) == "one\ntwo\nthree" && where == (Pos{2, 2}));   // end of the restored text
    CHECK(e.redo(nullptr, &where));
    CHECK(where == (Pos{0, 1}));
    e.undo();
    e.undo(nullptr, &where);                               // the first insert is undone
    CHECK(text(e.doc()).empty() && where == (Pos{0, 0}));
    e.redo(nullptr, &where);
    CHECK(where == (Pos{2, 5}));                           // just after the re-inserted text
    e.replaceRange(Pos{0, 0}, Pos{0, 3}, "ONE!");
    CHECK(e.undo(nullptr, &where) && where == (Pos{0, 3}));
    CHECK(e.redo(nullptr, &where) && where == (Pos{0, 4}));
    CHECK(!e.redo(nullptr, &where));
    CHECK(e.undo());                                       // old calls without 'where' still work
}

// ------------------------------------------------------------------ phase 5: line commands

static std::string joinLines(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s.push_back('\n'); s += v[i]; }
    return s;
}

static void testMoveLines() {
    section("EditOps: move / duplicate lines");
    Editor e;
    e.insertText(Pos{0, 0}, "a\nb\nc\nd");
    e.breakTyping();
    size_t base = e.undoCount();
    CHECK(moveLines(e, 1, 1, -1));                         // b up
    CHECK(text(e.doc()) == "b\na\nc\nd" && e.undoCount() == base + 1);
    CHECK(!moveLines(e, 0, 0, -1));                        // already at the top
    CHECK(text(e.doc()) == "b\na\nc\nd" && e.undoCount() == base + 1);
    CHECK(moveLines(e, 1, 2, 1));                          // a,c down
    CHECK(text(e.doc()) == "b\nd\na\nc");
    CHECK(!moveLines(e, 2, 3, 1));                         // bottom
    CHECK(!moveLines(e, 1, 1, 0));
    CHECK_THROWS(moveLines(e, 2, 9, 1));
    CHECK_THROWS(moveLines(e, 3, 2, 1));
    CHECK(moveLines(e, 0, 3, -1) == false);
    e.undo();
    CHECK(text(e.doc()) == "b\na\nc\nd");                  // one Undo reverses one move
    e.undo();
    CHECK(text(e.doc()) == "a\nb\nc\nd");

    Editor f;                                              // empty lines and long lines
    f.insertText(Pos{0, 0}, "x\n\n  y  \n");
    CHECK(f.doc().lineCount() == 4);
    CHECK(moveLines(f, 1, 1, 1));
    CHECK(text(f.doc()) == "x\n  y  \n\n");
    CHECK(moveLines(f, 3, 3, -1));
    CHECK(text(f.doc()) == "x\n  y  \n\n");                // swapping two empty lines
    CHECK(f.doc().checkInvariants());

    Editor d;
    d.insertText(Pos{0, 0}, "p\nq\nr");
    duplicateLines(d, 0, 1);
    CHECK(text(d.doc()) == "p\nq\np\nq\nr");
    d.undo();
    CHECK(text(d.doc()) == "p\nq\nr");
    duplicateLines(d, 2, 2);                               // the last line
    CHECK(text(d.doc()) == "p\nq\nr\nr");
    CHECK_THROWS(duplicateLines(d, 0, 9));
}

static void testMoveLinesRandom() {
    section("EditOps: move lines random stress against a model");
    unsigned seed = 31337;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    Editor e;
    e.setHistoryLimit(1000000);
    std::vector<std::string> model;
    for (int i = 0; i < 12; ++i) model.push_back("line " + std::to_string(i) + std::string(rnd() % 5, 'x'));
    e.insertText(Pos{0, 0}, joinLines(model));
    std::vector<std::string> start = model;
    size_t steps0 = e.undoCount();
    for (int step = 0; step < 600; ++step) {
        size_t n = model.size();
        size_t from = rnd() % n;
        size_t to = from + rnd() % (n - from < 4 ? n - from : 4);
        int dir = (rnd() % 2) ? 1 : -1;
        bool expect = dir < 0 ? from > 0 : to + 1 < n;
        bool did = moveLines(e, from, to, dir);
        CHECK(did == expect);
        if (did) {
            std::vector<std::string> block(model.begin() + from, model.begin() + to + 1);
            model.erase(model.begin() + from, model.begin() + to + 1);
            size_t at = dir < 0 ? from - 1 : from + 1;
            model.insert(model.begin() + at, block.begin(), block.end());
        }
        CHECK(text(e.doc()) == joinLines(model));
    }
    CHECK(e.doc().checkInvariants());
    while (e.undoCount() > steps0) e.undo();
    CHECK(text(e.doc()) == joinLines(start));
}

static std::string linesOf(const Editor& e) { return text(e.doc()); }

static void testIndentAndComment() {
    section("EditOps: indent / outdent / comment");
    Editor e;
    e.insertText(Pos{0, 0}, "a\n\n  b\n\tc");
    DynArray<ColEdit> ed = indentLines(e, 0, 3, 4);
    CHECK(linesOf(e) == "    a\n\n      b\n    \tc");
    CHECK(ed.size() == 3);                                 // the empty line is skipped
    e.undo();
    CHECK(linesOf(e) == "a\n\n  b\n\tc");

    ed = outdentLines(e, 0, 3, 4);                         // a: nothing; b: 2 spaces; c: one tab
    CHECK(linesOf(e) == "a\n\nb\nc" && ed.size() == 2);
    e.undo();
    e.insertText(Pos{0, 0}, "        ");                   // 8 spaces before "a"
    e.breakTyping();
    ed = outdentLines(e, 0, 0, 4);
    CHECK(linesOf(e).substr(0, 5) == "    a");
    CHECK(outdentLines(e, 1, 1, 4).empty());               // nothing to remove: no history step
    size_t before = e.undoCount();
    indentLines(e, 1, 1, 4);                               // only an empty line
    CHECK(e.undoCount() == before);

    // adjustPos
    DynArray<ColEdit> a1;
    a1.push_back(ColEdit{2, 0, 4});
    CHECK(adjustPos(Pos{2, 0}, a1) == (Pos{2, 4}));
    CHECK(adjustPos(Pos{2, 7}, a1) == (Pos{2, 11}));
    CHECK(adjustPos(Pos{1, 7}, a1) == (Pos{1, 7}));
    DynArray<ColEdit> a2;
    a2.push_back(ColEdit{0, 2, -3});
    CHECK(adjustPos(Pos{0, 1}, a2) == (Pos{0, 1}));
    CHECK(adjustPos(Pos{0, 3}, a2) == (Pos{0, 2}));        // inside the removed text
    CHECK(adjustPos(Pos{0, 5}, a2) == (Pos{0, 2}));
    CHECK(adjustPos(Pos{0, 9}, a2) == (Pos{0, 6}));

    // comments
    Editor c;
    c.insertText(Pos{0, 0}, "int a;\n    int b;\n\n  // note\nint c;");
    bool commented = false;
    toggleLineComment(c, 0, 1, "//", &commented);
    CHECK(commented && linesOf(c) == "// int a;\n//     int b;\n\n  // note\nint c;");
    toggleLineComment(c, 0, 1, "//", &commented);
    CHECK(!commented && linesOf(c) == "int a;\n    int b;\n\n  // note\nint c;");
    c.undo();
    CHECK(linesOf(c).substr(0, 9) == "// int a;");
    c.undo();
    CHECK(linesOf(c).substr(0, 6) == "int a;");
    toggleLineComment(c, 1, 1, "//", &commented);          // keeps the indentation
    CHECK(commented && c.doc().getLine(1) == "    // int b;");
    toggleLineComment(c, 1, 4, "//", &commented);          // mixed: comment everything (blank skipped)
    CHECK(commented);                                      // smallest indentation (0) is used for all
    CHECK(c.doc().getLine(1) == "//     // int b;" && c.doc().getLine(3) == "//   // note");
    CHECK(c.doc().getLine(4) == "// int c;");
    CHECK(c.doc().getLine(2).empty());                     // blank line untouched
    c.undo();
    toggleLineComment(c, 3, 3, "//", &commented);          // "// note" without a space after
    CHECK(!commented && c.doc().getLine(3) == "  note");
    CHECK(toggleLineComment(c, 2, 2, "//", &commented).empty() && !commented);   // only a blank line
    CHECK_THROWS(toggleLineComment(c, 0, 0, ""));
    CHECK_THROWS(toggleLineComment(c, 0, 99));
    Editor py;
    py.insertText(Pos{0, 0}, "x = 1\ny = 2");
    toggleLineComment(py, 0, 1, "#", &commented);
    CHECK(commented && linesOf(py) == "# x = 1\n# y = 2");
    toggleLineComment(py, 0, 1, "#", &commented);
    CHECK(!commented && linesOf(py) == "x = 1\ny = 2");

    // new lines above / below
    Editor n;
    n.insertText(Pos{0, 0}, "  foo {\nbar");
    Pos p = insertLineBelow(n, 0, 4);
    CHECK(linesOf(n) == "  foo {\n      \nbar" && p == (Pos{1, 6}));    // one level deeper after '{'
    n.undo();
    p = insertLineBelow(n, 1, 4);
    CHECK(linesOf(n) == "  foo {\nbar\n" && p == (Pos{2, 0}));
    n.undo();
    p = insertLineAbove(n, 0, 4);
    CHECK(linesOf(n) == "  \n  foo {\nbar" && p == (Pos{0, 2}));
    n.undo();
    CHECK(linesOf(n) == "  foo {\nbar");
    CHECK_THROWS(insertLineBelow(n, 7));
    CHECK_THROWS(insertLineAbove(n, 7));
}

// ------------------------------------------------------------------ phase 5: smart typing

static void testSmartTyping() {
    section("EditOps: auto pairs, surround, smart Enter");
    CHECK(pairCloser('(') == ')' && pairCloser('[') == ']' && pairCloser('{') == '}');
    CHECK(pairCloser('"') == '"' && pairCloser('\'') == '\'' && pairCloser('`') == '`');
    CHECK(pairCloser('a') == 0 && pairCloser(')') == 0);

    Document d;
    d.insertText(Pos{0, 0}, "foo bar\nx = \nf(a)\ndon\n12\n");
    CHECK(shouldAutoPair(d, Pos{1, 4}, '('));              // end of line
    CHECK(shouldAutoPair(d, Pos{0, 3}, '('));              // before a blank
    CHECK(!shouldAutoPair(d, Pos{0, 4}, '('));             // in front of a word
    CHECK(shouldAutoPair(d, Pos{2, 3}, '['));              // before ')'
    CHECK(shouldAutoPair(d, Pos{1, 4}, '"'));
    CHECK(!shouldAutoPair(d, Pos{3, 3}, '\''));            // don't
    CHECK(!shouldAutoPair(d, Pos{4, 2}, '\''));            // 12'
    CHECK(!shouldAutoPair(d, Pos{0, 0}, 'a'));
    Document q;
    q.insertText(Pos{0, 0}, "\"\"");
    CHECK(!shouldAutoPair(q, Pos{0, 1}, '"'));             // between "" : no third quote
    CHECK(isEmptyPairAround(q, Pos{0, 1}));
    Document b;
    b.insertText(Pos{0, 0}, "()[]{} (x) ['] ");
    CHECK(isEmptyPairAround(b, Pos{0, 1}) && isEmptyPairAround(b, Pos{0, 3}) && isEmptyPairAround(b, Pos{0, 5}));
    CHECK(!isEmptyPairAround(b, Pos{0, 0}) && !isEmptyPairAround(b, Pos{0, 2}) && !isEmptyPairAround(b, Pos{0, 8}));
    CHECK(!isEmptyPairAround(b, Pos{0, 15}));              // end of line

    // surround
    Editor e;
    e.insertText(Pos{0, 0}, "alpha beta\ngamma");
    e.breakTyping();
    size_t steps = e.undoCount();
    SelRange r = surroundSelection(e, Pos{0, 6}, Pos{0, 10}, '(');
    CHECK(text(e.doc()) == "alpha (beta)\ngamma" && e.undoCount() == steps + 1);
    CHECK(r.start == (Pos{0, 7}) && r.end == (Pos{0, 11}));
    CHECK(e.doc().getRange(r.start, r.end) == "beta");
    r = surroundSelection(e, r.start, r.end, '"');         // wrapping again keeps the selection on the text
    CHECK(text(e.doc()) == "alpha (\"beta\")\ngamma");
    CHECK(e.doc().getRange(r.start, r.end) == "beta");
    e.undo(); e.undo();
    CHECK(text(e.doc()) == "alpha beta\ngamma");
    r = surroundSelection(e, Pos{1, 5}, Pos{0, 6}, '{'); // multi-line, reversed order
    CHECK(text(e.doc()) == "alpha {beta\ngamma}");
    CHECK(r.start == (Pos{0, 7}) && r.end == (Pos{1, 5}));
    CHECK(e.doc().getRange(r.start, r.end) == "beta\ngamma");
    e.undo();
    steps = e.undoCount();
    CHECK_THROWS(surroundSelection(e, Pos{0, 2}, Pos{0, 2}, '('));
    CHECK_THROWS(surroundSelection(e, Pos{0, 2}, Pos{0, 4}, 'x'));
    CHECK_THROWS(surroundSelection(e, Pos{0, 99}, Pos{0, 4}, '('));
    CHECK_THROWS(surroundSelection(e, Pos{0, 2}, Pos{7, 0}, '('));
    CHECK(text(e.doc()) == "alpha beta\ngamma" && e.undoCount() == steps);   // nothing half-done

    // toggle: the same bracket / quote again removes the pair
    bool changed = false;
    r = toggleSurround(e, Pos{0, 6}, Pos{0, 10}, '(', &changed);
    CHECK(changed && text(e.doc()) == "alpha (beta)\ngamma");
    CHECK(isWrappedBy(e.doc(), r.start, r.end, '(') && isWrappedBy(e.doc(), r.end, r.start, ')'));
    CHECK(!isWrappedBy(e.doc(), r.start, r.end, '[') && !isWrappedBy(e.doc(), r.start, r.end, '"'));
    steps = e.undoCount();
    r = toggleSurround(e, r.start, r.end, '(', &changed);
    CHECK(changed && text(e.doc()) == "alpha beta\ngamma" && e.doc().getRange(r.start, r.end) == "beta");
    CHECK(e.undoCount() == steps + 1);                     // one undo step
    e.undo();
    CHECK(text(e.doc()) == "alpha (beta)\ngamma");
    r = toggleSurround(e, Pos{0, 7}, Pos{0, 11}, ')', &changed);   // the closer removes it too
    CHECK(changed && text(e.doc()) == "alpha beta\ngamma" && e.doc().getRange(r.start, r.end) == "beta");
    r = toggleSurround(e, r.start, r.end, ')', &changed);           // closer, nothing to remove: no change
    CHECK(!changed && text(e.doc()) == "alpha beta\ngamma");
    r = toggleSurround(e, Pos{0, 6}, Pos{0, 10}, '"');
    r = toggleSurround(e, r.start, r.end, '(');                      // different pairs nest
    CHECK(text(e.doc()) == "alpha \"(beta)\"\ngamma");
    r = toggleSurround(e, r.start, r.end, '"');                      // not directly around: wraps again
    CHECK(text(e.doc()) == "alpha \"(\"beta\")\"\ngamma");
    r = toggleSurround(e, r.start, r.end, '"');
    r = toggleSurround(e, r.start, r.end, '(');
    r = toggleSurround(e, r.start, r.end, '"');
    CHECK(text(e.doc()) == "alpha beta\ngamma" && e.doc().getRange(r.start, r.end) == "beta");
    r = toggleSurround(e, Pos{0, 2}, Pos{1, 3}, '{');                // across lines, both ways
    CHECK(text(e.doc()) == "al{pha beta\ngam}ma");
    r = toggleSurround(e, r.end, r.start, '}');
    CHECK(text(e.doc()) == "alpha beta\ngamma" && e.doc().getRange(r.start, r.end) == "pha beta\ngam");
    CHECK(!isWrappedBy(e.doc(), Pos{0, 0}, Pos{0, 5}, '('));        // line edges
    CHECK(!isWrappedBy(e.doc(), Pos{0, 1}, Pos{1, 5}, '(') && !isWrappedBy(e.doc(), Pos{0, 1}, Pos{9, 0}, '('));
    CHECK_THROWS(unwrapSelection(e, Pos{0, 1}, Pos{0, 3}, '('));

    // smart Enter
    Editor n;
    n.insertText(Pos{0, 0}, "    if (x) {");
    Pos p = insertNewLineSmart(n, Pos{0, 12}, 4);
    CHECK(text(n.doc()) == "    if (x) {\n        " && p == (Pos{1, 8}));
    n.undo();
    CHECK(text(n.doc()) == "    if (x) {");
    n.breakTyping();
    n.deleteChars(Pos{0, 11}, 1);                           // remove '{'
    n.insertText(Pos{0, 11}, "{}");
    p = insertNewLineSmart(n, Pos{0, 12}, 4);              // between { and }
    CHECK(text(n.doc()) == "    if (x) {\n        \n    }" && p == (Pos{1, 8}));
    n.undo();
    CHECK(text(n.doc()) == "    if (x) {}");
    p = insertNewLineSmart(n, Pos{0, 2}, 4);               // inside the indentation
    CHECK(text(n.doc()) == "  \n  " "  if (x) {}" && p == (Pos{1, 2}));
    n.undo();
    p = insertNewLineSmart(n, Pos{0, 13}, 4);              // end of a line without an opener
    CHECK(text(n.doc()) == "    if (x) {}\n    " && p == (Pos{1, 4}));
    Editor t;
    t.insertText(Pos{0, 0}, "f(");
    p = insertNewLineSmart(t, Pos{0, 2}, 2);
    CHECK(text(t.doc()) == "f(\n  " && p == (Pos{1, 2}));
    CHECK_THROWS(insertNewLineSmart(t, Pos{0, 9}));
    CHECK_THROWS(insertNewLineSmart(t, Pos{5, 0}));

    CHECK(leadingWhitespace("  \t x") == "  \t ");
    CHECK(leadingWhitespace("x").empty() && leadingWhitespace("").empty());
    CHECK(smartHomeCol("    abc", 7) == 4);                // to the first non-blank
    CHECK(smartHomeCol("    abc", 4) == 0);                // then to column 0
    CHECK(smartHomeCol("    abc", 0) == 4);
    CHECK(smartHomeCol("abc", 2) == 0 && smartHomeCol("abc", 0) == 0);
}

// ------------------------------------------------------------------ phase 5: workspace

static void testFuzzy() {
    section("Workspace: fuzzy score and ranking");
    CHECK(fuzzyScore("", "anything") == 0);
    CHECK(fuzzyScore("xyz", "main.cpp") == -1);
    CHECK(fuzzyScore("maincppp", "main.cpp") == -1);       // longer than the candidate
    CHECK(fuzzyScore("pcm", "main.cpp") == -1);            // letters in the wrong order
    CHECK(fuzzyScore("MAIN", "main.cpp") >= 0);            // case does not matter
    CHECK(fuzzyScore("m a i n", "main.cpp") == fuzzyScore("main", "main.cpp"));   // blanks ignored
    CHECK(fuzzyScore("mw", "gui/MainWindow.cpp") > fuzzyScore("mw", "docs/memo_with_notes.txt"));
    CHECK(fuzzyScore("main", "main.cpp") > fuzzyScore("main", "domain.cpp"));      // start beats middle
    CHECK(fuzzyScore("edit", "engine/Editor.cpp") > fuzzyScore("edit", "tests/credits.txt"));
    CHECK(fuzzyScore("ed.h", "engine/Editor.h") >= 0);
    CHECK(fuzzyScore("gui/main", "gui\\main.cpp") >= 0);   // '\' counts as '/'
    CHECK(fuzzyScore("a", "") == -1);

    DynArray<std::string> files;
    const char* names[] = {"README.md", "engine/Editor.cpp", "engine/Editor.h", "engine/EditOps.cpp",
                           "gui/MainWindow.cpp", "gui/CodeEditor.cpp", "tests/test_main.cpp", "CMakeLists.txt"};
    for (size_t i = 0; i < 8; ++i) files.push_back(names[i]);
    DynArray<size_t> r = rankFiles(files, "editor", 3);
    CHECK(r.size() == 3);
    CHECK(files[r[0]] == "engine/Editor.h" || files[r[0]] == "engine/Editor.cpp" || files[r[0]] == "gui/CodeEditor.cpp");
    bool onlyMatches = true;
    for (size_t i = 0; i < r.size(); ++i) if (fuzzyScore("editor", files[r[i]]) < 0) onlyMatches = false;
    CHECK(onlyMatches);
    r = rankFiles(files, "mainwin", 5);
    CHECK(r.size() == 1 && files[r[0]] == "gui/MainWindow.cpp");
    r = rankFiles(files, "zzz", 5);
    CHECK(r.empty());
    r = rankFiles(files, "", 3);
    CHECK(r.size() == 3 && r[0] == 0 && r[2] == 2);        // empty query: first ones, in order
    CHECK(rankFiles(files, "e", 0).empty());
    r = rankFiles(files, "cpp", 100);
    CHECK(r.size() == 5);                                  // every .cpp file, nothing else

    // top-k equals sorting everything (brute force)
    unsigned seed = 99;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    DynArray<std::string> many;
    for (int i = 0; i < 300; ++i) {
        std::string s;
        int parts = 1 + rnd() % 3;
        for (int p = 0; p < parts; ++p) {
            int len = 2 + rnd() % 7;
            for (int k = 0; k < len; ++k) s.push_back(static_cast<char>((rnd() % 4 == 0 ? 'A' : 'a') + rnd() % 6));
            if (p + 1 < parts) s.push_back('/');
        }
        s += ".cpp";
        many.push_back(s);
    }
    for (int round = 0; round < 40; ++round) {
        std::string query;
        int ql = 1 + rnd() % 3;
        for (int k = 0; k < ql; ++k) query.push_back(static_cast<char>('a' + rnd() % 6));
        size_t k = 1 + rnd() % 10;
        DynArray<size_t> got = rankFiles(many, query, k);
        std::vector<size_t> all;                           // brute force: all matches, bubble-sorted
        for (size_t i = 0; i < many.size(); ++i) if (fuzzyScore(query, many[i]) >= 0) all.push_back(i);
        for (size_t i = 0; i < all.size(); ++i)
            for (size_t j = 0; j + 1 < all.size() - i; ++j) {
                size_t x = all[j], y = all[j + 1];
                int sx = fuzzyScore(query, many[x]), sy = fuzzyScore(query, many[y]);
                bool swap = sx != sy ? sx < sy : (many[x].size() != many[y].size() ? many[x].size() > many[y].size() : many[x] > many[y]);
                if (swap) { all[j] = y; all[j + 1] = x; }
            }
        size_t expect = all.size() < k ? all.size() : k;
        CHECK(got.size() == expect);
        for (size_t i = 0; i < got.size() && i < expect; ++i) CHECK(got[i] == all[i]);
    }
}

static void testListFiles() {
    section("Workspace: listing files");
    namespace fs = std::filesystem;
    const fs::path root = "engine_test_ws_tmp";
    fs::remove_all(root);
    fs::create_directories(root / "src" / "deep");
    fs::create_directories(root / ".git");
    fs::create_directories(root / "build");
    fs::create_directories(root / "node_modules" / "pkg");
    fs::create_directories(root / "cmake-build-debug");
    auto touch = [&](const fs::path& p) { std::ofstream(p) << "x"; };
    touch(root / "a.txt");
    touch(root / "src" / "main.cpp");
    touch(root / "src" / "deep" / "x.h");
    touch(root / ".gitignore");
    touch(root / ".git" / "config");
    touch(root / "build" / "out.txt");
    touch(root / "node_modules" / "pkg" / "i.js");
    touch(root / "cmake-build-debug" / "c.txt");
    touch(root / "logo.PNG");
    touch(root / "tool.exe");

    DynArray<std::string> f = listWorkspaceFiles(root.string());
    auto has = [&](const std::string& s) { for (size_t i = 0; i < f.size(); ++i) if (f[i] == s) return true; return false; };
    CHECK(f.size() == 4);
    CHECK(has("a.txt") && has("src/main.cpp") && has("src/deep/x.h") && has(".gitignore"));
    CHECK(!has(".git/config") && !has("build/out.txt") && !has("logo.PNG") && !has("tool.exe"));
    CHECK(listWorkspaceFiles(root.string(), 2).size() == 2);   // the cap
    CHECK(listWorkspaceFiles(root.string(), 0).empty());
    CHECK(listWorkspaceFiles("no_such_folder_98765").empty());
    CHECK(listWorkspaceFiles("").empty());
    CHECK(listWorkspaceFiles((root / "a.txt").string()).empty());   // a file, not a folder
    fs::remove_all(root);
}

static void testRenameFile() {
    section("Rename file");
    namespace fs = std::filesystem;
    const fs::path root = "engine_test_rn_tmp";
    fs::remove_all(root);
    fs::create_directories(root);
    auto touch = [&](const std::string& n, const char* c) { std::ofstream(root / n) << c; };
    touch("one.txt", "1");
    touch("two.txt", "2");
    const std::string one = (root / "one.txt").generic_string();

    std::string np;
    IOResult r = renameFile(one, "uno.txt", &np);
    CHECK(r.ok && np == (root / "uno.txt").generic_string());
    CHECK(!fs::exists(root / "one.txt") && fs::exists(root / "uno.txt"));
    r = renameFile(np, "two.txt");                         // would overwrite
    CHECK(!r.ok && fs::exists(root / "uno.txt") && fs::exists(root / "two.txt"));
    r = renameFile(np, "uno.txt");                         // same name
    CHECK(r.ok);
    r = renameFile(one, "x.txt");                          // the file is gone
    CHECK(!r.ok);
    const char* bad[] = {"", ".", "..", "a/b.txt", "a\\b.txt", "bad?.txt", "bad|name", "c:x", "x<y", "end.", "end ", "tab\there"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        r = renameFile(np, bad[i]);
        CHECK(!r.ok && fs::exists(root / "uno.txt"));
    }
    CHECK(!renameFile(np, std::string(300, 'a')).ok);
    CHECK(!renameFile("", "x.txt").ok);
    CHECK(!renameFile(root.generic_string(), "x.txt").ok); // a folder is not a file

    // through the Editor: path changes, text and history do not
    Editor e;
    CHECK(!e.renameFile("x.txt").ok);                      // no file yet
    CHECK(e.open((root / "two.txt").generic_string()).ok);
    e.insertText(Pos{0, 1}, "!");
    CHECK(e.doc().modified());
    CHECK(e.renameFile("zwei.txt").ok);
    CHECK(e.doc().path() == (root / "zwei.txt").generic_string());
    CHECK(e.doc().modified() && text(e.doc()) == "2!" && e.canUndo());
    CHECK(fs::exists(root / "zwei.txt") && !fs::exists(root / "two.txt"));
    CHECK(!e.renameFile("uno.txt").ok && e.doc().path() == (root / "zwei.txt").generic_string());
    CHECK(e.save(e.doc().path()).ok);
    fs::remove_all(root);
}

static void testFileManager() {
    section("File manager: create / rename / move / copy");
    namespace fs = std::filesystem;
    const fs::path root = "engine_test_fm_tmp";
    fs::remove_all(root);
    fs::create_directories(root / "src");
    std::ofstream(root / "src" / "main.cpp") << "int main(){}";
    const std::string R = root.generic_string();
    std::string np;

    // names
    CHECK(checkFileName("notes.txt").ok && checkFileName(".gitignore").ok && checkFileName("My File 2").ok);
    const char* bad[] = {"", ".", "..", "a/b", "a\\b", "q?", "p|q", "c:x", "<x>", "end.", "end ", "tab\there"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) CHECK(!checkFileName(bad[i]).ok);
    CHECK(!checkFileName(std::string(256, 'n')).ok);

    // create
    CHECK(createFile(R, "readme.md", &np).ok && np == R + "/readme.md" && fs::is_regular_file(root / "readme.md"));
    CHECK(fs::file_size(root / "readme.md") == 0);
    CHECK(!createFile(R, "readme.md").ok);                          // taken
    CHECK(!createFile(R, "src").ok);                                // a folder has that name
    CHECK(!createFile(R, "a/b.txt").ok && !fs::exists(root / "a"));
    CHECK(!createFile(R + "/nope", "x.txt").ok);                    // no such folder
    CHECK(createFolder(R, "docs", &np).ok && np == R + "/docs" && fs::is_directory(root / "docs"));
    CHECK(!createFolder(R, "docs").ok && !createFolder(R, "readme.md").ok);
    CHECK(createFile(R + "/", "x.txt", &np).ok && np == R + "/x.txt");   // trailing separator: no doubled '/'

    // rename files and folders
    CHECK(renamePath(R + "/docs", "doc", &np).ok && np == R + "/doc" && fs::is_directory(root / "doc"));
    CHECK(!renamePath(R + "/doc", "src").ok && fs::is_directory(root / "doc"));
    CHECK(renamePath(R + "/x.txt", "y.txt", &np).ok && fs::exists(root / "y.txt") && !fs::exists(root / "x.txt"));
    CHECK(renamePath(R + "/y.txt", "y.txt").ok);                    // same name
    CHECK(!renamePath(R + "/gone.txt", "z.txt").ok);
    CHECK(!renamePath(R + "/y.txt", "bad?.txt").ok);

    // move
    CHECK(movePath(R + "/y.txt", R + "/doc", &np).ok && np == R + "/doc/y.txt");
    CHECK(fs::exists(root / "doc" / "y.txt") && !fs::exists(root / "y.txt"));
    CHECK(movePath(R + "/doc/y.txt", R + "/doc", &np).ok && np == R + "/doc/y.txt");   // its own folder: no change
    std::ofstream(root / "y.txt") << "other";
    CHECK(!movePath(R + "/y.txt", R + "/doc").ok && fs::exists(root / "y.txt"));        // never overwrite
    CHECK(!movePath(R + "/doc", R + "/doc").ok);                                        // into itself
    fs::create_directories(root / "doc" / "inner");
    CHECK(!movePath(R + "/doc", R + "/doc/inner").ok && fs::is_directory(root / "doc"));
    CHECK(movePath(R + "/doc", R + "/src", &np).ok && np == R + "/src/doc" && fs::exists(root / "src" / "doc" / "y.txt"));
    CHECK(!movePath(R + "/nothing", R + "/src").ok && !movePath(R + "/y.txt", R + "/nothing").ok);

    // copy / duplicate
    CHECK(copyName("a.txt", 1) == "a copy.txt" && copyName("a.txt", 2) == "a copy 2.txt");
    CHECK(copyName(".gitignore", 1) == ".gitignore copy" && copyName("Makefile", 3) == "Makefile copy 3");
    CHECK(copyName("x.tar.gz", 1) == "x.tar copy.gz");
    CHECK(copyPath(R + "/y.txt", R, &np).ok && np == R + "/y copy.txt");                // duplicate in place
    CHECK(copyPath(R + "/y.txt", R, &np).ok && np == R + "/y copy 2.txt");
    { std::ifstream in(root / "y copy 2.txt"); std::string t; in >> t; CHECK(t == "other"); }
    CHECK(copyPath(R + "/src", R + "/backup_dest_missing").ok == false);
    CHECK(createFolder(R, "backup").ok);
    CHECK(copyPath(R + "/src", R + "/backup", &np).ok && np == R + "/backup/src");
    CHECK(fs::exists(root / "backup" / "src" / "main.cpp") && fs::exists(root / "backup" / "src" / "doc" / "y.txt"));
    CHECK(fs::exists(root / "src" / "main.cpp"));                                       // the original stays
    CHECK(!copyPath(R + "/src", R + "/src/doc").ok);                                    // into itself
    CHECK(copyPath(R + "/src", R, &np).ok && np == R + "/src copy" && fs::exists(root / "src copy" / "main.cpp"));

    // inside
    CHECK(isSameOrInside(R + "/src/doc/y.txt", R + "/src") && isSameOrInside(R + "/src", R + "/src/"));
    CHECK(isSameOrInside(R + "/src/../src/main.cpp", R + "/src"));
    CHECK(!isSameOrInside(R + "/src copy/main.cpp", R + "/src") && !isSameOrInside(R, R + "/src"));
    CHECK(isSameOrInside(R + "/src/not_yet.txt", R + "/src"));                          // need not exist
    fs::remove_all(root);
}

struct ConstHash {                       // every key in ONE chain: worst case on purpose
    size_t operator()(const std::string&) const { return 7; }
};
struct IntHash {
    size_t operator()(int k) const { return static_cast<size_t>(k) * 2654435761u; }
};


static void testHashTableBasics() {
    section("HashTable: basics");
    HashTable<std::string, int> t;
    CHECK(t.empty() && t.size() == 0 && t.bucketCount() == 16);
    CHECK(t.get("a") == nullptr && !t.contains("a") && !t.erase("a") && t.longestChain() == 0);
    CHECK(t.put("a", 1) && t.size() == 1 && t.contains("a"));
    CHECK(t.get("a") && *t.get("a") == 1);
    CHECK(!t.put("a", 2) && t.size() == 1 && *t.get("a") == 2);        // overwrite, not a new key
    CHECK(t.put("", 5) && t.contains("") && *t.get("") == 5);          // empty key is a key
    *t.get("a") = 9;                                                    // pointer is writable
    CHECK(*t.get("a") == 9);
    const HashTable<std::string, int>& ct = t;
    CHECK(ct.get("a") && *ct.get("a") == 9 && ct.get("zz") == nullptr);
    CHECK(t.erase("a") && !t.contains("a") && t.size() == 1 && !t.erase("a"));
    t.clear();
    CHECK(t.empty() && !t.contains("") && t.bucketCount() >= 16);
    CHECK(t.put("again", 1) && t.size() == 1);                          // usable after clear
    // keys that differ in one character / in case / with embedded NUL
    HashTable<std::string, int> u;
    u.put("for", 1); u.put("fori", 2); u.put("form", 3); u.put("For", 4);
    u.put(std::string("a\0b", 3), 5); u.put(std::string("a\0c", 3), 6);
    CHECK(u.size() == 6 && *u.get("for") == 1 && *u.get("For") == 4 && *u.get(std::string("a\0c", 3)) == 6);
    CHECK(u.get("a") == nullptr);
    // other key type
    HashTable<int, int, IntHash> h;
    for (int i = -50; i < 50; ++i) h.put(i, i * i);
    bool ok = h.size() == 100;
    for (int i = -50; i < 50; ++i) ok = ok && h.get(i) && *h.get(i) == i * i;
    CHECK(ok && h.get(50) == nullptr);
}

static void testHashTableCollisions() {
    section("HashTable: collisions, erase in a chain, rehash");
    HashTable<std::string, int, ConstHash> t;
    for (int i = 0; i < 40; ++i) t.put("k" + std::to_string(i), i);
    CHECK(t.size() == 40 && t.longestChain() == 40);                    // one long chain
    bool ok = true;
    for (int i = 0; i < 40; ++i) ok = ok && t.get("k" + std::to_string(i)) && *t.get("k" + std::to_string(i)) == i;
    CHECK(ok);
    CHECK(t.erase("k39") && t.erase("k0") && t.erase("k20"));            // head, tail and middle of the chain
    CHECK(!t.contains("k39") && !t.contains("k0") && !t.contains("k20") && t.size() == 37);
    ok = true;
    for (int i = 1; i < 39; ++i) if (i != 20) ok = ok && t.contains("k" + std::to_string(i));
    CHECK(ok);
    // rehash keeps everything and keeps the load factor under 0.75
    HashTable<std::string, int> g;
    size_t buckets0 = g.bucketCount();
    for (int i = 0; i < 1000; ++i) g.put("key" + std::to_string(i), i);
    CHECK(g.bucketCount() > buckets0 && g.loadFactor() <= 0.75 + 1e-9);
    size_t b = g.bucketCount();
    CHECK((b & (b - 1)) == 0);                                          // still a power of two
    ok = true;
    for (int i = 0; i < 1000; ++i) ok = ok && g.get("key" + std::to_string(i)) && *g.get("key" + std::to_string(i)) == i;
    CHECK(ok);
    // overwriting must not grow the table or change the size
    size_t before = g.bucketCount();
    for (int i = 0; i < 1000; ++i) g.put("key" + std::to_string(i), -i);
    CHECK(g.size() == 1000 && g.bucketCount() == before && *g.get("key7") == -7);
    // forEach visits every entry exactly once
    size_t visits = 0;
    long long sum = 0;
    g.forEach([&](const std::string&, const int& v) { ++visits; sum += v; });
    CHECK(visits == 1000 && sum == -(999LL * 1000 / 2));
}

static void testHashTableRandom() {
    section("HashTable: random operations against std::map");
    unsigned seed = 12345;
    auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 8) & 0xFFFFFF; };
    HashTable<std::string, int> t;
    std::map<std::string, int> model;
    bool same = true;
    for (int step = 0; step < 60000 && same; ++step) {
        std::string key = "w" + std::to_string(rnd() % 300);
        switch (rnd() % 4) {
            case 0: case 1: { int v = static_cast<int>(rnd()); bool fresh = model.find(key) == model.end(); model[key] = v; same = same && t.put(key, v) == fresh; break; }
            case 2: same = same && t.erase(key) == (model.erase(key) == 1); break;
            default: { auto it = model.find(key); const int* p = t.get(key); same = same && ((it == model.end()) == (p == nullptr)) && (!p || *p == it->second); break; }
        }
        same = same && t.size() == model.size();
    }
    CHECK(same);
    size_t seen = 0;
    bool all = true;
    t.forEach([&](const std::string& k, const int& v) { ++seen; auto it = model.find(k); all = all && it != model.end() && it->second == v; });
    CHECK(all && seen == model.size());
}

static void testHashTableLarge() {
    section("HashTable: 200 000 keys stay fast and balanced");
    HashTable<std::string, int> t;
    const int N = 200000;
    for (int i = 0; i < N; ++i) t.put("identifier_" + std::to_string(i), i);
    CHECK(t.size() == static_cast<size_t>(N));
    CHECK(t.longestChain() <= 12);                                      // FNV-1a spreads similar names well
    bool ok = true;
    for (int i = 0; i < N; i += 7) ok = ok && t.get("identifier_" + std::to_string(i)) && *t.get("identifier_" + std::to_string(i)) == i;
    CHECK(ok && !t.contains("identifier_" + std::to_string(N)));
    for (int i = 0; i < N; ++i) t.erase("identifier_" + std::to_string(i));
    CHECK(t.empty() && t.longestChain() == 0);
}

static void testSnippetRules() {
    section("Snippets: trigger / body rules and the table");
    CHECK(SnippetTable::validTrigger("for") && SnippetTable::validTrigger("_x1") && SnippetTable::validTrigger("A"));
    CHECK(SnippetTable::validTrigger(std::string(32, 'a')));
    CHECK(!SnippetTable::validTrigger("") && !SnippetTable::validTrigger(std::string(33, 'a')));
    CHECK(!SnippetTable::validTrigger("1a") && !SnippetTable::validTrigger("a b") && !SnippetTable::validTrigger("a-b"));
    CHECK(!SnippetTable::validTrigger("a\t") && !SnippetTable::validTrigger("a\n") && !SnippetTable::validTrigger(std::string("a\0", 2)));
    CHECK(SnippetTable::bodyProblem("ok").empty() && SnippetTable::bodyProblem("a$0b").empty() && SnippetTable::bodyProblem("$$0 and $0").empty());
    CHECK(!SnippetTable::bodyProblem("").empty());
    CHECK(!SnippetTable::bodyProblem(std::string(4001, 'x')).empty() && SnippetTable::bodyProblem(std::string(4000, 'x')).empty());
    CHECK(!SnippetTable::bodyProblem("a\rb").empty() && !SnippetTable::bodyProblem(std::string("a\0b", 3)).empty());
    CHECK(!SnippetTable::bodyProblem("$0 $0").empty());
    CHECK(SnippetTable::bodyProblem("$$0 $$0").empty());                // two literal "$0" are fine
    CHECK(SnippetTable::bodyProblem("cost: $").empty() && SnippetTable::bodyProblem("$5 $x").empty());

    SnippetTable s;
    CHECK(s.size() == 0 && s.find("x") == nullptr && s.triggers().empty() && !s.remove("x"));
    CHECK(s.add("x", "one") && s.size() == 1 && s.find("x")->body == "one" && s.find("x")->trigger == "x");
    CHECK(!s.add("x", "two") && s.size() == 1 && s.find("x")->body == "two");          // replace
    CHECK_THROWS(s.add("", "b"));
    CHECK_THROWS(s.add("bad name", "b"));
    CHECK_THROWS(s.add("ok", ""));
    CHECK_THROWS(s.add("ok", "$0$0"));
    CHECK(s.size() == 1 && s.find("ok") == nullptr);                    // failed adds changed nothing
    CHECK(s.remove("x") && s.size() == 0 && !s.remove("x"));

    // the 500 limit: replacing an existing one is still allowed when full
    for (size_t i = 0; i < SnippetTable::kMaxSnippets; ++i) s.add("t" + std::to_string(i), "b");
    CHECK(s.size() == SnippetTable::kMaxSnippets);
    bool threwLength = false;
    try { s.add("one_too_many", "b"); } catch (const std::length_error&) { threwLength = true; }
    CHECK(threwLength && s.size() == SnippetTable::kMaxSnippets && s.find("one_too_many") == nullptr);
    CHECK(!s.add("t0", "new body") && s.find("t0")->body == "new body");
    CHECK(s.remove("t1") && s.add("one_too_many", "b"));                // room again
    CHECK(s.longestChain() <= 10 && s.bucketCount() >= 512);

    // triggers() is sorted and complete
    SnippetTable d;
    d.add("zeta", "z"); d.add("Alpha", "a"); d.add("beta", "b"); d.add("_u", "u"); d.add("alpha", "a2");
    DynArray<std::string> k = d.triggers();
    CHECK(k.size() == 5 && k[0] == "Alpha" && k[1] == "_u" && k[2] == "alpha" && k[3] == "beta" && k[4] == "zeta");
    d.clear();
    CHECK(d.size() == 0 && d.find("beta") == nullptr);
}

static void testSnippetDefaults() {
    section("Snippets: built-in C++ snippets");
    SnippetTable s;
    s.loadDefaults();
    CHECK(s.size() >= 15);
    const char* must[] = {"for", "while", "if", "ifelse", "switch", "main", "class", "struct", "cout", "inc"};
    for (size_t i = 0; i < sizeof must / sizeof must[0]; ++i) CHECK(s.find(must[i]) != nullptr);
    size_t before = s.size();
    s.add("for", "custom $0");
    s.loadDefaults();                                                    // defaults replace, never duplicate
    CHECK(s.size() == before && s.find("for")->body != "custom $0");
    // every default is valid and expands without error
    DynArray<std::string> k = s.triggers();
    bool ok = true;
    for (size_t i = 0; i < k.size(); ++i) {
        try { ExpandedSnippet e = expandBody(s.find(k[i])->body, "  "); ok = ok && !e.text.empty() && e.text.find('\r') == std::string::npos; }
        catch (...) { ok = false; }
    }
    CHECK(ok);
}

static void testExpandBody() {
    section("Snippets: expandBody (cursor, indent, escapes)");
    ExpandedSnippet e = expandBody("abc", "");
    CHECK(e.text == "abc" && !e.hasCursor && e.cursorLine == 0 && e.cursorCol == 3);      // no $0: end of text
    e = expandBody("f($0)", "");
    CHECK(e.text == "f()" && e.hasCursor && e.cursorLine == 0 && e.cursorCol == 2);
    e = expandBody("if ($0) {\n    \n}", "  ");
    CHECK(e.text == "if () {\n      \n  }" && e.cursorLine == 0 && e.cursorCol == 4);
    e = expandBody("{\n    $0\n}", "\t");
    CHECK(e.text == "{\n\t    \n\t}" && e.cursorLine == 1 && e.cursorCol == 5);          // indent (1) + 4 spaces
    e = expandBody("a\n$0b", "  ");
    CHECK(e.text == "a\n  b" && e.cursorLine == 1 && e.cursorCol == 2);                  // $0 at the start of an indented line
    e = expandBody("a\n\nb", "    ");
    CHECK(e.text == "a\n\n    b");                                                       // blank line stays blank
    e = expandBody("a\n", "  ");
    CHECK(e.text == "a\n" && e.cursorLine == 1 && e.cursorCol == 0);                     // trailing newline: no indent
    e = expandBody("cost $$5 and $x and $", "");
    CHECK(e.text == "cost $5 and $x and $" && !e.hasCursor);
    e = expandBody("$$0", "");
    CHECK(e.text == "$0" && !e.hasCursor);                                               // escaped, so NOT a cursor
    e = expandBody("$0", "");
    CHECK(e.text.empty() && e.hasCursor && e.cursorLine == 0 && e.cursorCol == 0);
    e = expandBody("x$$$0y", "");
    CHECK(e.text == "x$y" && e.hasCursor && e.cursorCol == 2);
    CHECK_THROWS(expandBody("", ""));
    CHECK_THROWS(expandBody("$0 $0", ""));
    CHECK_THROWS(expandBody("a\rb", ""));
}

static void testSnippetFiles() {
    section("Snippets: save / load, bad files");
    const std::string path = "engine_snip_tmp.txt";
    SnippetTable a;
    a.add("fn", "void $0() {\n\treturn;\n}");
    a.add("back", "path C:\\dir\\n $$ done");                                          // backslash and "\n" text survive
    a.add("tab", "a\tb");
    a.add("dollar", "$$5");
    IOResult r = a.saveToFile(path);
    CHECK(r.ok);
    SnippetTable b;
    r = b.loadFromFile(path);
    CHECK(r.ok && b.size() == 4);
    CHECK(b.find("fn") && b.find("fn")->body == a.find("fn")->body);
    CHECK(b.find("back") && b.find("back")->body == a.find("back")->body);
    CHECK(b.find("tab") && b.find("tab")->body == "a\tb" && b.find("dollar")->body == "$$5");
    // full defaults round trip
    SnippetTable d, d2;
    d.loadDefaults();
    CHECK(d.saveToFile(path).ok && d2.loadFromFile(path).ok && d2.size() == d.size());
    DynArray<std::string> k = d.triggers();
    bool ok = true;
    for (size_t i = 0; i < k.size(); ++i) ok = ok && d2.find(k[i]) && d2.find(k[i])->body == d.find(k[i])->body;
    CHECK(ok);

    // messy file: BOM, CRLF, comments, blank lines, bad lines
    {
        std::ofstream f(path.c_str(), std::ios::binary);
        f << "\xEF\xBB\xBF# comment\r\n\r\ngood\tvalue $0\r\n"
             "no tab here\r\n"            // no TAB
             "1bad\tbody\r\n"             // bad trigger
             "two\ta $0 b $0\r\n"         // two cursors
             "esc\tbad \\q escape\r\n"    // bad escape
             "end\tlast line without newline";
    }
    SnippetTable m;
    r = m.loadFromFile(path);
    CHECK(r.ok && m.size() == 2 && m.find("good") && m.find("good")->body == "value $0");
    CHECK(m.find("end") && m.find("end")->body == "last line without newline");
    CHECK(r.message.find("Skipped 4") != std::string::npos && r.message.find("line 4") != std::string::npos);
    // a file over the limit is refused and changes nothing
    {
        std::ofstream f(path.c_str(), std::ios::binary);
        for (int i = 0; i < 40000; ++i) f << "x" << i << "\t" << std::string(30, 'b') << "\n";
    }
    SnippetTable big;
    big.add("keep", "me");
    r = big.loadFromFile(path);
    CHECK(!r.ok && big.size() == 1 && big.find("keep"));
    // empty file is fine, zero snippets
    { std::ofstream f(path.c_str(), std::ios::binary); }
    SnippetTable e;
    r = e.loadFromFile(path);
    CHECK(r.ok && e.size() == 0);
    std::remove(path.c_str());
    // errors: nothing is thrown, and the table is unchanged
    SnippetTable x;
    x.add("keep", "me");
    CHECK(!x.loadFromFile("").ok && !x.loadFromFile("no_such_dir/no_such_file.txt").ok && x.size() == 1);
    CHECK(!x.saveToFile("").ok && !x.saveToFile("no_such_dir/out.txt").ok);
    CHECK(!x.loadFromFile(".").ok && x.size() == 1);                                   // a folder is not a snippet file
}

static std::string allText(const Editor& e) { return text(e.doc()); }

static void testApplySnippet() {
    section("Snippets: applySnippet through the Editor (one undo step)");
    SnippetTable s;
    s.loadDefaults();
    Editor ed;
    ed.insertText(Pos{0, 0}, "for");
    size_t steps = ed.undoCount();
    SnippetExpansion r = applySnippet(ed, s, Pos{0, 3});
    CHECK(r.expanded && r.trigger == "for" && r.cursor == (Pos{0, 20}));
    CHECK(allText(ed) == "for (int i = 0; i < ; ++i) {\n    \n}");
    CHECK(ed.undoCount() == steps + 1 && ed.nextUndoLabel() == "snippet");
    CHECK(ed.undo() && allText(ed) == "for" && ed.redo() && ed.doc().lineCount() == 3);
    CHECK(ed.undo() && allText(ed) == "for");

    // indentation of the current line is added to the following lines; text after the trigger stays
    Editor e2;
    e2.insertText(Pos{0, 0}, "void f() {\n    if rest");
    r = applySnippet(e2, s, Pos{1, 6});
    CHECK(r.expanded && e2.doc().getLine(1) == "    if () {" && e2.doc().getLine(2) == "        " && e2.doc().getLine(3) == "    } rest");   // text after the trigger follows the last body line
    CHECK(r.cursor == (Pos{1, 8}));
    // the cursor lands on $0, also on a later line
    Editor e3;
    e3.insertText(Pos{0, 0}, "\t\tmain");
    r = applySnippet(e3, s, Pos{0, 6});
    CHECK(r.expanded && r.cursor == (Pos{1, 6}) && e3.doc().getLine(1) == "\t\t    " && e3.doc().getLine(0) == "\t\tint main() {");
    // inserting at the cursor reported really is valid and typing continues there
    e3.insertText(r.cursor, "X");
    CHECK(e3.doc().getLine(1) == "\t\t    X");

    // not expanded -> nothing changes, nothing is recorded
    Editor e4;
    e4.insertText(Pos{0, 0}, "xfor foo  for_ for");
    steps = e4.undoCount();
    const std::string snap = allText(e4);
    r = applySnippet(e4, s, Pos{0, 4});                                  // "xfor" is not a snippet
    CHECK(!r.expanded && r.trigger == "xfor" && !r.reason.empty());
    r = applySnippet(e4, s, Pos{0, 8});                                  // "foo"
    CHECK(!r.expanded && r.trigger == "foo");
    r = applySnippet(e4, s, Pos{0, 9});                                  // blank before the cursor
    CHECK(!r.expanded && r.trigger.empty());
    r = applySnippet(e4, s, Pos{0, 0});                                  // start of line
    CHECK(!r.expanded && r.trigger.empty());
    r = applySnippet(e4, s, Pos{0, 14});                                 // "for_" is not "for"
    CHECK(!r.expanded && r.trigger == "for_");
    r = applySnippet(e4, s, Pos{0, 16});                                 // after "for": cursor in the middle of the word "for"? no: "f" "or"
    CHECK(!r.expanded);
    CHECK(allText(e4) == snap && e4.undoCount() == steps);
    r = applySnippet(e4, s, Pos{0, 18});                                 // the last "for" does expand
    CHECK(r.expanded && e4.doc().getLine(0).compare(0, 17, "xfor foo  for_ fo") == 0);
    // bad cursor -> exception, nothing changes
    Editor e5;
    e5.insertText(Pos{0, 0}, "if");
    steps = e5.undoCount();
    CHECK_THROWS(applySnippet(e5, s, Pos{4, 0}));
    CHECK_THROWS(applySnippet(e5, s, Pos{0, 3}));
    CHECK(allText(e5) == "if" && e5.undoCount() == steps);
    // empty document, empty table
    Editor e6;
    CHECK(!applySnippet(e6, s, Pos{0, 0}).expanded && e6.doc().isEmpty());
    SnippetTable none;
    CHECK(!applySnippet(ed, none, Pos{0, 3}).expanded && allText(ed) == "for");
    // a very long word is simply "not a snippet"
    Editor e7;
    e7.insertText(Pos{0, 0}, std::string(5000, 'a'));
    r = applySnippet(e7, s, Pos{0, 5000});
    CHECK(!r.expanded && !r.reason.empty() && e7.doc().charCount() == 5000);
    // a replaced snippet body is used at once; a removed one stops working
    Editor e8;
    e8.insertText(Pos{0, 0}, "hi");
    s.add("hi", "hello $0!");
    r = applySnippet(e8, s, Pos{0, 2});
    CHECK(r.expanded && allText(e8) == "hello !" && r.cursor == (Pos{0, 6}));
    e8.undo();
    s.remove("hi");
    CHECK(!applySnippet(e8, s, Pos{0, 2}).expanded && allText(e8) == "hi");
    CHECK(ed.doc().checkInvariants() && e2.doc().checkInvariants() && e3.doc().checkInvariants() && e4.doc().checkInvariants());
}

static void testSnippetRandomEditing() {
    section("Snippets: random expand / undo / redo keeps the document consistent");
    unsigned seed = 777;
    auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 8) & 0xFFFFFF; };
    SnippetTable s;
    s.loadDefaults();
    DynArray<std::string> keys = s.triggers();
    Editor ed;
    bool consistent = true;
    for (int round = 0; round < 400 && consistent; ++round) {
        // type a trigger (maybe indented) at the end of a random line, then expand it
        size_t line = rnd() % ed.doc().lineCount();
        size_t len = ed.doc().lineLength(line);
        std::string pad = (len > 0 ? " " : "") + ((rnd() % 2) ? std::string(rnd() % 6, ' ') : std::string(rnd() % 3, '\t'));
        std::string trig = keys[rnd() % keys.size()];
        Pos at = ed.insertText(Pos{line, len}, pad + trig);
        std::string before = allText(ed);
        SnippetExpansion r = applySnippet(ed, s, at);
        consistent = consistent && r.expanded && ed.doc().checkInvariants();
        if (r.expanded) {
            consistent = consistent && r.cursor.line < ed.doc().lineCount() && r.cursor.col <= ed.doc().lineLength(r.cursor.line);
            std::string after = allText(ed);
            ed.undo();
            consistent = consistent && allText(ed) == before;
            ed.redo();
            consistent = consistent && allText(ed) == after;
        }
        if (ed.doc().charCount() > 20000) ed.newDocument();
    }
    CHECK(consistent && ed.doc().checkInvariants());
}


static void testDiagParse() {
    section("Diagnostics: compiler output parser");
    DynArray<Diagnostic> d = parseCompilerOutput("");
    CHECK(d.empty());
    d = parseCompilerOutput("a.cpp: In function 'int main()':\n"
                            "a.cpp:3:13: error: expected primary-expression before ';' token\n"
                            "    3 |   int x = ;\n"
                            "      |             ^\n"
                            "a.cpp:2:7: warning: unused variable 'y' [-Wunused-variable]\n"
                            "a.cpp:1:1: note: declared here\n");
    CHECK(d.size() == 3);
    CHECK(d[0].severity == Severity::Error && d[0].line == 3 && d[0].col == 13 && d[0].file == "a.cpp");
    CHECK(d[0].message == "expected primary-expression before ';' token" && d[0].source == "g++");
    CHECK(d[1].severity == Severity::Warning && d[1].line == 2 && d[1].col == 7);
    CHECK(d[2].severity == Severity::Note);
    // no column, fatal error
    d = parseCompilerOutput("x.cpp:9: error: bad thing\nx.cpp:1:10: fatal error: foo.h: No such file or directory\n");
    CHECK(d.size() == 2 && d[0].line == 9 && d[0].col == 0 && d[0].severity == Severity::Error);
    CHECK(d[1].line == 1 && d[1].col == 10 && d[1].message == "foo.h: No such file or directory");
    // Windows paths, CRLF, colours
    d = parseCompilerOutput("C:\\proj\\main.cpp:12:5: error: 'x' was not declared in this scope\r\n"
                            "C:/proj/b.cpp:4:1: warning: no return statement\r\n");
    CHECK(d.size() == 2 && d[0].file == "C:\\proj\\main.cpp" && d[0].line == 12 && d[0].col == 5);
    CHECK(d[1].file == "C:/proj/b.cpp" && d[1].severity == Severity::Warning);
    d = parseCompilerOutput("\x1b[01m\x1b[Ka.cpp:2:3:\x1b[m\x1b[K \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[Kboom\n");
    CHECK(d.size() == 1 && d[0].message == "boom" && d[0].line == 2 && d[0].col == 3);
    // lines that are not diagnostics, "required from here", numbers in messages
    d = parseCompilerOutput("a.cpp:5:3:   required from here\nnothing: to see here\n12:34: just numbers\n");
    CHECK(d.empty());
    d = parseCompilerOutput("a.cpp:5:3: error: expected ';' at 3:4 in 'a:b'\n");
    CHECK(d.size() == 1 && d[0].message == "expected ';' at 3:4 in 'a:b'");
    // linker and driver errors have no location
    d = parseCompilerOutput("/usr/bin/ld: m.o: in function `main':\nm.cpp:(.text+0x9): undefined reference to `foo()'\n"
                            "collect2: error: ld returned 1 exit status\n"
                            "g++: error: nofile.cpp: No such file or directory\n");
    CHECK(d.size() == 2);
    CHECK(d[0].line == 0 && d[0].severity == Severity::Error && d[0].message.find("undefined reference") == 0);
    CHECK(d[1].line == 0 && d[1].message == "nofile.cpp: No such file or directory");
    // clang flavour and a source-source label
    d = parseCompilerOutput("t.cpp:1:5: error: unknown type name 'foo'\n", "clang");
    CHECK(d.size() == 1 && d[0].source == "clang");
    CHECK(countSeverity(d, Severity::Error) == 1 && countSeverity(d, Severity::Warning) == 0);
    // flood protection
    std::string flood;
    for (int i = 0; i < 2000; ++i) flood += "f.cpp:" + std::to_string(i + 1) + ":1: error: e\n";
    d = parseCompilerOutput(flood);
    CHECK(d.size() == kMaxDiagnostics && d[0].line == 1);
}

static void testDiagBracketsAndSquiggle() {
    section("Diagnostics: brackets -> list, squiggle length");
    Document doc;
    doc.loadFromString("int main() {\n  foo(1;\n}\n");
    DynArray<Diagnostic> d = fromBrackets(checkBrackets(doc));
    CHECK(d.size() == 1 && d[0].source == "brackets" && d[0].severity == Severity::Error);
    CHECK(d[0].line == 2 && d[0].col == 6);                 // 1-based
    CHECK(!d[0].message.empty());
    doc.loadFromString("int main() { return 0; }\n");
    CHECK(fromBrackets(checkBrackets(doc)).empty());
    Document empty;
    CHECK(fromBrackets(checkBrackets(empty)).empty());

    CHECK(squiggleLength("int foo_1 = 5;", 4) == 5);       // identifier
    CHECK(squiggleLength("int foo_1 = 5;", 0) == 3);
    CHECK(squiggleLength("x = 12345;", 4) == 5);           // number
    CHECK(squiggleLength("a = \"he\\\"llo\";", 4) == 9);   // whole string, escaped quote inside
    CHECK(squiggleLength("a = \"open", 4) == 5);           // unterminated: to end of line
    CHECK(squiggleLength("a = ;", 4) == 1);                // punctuation
    CHECK(squiggleLength("a   b", 2) == 1);                // blank
    CHECK(squiggleLength("abc", 3) == 1 && squiggleLength("abc", 99) == 1 && squiggleLength("", 0) == 1);
}

static void testDiagSyntaxCheck() {
    section("Diagnostics: running g++ -fsyntax-only");
    CHECK(isCompilableSource("a.cpp") && isCompilableSource("B.H") && isCompilableSource("dir.v2/x.cc"));
    CHECK(!isCompilableSource("a.py") && !isCompilableSource("noext") && !isCompilableSource("a.cpp/readme"));
    CHECK(syntaxCheckCompiler("a.c") == "gcc" && syntaxCheckCompiler("a.cpp") == "g++");
    SyntaxCheckOptions custom;
    custom.compiler = "clang++";
    CHECK(syntaxCheckCompiler("a.c", custom) == "clang++");
    DynArray<std::string> args = syntaxCheckArgs("a.cpp");
    bool syn = false, plain = false;
    for (size_t i = 0; i < args.size(); ++i) { if (args[i] == "-fsyntax-only") syn = true; if (args[i] == "-std=c++17") plain = true; }
    CHECK(syn && plain && args[args.size() - 1] == "a.cpp");

    SyntaxCheckResult none = runSyntaxCheck("");
    CHECK(!none.ran && !none.message.empty());
    SyntaxCheckOptions bad;
    bad.compiler = "no_such_compiler_xyz";
    std::string dir = std::filesystem::temp_directory_path().string();
    std::string good = dir + "/diag_test_good.cpp", broken = dir + "/diag_test_bad.cpp";
    { std::ofstream(good) << "#include <cstdio>\nint main() { std::puts(\"ok\"); return 0; }\n"; }
    { std::ofstream(broken) << "int main() {\n  int x = ;\n  return 0;\n}\n"; }
    SyntaxCheckResult missing = runSyntaxCheck(good, bad);
    CHECK(!missing.ran && !missing.compilerFound && !missing.message.empty());
    SyntaxCheckResult ok = runSyntaxCheck(good);
    if (ok.compilerFound) {                                   // g++ may be absent on a test machine
        CHECK(ok.ran && ok.exitCode == 0 && countSeverity(ok.diagnostics, Severity::Error) == 0);
        SyntaxCheckResult bd = runSyntaxCheck(broken);
        CHECK(bd.ran && bd.exitCode != 0 && countSeverity(bd.diagnostics, Severity::Error) >= 1);
        bool onLine2 = false;
        for (size_t i = 0; i < bd.diagnostics.size(); ++i)
            if (bd.diagnostics[i].severity == Severity::Error && bd.diagnostics[i].line == 2) onLine2 = true;
        CHECK(onLine2);
    } else {
        std::cout << "  (g++ not installed: compiler run skipped)\n";
    }
    SyntaxCheckResult nofile = runSyntaxCheck(dir + "/diag_test_does_not_exist.cpp");
    if (nofile.compilerFound) CHECK(nofile.ran && countSeverity(nofile.diagnostics, Severity::Error) >= 1);
    std::remove(good.c_str());
    std::remove(broken.c_str());
}

int main() {
    testDynArray();
    testStack();
    testDocBasics();
    testDocDelete();
    testDocLines();
    testDocErrors();
    testLoadSave();
    testFiles();
    testRandom();
    testLarge();
    testUndoBasics();
    testUndoEachKind();
    testReplaceAndGroups();
    testUndoErrorsAndLimits();
    testUndoRandom();
    testKmpStrings();
    testDocSearch();
    testSearchRandom();
    testSearchLarge();
    testReplaceAll();
    testReplaceNthAndNext();
    testReplaceRollback();
    testReplaceRandom();
    testReplaceLarge();
    testStackPeek();
    testBracketsBasic();
    testBracketsRecovery();
    testBracketsIgnored();
    testBracketsIssuesKinds();
    testBracketsCursor();
    testBracketsRandom();
    testBracketsLarge();
    testTrieBasics();
    testTrieErrors();
    testTrieRandom();
    testTrieLarge();
    testAssist();
    testAssistRankingThreshold();
    testAssistRandomRanking();
    testApplyCompletion();
    testTypingRuns();
    testTypingRandom();
    testUndoWhere();
    testMoveLines();
    testMoveLinesRandom();
    testIndentAndComment();
    testSmartTyping();
    testFuzzy();
    testListFiles();
    testRenameFile();
    testFileManager();
    testHashTableBasics();
    testHashTableCollisions();
    testHashTableRandom();
    testHashTableLarge();
    testSnippetRules();
    testSnippetDefaults();
    testExpandBody();
    testSnippetFiles();
    testApplySnippet();
    testSnippetRandomEditing();
    testDiagParse();
    testDiagBracketsAndSquiggle();
    testDiagSyntaxCheck();

    std::cout << "\n" << (g_checks - g_failed) << "/" << g_checks << " checks passed";
    if (g_failed) std::cout << "  (" << g_failed << " FAILED)";
    std::cout << "\n";
    return g_failed ? 1 : 0;
}

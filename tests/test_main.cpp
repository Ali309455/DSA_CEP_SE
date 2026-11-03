// test_main.cpp - console test suite for the engine (no GUI needed).
// Build target: engine_tests.   Exit code 0 = all passed.

#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

#include "DynArray.h"
#include "Document.h"
#include "Editor.h"
#include "FileIO.h"
#include "Stack.h"

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

    std::cout << "\n" << (g_checks - g_failed) << "/" << g_checks << " checks passed";
    if (g_failed) std::cout << "  (" << g_failed << " FAILED)";
    std::cout << "\n";
    return g_failed ? 1 : 0;
}

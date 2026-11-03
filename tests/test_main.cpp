// test_main.cpp - console test suite for the engine (no GUI needed).
// Build target: engine_tests.   Exit code 0 = all passed.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "DynArray.h"
#include "Document.h"
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

    std::cout << "\n" << (g_checks - g_failed) << "/" << g_checks << " checks passed";
    if (g_failed) std::cout << "  (" << g_failed << " FAILED)";
    std::cout << "\n";
    return g_failed ? 1 : 0;
}

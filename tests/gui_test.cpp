// GUI smoke tests: the real MainWindow / CodeView, driven with simulated key presses.
// Runs without a screen:  QT_QPA_PLATFORM=offscreen ./gui_tests   (ctest sets this itself)
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include "CodeView.h"
#include "MainWindow.h"

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (!(cond)) { ++g_failed; std::cout << "  FAIL  line " << __LINE__ << ": " #cond << std::endl; } \
    } while (0)
#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (!((a) == (b))) { ++g_failed; std::cout << "  FAIL  line " << __LINE__ << ": " #a " == " #b << "\n        got: \"" << (a).toStdString() << "\"" << std::endl; } \
    } while (0)

static QString txt(CodeView* v) { return QString::fromStdString(v->editor().doc().toString()); }
static void setText(CodeView* v, const QString& s) {
    v->editor().newDocument();
    v->editor().insertText(Pos{0, 0}, s.toStdString());
    v->notifyEdited(0);
    v->setCursor(Pos{0, 0});
}
static void type(CodeView* v, const QString& s) { QTest::keyClicks(v, s); }
static void key(QWidget* w, Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::keyClick(w, k, m); }

static void testTyping() {
    std::cout << "[typing, auto pairs, wrapping]\n";
    CodeView v;
    v.resize(600, 400);
    v.show();
    type(&v, "hello world");
    CHECK_EQ(txt(&v), QString("hello world\n"));
    CHECK(v.editor().undoCount() == 3);                 // typing merges: "hello" " " "world"
    v.undo();
    CHECK_EQ(txt(&v), QString("hello \n"));
    v.redo();
    CHECK_EQ(txt(&v), QString("hello world\n"));

    setText(&v, "");
    type(&v, "f(");                                     // auto pair
    CHECK_EQ(txt(&v), QString("f()\n"));
    CHECK(v.cursor().col == 2);
    type(&v, "x)");                                     // typing ) skips the existing one
    CHECK_EQ(txt(&v), QString("f(x)\n"));
    CHECK(v.cursor().col == 4);
    key(&v, Qt::Key_Left);
    key(&v, Qt::Key_Left);
    key(&v, Qt::Key_Backspace);                         // ( x ) -> backspace after '(' with x inside: just removes '('
    CHECK_EQ(txt(&v), QString("fx)\n"));

    setText(&v, "");
    type(&v, "[");
    key(&v, Qt::Key_Backspace);                         // empty pair: both go
    CHECK_EQ(txt(&v), QString());
    type(&v, "don't");                                  // no pair after a letter
    CHECK_EQ(txt(&v), QString("don't\n"));
    setText(&v, "");
    type(&v, "\"hi\"");                                 // quote pairs, typed-over closer
    CHECK_EQ(txt(&v), QString("\"hi\"\n"));

    // wrapping a selection
    setText(&v, "abc def");
    v.select(Pos{0, 0}, Pos{0, 3});
    type(&v, "(");
    CHECK_EQ(txt(&v), QString("(abc) def\n"));
    CHECK_EQ(v.selectedText(), QString("abc"));         // selection stays on the original text
    type(&v, "\"");
    CHECK_EQ(txt(&v), QString("(\"abc\") def\n"));
    CHECK_EQ(v.selectedText(), QString("abc"));
    type(&v, "{");
    CHECK_EQ(txt(&v), QString("(\"{abc}\") def\n"));
    v.undo(); v.undo(); v.undo();
    CHECK_EQ(txt(&v), QString("abc def\n"));            // each wrap was one undo step
    v.select(Pos{0, 4}, Pos{0, 7});
    type(&v, "[");
    CHECK_EQ(txt(&v), QString("abc [def]\n"));
    v.select(Pos{0, 0}, Pos{0, 3});
    type(&v, "x");                                      // a normal character still replaces
    CHECK_EQ(txt(&v), QString("x [def]\n"));
    setText(&v, "one\ntwo");                            // wrapping across lines
    v.select(Pos{0, 1}, Pos{1, 2});
    type(&v, "{");
    CHECK_EQ(txt(&v), QString("o{ne\ntw}o\n"));
}

static void testEnterTabAndBackspace() {
    std::cout << "[Enter, Tab, Backspace]\n";
    CodeView v;
    v.show();
    setText(&v, "int main() {");
    v.setCursor(Pos{0, 12});
    key(&v, Qt::Key_Return);
    CHECK_EQ(txt(&v), QString("int main() {\n    \n"));
    CHECK(v.cursor().line == 1 && v.cursor().col == 4);
    type(&v, "}");
    setText(&v, "void f() {}");
    v.setCursor(Pos{0, 10});
    key(&v, Qt::Key_Return);                            // between { and }
    CHECK_EQ(txt(&v), QString("void f() {\n    \n}\n"));
    CHECK(v.cursor().line == 1 && v.cursor().col == 4);
    key(&v, Qt::Key_Backspace);                         // to the previous tab stop
    CHECK(v.cursor().col == 0);

    setText(&v, "a\nb\nc");
    v.select(Pos{0, 0}, Pos{2, 1});
    key(&v, Qt::Key_Tab);                               // indents every selected line
    CHECK_EQ(txt(&v), QString("    a\n    b\n    c\n"));
    key(&v, Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK_EQ(txt(&v), QString("a\nb\nc\n"));
    v.setCursor(Pos{0, 1});
    key(&v, Qt::Key_Tab);                               // no selection: spaces up to the next stop
    CHECK_EQ(txt(&v), QString("a   \nb\nc\n"));
    v.undo();
    CHECK_EQ(txt(&v), QString("a\nb\nc\n"));
}

static void testLineCommands() {
    std::cout << "[line commands]\n";
    CodeView v;
    v.show();
    setText(&v, "one\ntwo\nthree\nfour");
    v.setCursor(Pos{1, 2});
    v.moveLinesUp();                                    // Alt+Up
    CHECK_EQ(txt(&v), QString("two\none\nthree\nfour\n"));
    CHECK(v.cursor().line == 0 && v.cursor().col == 2); // the cursor moves with its line
    v.moveLinesUp();                                    // already first: nothing happens
    CHECK_EQ(txt(&v), QString("two\none\nthree\nfour\n"));
    v.moveLinesDown(); v.moveLinesDown();
    CHECK_EQ(txt(&v), QString("one\nthree\ntwo\nfour\n"));
    CHECK(v.cursor().line == 2);
    v.undo();
    CHECK_EQ(txt(&v), QString("one\ntwo\nthree\nfour\n"));   // one undo = one move

    v.select(Pos{1, 1}, Pos{2, 3});                     // two lines selected move together
    v.moveLinesDown();
    CHECK_EQ(txt(&v), QString("one\nfour\ntwo\nthree\n"));
    CHECK(v.selStart().line == 2 && v.selEnd().line == 3);
    CHECK_EQ(v.selectedText(), QString("wo\nthr"));
    v.moveLinesDown();                                  // bottom reached
    CHECK_EQ(txt(&v), QString("one\nfour\ntwo\nthree\n"));

    setText(&v, "a\nb\nc");
    v.setCursor(Pos{1, 0});
    v.copyLinesDown();                                  // Shift+Alt+Down
    CHECK_EQ(txt(&v), QString("a\nb\nb\nc\n"));
    CHECK(v.cursor().line == 2);
    v.deleteLine();                                     // Ctrl+Shift+K
    CHECK_EQ(txt(&v), QString("a\nb\nc\n"));
    v.setCursor(Pos{0, 0});
    v.newLineBelow();
    CHECK_EQ(txt(&v), QString("a\n\nb\nc\n"));
    CHECK(v.cursor().line == 1);

    setText(&v, "int a;\n    int b;");
    v.selectAll();
    v.toggleComment();                                  // Ctrl+/
    CHECK_EQ(txt(&v), QString("// int a;\n//     int b;\n"));
    v.toggleComment();
    CHECK_EQ(txt(&v), QString("int a;\n    int b;\n"));
    v.setCursor(Pos{1, 4});
    v.toggleComment();
    CHECK_EQ(txt(&v), QString("int a;\n    // int b;\n"));
    CHECK(v.cursor().col == 7);                         // the cursor kept its place in the text
    v.undo(); v.undo();                                 // (comment, then nothing before it) must not crash
}

static void testClipboardAndNav() {
    std::cout << "[clipboard, navigation]\n";
    CodeView v;
    v.show();
    setText(&v, "alpha beta\ngamma");
    v.select(Pos{0, 6}, Pos{0, 10});
    v.copy();
    v.setCursor(Pos{1, 5});
    v.paste();
    CHECK_EQ(txt(&v), QString("alpha beta\ngammabeta\n"));
    v.undo();
    CHECK_EQ(txt(&v), QString("alpha beta\ngamma\n"));
    v.setCursor(Pos{1, 0});
    v.cut();                                            // no selection: the whole line
    CHECK_EQ(txt(&v), QString("alpha beta\n"));
    v.paste();                                          // a copied line is pasted above the cursor line
    CHECK_EQ(txt(&v), QString("gamma\nalpha beta\n"));
    CHECK(v.cursor().line == 1);
    v.setCursor(Pos{1, 5});
    v.paste();                                          // ... even if the cursor is in the middle of a line
    CHECK_EQ(txt(&v), QString("gamma\ngamma\nalpha beta\n"));
    CHECK(v.cursor().line == 2 && v.cursor().col == 5);

    setText(&v, "foo bar_baz  qux");
    v.setCursor(Pos{0, 0});
    key(&v, Qt::Key_Right, Qt::ControlModifier);
    CHECK(v.cursor().col == 4);
    key(&v, Qt::Key_Right, Qt::ControlModifier);
    CHECK(v.cursor().col == 13);
    key(&v, Qt::Key_Left, Qt::ControlModifier);
    CHECK(v.cursor().col == 4);
    key(&v, Qt::Key_End, Qt::ShiftModifier);
    CHECK_EQ(v.selectedText(), QString("bar_baz  qux"));
    key(&v, Qt::Key_Home);
    CHECK(v.cursor().col == 0 && !v.hasSelection());
    key(&v, Qt::Key_Delete, Qt::ControlModifier);
    CHECK_EQ(txt(&v), QString("bar_baz  qux\n"));
    v.setCursor(Pos{0, 12});
    key(&v, Qt::Key_Backspace, Qt::ControlModifier);
    CHECK_EQ(txt(&v), QString("bar_baz  \n"));

    setText(&v, "héllo wörld");                         // UTF-8: columns are bytes, the cursor moves by characters
    v.setCursor(Pos{0, 0});
    key(&v, Qt::Key_Right); key(&v, Qt::Key_Right);
    CHECK(v.cursor().col == 3);
    key(&v, Qt::Key_Backspace);
    CHECK_EQ(txt(&v), QString("hllo wörld\n"));
}

static void testAutocomplete() {
    std::cout << "[autocomplete]\n";
    CodeView v;
    v.show();
    setText(&v, "");
    type(&v, "retu");
    QTest::qWait(400);                                  // the popup appears after a short pause
    key(&v, Qt::Key_Return);                            // accept the first suggestion
    CHECK(txt(&v).startsWith("return"));
    CHECK(v.editor().doc().lineCount() == 1);           // Enter was used for the popup, not as a new line
}

static void testBracketReport() {
    std::cout << "[bracket report]\n";
    CodeView v;
    v.show();
    setText(&v, "int f( {\n");
    QTest::qWait(500);
    CHECK(v.bracketsReady() && v.bracketIssueCount() > 0);
    Pos p;
    CHECK(v.firstBracketIssuePos(p));
    setText(&v, "int f() {}\n");
    QTest::qWait(500);
    CHECK(v.bracketsReady() && v.bracketIssueCount() == 0);
}

static void testTabsAndFiles() {
    std::cout << "[tabs, shortcuts, files, rename, find]\n";
    QTemporaryDir dir;
    CHECK(dir.isValid());
    auto write = [&](const char* name, const char* body) {
        QFile f(dir.filePath(name));
        f.open(QIODevice::WriteOnly);
        f.write(body);
        return dir.filePath(name);
    };
    const QString a = write("a.cpp", "int a = 1;\nint a2 = 2;\n");
    const QString b = write("b.cpp", "int b;\n");
    const QString c = write("c.txt", "plain\n");

    MainWindow w;
    w.show();
    QApplication::setActiveWindow(&w);
    (void)QTest::qWaitForWindowActive(&w);
    CHECK(w.tabCount() == 1);
    CHECK(w.openPath(a));                               // replaces the untouched Untitled tab
    CHECK(w.tabCount() == 1 && w.tabTitle(0) == "a.cpp");
    CHECK(w.openPath(b));
    CHECK(w.openPath(c));
    CHECK(w.tabCount() == 3);
    CHECK(w.openPath(a));                               // already open: switch, no second tab
    CHECK(w.tabCount() == 3 && w.currentView() == w.viewAt(0));

    // Ctrl+Tab / Ctrl+Shift+Tab, as a real key press through the shortcut system
    QTest::keyClick(w.currentView(), Qt::Key_Tab, Qt::ControlModifier);
    CHECK(w.currentView() == w.viewAt(1));
    QTest::keyClick(w.currentView(), Qt::Key_Tab, Qt::ControlModifier);
    CHECK(w.currentView() == w.viewAt(2));
    QTest::keyClick(w.currentView(), Qt::Key_Tab, Qt::ControlModifier);
    CHECK(w.currentView() == w.viewAt(0));              // wraps around
    QTest::keyClick(w.currentView(), Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(w.currentView() == w.viewAt(2));
    QTest::keyClick(w.currentView(), Qt::Key_1, Qt::AltModifier);
    CHECK(w.currentView() == w.viewAt(0));
    QTest::keyClick(w.currentView(), Qt::Key_9, Qt::AltModifier);
    CHECK(w.currentView() == w.viewAt(2));
    w.nextTab(+1);
    CHECK(w.currentView() == w.viewAt(0));

    // Alt+Down through the shortcut system moves the line in the CURRENT tab
    w.currentView()->setCursor(Pos{0, 0});
    QTest::keyClick(w.currentView(), Qt::Key_Down, Qt::AltModifier);
    CHECK_EQ(txt(w.currentView()), QString("int a2 = 2;\nint a = 1;\n"));
    CHECK(w.tabTitle(0).contains(QString::fromUtf8("●")));          // modified marker on the tab
    QTest::keyClick(w.currentView(), Qt::Key_Z, Qt::ControlModifier);
    CHECK_EQ(txt(w.currentView()), QString("int a = 1;\nint a2 = 2;\n"));
    QTest::keyClick(w.currentView(), Qt::Key_S, Qt::ControlModifier);
    CHECK(!w.tabTitle(0).contains(QString::fromUtf8("●")));

    // find
    w.showFind(true);
    w.findField()->setText("int a");
    CHECK_EQ(w.findCount()->text(), QString("1 of 2"));
    CHECK_EQ(w.currentView()->selectedText(), QString("int a"));
    w.findNext(false);
    CHECK(w.currentView()->selStart().line == 1);
    w.findNext(false);
    CHECK(w.currentView()->selStart().line == 0);       // wraps
    w.findNext(true);
    CHECK(w.currentView()->selStart().line == 1);       // backwards, wraps
    // replace one / replace all (the slots are private, so call them by name)
    w.currentView()->setCursor(Pos{0, 0});
    w.showFind(true);
    w.findField()->setText("int a");
    w.findField()->parentWidget()->findChildren<QLineEdit*>()[1]->setText("long x");
    QMetaObject::invokeMethod(&w, "replaceOne");
    CHECK_EQ(txt(w.currentView()), QString("long x = 1;\nint a2 = 2;\n"));
    CHECK_EQ(w.currentView()->selectedText(), QString("int a"));      // the next match is selected
    QMetaObject::invokeMethod(&w, "replaceEverything");
    CHECK_EQ(txt(w.currentView()), QString("long x = 1;\nlong x2 = 2;\n"));
    CHECK_EQ(w.findCount()->text(), QString("1 replaced"));
    w.currentView()->undo();                                          // replace all is ONE undo step
    CHECK_EQ(txt(w.currentView()), QString("long x = 1;\nint a2 = 2;\n"));
    w.currentView()->undo();
    CHECK_EQ(txt(w.currentView()), QString("int a = 1;\nint a2 = 2;\n"));
    QTest::keyClick(w.currentView(), Qt::Key_S, Qt::ControlModifier);    // back to the saved text
    w.findField()->setText("zzz");
    CHECK_EQ(w.findCount()->text(), QString("No results"));
    QTest::keyClick(w.findField(), Qt::Key_Escape);
    CHECK(!w.findField()->isVisible());

    // rename
    QString err;
    CHECK(w.renameCurrentFile("alpha.cpp", &err));
    CHECK(w.tabTitle(0) == "alpha.cpp");
    CHECK(QFile::exists(dir.filePath("alpha.cpp")) && !QFile::exists(a));
    CHECK(!w.renameCurrentFile("b.cpp", &err) && !err.isEmpty());   // would overwrite
    CHECK(w.tabTitle(0) == "alpha.cpp" && QFile::exists(dir.filePath("b.cpp")));
    CHECK(!w.renameCurrentFile("bad/name.cpp", &err));
    w.currentView()->setCursor(Pos{0, 0});
    type(w.currentView(), "// ");
    CHECK(w.renameCurrentFile("beta.cpp", &err));       // renaming keeps unsaved edits and history
    CHECK(w.currentView()->editor().doc().modified() && w.currentView()->editor().canUndo());
    QTest::keyClick(w.currentView(), Qt::Key_S, Qt::ControlModifier);
    QFile saved(dir.filePath("beta.cpp"));
    saved.open(QIODevice::ReadOnly);
    CHECK(QString::fromUtf8(saved.readAll()).startsWith("// int a = 1;"));

    // closing tabs
    CHECK(w.closeTab(2, true));
    CHECK(w.tabCount() == 2);
    CHECK(w.closeTab(1, true));
    CHECK(w.closeTab(0, true));
    CHECK(w.tabCount() == 1);                           // there is always one tab

    // window picture for a quick look at the result
    w.openPath(dir.filePath("beta.cpp"));
    w.currentView()->setCursor(Pos{0, 4});
    w.resize(900, 520);
    const QString shot = qEnvironmentVariable("DEVPAD_SCREENSHOT");
    if (!shot.isEmpty()) w.grab().save(shot);
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", qEnvironmentVariableIsSet("QT_QPA_PLATFORM") ? qgetenv("QT_QPA_PLATFORM") : QByteArray("offscreen"));
    QApplication app(argc, argv);
    testTyping();
    testEnterTabAndBackspace();
    testLineCommands();
    testClipboardAndNav();
    testAutocomplete();
    testBracketReport();
    testTabsAndFiles();
    std::cout << (g_checks - g_failed) << "/" << g_checks << " GUI checks passed";
    if (g_failed) std::cout << "  (" << g_failed << " FAILED)";
    std::cout << std::endl;
    return g_failed ? 1 : 0;
}

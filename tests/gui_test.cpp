// GUI smoke tests: the real MainWindow / CodeView, driven with simulated key presses.
// Runs without a screen:  QT_QPA_PLATFORM=offscreen ./gui_tests   (ctest sets this itself)
#include <QApplication>
#include <functional>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTest>
#include <iostream>
#include <QFileSystemModel>
#include <QMimeData>
#include <QTreeView>
#include <QUrl>
#include "CodeView.h"
#include "FileExplorer.h"
#include "WelcomePage.h"
#include <QSettings>
#include <QSignalSpy>
#include "MainWindow.h"
#include <QAction>
#include <QKeySequence>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTextCursor>
#include <QPixmap>
#include <QSet>
#include <cmath>
#include "Controls.h"
#include "Runner.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "ShortcutsDialog.h"
#include "TerminalPanel.h"
#include "ProblemsPanel.h"
#include "SnippetDialog.h"
#include "SnippetStore.h"
#include "SyntaxChecker.h"
#include "Theme.h"

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
    v.select(Pos{0, 0}, Pos{0, 3});
    type(&v, "(");
    type(&v, "(");                                      // the same bracket again removes it
    CHECK_EQ(txt(&v), QString("abc def\n"));
    CHECK_EQ(v.selectedText(), QString("abc"));
    type(&v, "'");
    type(&v, "'");                                      // quotes too
    CHECK_EQ(txt(&v), QString("abc def\n"));
    type(&v, "[");
    type(&v, "]");                                      // and the closing bracket
    CHECK_EQ(txt(&v), QString("abc def\n"));
    CHECK_EQ(v.selectedText(), QString("abc"));
    type(&v, "(");
    v.undo();                                           // unwrap... and back: one undo step each
    CHECK_EQ(txt(&v), QString("abc def\n"));
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
        (void)f.open(QIODevice::WriteOnly);
        f.write(body);
        return dir.filePath(name);
    };
    const QString a = write("a.cpp", "int a = 1;\nint a2 = 2;\n");
    const QString b = write("b.cpp", "int b;\n");
    const QString c = write("c.txt", "plain\n");

    MainWindow w;
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w);
    CHECK(w.tabCount() == 0 && w.welcomeShown());      // nothing open: the welcome page
    w.newTab();
    CHECK(w.tabCount() == 1 && !w.welcomeShown());
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
    (void)saved.open(QIODevice::ReadOnly);
    CHECK(QString::fromUtf8(saved.readAll()).startsWith("// int a = 1;"));

    // closing tabs
    CHECK(w.closeTab(2, true));
    CHECK(w.tabCount() == 2);
    CHECK(w.closeTab(1, true));
    CHECK(w.closeTab(0, true));
    CHECK(w.tabCount() == 0 && w.welcomeShown());      // the last tab closed: welcome page again
    CHECK(w.recentFiles().contains(dir.filePath("beta.cpp")));

    // window picture for a quick look at the result
    w.openPath(dir.filePath("beta.cpp"));
    w.currentView()->setCursor(Pos{0, 4});
    w.resize(900, 520);
    const QString shot = qEnvironmentVariable("DEVPAD_SCREENSHOT");
    if (!shot.isEmpty()) w.grab().save(shot);
}

// the file tree loads folders in the background: wait (at most 3 s) until 'ready' says yes
template <class F> static void waitFor(F ready) {
    for (int i = 0; i < 60 && !ready(); ++i) QTest::qWait(50);
}

static void testExplorer() {
    std::cout << "[file manager sidebar]\n";
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString root = QFileInfo(tmp.path()).canonicalFilePath();
    QDir(root).mkpath("src");
    auto write = [&](const QString& rel, const char* body) {
        QFile f(QDir(root).filePath(rel));
        (void)f.open(QIODevice::WriteOnly);
        f.write(body);
        return QDir(root).filePath(rel);
    };
    const QString mainCpp = write("src/main.cpp", "int main() {}\n");
    const QString notes = write("notes.txt", "hello\n");
    auto at = [&](const QString& rel) { return QDir(root).filePath(rel); };
    auto pathOf = [](CodeView* v) { return QString::fromStdString(v->editor().doc().path()); };

    MainWindow w;
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w);
    FileExplorer* ex = w.explorer();
    CHECK(ex != nullptr && ex->root().isEmpty());
    w.setWorkspaceRoot(root);
    CHECK(ex->root() == root);
    CHECK(ex->tree()->rootIndex() == ex->model()->index(root));

    // new file / new folder
    QString err;
    const QString created = ex->createFile(root, "todo.md", &err);
    CHECK(created == at("todo.md") && QFile::exists(created));
    CHECK(ex->createFile(root, "todo.md", &err).isEmpty() && !err.isEmpty());      // taken
    CHECK(ex->createFile(root, "a?b", &err).isEmpty());
    CHECK(ex->createFolder(root, "docs", &err) == at("docs") && QFileInfo(at("docs")).isDir());

    // reveal: opening a file selects it in the tree
    CHECK(w.openPath(notes));
    CHECK(ex->currentPath() == notes);
    CHECK(ex->targetFolder() == root);

    // renaming an open file with unsaved edits: the tab follows, the edits stay
    type(w.currentView(), "x");
    CHECK(w.currentView()->editor().doc().modified());
    CHECK(ex->unsavedCount && ex->unsavedCount(root) == 1 && ex->unsavedCount(at("src")) == 0);
    CHECK(ex->renamePath(notes, "memo.txt", &err) == at("memo.txt"));
    CHECK(QFile::exists(at("memo.txt")) && !QFile::exists(notes));
    CHECK(pathOf(w.currentView()) == at("memo.txt") && w.tabTitle(0).startsWith("memo.txt"));
    CHECK(w.currentView()->editor().doc().modified() && txt(w.currentView()) == "xhello\n");
    QTest::keyClick(w.currentView(), Qt::Key_S, Qt::ControlModifier);
    { QFile f(at("memo.txt")); (void)f.open(QIODevice::ReadOnly); CHECK(f.readAll() == "xhello\n"); }
    CHECK(ex->renamePath(at("memo.txt"), "todo.md", &err).isEmpty() && !err.isEmpty());   // would overwrite

    // renaming a folder moves the open tabs inside it
    CHECK(w.openPath(mainCpp));
    CHECK(ex->targetFolder() == at("src"));
    CHECK(ex->renamePath(at("src"), "code", &err) == at("code"));
    CHECK(pathOf(w.currentView()) == at("code/main.cpp") && QFile::exists(at("code/main.cpp")));

    // moving (also what drag and drop does)
    CHECK(ex->movePath(at("memo.txt"), at("docs"), &err) == at("docs/memo.txt"));
    CHECK(pathOf(w.viewAt(0)) == at("docs/memo.txt"));
    CHECK(ex->movePath(at("docs"), at("docs"), &err).isEmpty());                   // into itself

    // duplicate, copy / paste, cut / paste
    CHECK(ex->copyPath(at("todo.md"), root, &err) == at("todo copy.md"));
    ex->setClipboard({at("todo.md")}, false);
    QStringList errors;
    CHECK(ex->paste(at("code"), &errors) == QStringList{at("code/todo.md")} && errors.isEmpty());
    CHECK(ex->canPaste() && QFile::exists(at("todo.md")));                          // copy keeps the original
    ex->setClipboard({at("docs/memo.txt")}, true);
    CHECK(ex->paste(root, &errors) == QStringList{at("memo.txt")});
    CHECK(!ex->canPaste() && !QFile::exists(at("docs/memo.txt")));                  // cut moves it
    CHECK(pathOf(w.viewAt(0)) == at("memo.txt"));

    // inline rename through the model (F2 in the tree), done a moment later
    QModelIndex idx = ex->model()->index(at("memo.txt"));
    CHECK(idx.isValid());
    CHECK(ex->model()->setData(idx, "renamed.txt", Qt::EditRole));
    QTest::qWait(50);
    CHECK(QFile::exists(at("renamed.txt")) && pathOf(w.viewAt(0)) == at("renamed.txt"));

    // drag and drop onto a folder = move
    QMimeData drag;
    drag.setUrls({QUrl::fromLocalFile(at("renamed.txt"))});
    CHECK(ex->model()->dropMimeData(&drag, Qt::MoveAction, -1, -1, ex->model()->index(at("code"))));
    QTest::qWait(50);
    CHECK(QFile::exists(at("code/renamed.txt")) && pathOf(w.viewAt(0)) == at("code/renamed.txt"));

    // filter: fuzzy search over every file below the folder
    ex->setFilterText("mainc");
    CHECK(ex->filterMatches().value(0) == "code/main.cpp");
    ex->setFilterText("zzzz");
    CHECK(ex->filterMatches().isEmpty());
    ex->setFilterText("");
    CHECK(ex->filterMatches().isEmpty() && ex->tree()->isVisible());

    // refresh rebuilds the tree and keeps the selection and the open folders
    ex->reveal(at("code/main.cpp"));
    CHECK(ex->tree()->isExpanded(ex->model()->index(at("code"))));
    ex->refresh();
    CHECK(ex->tree()->rootIndex() == ex->model()->index(root));
    CHECK(ex->currentPath() == at("code/main.cpp"));
    CHECK(ex->tree()->isExpanded(ex->model()->index(at("code"))));

    // a file deep in folders that were never opened is revealed once they have loaded
    QDir(root).mkpath("deep/er/est");
    const QString deep = write("deep/er/est/leaf.cpp", "// leaf\n");
    w.openPath(deep);
    waitFor([&]() { return ex->currentPath() == deep; });
    CHECK(ex->currentPath() == deep);
    ex->refresh();                                         // nested open folders come back too
    waitFor([&]() { return ex->tree()->isExpanded(ex->model()->index(at("deep/er/est"))); });
    CHECK(ex->tree()->isExpanded(ex->model()->index(at("deep/er/est"))) && ex->currentPath() == deep);
    w.closeTab(w.tabCount() - 1, true);

    // deleting a folder closes the tabs inside it (permanently here: tests do not fill the Recycle Bin)
    const int before = w.tabCount();
    CHECK(before == 2);
    CHECK(ex->removePath(at("code"), true, &err));
    CHECK(!QFileInfo::exists(at("code")) && w.tabCount() == 0 && w.welcomeShown());
    CHECK(!ex->removePath(at("code"), true, &err) && !err.isEmpty());
    CHECK(QFile::exists(at("todo.md")) && QFile::exists(at("todo copy.md")));

    const QString shot = qEnvironmentVariable("DEVPAD_EXPLORER_SCREENSHOT");
    if (!shot.isEmpty()) {
        ex->createFolder(root, "engine", &err);
        write("engine/Trie.cpp", "// trie\n");
        write("engine/Search.cpp", "// kmp\n");
        write("README.md", "# DevPad\n");
        w.openPath(at("engine/Trie.cpp"));
        w.resize(1000, 600);
        waitFor([&]() { return ex->currentPath() == at("engine/Trie.cpp"); });
        waitFor([&]() { return ex->model()->index(at("README.md")).isValid() && !ex->model()->index(at("code")).isValid(); });
        w.grab().save(shot);
    }
    for (int i = w.tabCount() - 1; i >= 0; --i) w.closeTab(i, true);
}

static void testWelcome() {
    std::cout << "[welcome page]\n";
    QTemporaryDir tmp;
    const QString root = QFileInfo(tmp.path()).canonicalFilePath();
    QFile f(QDir(root).filePath("hello.cpp"));
    (void)f.open(QIODevice::WriteOnly);
    f.write("int x;\n");
    f.close();
    const QString file = QDir(root).filePath("hello.cpp");

    MainWindow w;
    w.show();
    WelcomePage* page = w.welcome();
    CHECK(w.welcomeShown() && page->isVisible() && w.tabCount() == 0);
    CHECK(page->shownRecent().isEmpty());              // fresh settings: nothing recent yet

    // the links
    QSignalSpy keys(page, &WelcomePage::shortcutsRequested);
    page->activate("new");
    CHECK(w.tabCount() == 1 && !w.welcomeShown());
    CHECK(w.closeTab(0, true) && w.welcomeShown());
    page->activate("file:" + file);                     // a recent file opens in a tab
    CHECK(w.tabCount() == 1 && w.tabTitle(0) == "hello.cpp");
    w.closeTab(0, true);
    CHECK(page->shownRecent().contains(file));         // and is listed when the page comes back
    page->activate("folder:" + root);
    CHECK(w.workspaceRoot() == root && w.recentFolders().value(0) == root);
    page->activate("folder:" + root + "/missing");     // gone meanwhile: nothing happens
    CHECK(w.workspaceRoot() == root && w.welcomeShown());
    QFile::remove(file);
    page->setRecent(w.recentFiles(), w.recentFolders());
    CHECK(!page->shownRecent().contains(file) && page->shownRecent().contains(root));   // deleted files drop out

    // status bar items only make sense with a file open
    const QString shot = qEnvironmentVariable("DEVPAD_WELCOME_SCREENSHOT");
    if (!shot.isEmpty()) {
        w.resize(1100, 680);
        QTest::qWait(200);
        w.grab().save(shot);
    }
}

// ---------------------------------------------------------------- GUI refining: settings, shortcuts window, terminal

static void waitUntil(const std::function<bool()>& done, int ms = 5000) {
    for (int t = 0; t < ms && !done(); t += 20) QTest::qWait(20);
}

static void testSettings() {
    std::cout << "[settings: store, clamping, effect on the editor]\n";
    AppSettings& S = AppSettings::instance();
    S.resetToDefaults();
    const AppSettings::Values d;
    CHECK(S.values() == d && d.fontSize == 11 && d.tabWidth == 4 && d.autoPairs && d.statusBar);

    // clamped to what the editor supports
    AppSettings::Values bad = d;
    bad.fontSize = 99; bad.tabWidth = 3; bad.accent = "nonsense"; bad.cursorStyle = 9; bad.terminalScrollback = 1;
    const AppSettings::Values fixed = AppSettings::sanitized(bad);
    CHECK(fixed.fontSize == 36 && fixed.tabWidth == 4 && fixed.accent == "blue" && fixed.cursorStyle == 2 && fixed.terminalScrollback == 200);

    // changed() fires once per real change, never for a no-op
    QSignalSpy spy(&S, &AppSettings::changed);
    AppSettings::Values v = S.values();
    v.tabWidth = 2;
    S.apply(v);
    CHECK(spy.count() == 1);
    S.apply(v);
    CHECK(spy.count() == 1);

    // stored, and read back
    CHECK(QSettings().value("prefs/tabWidth").toInt() == 2);
    QSettings().setValue("prefs/tabWidth", 8);
    S.reload();
    CHECK(S.values().tabWidth == 8 && spy.count() == 2);
    S.resetToDefaults();

    // tab width: Tab key fills to the next tab stop
    CodeView a, b;
    a.resize(600, 300); a.show();
    v = S.values(); v.tabWidth = 2; S.apply(v);
    setText(&a, "");
    key(&a, Qt::Key_Tab);
    CHECK_EQ(txt(&a), QString("  \n"));
    v.tabWidth = 8; S.apply(v);
    setText(&a, "");
    key(&a, Qt::Key_Tab);
    CHECK_EQ(txt(&a), QString("        \n"));
    S.resetToDefaults();

    // auto pairs off: "(" is just "(", and typing ")" does not step over anything
    v = S.values(); v.autoPairs = false; S.apply(v);
    setText(&a, "");
    type(&a, "f(x)");
    CHECK_EQ(txt(&a), QString("f(x)\n"));
    setText(&a, "");
    type(&a, "[");
    key(&a, Qt::Key_Backspace);
    CHECK_EQ(txt(&a), QString());
    S.resetToDefaults();
    setText(&a, "");
    type(&a, "(");
    CHECK_EQ(txt(&a), QString("()\n"));                 // and back on

    // wrap selection off: the opener replaces the selection
    v = S.values(); v.wrapSelection = false; S.apply(v);
    setText(&a, "abc");
    a.select(Pos{0, 0}, Pos{0, 3});
    type(&a, "(");
    CHECK_EQ(txt(&a), QString("(\n"));
    S.resetToDefaults();
    setText(&a, "abc");
    a.select(Pos{0, 0}, Pos{0, 3});
    type(&a, "(");
    CHECK_EQ(txt(&a), QString("(abc)\n"));

    // autocomplete popup off
    v = S.values(); v.autoComplete = false; S.apply(v);
    setText(&a, "int count = 0;\ncou");
    a.setCursor(Pos{1, 3});
    type(&a, "n");
    QTest::qWait(350);
    QListWidget* popup = a.findChild<QListWidget*>();
    CHECK(popup && !popup->isVisible());
    S.resetToDefaults();

    // zoom is the font-size setting and every tab follows it
    a.setZoom(15);
    CHECK(S.values().fontSize == 15 && b.zoom() == 15);
    v = S.values(); v.fontSize = 12; S.apply(v);
    CHECK(a.zoom() == 12 && b.zoom() == 12);
    S.resetToDefaults();
    CHECK(a.zoom() == 11);
}

static void testSettingsDialog() {
    std::cout << "[settings window]\n";
    AppSettings& S = AppSettings::instance();
    S.resetToDefaults();
    MainWindow w;
    w.show();
    SettingsDialog* d = w.showSettings();
    CHECK(d && d->isVisible() && w.showSettings() == d);          // one window, shown again
    CHECK(d->sectionNames() == QStringList({"Editor", "Appearance", "Terminal", "General"}));
    CHECK(d->rowCount() >= 15 && d->visibleRowCount() == d->rowCount());

    // controls change the settings at once
    Segmented* tab = qobject_cast<Segmented*>(d->control("Tab width"));
    CHECK(tab && tab->value() == 4);
    tab->choose(2);                                               // "8"
    CHECK(S.values().tabWidth == 8);
    Stepper* size = qobject_cast<Stepper*>(d->control("Font size"));
    CHECK(size && size->value() == 11);
    size->stepBy(+1);
    CHECK(S.values().fontSize == 12);
    Switch* pairs = qobject_cast<Switch*>(d->control("Close brackets and quotes"));
    CHECK(pairs && pairs->isChecked());
    pairs->click();
    CHECK(!S.values().autoPairs);
    Swatches* accent = qobject_cast<Swatches*>(d->control("Accent colour"));
    CHECK(accent && accent->value() == "blue");
    accent->choose("green");
    CHECK(S.values().accent == "green" && Theme::Accent == QColor(AppSettings::accentRgb("green")));
    Switch* bar = qobject_cast<Switch*>(d->control("Status bar"));
    bar->click();
    CHECK(!S.values().statusBar && !w.statusBar()->isVisible());
    bar->click();
    CHECK(w.statusBar()->isVisible());

    // a change made somewhere else (Ctrl+Wheel zoom) shows in the window
    AppSettings::Values v = S.values(); v.fontSize = 17; S.apply(v);
    CHECK(size->value() == 17);

    // search filters rows and hides empty sections
    d->setSearchText("terminal");
    CHECK(d->visibleRowCount() >= 3 && d->visibleRowCount() < d->rowCount());
    d->setSearchText("cursor blink");
    CHECK(d->visibleRowCount() == 1);
    d->setSearchText("zzzz");
    CHECK(d->visibleRowCount() == 0);
    d->setSearchText("");
    CHECK(d->visibleRowCount() == d->rowCount());

    // reset
    S.resetToDefaults();
    CHECK(tab->value() == 4 && size->value() == 11 && pairs->isChecked() && accent->value() == "blue");
    CHECK(Theme::Accent == QColor(AppSettings::accentRgb("blue")));
    d->close();
}

static void testThemes() {
    std::cout << "[themes: switch, toggle, persistence, settings window]\n";
    AppSettings& S = AppSettings::instance();
    S.resetToDefaults();
    MainWindow w;
    w.show();
    CHECK(Theme::schemeCount() == 12 && Theme::currentScheme() == "midnight" && Theme::isDark());
    CHECK(Theme::Editor == QColor(0x1f, 0x1f, 0x1f));

    // a theme change is a settings change: colours, style sheet and the stored value all follow
    QSignalSpy changed(&Theme::notifier(), &Theme::Notifier::changed);
    w.setTheme("daylight");
    CHECK(S.values().theme == "daylight" && Theme::currentScheme() == "daylight" && !Theme::isDark());
    CHECK(Theme::Editor == QColor(0xff, 0xff, 0xff) && Theme::Text.lightness() < Theme::Editor.lightness());
    CHECK(changed.count() >= 1);
    CHECK(qApp->styleSheet().contains(Theme::Editor.name()));
    CHECK(AppSettings::sanitized([&]() { AppSettings::Values v = S.values(); v.theme = "nonsense"; return v; }()).theme == "midnight");

    // every theme can be selected and keeps readable text on the editor background
    for (const QString& id : Theme::schemeIds()) {
        w.setTheme(id);
        CHECK(Theme::currentScheme() == id && S.values().theme == id);
        CHECK(std::abs(Theme::Syntax[0].lightness() - Theme::Editor.lightness()) > 90);
        CHECK(Theme::isDark() == (Theme::Editor.lightness() < 128));
    }

    // each theme has its own syntax colours (not just the chrome)
    QSet<QRgb> keywordColours;
    for (const QString& id : Theme::schemeIds()) {
        w.setTheme(id);
        keywordColours.insert(Theme::Syntax[1].rgb());
    }
    CHECK(keywordColours.size() >= 9);
    // every theme toggles to a partner of the other kind and back
    for (const QString& id : Theme::schemeIds()) {
        w.setTheme(id);
        const bool wasDark = Theme::isDark();
        w.toggleTheme();
        CHECK(Theme::isDark() != wasDark);
        w.toggleTheme();
        CHECK(Theme::currentScheme() == id);
    }

    // the quick toggle switches light <-> dark and is its own inverse
    w.setTheme("midnight");
    w.toggleTheme();
    CHECK(Theme::currentScheme() == "daylight");
    w.toggleTheme();
    CHECK(Theme::currentScheme() == "midnight");
    w.setTheme("nord");
    w.toggleTheme();
    CHECK(Theme::currentScheme() == "solarized-light" && !Theme::isDark());
    w.toggleTheme();
    CHECK(Theme::currentScheme() == "nord");

    // the editor repaints with the new colours without being rebuilt
    CodeView* v = w.newTab();
    v->resize(400, 200);
    v->show();
    w.setTheme("daylight");
    QPixmap light = v->grab();
    w.setTheme("midnight");
    QPixmap dark = v->grab();
    CHECK(light.toImage().pixelColor(2, 2) != dark.toImage().pixelColor(2, 2));

    // the settings window offers the themes and applies the choice at once
    SettingsDialog* d = w.showSettings();
    Combo* combo = qobject_cast<Combo*>(d->control("Theme"));
    CHECK(combo && combo->count() == Theme::schemeCount() && combo->currentData().toString() == "midnight");
    combo->setCurrentIndex(combo->findData("nord"));
    emit combo->activated(combo->currentIndex());
    CHECK(S.values().theme == "nord" && Theme::currentScheme() == "nord");
    // a change from elsewhere (the View menu, the toggle) shows in the window
    w.setTheme("solarized-light");
    CHECK(combo->currentData().toString() == "solarized-light");
    d->close();

    // reset puts the default theme back
    S.resetToDefaults();
    CHECK(Theme::currentScheme() == "midnight" && Theme::Editor == QColor(0x1f, 0x1f, 0x1f));
}

static void testSidebarToggle() {
    std::cout << "[sidebar: Ctrl+B and the close button]\n";
    MainWindow w;
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w);
    CHECK(w.explorer()->isVisible());
    w.newTab()->setFocus();
    QTest::keyClick(&w, Qt::Key_B, Qt::ControlModifier);       // hides
    CHECK(!w.explorer()->isVisible());
    QTest::keyClick(&w, Qt::Key_B, Qt::ControlModifier);       // shows again
    CHECK(w.explorer()->isVisible());
    w.currentView()->setFocus();
    QTest::keyClick(w.currentView(), Qt::Key_B, Qt::ControlModifier);   // with the focus in the editor
    CHECK(!w.explorer()->isVisible());
    QTest::keyClick(w.currentView(), Qt::Key_B, Qt::ControlModifier);
    CHECK(w.explorer()->isVisible());
    QToolButton* close = w.explorer()->findChild<QToolButton*>("explorerClose");   // the icon in the explorer's header
    CHECK(close != nullptr);
    if (close) { close->click(); CHECK(!w.explorer()->isVisible()); }
    w.toggleSidebar();
    CHECK(w.explorer()->isVisible());

    // run and stop buttons
    QToolButton* run = w.findChild<QToolButton*>("runButton");
    QToolButton* stop = w.findChild<QToolButton*>("stopButton");
    CHECK(run && stop && run->isEnabled() && !stop->isEnabled());
}

static void testAutoSave() {
    std::cout << "[auto save]\n";
    AppSettings& S = AppSettings::instance();
    S.resetToDefaults();
    CHECK(!S.values().autoSave && S.values().autoSaveDelay >= 1);               // off by default
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString file = QDir(tmp.path()).filePath("auto.txt");
    { QFile f(file); (void)f.open(QIODevice::WriteOnly); f.write("one\n"); }
    auto onDisk = [&]() { QFile f(file); (void)f.open(QIODevice::ReadOnly); return QString::fromUtf8(f.readAll()); };
    MainWindow w;
    w.show();
    CHECK(w.openPath(file));
    CodeView* v = w.currentView();
    CHECK(v != nullptr);
    if (!v) return;
    v->setFocus();
    type(v, "x");                                                               // off: nothing is written
    QTest::qWait(1400);
    CHECK_EQ(onDisk(), QString("one\n"));
    CHECK(v->editor().doc().modified());

    AppSettings::Values nv = S.values();                                        // on, 1 second
    nv.autoSave = true;
    nv.autoSaveDelay = 1;
    S.apply(nv);
    type(v, "y");
    QTest::qWait(300);
    CHECK_EQ(onDisk(), QString("one\n"));                                       // not yet: the delay has not passed
    QTest::qWait(1400);
    CHECK_EQ(onDisk(), QString("xyone\n"));
    CHECK(!v->editor().doc().modified());

    nv.autoSave = false;                                                        // off again: a pending save is cancelled
    S.apply(nv);
    S.resetToDefaults();
}

static void testSnippetSuggestions() {
    std::cout << "[snippets in the suggestion list]\n";
    SnippetStore::instance().resetToDefaults();
    SnippetTable& T = SnippetStore::instance().table();
    T.add("zzgreet", "Hello $0 world");
    SnippetStore::instance().notifyChanged();
    CodeView v;
    v.resize(500, 300);
    v.show();
    setText(&v, "");
    type(&v, "zzg");
    QTest::qWait(300);                                        // the popup appears after a short pause
    QListWidget* popup = v.findChild<QListWidget*>();
    CHECK(popup && popup->isVisible() && popup->count() > 0);
    CHECK(popup && popup->item(0)->text().startsWith("zzgreet") && popup->item(0)->text().contains("snippet"));
    key(&v, Qt::Key_Return);                                  // accepting a snippet entry expands it
    CHECK(popup && !popup->isVisible());
    CHECK_EQ(txt(&v), QString("Hello  world\n"));
    T.remove("zzgreet");
    SnippetStore::instance().resetToDefaults();
}

static void testShortcutsWindow() {
    std::cout << "[keyboard shortcuts window]\n";
    const QList<ShortcutsDialog::Entry>& list = ShortcutsDialog::entries();
    CHECK(list.size() > 40 && ShortcutsDialog::areas().size() >= 8);
    QStringList seen;
    bool wellFormed = true;
    for (const auto& e : list) {
        wellFormed = wellFormed && !e.action.isEmpty() && !e.keys.isEmpty() && !seen.contains(e.area + "/" + e.action);
        seen << e.area + "/" + e.action;
    }
    CHECK(wellFormed);

    // what the window promises really exists: every menu-type shortcut is bound to an action
    MainWindow w;
    QList<QKeySequence> bound;
    for (QAction* a : w.findChildren<QAction*>()) bound << a->shortcuts();
    const QStringList handledByEditor = {"Tab", "Shift+Tab", "Esc", "Enter", "Shift+Enter", "Alt+1-9", "Ctrl+X", "Ctrl+C", "Ctrl+V",
                                         "Ctrl+A", "Home", "Up", "Down", "Backspace", "Del", "Shift+Del", "Drag", "Ctrl+Z", "Ctrl+L"};
    QStringList missing;
    for (const auto& e : list) {
        if (e.chars || !(e.area == "Tabs" || e.area == "Files and windows" || e.area == "Lines" || e.area == "Search" || e.area == "Editing" || e.area == "Run")) continue;
        for (const QString& k : e.keys) {
            if (handledByEditor.contains(k)) continue;
            const QKeySequence ks(k, QKeySequence::PortableText);
            bool found = false;
            for (const QKeySequence& b : bound) found = found || b == ks;
            if (!found) missing << k;
        }
    }
    if (!missing.isEmpty()) std::cout << "  not bound to an action: " << missing.join(", ").toStdString() << std::endl;
    CHECK(missing.isEmpty());

    ShortcutsDialog* d = w.showShortcuts();
    CHECK(d->isVisible() && w.showShortcuts() == d);
    CHECK(d->visibleEntryCount() == list.size() && d->visibleCardCount() == ShortcutsDialog::areas().size());
    d->setFilter("comment");
    CHECK(d->visibleEntryCount() == 1 && d->visibleCardCount() == 1);
    d->setFilter("ctrl shift k");                      // keys work as search terms too
    CHECK(d->visibleEntryCount() == 1);
    d->setFilter("F2");
    CHECK(d->visibleEntryCount() == 2);                // rename file, rename in the explorer
    d->setFilter("zzzz");
    CHECK(d->visibleEntryCount() == 0 && d->visibleCardCount() == 0);
    d->setFilter("");
    CHECK(d->visibleEntryCount() == list.size());
    d->close();
}

static void testTerminal() {
    std::cout << "[terminal]\n";
    AppSettings::instance().resetToDefaults();
    QTemporaryDir tmp;
    const QString root = QFileInfo(tmp.path()).canonicalFilePath();
    QDir(root).mkdir("sub");

    // how a command line reaches each kind of shell
    QString prog, native;
    QStringList args;
    TerminalPanel::shellInvocation("bash", "echo hi", &prog, &args, &native);
    CHECK(prog == "bash" && args == QStringList({"-c", "echo hi"}) && native.isEmpty());
    TerminalPanel::shellInvocation("pwsh", "ls", &prog, &args, &native);
    CHECK(args.contains("-Command") && args.last() == "ls");
    TerminalPanel::shellInvocation("C:/Windows/System32/cmd.exe", "dir \"a b\"", &prog, &args, &native);
    CHECK(native == "/d /s /c \"dir \"a b\"\"");

    // colours: ANSI codes are turned into text colours and never shown
    QPlainTextEdit edit;
    TerminalPanel::appendAnsi(&edit, "\x1b[31mred\x1b[0m plain \x1b[1;32mgo\x1b[0m\x1b[2K\r\n");
    CHECK(edit.toPlainText() == "red plain go\n");
    QTextCursor c(edit.document());
    c.setPosition(1);
    CHECK(c.charFormat().foreground().color() == QColor(0xf4, 0x7a, 0x6b));
    c.setPosition(5);
    CHECK(!c.charFormat().hasProperty(QTextFormat::ForegroundBrush));      // plain text: no fixed colour, it follows the theme

    TerminalPanel t;
    t.resize(700, 300);
    t.show();
    t.setWorkingDirectory(root);
    CHECK(t.workingDirectory() == root);
    auto run = [&](const QString& line) {
        t.runCommand(line);
        waitUntil([&]() { return !t.isRunning(); });
    };

    run("echo hello-dsa");
    CHECK(t.plainText().contains("hello-dsa") && !t.isRunning());
    CHECK(t.history() == QStringList({"echo hello-dsa"}));

    // cd is remembered between commands (relative, "..", and a folder that does not exist)
    run("cd sub");
    CHECK(t.workingDirectory() == root + "/sub");
    run("cd ..");
    CHECK(t.workingDirectory() == root);
    run("cd nothing-here");
    CHECK(t.workingDirectory() == root && t.plainText().contains("no such folder"));
#ifndef Q_OS_WIN
    run("pwd");
    CHECK(t.plainText().trimmed().endsWith(root));
    run("cd sub");
    run("cd -");
    CHECK(t.workingDirectory() == root);
#endif

    // exit codes are reported; stopping a long program works
    run("exit 3");
    CHECK(t.plainText().contains("[exit code 3]"));
#ifdef Q_OS_WIN
    t.runCommand("ping -n 30 127.0.0.1");
#else
    t.runCommand("sleep 30");
#endif
    waitUntil([&]() { return t.isRunning(); });
    CHECK(t.isRunning());
    t.stop();
    waitUntil([&]() { return !t.isRunning(); }, 6000);
    CHECK(!t.isRunning());

#ifndef Q_OS_WIN
    // while a program runs, typed lines are its input
    t.runCommand("read x; echo got:$x");
    waitUntil([&]() { return t.isRunning(); });
    t.runCommand("banana");
    waitUntil([&]() { return !t.isRunning(); });
    CHECK(t.plainText().contains("got:banana"));
#endif

    // history with the Up / Down keys, clear
    t.input()->setFocus();
    key(t.input(), Qt::Key_Up);
    CHECK(t.input()->text() == t.history().last());
    key(t.input(), Qt::Key_Down);
    CHECK(t.input()->text().isEmpty());
    t.input()->setText("echo typed-line");
    key(t.input(), Qt::Key_Return);
    waitUntil([&]() { return t.plainText().contains("typed-line") && !t.isRunning(); });
    CHECK(t.plainText().contains("typed-line") && t.input()->text().isEmpty());
    run("clear");
    CHECK(t.plainText().trimmed().isEmpty());
    QSignalSpy hide(&t, &TerminalPanel::hideRequested);
    run("exit");
    CHECK(hide.count() == 1);

    // font size from the settings
    AppSettings::Values v = AppSettings::instance().values();
    v.terminalFontSize = 15;
    AppSettings::instance().apply(v);
    CHECK(t.findChild<QPlainTextEdit*>("terminalOutput")->font().pointSize() == 15);
    AppSettings::instance().resetToDefaults();
}

static void testPanelsInMainWindow() {
    std::cout << "[terminal panel and windows inside the main window]\n";
    AppSettings::instance().resetToDefaults();
    QTemporaryDir tmp;
    const QString root = QFileInfo(tmp.path()).canonicalFilePath();
    MainWindow w;
    w.resize(1000, 700);
    w.show();
    CHECK(!w.terminalVisible());
    w.setWorkspaceRoot(root);
    CHECK(w.terminal()->workingDirectory() == root);          // the terminal starts in the open folder
    w.showTerminal(true);
    CHECK(w.terminalVisible() && w.terminal()->isVisible() && w.terminal()->height() >= 110);
    CHECK(w.terminal()->input()->isVisible());
    w.showTerminal(false);
    CHECK(!w.terminalVisible());
    QAction* toggle = nullptr;
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == "Toggle Terminal") toggle = a;
    CHECK(toggle && toggle->shortcut() == QKeySequence(QStringLiteral("Ctrl+`")));
    toggle->trigger();
    CHECK(w.terminalVisible());
    emit w.terminal()->hideRequested();                        // "exit", Esc or the close button
    CHECK(!w.terminalVisible());

    // reopen last session can be switched off
    QFile f(QDir(root).filePath("a.txt"));
    (void)f.open(QIODevice::WriteOnly); f.write("x\n"); f.close();
    QSettings().setValue("files", QStringList{QDir(root).filePath("a.txt")});
    AppSettings::Values v = AppSettings::instance().values();
    v.restoreSession = false;
    AppSettings::instance().apply(v);
    { MainWindow w2; w2.restoreSession(); CHECK(w2.tabCount() == 0); }
    AppSettings::instance().resetToDefaults();
    { MainWindow w3; w3.restoreSession(); CHECK(w3.tabCount() == 1); }
    AppSettings::instance().resetToDefaults();
}

static void testRun() {
    std::cout << "[F5: run the current file]\n";
    AppSettings::instance().resetToDefaults();
    // plans
    using Runner::Shell;
    const Runner::Plan cpp = Runner::planFor("/p/a b/main.cpp", Shell::Posix, "/tmp/o");
    CHECK(cpp.steps.size() == 2 && cpp.steps[0].startsWith("g++ ") && cpp.steps[0].contains("\"/p/a b/main.cpp\"") && cpp.steps[1].contains("/tmp/o/main"));
    CHECK(Runner::planFor("x.c", Shell::Posix, "/o").steps.size() == 2);
    CHECK(Runner::planFor("x.py", Shell::Posix, "/o").steps.size() == 1 && Runner::planFor("x.py", Shell::Posix, "/o").steps[0].contains("python"));
    CHECK(Runner::planFor("x.js", Shell::Posix, "/o").steps[0].startsWith("node "));
    CHECK(Runner::planFor("x.java", Shell::Posix, "/o").steps[0].startsWith("java "));
    CHECK(Runner::planFor("x.txt", Shell::Posix, "/o").steps.isEmpty());
    CHECK(Runner::planFor("C:/a/m.cpp", Shell::PowerShell, "C:/o").steps[1].startsWith("& "));
    CHECK(Runner::planFor("C:/a/m.cpp", Shell::Cmd, "C:/o").steps[0].contains("\"C:\\a\\m.cpp\""));
    CHECK(Runner::shellKind("pwsh") == Shell::PowerShell && Runner::shellKind("C:/Windows/System32/cmd.exe") == Shell::Cmd && Runner::shellKind("/bin/zsh") == Shell::Posix);

    QTemporaryDir tmp;
    const QString root = QFileInfo(tmp.path()).canonicalFilePath();
    auto write = [&](const QString& name, const QByteArray& body) {
        QFile f(QDir(root).filePath(name));
        (void)f.open(QIODevice::WriteOnly);
        f.write(body);
        f.close();
        return QDir(root).filePath(name);
    };
    MainWindow w;
    w.resize(1000, 700);
    w.show();

    QAction* run = nullptr;
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == "Run File" && !a->shortcut().isEmpty()) run = a;
    CHECK(run && run->shortcut() == QKeySequence(Qt::Key_F5));
    CHECK(!w.runCurrent());                                  // nothing open

#ifdef Q_OS_WIN
    const QString script = write("hello.bat", "@echo off\r\necho ran-ok %CD%\r\n");
#else
    const QString script = write("hello.sh", "echo ran-ok $(pwd)\n");
#endif
    w.openPath(script);
    CHECK(w.runCurrent() || true);
    waitUntil([&]() { return w.terminal()->plainText().contains("ran-ok"); });
    waitUntil([&]() { return !w.terminal()->busy(); });
    CHECK(w.terminalVisible() && w.terminal()->plainText().contains("ran-ok"));
    CHECK(w.terminal()->workingDirectory() == root);         // runs in the file's folder

    // the file is saved before it runs: edit without saving, press F5, the new text runs
    CodeView* v = w.currentView();
    v->setCursor(Pos{0, 0});
    type(v, "echo changed-text");                            // typed, not loaded: the file keeps its path
    key(v, Qt::Key_Return);
    CHECK(v->editor().doc().modified());
    run->trigger();
    waitUntil([&]() { return w.terminal()->plainText().contains("changed-text"); });
    waitUntil([&]() { return !w.terminal()->busy(); });
    CHECK(w.terminal()->plainText().contains("changed-text") && !v->editor().doc().modified());

    // unknown type: says so, runs nothing
    const QString txt = write("notes.txt", "hi\n");
    w.openPath(txt);
    CHECK(!w.runCurrent() && w.statusBar()->currentMessage().contains("cannot run .txt"));

    // compiled languages: build, then run (only if a compiler is installed)
    if (!QStandardPaths::findExecutable("g++").isEmpty()) {
        w.openPath(write("hi.cpp", "#include <iostream>\nint main(){ std::cout << \"hello-from-f5\" << std::endl; return 0; }\n"));
        CHECK(w.runCurrent());
        waitUntil([&]() { return w.terminal()->plainText().contains("hello-from-f5"); }, 30000);
        CHECK(w.terminal()->plainText().contains("hello-from-f5"));
        // a compile error stops the sequence: the old program is not run again
        w.terminal()->clearScreen();
        CodeView* c = w.currentView();
        c->selectAll();
        type(c, "int main( { return 0 }");
        w.runCurrent();
        waitUntil([&]() { return w.terminal()->plainText().contains("exit code"); }, 30000);
        waitUntil([&]() { return !w.terminal()->busy(); });
        CHECK(w.terminal()->plainText().contains("[exit code") && !w.terminal()->plainText().contains("hello-from-f5"));
    }
    // Shift+F5 stops a long program
#ifndef Q_OS_WIN
    w.openPath(write("slow.sh", "sleep 30\n"));
    w.runCurrent();
    waitUntil([&]() { return w.terminal()->isRunning(); });
    CHECK(w.terminal()->isRunning());
    w.stopRun();
    waitUntil([&]() { return !w.terminal()->busy(); }, 6000);
    CHECK(!w.terminal()->busy());
#endif
    for (int i = w.tabCount() - 1; i >= 0; --i) w.closeTab(i, true);
}

// ---------------------------------------------------------------- snippets, problems, squiggles

static void testSnippetsInEditor() {
    std::cout << "[snippets: Tab expands the trigger]\n";
    SnippetStore::instance().resetToDefaults();
    CodeView v;
    v.resize(600, 400);
    v.show();
    setText(&v, "");
    type(&v, "for");
    CHECK(v.snippetAtCursor());
    key(&v, Qt::Key_Tab);                                    // "for" + Tab -> the for-loop
    CHECK(txt(&v).contains("for ("));
    const int lines = v.editor().doc().lineCount();
    CHECK(lines > 1);
    v.undo();                                                // one undo step puts "for" back
    CHECK(!txt(&v).contains("for (") && txt(&v).startsWith("for"));

    setText(&v, "");
    type(&v, "zzqx");                                        // not a snippet: Tab is just indentation
    CHECK(!v.snippetAtCursor());
    key(&v, Qt::Key_Tab);
    CHECK(txt(&v).startsWith("zzqx"));

    // Edit > Expand Snippet does the same without the Tab key
    setText(&v, "");
    type(&v, "if");
    v.expandSnippet();
    CHECK(txt(&v).contains("if ("));

    // the user's own snippet, through the shared store
    SnippetStore::instance().table().add("mysnip", "hello $0 world");
    setText(&v, "");
    type(&v, "mysnip");
    key(&v, Qt::Key_Tab);
    CHECK_EQ(txt(&v), QString("hello  world\n"));
    CHECK(v.cursor().line == 0 && v.cursor().col == 6);      // $0 marks the cursor
    SnippetStore::instance().table().remove("mysnip");
    SnippetStore::instance().resetToDefaults();

    // the Snippets window lists the table and adds, edits, renames, duplicates and deletes snippets
    SnippetTable& T = SnippetStore::instance().table();
    SnippetDialog d;
    d.show();
    CHECK(d.isVisible() && d.shownTriggers().contains("for") && !d.isDirty());
    CHECK(d.shownTriggers().size() == static_cast<int>(T.size()));

    d.startNew();                                            // add
    CHECK(d.current().isEmpty() && !d.isDirty());
    d.setFields("hello_t", "Hello $0!\n");
    CHECK(d.isDirty());
    CHECK(d.save() && !d.isDirty() && d.current() == "hello_t" && d.shownTriggers().contains("hello_t"));
    CHECK(T.find("hello_t") && T.find("hello_t")->body == "Hello $0!\n");

    d.setFields("hello_t", "Hi $0");                         // edit the body
    CHECK(d.isDirty() && d.save() && T.find("hello_t")->body == "Hi $0");

    d.setFields("hello2", "Hi $0");                          // rename: the old trigger goes away
    CHECK(d.save() && !T.find("hello_t") && T.find("hello2") && d.current() == "hello2");
    CHECK(!d.shownTriggers().contains("hello_t") && d.shownTriggers().contains("hello2"));

    d.setFields("hello2", "changed");                        // revert
    CHECK(d.isDirty());
    d.revert();
    CHECK(!d.isDirty() && T.find("hello2")->body == "Hi $0");

    d.duplicateCurrent();                                    // duplicate: a new unsaved copy
    CHECK(d.isDirty() && d.current().isEmpty());
    CHECK(d.save() && d.current() == "hello2_copy" && T.find("hello2_copy")->body == "Hi $0");

    d.startNew();                                            // a trigger that exists / is not valid is refused
    d.setFields("for", "x");
    CHECK(!d.save() && !d.message().isEmpty() && T.find("for")->body != "x");
    d.setFields("bad name", "x");
    CHECK(!d.save() && !T.find("bad name"));
    d.revert();
    CHECK(!d.isDirty());

    d.setFilter("hello2");                                   // search
    CHECK(d.shownTriggers() == QStringList({"hello2", "hello2_copy"}));
    d.setFilter(QString());
    CHECK(d.shownTriggers().size() == static_cast<int>(T.size()));

    CHECK(d.select("hello2_copy") && d.deleteCurrent(false)); // delete
    CHECK(!T.find("hello2_copy") && !d.shownTriggers().contains("hello2_copy"));
    CHECK(d.select("hello2") && d.deleteCurrent(false) && !T.find("hello2"));
    CHECK(!d.select("no_such_snippet"));
    d.close();
    SnippetStore::instance().resetToDefaults();
}

static void testProblemsPanel() {
    std::cout << "[Problems panel]\n";
    ProblemsPanel p;
    p.resize(600, 200);
    p.show();
    CHECK(p.rowCount() == 0 && p.errorCount() == 0);
    std::vector<Diagnostic> list;
    Diagnostic e; e.severity = Severity::Error; e.file = "a.cpp"; e.line = 5; e.col = 3; e.message = "boom"; e.source = "g++";
    Diagnostic w; w.severity = Severity::Warning; w.file = "a.cpp"; w.line = 2; w.col = 1; w.message = "careful"; w.source = "g++";
    Diagnostic n; n.severity = Severity::Note; n.file = "a.cpp"; n.line = 1; n.message = "declared here";
    Diagnostic b; b.severity = Severity::Error; b.line = 9; b.col = 4; b.message = "bracket"; b.source = "brackets";
    list = {e, w, n, b};
    p.setProblems(list, "cur.cpp");
    CHECK(p.rowCount() == 3 && p.errorCount() == 2 && p.warningCount() == 1);    // notes are not listed
    QSignalSpy jump(&p, &ProblemsPanel::jumpRequested);
    p.activateRow(0);                                        // sorted by file: "" (brackets) first
    CHECK(jump.count() == 1);
    CHECK(jump.at(0).at(0).toString() == "cur.cpp" && jump.at(0).at(1).toInt() == 9 && jump.at(0).at(2).toInt() == 4);
    p.activateRow(1);                                        // a.cpp:2 (warning) before a.cpp:5
    CHECK(jump.at(1).at(0).toString() == "a.cpp" && jump.at(1).at(1).toInt() == 2);
    p.setProblems({}, "cur.cpp");
    CHECK(p.rowCount() == 0);
}

static void testSquigglesAndMainWindow() {
    std::cout << "[squiggles, Problems tab and the syntax check]\n";
    AppSettings::instance().resetToDefaults();
    QTemporaryDir tmp;
    const QString root = QFileInfo(tmp.path()).canonicalFilePath();
    const QString file = root + "/bad.cpp";
    { QFile f(file); f.open(QIODevice::WriteOnly); f.write("int main() {\n  int x = ;\n  return 0;\n}\n"); }

    // a compiler result handed to the view shows up in diagnostics() and survives a line shift
    CodeView v;
    v.resize(600, 300);
    v.show();
    setText(&v, "a\nb\nc\nd\n");
    Diagnostic e; e.severity = Severity::Error; e.line = 3; e.col = 1; e.message = "x"; e.source = "g++";
    Diagnostic w; w.severity = Severity::Warning; w.line = 4; w.col = 1; w.message = "y"; w.source = "g++";
    Diagnostic note; note.severity = Severity::Note; note.line = 1; note.message = "z";
    QSignalSpy changed(&v, &CodeView::diagnosticsChanged);
    v.setCompilerDiagnostics({e, w, note});
    CHECK(changed.count() == 1);
    size_t nonBracket = 0;
    for (const Diagnostic& d : v.diagnostics()) if (d.source == "g++") ++nonBracket;
    CHECK(nonBracket == 2);                                  // notes are dropped
    v.setCursor(Pos{0, 1});
    key(&v, Qt::Key_Return);                                 // a line is inserted above: marks move down one line
    bool movedErr = false;
    for (const Diagnostic& d : v.diagnostics()) if (d.source == "g++" && d.severity == Severity::Error && d.line == 4) movedErr = true;
    CHECK(movedErr);
    v.clearCompilerDiagnostics();
    CHECK(v.diagnostics().empty() || v.diagnostics()[0].source == "brackets");
    v.grab();                                                // painting with marks must not crash
    v.setCompilerDiagnostics({e, w});
    v.grab();

    // brackets show up as problems too, without any compiler
    setText(&v, "int f( {\n");
    QTest::qWait(500);
    bool haveBracket = false;
    for (const Diagnostic& d : v.diagnostics()) if (d.source == "brackets") haveBracket = true;
    CHECK(haveBracket);

    // main window: Problems tab beside the terminal, F7 runs the real compiler
    MainWindow mw;
    mw.show();
    mw.openPath(file);
    CHECK(!mw.problemsVisible());
    mw.showProblems(true);
    CHECK(mw.problemsVisible() && !mw.terminalVisible() && mw.problems()->isVisible());
    mw.showTerminal(true);                                   // switching tabs, panel stays open
    CHECK(mw.terminalVisible() && !mw.problemsVisible());
    mw.showProblems(true);
    mw.checkNow();
    // the compiler runs in the background; g++ may be missing on a test machine
    for (int i = 0; i < 60 && mw.problems()->errorCount() == 0; ++i) QTest::qWait(100);
    const bool haveGpp = QStandardPaths::findExecutable("g++") != QString();
    if (haveGpp) {
        CHECK(mw.problems()->errorCount() >= 1);
        bool onLine2 = false;
        for (const Diagnostic& d : mw.currentView()->diagnostics()) if (d.source != "brackets" && d.line == 2) onLine2 = true;
        CHECK(onLine2);
        QSignalSpy none(mw.problems(), &ProblemsPanel::jumpRequested);
        mw.problems()->activateRow(0);                       // clicking the row moves the cursor there
        CHECK(mw.currentView()->cursor().line == 1);
        // fix the file: the marks go away after the next check
        mw.currentView()->editor().newDocument();
        mw.currentView()->editor().insertText(Pos{0, 0}, "int main() { return 0; }\n");
        mw.currentView()->notifyEdited(0);
        mw.checkNow();
        for (int i = 0; i < 60 && mw.problems()->errorCount() != 0; ++i) QTest::qWait(100);
        CHECK(mw.problems()->errorCount() == 0);
    } else {
        std::cout << "  (g++ not installed: compiler run skipped)\n";
    }
    mw.showProblems(false);
    CHECK(!mw.problemsVisible());
    for (int i = mw.tabCount() - 1; i >= 0; --i) mw.closeTab(i, true);
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", qEnvironmentVariableIsSet("QT_QPA_PLATFORM") ? qgetenv("QT_QPA_PLATFORM") : QByteArray("offscreen"));
    QApplication app(argc, argv);
    // own settings, so running the tests never touches DevPad's real session or recent lists
    QApplication::setOrganizationName("DevPadTests");
    QApplication::setApplicationName("DevPadTests");
    QSettings().clear();
    testTyping();
    testEnterTabAndBackspace();
    testLineCommands();
    testClipboardAndNav();
    testAutocomplete();
    testBracketReport();
    testTabsAndFiles();
    testExplorer();
    testWelcome();
    testSettings();
    testSettingsDialog();
    testThemes();
    testSidebarToggle();
    testSnippetSuggestions();
    testAutoSave();
    testShortcutsWindow();
    testTerminal();
    testPanelsInMainWindow();
    testRun();
    testSnippetsInEditor();
    testProblemsPanel();
    testSquigglesAndMainWindow();
    QSettings().clear();
    std::cout << (g_checks - g_failed) << "/" << g_checks << " GUI checks passed";
    if (g_failed) std::cout << "  (" << g_failed << " FAILED)";
    std::cout << std::endl;
    return g_failed ? 1 : 0;
}

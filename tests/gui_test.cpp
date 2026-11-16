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
    saved.open(QIODevice::ReadOnly);
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
        f.open(QIODevice::WriteOnly);
        f.write(body);
        return QDir(root).filePath(rel);
    };
    const QString mainCpp = write("src/main.cpp", "int main() {}\n");
    const QString notes = write("notes.txt", "hello\n");
    auto at = [&](const QString& rel) { return QDir(root).filePath(rel); };
    auto pathOf = [](CodeView* v) { return QString::fromStdString(v->editor().doc().path()); };

    MainWindow w;
    w.show();
    QApplication::setActiveWindow(&w);
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
    { QFile f(at("memo.txt")); f.open(QIODevice::ReadOnly); CHECK(f.readAll() == "xhello\n"); }
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
    f.open(QIODevice::WriteOnly);
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
    QSettings().clear();
    std::cout << (g_checks - g_failed) << "/" << g_checks << " GUI checks passed";
    if (g_failed) std::cout << "  (" << g_failed << " FAILED)";
    std::cout << std::endl;
    return g_failed ? 1 : 0;
}

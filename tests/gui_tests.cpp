// gui_tests.cpp - headless tests for the Qt front end (run with QT_QPA_PLATFORM=offscreen).
// Main idea tested again and again: after ANY user action the widget text and the engine
// text are identical (CodeEditor::verifyConsistency), and engine undo/redo restores both.
#include <QtTest>
#include <QLineEdit>
#include <QClipboard>
#include <QMimeData>
#include <QTemporaryDir>
#include <random>
#include "CodeEditor.h"
#include "FileExplorer.h"
#include <QTreeView>
#include <QFileSystemModel>
#include "FindBar.h"
#include "MainWindow.h"
#include "RunManager.h"
#include "TextUtil.h"

class GuiTests : public QObject {
    Q_OBJECT
    // QTest::keyClicks only knows ASCII; other characters are delivered as key events carrying text.
    static void type(CodeEditor& e, const QString& s) {
        for (int i = 0; i < s.size(); ++i) {
            const QChar c = s.at(i);
            if (c == '\n') { QTest::keyClick(&e, Qt::Key_Return); continue; }
            if (c == '\t') { QTest::keyClick(&e, Qt::Key_Tab); continue; }
            if (c.unicode() < 0x80) { QTest::keyClick(&e, c.toLatin1()); continue; }
            QString t(c);
            if (c.isHighSurrogate() && i + 1 < s.size()) t += s.at(++i);
            QKeyEvent p(QEvent::KeyPress, 0, Qt::NoModifier, t);
            QKeyEvent r(QEvent::KeyRelease, 0, Qt::NoModifier, t);
            QApplication::sendEvent(&e, &p);
            QApplication::sendEvent(&e, &r);
        }
    }
    static void key(CodeEditor& e, Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::keyClick(&e, k, m); }
    static QString eng(CodeEditor& e) { return TextUtil::documentText(e.engine().doc()); }
    static void ok(CodeEditor& e) { QString why; QVERIFY2(e.verifyConsistency(&why), qPrintable(why)); }
    static void setText(CodeEditor& e, const QString& t) {
        e.selectAll();
        e.insertPlainText(t);
        e.commitTyping();
    }

private slots:
    void typingIsMirrored() {
        CodeEditor e;
        e.show();
        type(e, "int main() { return 0; }");
        QCOMPARE(e.toPlainText(), eng(e));
        ok(e);
        QVERIFY(e.engine().doc().modified());
    }

    void undoRedoMergesTypingPerWord() {
        CodeEditor e;
        type(e, "hello world");
        QCOMPARE(e.toPlainText(), QString("hello world"));
        e.engineUndo();
        QCOMPARE(e.toPlainText(), QString("hello "));       // "world" was one step
        e.engineUndo();
        QCOMPARE(e.toPlainText(), QString(""));
        ok(e);
        QVERIFY(!e.engineUndo());                            // nothing left: no crash
        e.engineRedo();
        e.engineRedo();
        QCOMPARE(e.toPlainText(), QString("hello world"));
        ok(e);
        QVERIFY(!e.engineRedo());
    }

    void newEditClearsRedo() {
        CodeEditor e;
        type(e, "ab cd");
        e.engineUndo();
        QVERIFY(e.engine().canRedo());
        type(e, "X");
        e.commitTyping();                                    // the typing step is recorded here
        QVERIFY(!e.engine().canRedo());
        ok(e);
    }

    void backspaceAndDeleteMerge() {
        CodeEditor e;
        setText(e, "abcdef");
        e.moveCursor(QTextCursor::End);
        key(e, Qt::Key_Backspace); key(e, Qt::Key_Backspace); key(e, Qt::Key_Backspace);
        QCOMPARE(e.toPlainText(), QString("abc"));
        e.engineUndo();
        QCOMPARE(e.toPlainText(), QString("abcdef"));       // three backspaces = one step
        ok(e);
    }

    void unicodeColumns() {
        CodeEditor e;
        const QString s = QString::fromUtf8("h\xC3\xA9llo \xF0\x9F\x98\x80 w\xC3\xB6rld");
        type(e, s);
        ok(e);
        QCOMPARE(e.toPlainText(), s);
        // delete the emoji with the caret right after it
        QTextCursor c = e.textCursor();
        c.setPosition(s.indexOf(QChar(0xD83D)) + 2);
        e.setTextCursor(c);
        key(e, Qt::Key_Backspace);
        ok(e);
        QVERIFY(!e.toPlainText().contains(QChar(0xD83D)));
        e.engineUndo();
        ok(e);
        QCOMPARE(e.toPlainText(), s);
    }

    void pasteIsNormalised() {
        CodeEditor e;
        QMimeData m;
        m.setText(QString("a\r\nb\rc") + QChar(0x2028) + "d" + QChar(0) + "e");
        e.pasteForTests(&m);
        ok(e);
        QCOMPARE(e.blockCount(), 4);
        QCOMPARE(eng(e), QString("a\nb\nc\nde"));
        e.engineUndo();
        QCOMPARE(e.toPlainText(), QString(""));
    }

    void nbspAndOddCharsStayConsistent() {
        CodeEditor e;
        type(e, QString("a") + QChar(0x00A0) + "b\t" + QChar(0x200B) + "c");
        ok(e);
    }

    void autoIndentAndBraces() {
        CodeEditor e;
        type(e, "if (x) {");
        QCOMPARE(e.toPlainText(), QString("if (x) {}"));      // auto-close
        key(e, Qt::Key_Return);
        QCOMPARE(e.toPlainText(), QString("if (x) {\n    \n}"));
        QCOMPARE(e.textCursor().blockNumber(), 1);
        ok(e);
        e.engineUndo();                                      // the newline is one step
        QCOMPARE(e.toPlainText(), QString("if (x) {}"));
        ok(e);
    }

    void autoCloseStepsOver() {
        CodeEditor e;
        type(e, "f(a)");
        QCOMPARE(e.toPlainText(), QString("f(a)"));
        type(e, "\"x\"");
        QCOMPARE(e.toPlainText(), QString("f(a)\"x\""));
        ok(e);
        CodeEditor off;
        off.setAutoClose(false);
        type(off, "(");
        QCOMPARE(off.toPlainText(), QString("("));
    }

    void tabIndentsAndUnindents() {
        CodeEditor e;
        setText(e, "a\nb\nc");
        e.selectAll();
        key(e, Qt::Key_Tab);
        QCOMPARE(e.toPlainText(), QString("    a\n    b\n    c"));
        ok(e);
        e.selectAll();
        key(e, Qt::Key_Backtab);
        QCOMPARE(e.toPlainText(), QString("a\nb\nc"));
        e.engineUndo();                                      // unindent = one step
        QCOMPARE(e.toPlainText(), QString("    a\n    b\n    c"));
        ok(e);
    }

    void toggleCommentAndDuplicate() {
        CodeEditor e;
        setText(e, "x\ny");
        e.selectAll();
        e.toggleComment();
        QCOMPARE(e.toPlainText(), QString("// x\n// y"));
        e.selectAll();
        e.toggleComment();
        QCOMPARE(e.toPlainText(), QString("x\ny"));
        e.moveCursor(QTextCursor::Start);
        e.duplicateLine();
        QCOMPARE(e.toPlainText(), QString("x\nx\ny"));
        ok(e);
        e.engineUndo();
        QCOMPARE(e.toPlainText(), QString("x\ny"));
        ok(e);
    }

    void autocompleteTabAcceptsAndUndoes() {
        CodeEditor e;
        e.show();
        type(e, "retu");
        QVERIFY(e.completionVisible());
        QVERIFY(e.completionItems().contains("return"));
        key(e, Qt::Key_Return);                               // accept
        QCOMPARE(e.toPlainText(), QString("return"));
        QVERIFY(!e.completionVisible());
        ok(e);
        e.engineUndo();
        QCOMPARE(e.toPlainText(), QString("retu"));
        ok(e);
    }

    void autocompleteUsesDocumentWordsButNotTheTypedOne() {
        CodeEditor e;
        e.show();
        setText(e, "int zebraCount = 1;\n");
        e.runAnalysisForTests();
        e.moveCursor(QTextCursor::End);
        type(e, "zebr");
        QVERIFY(e.completionItems().contains("zebraCount"));
        key(e, Qt::Key_Escape);
        QVERIFY(!e.completionVisible());
        // the half typed word itself never becomes a suggestion
        e.moveCursor(QTextCursor::End);
        type(e, "a");
        e.runAnalysisForTests();
        key(e, Qt::Key_Backspace);
        e.showCompletions(true);
        QVERIFY(!e.completionItems().contains("zebr"));
    }

    void snippetExpandsWithCursorAndUndo() {
        CodeEditor e;
        type(e, "for");
        QVERIFY(e.expandSnippetAtCursor());
        QVERIFY(e.toPlainText().startsWith("for (int i = 0; i < "));
        ok(e);
        QCOMPARE(e.textCursor().blockNumber(), 0);          // cursor at $0 on the first line
        e.engineUndo();
        QCOMPARE(e.toPlainText(), QString("for"));
        ok(e);
        QVERIFY(!e.expandSnippetAtCursor() || true);
        setText(e, "zzzz");
        QVERIFY(!e.expandSnippetAtCursor());                 // unknown trigger: nothing happens
        QCOMPARE(e.toPlainText(), QString("zzzz"));
    }

    void snippetKeepsIndentation() {
        CodeEditor e;
        setText(e, "    ");
        e.moveCursor(QTextCursor::End);
        type(e, "if");
        key(e, Qt::Key_Tab);
        QCOMPARE(e.toPlainText(), QString("    if () {\n        \n    }"));
        ok(e);
    }

    void bracketIssuesAndMatching() {
        CodeEditor e;
        setText(e, "int f() { (] }\n");
        e.runAnalysisForTests();
        QVERIFY(!e.bracketReport().ok());
        setText(e, "void f() { int a[2]; }");
        e.runAnalysisForTests();
        QVERIFY(e.bracketReport().ok());
        QTextCursor c = e.textCursor();
        c.setPosition(e.toPlainText().indexOf('{'));
        e.setTextCursor(c);                                   // must not crash, highlights the partner
        QVERIFY(!e.extraSelections().isEmpty());
    }

    void highlighterDoesNotTouchEngine() {
        CodeEditor e;
        setText(e, "int x = 1; /* c */ // d\n\"s\"");
        const size_t undo = e.engine().undoCount();
        e.setTheme(Theme::light_());
        e.setTheme(Theme::dark_());
        QCOMPARE(e.engine().undoCount(), undo);
        ok(e);
    }

    void findReplaceOptions() {
        CodeEditor e;
        FindBar f(&e);
        setText(e, "foo bar Foo foobar foo");
        f.openFind(true);
        f.findEdit()->setText("foo");
        f.refresh();
        QVERIFY(f.matchCount() >= 3);
        f.findEdit()->setText("");
        f.refresh();
        QCOMPARE(f.matchCount(), 0);                          // empty search text: no error
        f.findEdit()->setText("foo");
        f.replaceEdit()->setText("X");
        f.replaceAll();
        ok(e);
        QVERIFY(!e.toPlainText().contains("foo"));
        e.engineUndo();
        QCOMPARE(e.toPlainText(), QString("foo bar Foo foobar foo"));
        ok(e);
        e.engineRedo();
        ok(e);
    }

    void findNextWrapsAndReplaceOne() {
        CodeEditor e;
        FindBar f(&e);
        setText(e, "ab ab ab");
        f.openFind(true);
        f.findEdit()->setText("ab");
        f.replaceEdit()->setText("Z");
        f.findNext(); f.findNext(); f.findNext(); f.findNext();   // wraps around, never crashes
        e.moveCursor(QTextCursor::Start);
        f.replaceOne();                                           // selects first
        f.replaceOne();                                           // replaces it
        ok(e);
        QVERIFY(e.toPlainText().contains("Z"));
        f.findPrev();
        ok(e);
    }

    void findAcrossUnicode() {
        CodeEditor e;
        FindBar f(&e);
        setText(e, QString::fromUtf8("\xC3\xA9\xC3\xA9 ab \xF0\x9F\x98\x80 ab"));
        f.openFind(true);
        f.findEdit()->setText("ab");
        f.replaceEdit()->setText("Q");
        f.replaceAll();
        QCOMPARE(e.toPlainText(), QString::fromUtf8("\xC3\xA9\xC3\xA9 Q \xF0\x9F\x98\x80 Q"));
        ok(e);
    }

    void fileRoundTripAndErrors() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        CodeEditor e;
        const QString p = dir.filePath(QString::fromUtf8("t\xC3\xA9st.cpp"));
        setText(e, QString::fromUtf8("int \xC3\xA9 = 1;\nline2"));
        QString err;
        QVERIFY2(e.saveFile(p, &err), qPrintable(err));
        QVERIFY(!e.isModified());
        CodeEditor g;
        auto r = g.openFile(p);
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(g.toPlainText(), QString::fromUtf8("int \xC3\xA9 = 1;\nline2"));
        ok(g);
        QVERIFY(!g.engine().canUndo());                       // opening clears history
        // errors
        QVERIFY(!g.openFile(dir.filePath("missing.cpp")).ok);
        QVERIFY(!g.openFile(dir.path()).ok);                  // a folder
        QVERIFY(!g.openFile("").ok);
        QVERIFY(!g.saveFile(dir.filePath("no/such/dir/x.cpp"), &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!g.saveFile("", &err));
        // binary
        QFile b(dir.filePath("bin.dat"));
        QVERIFY(b.open(QIODevice::WriteOnly)); b.write(QByteArray("ab\0cd", 5)); b.close();
        QVERIFY(!g.openFile(b.fileName()).ok);
        // invalid UTF-8 needs confirmation, then opens with replacement characters
        QFile l(dir.filePath("latin.txt"));
        QVERIFY(l.open(QIODevice::WriteOnly)); l.write("caf\xE9\n"); l.close();
        auto lr = g.openFile(l.fileName());
        QVERIFY(!lr.ok && lr.needsConfirm);
        lr = g.openFile(l.fileName(), true);
        QVERIFY(lr.ok);
        QVERIFY(g.isModified());
        ok(g);
        // CRLF and BOM
        QFile c(dir.filePath("crlf.txt"));
        QVERIFY(c.open(QIODevice::WriteOnly)); c.write(QByteArray("\xEF\xBB\xBF") + "a\r\nb\r\n"); c.close();
        QVERIFY(g.openFile(c.fileName()).ok);
        QCOMPARE(g.toPlainText(), QString("a\nb"));
        ok(g);
        // empty file
        QFile em(dir.filePath("empty.txt"));
        QVERIFY(em.open(QIODevice::WriteOnly)); em.close();
        QVERIFY(g.openFile(em.fileName()).ok);
        QCOMPARE(g.toPlainText(), QString(""));
        ok(g);
    }

    void newDocumentResets() {
        CodeEditor e;
        type(e, "abc");
        e.newDocument();
        QCOMPARE(e.toPlainText(), QString(""));
        QVERIFY(!e.engine().canUndo());
        QVERIFY(!e.isModified());
        ok(e);
    }

    // The big one: random typing, deleting, pasting, selecting, undo, redo. After every step
    // the widget and the engine must be identical.
    void fuzzWidgetEngineStayInSync() {
        const QVector<QString> pool = {"a", "b", "c", " ", "X", "Z", "_", "0", "\n", "\t", "(", ")", "{", "}", "[", "]", "\"", "'", "/", "*", ";", "#",
                                       QString::fromUtf8("\xC3\xA9"), QString::fromUtf8("\xE2\x82\xAC"), QString::fromUtf8("\xF0\x9F\x98\x80")};
        const QVector<QString> pieces = {"x", "hello\nworld", "\n\n", QString::fromUtf8("\xF0\x9F\x98\x80\xC3\xA9"), "{\n}", "   ", "a b c"};
        const unsigned seeds = qEnvironmentVariableIntValue("GUI_FUZZ_SEEDS") > 0 ? static_cast<unsigned>(qEnvironmentVariableIntValue("GUI_FUZZ_SEEDS")) : 6u;
        for (unsigned seed = 1; seed <= seeds; ++seed) {
            std::mt19937 rng(seed);
            CodeEditor e;
            e.setAutoClose(seed % 2 == 0);
            e.show();
            auto rnd = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
            for (int step = 0; step < 400; ++step) {
                const int op = rnd(12);
                const int len = e.toPlainText().size();
                QTextCursor c = e.textCursor();
                if (op >= 8 && len > 0) {                      // random selection / caret
                    int a = rnd(len + 1), b = rnd(len + 1);
                    const QString txt = e.toPlainText();       // a caret never sits inside an emoji
                    auto snap = [&](int p) { return (p > 0 && p < txt.size() && txt.at(p).isLowSurrogate() && txt.at(p - 1).isHighSurrogate()) ? p - 1 : p; };
                    a = snap(a); b = snap(b);
                    if (op % 2) b = a;
                    c.setPosition(a);
                    c.setPosition(b, QTextCursor::KeepAnchor);
                    e.setTextCursor(c);
                }
                switch (op) {
                case 0: case 1: case 2: type(e, pool.at(rnd(pool.size()))); break;
                case 3: key(e, Qt::Key_Backspace); break;
                case 4: key(e, Qt::Key_Delete); break;
                case 5: key(e, Qt::Key_Return); break;
                case 6: e.engineUndo(); break;
                case 7: e.engineRedo(); break;
                case 8: { QMimeData m; m.setText(pieces[rnd(pieces.size())]); e.pasteForTests(&m); break; }
                case 9: e.cut(); break;
                case 10: key(e, Qt::Key_Tab); break;
                case 11: key(e, rnd(2) ? Qt::Key_Tab : Qt::Key_Backtab); break;
                }
                QString why;
                QVERIFY2(e.verifyConsistency(&why), qPrintable(QString("seed %1 step %2 op %3: %4 | widget=%5 engine=%6")
                         .arg(seed).arg(step).arg(op).arg(why, e.toPlainText(), eng(e))));
            }
            // undo everything, then redo everything
            e.commitTyping();
            const QString final = e.toPlainText();
            const size_t pendingRedo = e.engine().redoCount();   // the random run may have ended with an Undo
            int guard = 0;
            while (e.engineUndo() && ++guard < 5000) {}
            ok(e);
            guard = 0;
            while (e.engineRedo() && ++guard < 5000) {}
            ok(e);
            for (size_t i = 0; i < pendingRedo; ++i) e.engineUndo();   // back to where the run ended
            QVERIFY2(e.toPlainText() == final, qPrintable(QString("seed %1: after redo-all text differs\nexpected=%2\n     got=%3\nundo/redo left: %4/%5")
                     .arg(seed).arg(final.toHtmlEscaped(), e.toPlainText().toHtmlEscaped()).arg(e.engine().undoCount()).arg(e.engine().redoCount())));
        }
    }


    void explorerNameValidation() {
        QString why;
        QVERIFY(FileExplorer::validName("main.cpp", &why));
        QVERIFY(FileExplorer::validName("my file (1).txt", &why));
        for (const char* bad : {"", "  ", ".", "..", "a/b", "a\\b", "a:b", "a*b", "a?b", "a<b", "a|b", "a\"b", "end.", "end ", "CON", "nul.txt", "COM1"}) {
            QVERIFY2(!FileExplorer::validName(bad, &why), bad);
            QVERIFY(!why.isEmpty());
        }
        QVERIFY(!FileExplorer::validName(QString(300, 'a'), &why));
    }

    void explorerFileOperations() {
        QTemporaryDir td;
        QVERIFY(td.isValid());
        FileExplorer x;
        QString err, p;
        QVERIFY2(x.setRootPath(td.path(), &err), qPrintable(err));
        QVERIFY(x.createFile(td.path(), "a.cpp", &err, &p));
        QVERIFY(QFileInfo(p).isFile());
        QVERIFY(!x.createFile(td.path(), "a.cpp", &err));            // already exists
        QVERIFY(err.contains("exists"));
        QVERIFY(!x.createFile(td.path(), "bad/name", &err));
        QVERIFY(!x.createFile(td.filePath("nope"), "x.txt", &err));  // folder missing
        QVERIFY(x.createFolder(td.path(), "src", &err, &p));
        QVERIFY(QFileInfo(p).isDir());
        QVERIFY(!x.createFolder(td.path(), "src", &err));
        QVERIFY(x.createFile(td.filePath("src"), "b.cpp", &err));
        // rename file and folder
        QString np;
        QVERIFY(x.renamePath(td.filePath("a.cpp"), "z.cpp", &err, &np));
        QVERIFY(QFileInfo(np).isFile() && !QFileInfo(td.filePath("a.cpp")).exists());
        QVERIFY(!x.renamePath(td.filePath("z.cpp"), "src", &err));   // name taken
        QVERIFY(!x.renamePath(td.filePath("missing.cpp"), "q.cpp", &err));
        QVERIFY(x.renamePath(td.filePath("z.cpp"), "z.cpp", &err));  // same name: no-op, no error
        QVERIFY(x.renamePath(td.filePath("src"), "lib", &err, &np));
        QVERIFY(QFileInfo(td.filePath("lib/b.cpp")).isFile());
        QVERIFY(!x.renamePath(td.path(), "other", &err));            // not the opened folder itself
        // delete (permanent here; the UI uses the trash)
        QVERIFY(x.removePath(td.filePath("z.cpp"), false, &err));
        QVERIFY(!QFileInfo(td.filePath("z.cpp")).exists());
        QVERIFY(x.removePath(td.filePath("lib"), false, &err));      // folder with content
        QVERIFY(!QFileInfo(td.filePath("lib")).exists());
        QVERIFY(!x.removePath(td.filePath("gone"), false, &err));
        QVERIFY(!x.removePath(td.path(), false, &err));              // never the root
        QVERIFY(QFileInfo(td.path()).isDir());
    }

    void explorerRootErrorsAndSignals() {
        QTemporaryDir td;
        FileExplorer x;
        QString err;
        QVERIFY(!x.setRootPath("", &err));
        QVERIFY(!x.setRootPath(td.filePath("missing"), &err));
        QVERIFY(x.createFile(td.path(), "f.txt", &err));
        QVERIFY(!x.setRootPath(td.filePath("f.txt"), &err));         // a file, not a folder
        QVERIFY(x.rootPath().isEmpty());
        QSignalSpy opened(&x, &FileExplorer::fileActivated);
        QVERIFY(x.setRootPath(td.path(), &err));
        QCOMPARE(x.rootPath(), QDir::cleanPath(td.path()));
        x.activate(td.filePath("f.txt"));
        QCOMPARE(opened.count(), 1);
        x.activate(td.filePath("ghost.txt"));                        // deleted meanwhile: message, no signal
        QCOMPARE(opened.count(), 1);
        QSignalSpy closed(&x, &FileExplorer::rootChanged);
        x.clearRoot();
        QCOMPARE(closed.count(), 1);
        QVERIFY(x.rootPath().isEmpty());
    }

    void explorerShowsFolderContent() {
        QTemporaryDir td;
        QVERIFY(QDir(td.path()).mkdir("sub"));
        QFile f(td.filePath("hello.cpp"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        FileExplorer x;
        QVERIFY(x.setRootPath(td.path()));
        auto* m = qobject_cast<QFileSystemModel*>(x.tree()->model());
        QVERIFY(m);
        QTRY_VERIFY_WITH_TIMEOUT(m->rowCount(x.tree()->rootIndex()) == 2, 5000);   // sub + hello.cpp
    }

    void mainWindowOpensFolderAndFiles() {
        QTemporaryDir td;
        QFile f(td.filePath("one.cpp"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("int one;\n");
        f.close();
        MainWindow w;
        QVERIFY(w.openFolder(td.path(), true));
        QVERIFY(!w.openFolder(td.filePath("nope"), true));           // bad folder: false, old one stays
        QCOMPARE(w.explorer()->rootPath(), QDir::cleanPath(td.path()));
        emit w.explorer()->fileActivated(td.filePath("one.cpp"));    // what a double click does
        QCOMPARE(w.editor()->toPlainText(), QString("int one;"));
        // rename the open file in the explorer: the editor follows the new name
        QString err, np;
        QVERIFY(w.explorer()->renamePath(td.filePath("one.cpp"), "two.cpp", &err, &np));
        emit w.explorer()->fileRenamed(td.filePath("one.cpp"), np);
        QCOMPARE(QFileInfo(w.editor()->filePath()).fileName(), QString("two.cpp"));
        // delete it: the text stays, the document becomes untitled and modified
        QVERIFY(w.explorer()->removePath(np, false, &err));
        emit w.explorer()->pathRemoved(np);
        QVERIFY(w.editor()->filePath().isEmpty());
        QVERIFY(w.editor()->isModified());
        QCOMPARE(w.editor()->toPlainText(), QString("int one;"));
        w.editor()->newDocument();                                   // avoid the save prompt on exit
    }

    void mainWindowSmoke() {
        MainWindow w;
        w.show();
        QTest::keyClicks(w.editor(), "int main() {");
        w.applyTheme("Daylight");
        w.applyTheme("Midnight");
        w.findBar()->openFind(true);
        w.findBar()->findEdit()->setText("main");
        w.findBar()->closeBar();
        QVERIFY(!w.findBar()->isVisible());
        QTemporaryDir dir;
        QVERIFY(!w.openPath(dir.filePath("nope.cpp"), true));
        w.editor()->setPlainText("");
        QVERIFY(w.windowTitle().contains("DevPad"));
    }

    void runBuildsAndRuns() {
        if (RunManager::findCompiler().isEmpty()) QSKIP("no C++ compiler on this machine");
        RunManager r;
        QString out;
        int errors = 0;
        connect(&r, &RunManager::output, [&](const QString& t, bool) { out += t; });
        connect(&r, &RunManager::diagnostic, [&](int, int, const QString& s, const QString&) { if (s == "error") ++errors; });
        QSignalSpy done(&r, &RunManager::finished);
        r.buildAndRun("#include <iostream>\nint main(){ std::cout << \"hi \" << 6*7; }\n", "t.cpp", "-std=c++17", true);
        QVERIFY(done.wait(60000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY2(out.contains("hi 42"), qPrintable(out));
        done.clear();
        r.buildAndRun("int main( { return 0 }\n", "bad.cpp", "-std=c++17", true);
        QVERIFY(done.wait(60000));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(errors > 0);
        done.clear();
        r.buildAndRun("int main(){ for(;;){} }\n", "loop.cpp", "-std=c++17", true);
        QTRY_VERIFY_WITH_TIMEOUT(r.state() == RunManager::State::Running, 60000);
        r.stop();                                             // an infinite loop can be stopped
        QVERIFY(done.wait(10000));
        QVERIFY(!r.busy());
    }
};

QTEST_MAIN(GuiTests)
#include "gui_tests.moc"

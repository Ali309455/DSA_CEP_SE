#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTextBlock>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "CodeEditor.h"
#include "FindBar.h"
#include "RunManager.h"
#include "SnippetDialog.h"
#include "TextUtil.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setAcceptDrops(true);
    resize(1100, 740);

    editor_ = new CodeEditor(this);
    find_ = new FindBar(editor_, this);
    run_ = new RunManager(this);
    compilerFlags_ = QStringLiteral("-std=c++17 -Wall -Wextra -g");

    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(editor_, 1);
    lay->addWidget(find_);
    setCentralWidget(central);

    // ---- bottom panel: Problems + Output
    tabs_ = new QTabWidget(this);
    problems_ = new QTreeWidget(this);
    problems_->setHeaderLabels({tr("Severity"), tr("Line"), tr("Col"), tr("Message")});
    problems_->setRootIsDecorated(false);
    problems_->header()->setStretchLastSection(true);
    problems_->setColumnWidth(0, 90);
    problems_->setColumnWidth(1, 55);
    problems_->setColumnWidth(2, 45);
    connect(problems_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) {
        jumpTo(it->text(1).toInt(), it->text(2).toInt());
    });
    auto* outW = new QWidget(this);
    auto* outL = new QVBoxLayout(outW);
    outL->setContentsMargins(0, 0, 0, 0);
    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(20000);
    output_->setFont(editor_->font());
    input_ = new QLineEdit(this);
    input_->setPlaceholderText(tr("Type input for the running program and press Enter"));
    outL->addWidget(output_, 1);
    outL->addWidget(input_);
    tabs_->addTab(problems_, tr("Problems"));
    tabs_->addTab(outW, tr("Output"));
    auto* dock = new QDockWidget(tr("Panel"), this);
    dock->setObjectName("panelDock");
    dock->setWidget(tabs_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    inspector_ = new QPlainTextEdit(this);
    inspector_->setReadOnly(true);
    inspector_->setFont(editor_->font());
    auto* insDock = new QDockWidget(tr("Engine inspector"), this);
    insDock->setObjectName("inspectorDock");
    insDock->setWidget(inspector_);
    addDockWidget(Qt::RightDockWidgetArea, insDock);
    insDock->hide();

    // ---- status bar
    posLabel_ = new QLabel(this);
    bracketLabel_ = new QLabel(this);
    historyLabel_ = new QLabel(this);
    infoLabel_ = new QLabel(QStringLiteral("UTF-8 · LF"), this);
    for (QLabel* l : {posLabel_, bracketLabel_, historyLabel_, infoLabel_}) statusBar()->addPermanentWidget(l);

    buildMenus();
    insDock->toggleViewAction()->setText(tr("Engine inspector"));
    dock->toggleViewAction()->setText(tr("Problems / Output panel"));
    menuBar()->actions().at(2)->menu()->addSeparator();
    menuBar()->actions().at(2)->menu()->addAction(dock->toggleViewAction());
    menuBar()->actions().at(2)->menu()->addAction(insDock->toggleViewAction());

    connect(editor_, &CodeEditor::stateChanged, this, &MainWindow::refreshState);
    connect(editor_, &CodeEditor::analysisUpdated, this, &MainWindow::refreshProblems);
    connect(editor_, &CodeEditor::analysisUpdated, this, &MainWindow::refreshInspector);
    connect(editor_, &CodeEditor::message, this, [this](const QString& m) { statusBar()->showMessage(m, 4000); });
    connect(find_, &FindBar::message, this, [this](const QString& m) { statusBar()->showMessage(m, 5000); });
    connect(editor_, &CodeEditor::filesDropped, this, [this](const QStringList& f) { if (!f.isEmpty() && maybeSave()) openPath(f.first()); });
    connect(run_, &RunManager::output, this, &MainWindow::appendOutput);
    connect(run_, &RunManager::diagnostic, this, &MainWindow::addCompilerDiagnostic);
    connect(run_, &RunManager::stateChanged, this, [this] {
        runA_->setEnabled(!run_->busy());
        stopA_->setEnabled(run_->busy());
        input_->setEnabled(run_->state() == RunManager::State::Running);
    });
    connect(run_, &RunManager::finished, this, [this](bool ok, const QString& s) {
        statusBar()->showMessage(s, 6000);
        if (!ok && !compilerItems_.isEmpty()) tabs_->setCurrentIndex(0);
        else tabs_->setCurrentIndex(1);
    });
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        appendOutput(input_->text() + "\n", false);
        run_->writeInput(input_->text());
        input_->clear();
    });
    stopA_->setEnabled(false);
    input_->setEnabled(false);

    loadSettings();
    applyTheme(themeName_);
    refreshState();
    refreshProblems();
    refreshInspector();
    editor_->setFocus();
}

MainWindow::~MainWindow() {}

QString MainWindow::snippetFile() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/snippets.txt");
}

// ====================================================================== menus

void MainWindow::buildMenus() {
    auto act = [this](QMenu* m, const QString& text, const QKeySequence& ks, auto slot) {
        QAction* a = m->addAction(text);
        if (!ks.isEmpty()) a->setShortcut(ks);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    QMenu* f = menuBar()->addMenu(tr("&File"));
    act(f, tr("&New"), QKeySequence::New, [this] { fileNew(); });
    act(f, tr("&Open..."), QKeySequence::Open, [this] { fileOpen(); });
    recentMenu_ = f->addMenu(tr("Open &Recent"));
    f->addSeparator();
    saveA_ = act(f, tr("&Save"), QKeySequence::Save, [this] { fileSave(); });
    act(f, tr("Save &As..."), QKeySequence::SaveAs, [this] { fileSaveAs(); });
    f->addSeparator();
    act(f, tr("&Quit"), QKeySequence::Quit, [this] { close(); });

    QMenu* e = menuBar()->addMenu(tr("&Edit"));
    undoA_ = act(e, tr("&Undo"), QKeySequence::Undo, [this] { editor_->engineUndo(); });
    redoA_ = act(e, tr("&Redo"), QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { editor_->engineRedo(); });
    e->addSeparator();
    act(e, tr("Cu&t"), QKeySequence::Cut, [this] { editor_->cut(); });
    act(e, tr("&Copy"), QKeySequence::Copy, [this] { editor_->copy(); });
    act(e, tr("&Paste"), QKeySequence::Paste, [this] { editor_->paste(); });
    act(e, tr("Select &All"), QKeySequence::SelectAll, [this] { editor_->selectAll(); });
    e->addSeparator();
    act(e, tr("&Find..."), QKeySequence::Find, [this] { find_->openFind(false); });
    act(e, tr("&Replace..."), QKeySequence(Qt::CTRL | Qt::Key_H), [this] { find_->openFind(true); });
    act(e, tr("Find &Next"), QKeySequence(Qt::Key_F3), [this] { if (find_->isVisible()) find_->findNext(); else find_->openFind(false); });
    act(e, tr("Find &Previous"), QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] { if (find_->isVisible()) find_->findPrev(); else find_->openFind(false); });
    act(e, tr("&Go to Line..."), QKeySequence(Qt::CTRL | Qt::Key_G), [this] { goToLineDialog(); });
    e->addSeparator();
    act(e, tr("Toggle &Comment"), QKeySequence(Qt::CTRL | Qt::Key_Slash), [this] { editor_->toggleComment(); });
    act(e, tr("&Duplicate Line"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), [this] { editor_->duplicateLine(); });
    act(e, tr("&Indent"), QKeySequence(), [this] { editor_->indentSelection(true); });
    act(e, tr("U&nindent"), QKeySequence(), [this] { editor_->indentSelection(false); });
    e->addSeparator();
    act(e, tr("Auto&complete"), QKeySequence(Qt::CTRL | Qt::Key_Space), [this] { editor_->showCompletions(true); });
    act(e, tr("Expand &Snippet"), QKeySequence(Qt::CTRL | Qt::Key_J), [this] {
        if (!editor_->expandSnippetAtCursor()) statusBar()->showMessage(tr("No snippet trigger before the cursor."), 3000);
    });
    act(e, tr("Manage S&nippets..."), QKeySequence(), [this] {
        SnippetDialog d(editor_->snippets(), snippetFile(), this);
        d.exec();
    });
    autoCloseA_ = act(e, tr("Auto-close &brackets and quotes"), QKeySequence(), [this](bool) {});
    autoCloseA_->setCheckable(true);
    autoCloseA_->setChecked(true);
    connect(autoCloseA_, &QAction::toggled, this, [this](bool on) { editor_->setAutoClose(on); });

    QMenu* v = menuBar()->addMenu(tr("&View"));
    QMenu* tm = v->addMenu(tr("&Theme"));
    auto* grp = new QActionGroup(this);
    darkA_ = tm->addAction(tr("Midnight (dark)"));
    lightA_ = tm->addAction(tr("Daylight (light)"));
    for (QAction* a : {darkA_, lightA_}) { a->setCheckable(true); grp->addAction(a); }
    connect(darkA_, &QAction::triggered, this, [this] { applyTheme("Midnight"); });
    connect(lightA_, &QAction::triggered, this, [this] { applyTheme("Daylight"); });
    act(v, tr("Zoom &In"), QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] { editor_->zoomIn(1); });
    act(v, tr("Zoom &Out"), QKeySequence(Qt::CTRL | Qt::Key_Minus), [this] { editor_->zoomOut(1); });
    wrapA_ = act(v, tr("&Word Wrap"), QKeySequence(), [](bool) {});
    wrapA_->setCheckable(true);
    connect(wrapA_, &QAction::toggled, this, [this](bool on) {
        editor_->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });

    QMenu* r = menuBar()->addMenu(tr("&Run"));
    runA_ = act(r, tr("&Build && Run"), QKeySequence(Qt::Key_F5), [this] { runClicked(); });
    stopA_ = act(r, tr("&Stop"), QKeySequence(Qt::SHIFT | Qt::Key_F5), [this] { run_->stop(); });
    act(r, tr("Build &Only"), QKeySequence(Qt::Key_F7), [this] {
        compilerItems_.clear();
        problems_->clear();
        run_->buildAndRun(editor_->widgetText() + "\n", editor_->displayName(), compilerFlags_, false);
    });
    act(r, tr("Compiler &Flags..."), QKeySequence(), [this] {
        bool ok = false;
        const QString t = QInputDialog::getText(this, tr("Compiler flags"), tr("Flags passed to the compiler:"), QLineEdit::Normal, compilerFlags_, &ok);
        if (ok) compilerFlags_ = t;
    });
    act(r, tr("Next &Problem"), QKeySequence(Qt::Key_F8), [this] { nextProblem(); });

    QMenu* h = menuBar()->addMenu(tr("&Help"));
    act(h, tr("&Shortcuts"), QKeySequence(Qt::Key_F1), [this] {
        QMessageBox::information(this, tr("Shortcuts"),
            tr("Ctrl+Z / Ctrl+Y  Undo / Redo (engine, two stacks)\nCtrl+F / Ctrl+H  Find / Replace (KMP)\n"
               "F3 / Shift+F3  Next / previous match\nCtrl+G  Go to line\nTab  Expand snippet / indent\n"
               "Ctrl+Space  Autocomplete (Trie)\nCtrl+/  Toggle comment\nCtrl+Shift+D  Duplicate line\n"
               "F5  Build && Run     Shift+F5  Stop     F8  Next problem"));
    });
    act(h, tr("&About DevPad"), QKeySequence(), [this] {
        QMessageBox::about(this, tr("About DevPad"),
            tr("DevPad - C++ code editor (CS-218 DSA CEP).\n\nText buffer: doubly linked list of lines. Undo/redo: two stacks. "
               "Search: KMP. Brackets: stack. Autocomplete: Trie. Snippets: hash table.\nThis window is the Qt front end (phase 5)."));
    });

    // toolbar
    QToolBar* tb = addToolBar(tr("Main"));
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    for (const QString& n : {tr("New"), tr("Open"), tr("Save")}) Q_UNUSED(n);
    tb->addAction(tr("New"), this, [this] { fileNew(); });
    tb->addAction(tr("Open"), this, [this] { fileOpen(); });
    tb->addAction(tr("Save"), this, [this] { fileSave(); });
    tb->addSeparator();
    tb->addAction(undoA_);
    tb->addAction(redoA_);
    tb->addSeparator();
    tb->addAction(runA_);
    tb->addAction(stopA_);
    undoA_->setIconText(tr("Undo"));
    redoA_->setIconText(tr("Redo"));
    runA_->setIconText(QStringLiteral("▶ Run"));
    stopA_->setIconText(QStringLiteral("■ Stop"));
}

// ====================================================================== state

void MainWindow::refreshState() {
    const Editor& ed = editor_->engine();
    const QTextCursor c = editor_->textCursor();
    posLabel_->setText(tr("Ln %1, Col %2%3  ").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1)
                           .arg(c.hasSelection() ? tr(" (%1 selected)").arg(c.selectionEnd() - c.selectionStart()) : QString()));
    historyLabel_->setText(tr("Undo %1 / Redo %2  ").arg(ed.undoCount()).arg(ed.redoCount()));
    infoLabel_->setText(tr("%1 lines · UTF-8 · LF").arg(ed.doc().lineCount()));
    const bool canU = ed.canUndo() || ed.inGroup();
    undoA_->setEnabled(canU);
    redoA_->setEnabled(ed.canRedo() && !ed.inGroup());   // typing in progress already invalidates redo
    undoA_->setText(ed.canUndo() ? tr("&Undo %1").arg(TextUtil::fromStd(ed.nextUndoLabel())) : tr("&Undo"));
    redoA_->setText(ed.canRedo() ? tr("&Redo %1").arg(TextUtil::fromStd(ed.nextRedoLabel())) : tr("&Redo"));
    setWindowTitle(QStringLiteral("%1%2 — DevPad").arg(editor_->displayName(), editor_->isModified() ? "*" : ""));
}

void MainWindow::refreshProblems() {
    const BracketReport& r = editor_->bracketReport();
    // keep compiler diagnostics, replace bracket ones
    for (int i = problems_->topLevelItemCount() - 1; i >= 0; --i)
        if (!compilerItems_.contains(problems_->topLevelItem(i))) delete problems_->takeTopLevelItem(i);
    for (size_t i = 0; i < r.issues.size(); ++i) {
        const BracketIssue& is = r.issues[i];
        auto* it = new QTreeWidgetItem(problems_);
        it->setText(0, tr("brackets"));
        it->setText(1, QString::number(is.pos.line + 1));
        it->setText(2, QString::number(is.pos.col + 1));
        it->setText(3, TextUtil::fromStd(is.message));
        it->setForeground(0, editor_->theme().error);
    }
    if (r.ok()) bracketLabel_->setText(tr("✓ Brackets OK  "));
    else bracketLabel_->setText(tr("✗ %1 bracket problem(s)%2  ").arg(r.issues.size()).arg(r.truncated ? "+" : ""));
    bracketLabel_->setStyleSheet(r.ok() ? QString() : QStringLiteral("color:%1;").arg(editor_->theme().error.name()));
}

void MainWindow::refreshInspector() {
    if (!inspector_->isVisible()) return;
    const Editor& ed = editor_->engine();
    const BracketReport& br = editor_->bracketReport();
    const SnippetTable& sn = editor_->snippets();
    QString s;
    s += tr("DOCUMENT (doubly linked list of lines)\n  lines: %1   characters: %2\n\n").arg(ed.doc().lineCount()).arg(ed.doc().charCount());
    s += tr("UNDO / REDO (two stacks)\n  undo stack: %1 (limit %2)\n  redo stack: %3\n  next undo: %4\n  next redo: %5\n\n")
             .arg(ed.undoCount()).arg(ed.historyLimit()).arg(ed.redoCount())
             .arg(ed.canUndo() ? TextUtil::fromStd(ed.nextUndoLabel()) : "-").arg(ed.canRedo() ? TextUtil::fromStd(ed.nextRedoLabel()) : "-");
    s += tr("BRACKETS (stack)\n  matched pairs: %1   problems: %2   max depth: %3\n\n").arg(br.pairs.size()).arg(br.issues.size()).arg(br.maxDepth);
    s += tr("AUTOCOMPLETE (two Tries)\n  dictionary words: %1   document words: %2\n\n")
             .arg(editor_->autocomplete().dictionarySize()).arg(editor_->autocomplete().documentWordCount());
    s += tr("SNIPPETS (hash table, chaining)\n  snippets: %1   buckets: %2   longest chain: %3\n")
             .arg(sn.size()).arg(sn.bucketCount()).arg(sn.longestChain());
    inspector_->setPlainText(s);
}

// ====================================================================== files

bool MainWindow::maybeSave() {
    if (!editor_->isModified()) return true;
    const auto b = QMessageBox::question(this, tr("Unsaved changes"),
                                         tr("Save changes to %1?").arg(editor_->displayName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (b == QMessageBox::Cancel) return false;
    if (b == QMessageBox::Save) return fileSave();
    return true;
}

void MainWindow::fileNew() {
    if (!maybeSave()) return;
    editor_->newDocument();
    problems_->clear();
    compilerItems_.clear();
    statusBar()->showMessage(tr("New document."), 2000);
}

bool MainWindow::openPath(const QString& path, bool silent) {
    CodeEditor::LoadResult r = editor_->openFile(path, false);
    if (r.needsConfirm) {
        if (silent) return false;
        const auto b = QMessageBox::warning(this, tr("Not valid UTF-8"), r.message, QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel);
        if (b != QMessageBox::Open) return false;
        r = editor_->openFile(path, true);
    }
    if (!r.ok) {
        if (!silent) QMessageBox::warning(this, tr("Cannot open file"), r.message);
        return false;
    }
    addRecent(path);
    problems_->clear();
    compilerItems_.clear();
    statusBar()->showMessage(r.message, 3000);
    return true;
}

void MainWindow::fileOpen() {
    if (!maybeSave()) return;
    const QString p = QFileDialog::getOpenFileName(this, tr("Open file"), QString(),
                                                   tr("C++ and text files (*.cpp *.cc *.cxx *.h *.hpp *.c *.txt *.md);;All files (*)"));
    if (!p.isEmpty()) openPath(p);
}

bool MainWindow::fileSave() {
    if (editor_->filePath().isEmpty()) return fileSaveAs();
    QString err;
    if (!editor_->saveFile(editor_->filePath(), &err)) {
        QMessageBox::warning(this, tr("Cannot save"), err);
        return false;
    }
    statusBar()->showMessage(tr("Saved '%1'.").arg(editor_->filePath()), 3000);
    return true;
}

bool MainWindow::fileSaveAs() {
    const QString start = editor_->filePath().isEmpty() ? QStringLiteral("untitled.cpp") : editor_->filePath();
    const QString p = QFileDialog::getSaveFileName(this, tr("Save as"), start, tr("C++ files (*.cpp *.h *.hpp);;All files (*)"));
    if (p.isEmpty()) return false;
    QString err;
    if (!editor_->saveFile(p, &err)) {
        QMessageBox::warning(this, tr("Cannot save"), err);
        return false;
    }
    addRecent(p);
    statusBar()->showMessage(tr("Saved '%1'.").arg(p), 3000);
    return true;
}

void MainWindow::addRecent(const QString& path) {
    recent_.removeAll(path);
    recent_.prepend(path);
    while (recent_.size() > 8) recent_.removeLast();
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    for (const QString& p : recent_) {
        recentMenu_->addAction(p, this, [this, p] { if (maybeSave()) openPath(p); });
    }
    recentMenu_->setEnabled(!recent_.isEmpty());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) { if (maybeSave()) openPath(u.toLocalFile()); break; }
}

// ====================================================================== run / problems

void MainWindow::appendOutput(const QString& text, bool isError) {
    Q_UNUSED(isError);
    output_->moveCursor(QTextCursor::End);
    output_->insertPlainText(text);
    output_->moveCursor(QTextCursor::End);
}

void MainWindow::addCompilerDiagnostic(int line, int col, const QString& sev, const QString& text) {
    auto* it = new QTreeWidgetItem(problems_);
    it->setText(0, sev);
    it->setText(1, QString::number(line));
    it->setText(2, QString::number(col));
    it->setText(3, text);
    it->setForeground(0, sev == "error" ? editor_->theme().error : editor_->theme().warning);
    compilerItems_.append(it);
}

void MainWindow::runClicked() {
    if (run_->busy()) return;
    for (QTreeWidgetItem* it : compilerItems_) delete problems_->takeTopLevelItem(problems_->indexOfTopLevelItem(it));
    compilerItems_.clear();
    output_->clear();
    tabs_->setCurrentIndex(1);
    // the editor text is always the engine text; the trailing newline mirrors what Save writes
    run_->buildAndRun(TextUtil::fromStd(editor_->engine().doc().toString()), editor_->displayName(), compilerFlags_, true);
}

void MainWindow::jumpTo(int line1, int col1) {
    QTextBlock b = editor_->document()->findBlockByNumber(qMax(0, line1 - 1));
    if (!b.isValid()) return;
    QTextCursor c(b);
    c.setPosition(b.position() + qMin(qMax(0, col1 - 1), b.length() - 1));
    editor_->setTextCursor(c);
    editor_->centerCursor();
    editor_->setFocus();
}

void MainWindow::nextProblem() {
    const int n = problems_->topLevelItemCount();
    if (n == 0) { statusBar()->showMessage(tr("No problems."), 2000); return; }
    const int cur = editor_->textCursor().blockNumber() + 1;
    for (int i = 0; i < n; ++i) {
        QTreeWidgetItem* it = problems_->topLevelItem(i);
        if (it->text(1).toInt() > cur) { jumpTo(it->text(1).toInt(), it->text(2).toInt()); return; }
    }
    QTreeWidgetItem* it = problems_->topLevelItem(0);
    jumpTo(it->text(1).toInt(), it->text(2).toInt());
}

void MainWindow::goToLineDialog() {
    bool ok = false;
    const int total = static_cast<int>(editor_->engine().doc().lineCount());
    const int n = QInputDialog::getInt(this, tr("Go to line"), tr("Line number (1-%1):").arg(total), editor_->textCursor().blockNumber() + 1, 1, total, 1, &ok);
    if (ok) editor_->goToLine(n);
}

// ====================================================================== look + settings

void MainWindow::applyTheme(const QString& name) {
    themeName_ = name;
    const Theme t = Theme::byName(name);
    qApp->setStyle("Fusion");
    qApp->setPalette(t.palette());
    editor_->setTheme(t);
    (name == "Daylight" ? lightA_ : darkA_)->setChecked(true);
    refreshProblems();
}

void MainWindow::loadSettings() {
    QSettings s("DSA-CEP", "DevPad");
    themeName_ = s.value("theme", "Midnight").toString();
    compilerFlags_ = s.value("flags", compilerFlags_).toString();
    recent_ = s.value("recent").toStringList();
    const int fs = s.value("fontSize", 12).toInt();
    QFont f = editor_->font();
    f.setPointSize(qBound(6, fs, 48));
    editor_->setFont(f);
    wrapA_->setChecked(s.value("wrap", false).toBool());
    autoCloseA_->setChecked(s.value("autoClose", true).toBool());
    restoreGeometry(s.value("geometry").toByteArray());
    restoreState(s.value("state").toByteArray());
    rebuildRecentMenu();
    // user snippets (problems are reported, never fatal)
    if (QFileInfo::exists(snippetFile())) {
        const IOResult r = editor_->snippets().loadFromFile(TextUtil::toStd(snippetFile()));
        if (!r.ok) statusBar()->showMessage(TextUtil::fromStd(r.message), 5000);
    }
}

void MainWindow::saveSettings() {
    QSettings s("DSA-CEP", "DevPad");
    s.setValue("theme", themeName_);
    s.setValue("flags", compilerFlags_);
    s.setValue("recent", recent_);
    s.setValue("fontSize", editor_->font().pointSize());
    s.setValue("wrap", wrapA_->isChecked());
    s.setValue("autoClose", autoCloseA_->isChecked());
    s.setValue("geometry", saveGeometry());
    s.setValue("state", saveState());
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) { e->ignore(); return; }
    run_->stop();
    saveSettings();
    e->accept();
}

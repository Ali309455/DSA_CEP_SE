// CodeEditor.h - the editor widget (phase 5).
//
// ARCHITECTURE: the engine (Editor + Document + UndoManager) is the single source of truth
// for the text. The QPlainTextEdit only DISPLAYS it and captures input:
//
//   user types / pastes / cuts  ->  Qt changes its own text  ->  contentsChange signal
//        ->  onContentsChange() copies exactly that change into the engine (Editor)
//   Undo / Redo / Replace / Autocomplete / Snippet  ->  done by the ENGINE  ->  syncFromEngine()
//        copies the (small) difference back into the widget.
//
// Qt's own undo is switched off; Ctrl+Z / Ctrl+Y run the engine's two-stack undo/redo.
// Typing is merged into one undo step per word (a space, a pause, a cursor jump or any other
// kind of edit ends the step), using Editor::beginGroup / endGroup.
//
// The engine counts columns in bytes (UTF-8), Qt in UTF-16 units; see TextUtil.h.
#pragma once
#include <functional>
#include <QElapsedTimer>
#include <QList>
#include <QPlainTextEdit>
#include <QSet>
#include <QTextEdit>
#include <QTimer>
#include <QVector>
#include "Assist.h"
#include "Brackets.h"
#include "Editor.h"
#include "Snippets.h"
#include "Theme.h"

class CppHighlighter;
class LineNumberArea;
class QListWidget;

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    ~CodeEditor() override;

    // ---- engine access (read-only for the outside; edits go through the methods below) ----
    Editor& engine() { return ed_; }
    const Editor& engine() const { return ed_; }
    Autocomplete& autocomplete() { return ac_; }
    SnippetTable& snippets() { return snips_; }
    const BracketReport& bracketReport();          // refreshed first if the text changed

    // ---- files ----
    struct LoadResult {
        bool ok = false;
        bool needsConfirm = false;   // file is not valid UTF-8: ask the user, then call again with acceptLossy
        QString message;
    };
    LoadResult openFile(const QString& path, bool acceptLossy = false);
    bool saveFile(const QString& path, QString* error);
    void newDocument();
    QString filePath() const { return filePath_; }
    void setFilePath(const QString& p) { filePath_ = p; emit stateChanged(); }   // file renamed on disk
    void markDirty() { lossy_ = true; emit stateChanged(); }                     // file deleted on disk: unsaved
    QString displayName() const;
    bool isModified() const { return ed_.doc().modified() || lossy_; }

    // ---- editing through the engine ----
    bool engineUndo();
    bool engineRedo();
    void commitTyping();                            // close the open "typing" undo step
    ReplaceResult replaceAllText(const std::string& pat, const std::string& rep, const SearchOptions& o, QString* error);
    bool replaceNextText(const Match& at, const std::string& pat, const std::string& rep, const SearchOptions& o,
                         Pos* after, QString* error);
    bool expandSnippetAtCursor();
    void showCompletions(bool force);
    void indentSelection(bool in);
    void toggleComment();
    void duplicateLine();
    void goToLine(int oneBased);

    // ---- positions: engine Pos (line, byte column) <-> widget offsets ----
    Pos posAt(int offset) const;
    int offsetOf(Pos p) const;                      // -1 if the line does not exist
    Pos cursorPos() const { return posAt(textCursor().position()); }
    void selectRange(Pos a, Pos b, bool center = true);

    // ---- search highlights (set by the find bar) ----
    void setSearchMatches(const DynArray<Match>& m, int current);

    // ---- look ----
    void setTheme(const Theme& t);
    const Theme& theme() const { return theme_; }
    void setAutoClose(bool on) { autoClose_ = on; }
    bool autoClose() const { return autoClose_; }

    // ---- checks (tests, save) ----
    bool verifyConsistency(QString* why = nullptr) const;   // widget text == engine text
    QString widgetText() const;
    bool completionVisible() const;
    QStringList completionItems() const;

    void lineNumberAreaPaintEvent(QPaintEvent* e);
    int lineNumberAreaWidth() const;

signals:
    void stateChanged();                     // modified flag, undo/redo labels, counts
    void analysisUpdated();                  // bracket report refreshed
    void message(const QString& text);       // short status-bar message
    void filesDropped(const QStringList& paths);
    void textMirrored();                     // the engine text changed (for the find bar)

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void insertFromMimeData(const QMimeData* src) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void changeEvent(QEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private slots:
    void onContentsChange(int pos, int removed, int added);
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& r, int dy);
    void onCursorMoved();
    void runAnalysis();
public:
    void runAnalysisForTests() { runAnalysis(); }
    void pasteForTests(const QMimeData* m) { insertFromMimeData(m); }
private slots:

private:
    enum class Kind { Other, Type, Back, Del };

    Editor ed_;
    Autocomplete ac_;
    SnippetTable snips_;
    BracketReport report_;
    bool reportDirty_ = true;
    QString filePath_;
    bool lossy_ = false;

    bool syncing_ = false;          // widget is being changed FROM the engine: do not mirror
    int userEditDepth_ = 0;         // inside UserEdit: one engine group for several widget changes
    Kind typingKind_ = Kind::Other;
    bool typingOpen_ = false;
    Pos typingNext_;
    QElapsedTimer typingClock_;
    Kind hint_ = Kind::Other;       // which key caused a one-character delete
    bool autoClose_ = true;

    QTimer analysisTimer_;
    Theme theme_;
    CppHighlighter* highlighter_ = nullptr;
    LineNumberArea* gutter_ = nullptr;
    QListWidget* popup_ = nullptr;

    QVector<Match> matches_;
    int currentMatch_ = -1;
    QSet<int> issueLines_;

    friend class UserEdit;
    void mirror(int pos, int removed, int added);
    void readEngine(Pos from, int units, QString& out) const;
    bool syncFromEngine();
    void resyncEngineFromWidget();
    void setWidgetFromEngine();
    bool runEngine(const std::function<void()>& op, QString* error);
    void updateExtraSelections();
    void acceptCompletion();
    void updateCompletion(bool force);
    void hidePopup();
    void smartNewline();
    bool tryAutoClose(const QString& typed);
    void updateTabStop();
    void positionPopup();
};

// Several widget changes -> ONE engine undo step.
class UserEdit {
public:
    UserEdit(CodeEditor& e, const char* label);
    ~UserEdit();
private:
    CodeEditor& e_;
    QTextCursor c_;
};

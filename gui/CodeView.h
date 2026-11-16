// CodeView.h - the text area of one tab (phase 5, Qt Widgets).
//
// A custom-painted widget on top of the engine: the text lives in engine::Document and
// every change goes through engine::Editor, so undo / redo, search, bracket checking
// and autocomplete are the DSA code from the earlier phases, not Qt's QTextDocument.
// This class only turns key presses / mouse clicks into engine calls and draws the result.
#pragma once
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QString>
#include <QTimer>
#include <string>
#include <vector>
#include "Assist.h"
#include "Brackets.h"
#include "EditOps.h"
#include "Editor.h"
#include "Settings.h"

class QListWidget;

class CodeView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit CodeView(QWidget* parent = nullptr);

    // ---- document ----
    Editor& editor() { return ed_; }
    const Editor& editor() const { return ed_; }
    bool openFile(const QString& path, QString* error);
    bool saveFile(const QString& path, QString* error);
    void setLanguageForPath(const QString& path);
    QString languageName() const;
    QString commentMarker() const { return QString::fromStdString(comment_); }

    // ---- cursor and selection (engine positions: 0-based line, byte column) ----
    Pos cursor() const { return cur_; }
    Pos anchor() const { return anchor_; }
    bool hasSelection() const { return cur_ != anchor_; }
    Pos selStart() const { return cur_ < anchor_ ? cur_ : anchor_; }
    Pos selEnd() const { return cur_ < anchor_ ? anchor_ : cur_; }
    QString selectedText() const;
    void setCursor(Pos p);               // clears the selection
    void select(Pos a, Pos b);           // anchor a, cursor b
    int displayColumn(Pos p) const;      // 0-based, tabs expanded

    // ---- search highlights (kept sorted by the caller) ----
    void setSearchHighlights(const std::vector<Match>& m);
    void clearSearchHighlights();
    // Call after changing the document through editor() directly (replace all, ...).
    void notifyEdited(size_t fromLine = 0);

    // ---- bracket report (up to date a moment after the last edit) ----
    size_t bracketIssueCount() const { return dirtyBrackets_ ? 0 : report_.issues.size(); }
    QString firstBracketIssue() const;
    bool bracketsReady() const { return !dirtyBrackets_; }
    bool firstBracketIssuePos(Pos& where) const;

    int zoom() const { return zoom_; }
    void setZoom(int points);

public slots:
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void selectAll();
    void selectLine();
    void moveLinesUp();
    void moveLinesDown();
    void copyLinesUp();
    void copyLinesDown();
    void deleteLine();
    void toggleComment();
    void indentSelection();
    void outdentSelection();
    void newLineBelow();
    void newLineAbove();
    void triggerAutocomplete();
    void goToLine(int line1);            // 1-based, clamped

signals:
    void stateChanged();                 // cursor, selection, text, modified flag or report changed
    void textEdited();                   // the text changed

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery) const override;
    bool focusNextPrevChild(bool) override { return false; }   // Tab belongs to the editor

private:
    struct Span { int start; int len; int color; };
    enum Color { cText, cKeyword, cType, cString, cNumber, cComment, cPreproc, cCount };

    Editor ed_;
    Autocomplete ac_;
    Pos cur_, anchor_;
    int wantCol_ = 0;                    // display column Up / Down try to keep
    std::string comment_ = "//";
    int language_ = 0;                   // 0 C-like, 1 hash-comment, 2 plain
    int zoom_ = 11;
    QString fontFamily_;                 // empty: automatic (from the settings)
    int tabWidth_ = 4;
    int charW_ = 8, lineH_ = 18, gutterW_ = 50;
    int maxCols_ = 0;                    // widest line seen so far (display columns)
    bool cursorOn_ = true;
    bool dragging_ = false;
    qint64 lastDblClick_ = 0;            // for triple click (select line)
    QTimer blink_, bracketTimer_, autoTimer_;
    std::vector<Match> highlights_;
    BracketReport report_;
    bool dirtyBrackets_ = true;
    std::vector<char> blockAtStart_;     // 1 if line i begins inside /* ... */
    size_t blockValid_ = 0;              // blockAtStart_[0 .. blockValid_) are up to date
    QListWidget* popup_ = nullptr;
    bool lastModified_ = false;

    // the user's preferences (Settings window): font, tabs, cursor, what is drawn
    void applySettings();
    const AppSettings::Values& prefs() const { return AppSettings::instance().values(); }

    // geometry
    void applyFont();
    void updateGutter();
    int textLeft() const { return gutterW_ + 8; }
    size_t firstVisible() const { return static_cast<size_t>(verticalScrollBar()->value()); }
    int visibleLines() const;
    void updateScrollBars();
    void ensureCursorVisible();
    Pos pointToPos(const QPoint& p) const;
    int displayCol(const std::string& line, size_t byteCol) const;
    size_t byteColFor(const std::string& line, int dcol) const;
    QString expandTabs(const std::string& line) const;

    // text helpers
    static size_t prevCharCol(const std::string& line, size_t col);
    static size_t nextCharCol(const std::string& line, size_t col);
    static size_t wordLeft(const std::string& line, size_t col);
    static size_t wordRight(const std::string& line, size_t col);
    Pos clamp(Pos p) const;
    void lineRangeOfSelection(size_t& from, size_t& to) const;
    std::string lineText(size_t l) const { return ed_.doc().getLine(l); }

    // editing
    void edited(size_t fromLine);        // bookkeeping after any change
    void afterMove(bool extend, bool keepWantCol = false);
    void insertTypedText(const std::string& s);
    void typeChar(char c);
    void deleteSelection();
    void backspace(bool word);
    void deleteForward(bool word);
    void pressEnter();
    void pressTab(bool back);
    void moveCursor(QKeyEvent* e);
    void adjustBoth(const DynArray<ColEdit>& edits);

    // highlighting
    void tokenize(const QString& s, bool& inBlock, std::vector<Span>& out) const;
    void ensureBlockState(size_t upToLine);
    void recomputeBrackets();

    // autocomplete popup
    void showCompletions(bool force = false);
    void hidePopup();
    void acceptCompletion();
    bool popupVisible() const;
};

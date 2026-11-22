// SidePanels.h - the two panels on the right of the editor.
//
//   OutlinePanel  the symbols of the current file (engine/Outline.h) as a tree: namespaces, classes,
//                 functions. Click to jump; the symbol around the cursor is highlighted; type to filter.
//   HistoryPanel  the undo / redo stacks of the current file as one timeline, oldest at the top:
//                 "Start", the steps that can be undone, then the undone steps that Redo can bring back
//                 (dimmed). Click any row to go back or forward to that point (Editor::jumpToHistory).
//
// Both follow the current tab through setView(); neither keeps a copy of the text.
#pragma once
#include <QPointer>
#include <QVector>
#include <QWidget>

class CodeView;
class QLabel;
class QLineEdit;
class QListWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class OutlinePanel : public QWidget {
    Q_OBJECT
public:
    explicit OutlinePanel(QWidget* parent = nullptr);
    void setView(CodeView* v);

    // for the tests
    QTreeWidget* tree() const { return tree_; }
    int symbolCount() const { return items_.size(); }
    int visibleCount() const;                 // symbols that pass the filter
    void setFilter(const QString& text);
    bool activate(int symbolIndex);           // what a click does: move the cursor to that symbol
    int currentSymbol() const;                // the highlighted one (around the cursor), -1 if none

signals:
    void closeRequested();

private:
    QPointer<CodeView> view_;
    QTreeWidget* tree_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* count_ = nullptr;
    QVector<QTreeWidgetItem*> items_;         // items_[i] shows view_->symbols()[i]
    QList<QMetaObject::Connection> links_;
    void rebuild();
    void followCursor();
    void applyFilter();
};

class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(QWidget* parent = nullptr);
    void setView(CodeView* v);

    // for the tests
    QListWidget* list() const { return list_; }
    int entryCount() const;                   // 1 ("Start") + undo steps + redo steps
    int currentRow() const;                   // the row of the state the text is in now
    bool jumpTo(int row);                     // what a click does

signals:
    void closeRequested();

private:
    QPointer<CodeView> view_;
    QListWidget* list_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* count_ = nullptr;
    QToolButton* undoBtn_ = nullptr;
    QToolButton* redoBtn_ = nullptr;
    QList<QMetaObject::Connection> links_;
    unsigned shownRevision_ = ~0u;
    size_t shownUndo_ = ~size_t(0), shownRedo_ = ~size_t(0);
    bool rebuilding_ = false;
    void rebuild(bool force = false);
};

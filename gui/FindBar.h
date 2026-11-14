// FindBar.h - inline find / replace bar. Uses the engine's KMP search (findAll) and
// Editor::replaceNext / replaceAll (one undo step for "replace all").
#pragma once
#include <QWidget>
#include <QTimer>
#include "Search.h"

class CodeEditor;
class QLineEdit;
class QLabel;
class QPushButton;
class QToolButton;

class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(CodeEditor* editor, QWidget* parent = nullptr);
    void openFind(bool withReplace);          // show, prefill from the selection, focus
    int matchCount() const { return static_cast<int>(matches_.size()); }
    QLineEdit* findEdit() const { return find_; }
    QLineEdit* replaceEdit() const { return replace_; }

signals:
    void message(const QString& text);

public slots:
    void findNext() { step(true); }
    void findPrev() { step(false); }
    void replaceOne();
    void replaceAll();
    void closeBar();
    void refresh();                           // recompute matches (typing, options, text changed)
    void toggleReplace();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    CodeEditor* ed_;
    QLineEdit *find_, *replace_;
    QToolButton *caseBtn_, *wordBtn_;
    QPushButton *prev_, *next_, *rep_, *repAll_, *close_;
    QLabel* count_;
    QTimer timer_;
    DynArray<Match> matches_;
    int current_ = -1;
    bool replaceVisible_ = false;
    QWidget* replaceRow_;

    SearchOptions options() const;
    bool recompute(QString* err = nullptr);
    void step(bool forward);
    void updateLabel(bool bad);
    int indexOfSelection() const;
};

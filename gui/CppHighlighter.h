// CppHighlighter.h - small hand-written C++ lexer used for colouring. Colouring only:
// it never changes the text and never talks to the engine.
#pragma once
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include "Theme.h"

class CppHighlighter : public QSyntaxHighlighter {
public:
    explicit CppHighlighter(QTextDocument* doc);
    void setTheme(const Theme& t);

protected:
    void highlightBlock(const QString& text) override;

private:
    Theme theme_;
    QTextCharFormat fKeyword_, fType_, fString_, fComment_, fNumber_, fPreproc_, fFunction_;
    QSet<QString> keywords_, types_;
};

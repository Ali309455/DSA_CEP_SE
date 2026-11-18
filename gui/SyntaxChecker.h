// SyntaxChecker.h - runs "g++ -fsyntax-only" on the text of the open file, in the background.
//
// A temporary copy of the text is compiled, so unsaved edits are checked too and the real file
// is never touched. Only one check runs at a time: starting a new one abandons the old one.
// The command line comes from the engine (Diagnostics.h: syntaxCheckArgs), and so does the
// parsing of what the compiler prints (parseCompilerOutput), so the console `check` command and
// this class always agree.
#pragma once
#include <QObject>
#include <QString>
#include <string>
#include <vector>
#include "Diagnostics.h"

class QProcess;

class SyntaxChecker : public QObject {
    Q_OBJECT
public:
    explicit SyntaxChecker(QObject* parent = nullptr) : QObject(parent) {}
    ~SyntaxChecker() override { cancel(); }

    // Check 'text' as if it were the file 'path' (its folder is used for #include "..."). 'token'
    // is handed back in finished(). A check that is still running is abandoned.
    void check(const QString& path, const std::string& text, quint64 token);
    void cancel();
    bool busy() const { return proc_ != nullptr; }

signals:
    // Diagnostics of the checked file have file == path; ones in included files keep the
    // compiler's path. 'message' is empty when the compiler ran; otherwise it says why it did not
    // (compiler not installed, ...) and 'diags' is empty.
    void finished(const QString& path, quint64 token, const std::vector<Diagnostic>& diags, const QString& message);

private:
    QProcess* proc_ = nullptr;
    int counter_ = 0;
};

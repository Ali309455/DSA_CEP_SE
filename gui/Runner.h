// Runner.h - what F5 does for a file: the commands that build (if needed) and run it.
//
// The plan is a list of command lines, run one after another in the terminal; the list stops at
// the first one that fails (so a compile error never runs an old program). Lines are written for
// the shell the terminal uses (cmd, PowerShell or a POSIX shell), because quoting differs.
#pragma once
#include <QString>
#include <QStringList>

namespace Runner {

enum class Shell { Posix, Cmd, PowerShell };

// Shell kind for the program the terminal runs (path or name; empty = system default).
Shell shellKind(const QString& shellProgram);

struct Plan {
    QStringList steps;        // empty: this kind of file cannot be run
    QString description;      // "C++ (g++)", "Python" - for the status bar
    QString extension;        // as asked, lower case, without the dot
};

// outDir is where compiled programs go (a temp folder, so the project stays clean).
Plan planFor(const QString& filePath, Shell shell, const QString& outDir);

// File types F5 understands, for messages and the shortcuts window.
QStringList supportedExtensions();

}  // namespace Runner

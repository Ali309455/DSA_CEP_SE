// TerminalPanel.h - the built-in terminal (Ctrl+`), docked under the editor.
//
// Not a full terminal emulator: each command line you enter runs in the shell (cmd.exe on
// Windows, $SHELL elsewhere, or the program chosen in Settings) in the panel's current folder,
// and its output is shown in colour (ANSI colours are understood). That covers building, git,
// running tests and scripts. While a program runs, what you type goes to its input, and
// Ctrl+C stops it. `cd` is handled here so the folder sticks from one command to the next.
#pragma once
#include <QPlainTextEdit>
#include <QProcess>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

class TerminalPanel : public QWidget {
    Q_OBJECT
public:
    explicit TerminalPanel(QWidget* parent = nullptr);
    ~TerminalPanel() override;

    // ---- used by the main window and by the tests ----
    void setWorkingDirectory(const QString& dir);      // the folder the next command runs in
    QString workingDirectory() const { return cwd_; }
    void runCommand(const QString& line);              // same as typing it and pressing Enter
    bool isRunning() const { return proc_ && proc_->state() != QProcess::NotRunning; }
    void stop();                                       // Ctrl+C: ends the running program (and what was queued after it)
    // Run these command lines one after another; the first one that fails ends the sequence.
    // A program that is still running is stopped first.
    void runSequence(const QStringList& lines);
    bool busy() const { return isRunning() || !queue_.isEmpty(); }
    void clearScreen();
    QString plainText() const;
    QLineEdit* input() const { return input_; }
    QStringList history() const { return history_; }
    QString shellName() const;                         // what the header shows
    void focusInput();

    // Program and arguments used to run one command line with a given shell (tests, and the run itself).
    static void shellInvocation(const QString& shell, const QString& line, QString* program, QStringList* args, QString* nativeArgs);
    // ANSI colour / style sequences turned into text formats; every other escape sequence is dropped.
    static void appendAnsi(QPlainTextEdit* edit, const QString& text);

signals:
    void hideRequested();
    void directoryChanged(const QString& dir);

protected:
    bool eventFilter(QObject*, QEvent*) override;

private:
    QPlainTextEdit* out_ = nullptr;
    QLineEdit* input_ = nullptr;
    QLabel* prompt_ = nullptr;
    QLabel* chip_ = nullptr;
    QLabel* cwdLabel_ = nullptr;
    QToolButton* stopBtn_ = nullptr;
    QProcess* proc_ = nullptr;
    QString cwd_;
    QString previousCwd_;
    QStringList history_;
    QStringList queue_;                                // command lines waiting for the running one to succeed
    int historyPos_ = 0;
    QString draft_;
    bool needNewline_ = false;                         // output did not end with a newline yet

    void submit();
    bool builtin(const QString& line);
    void start(const QString& line);
    void echoCommand(const QString& line);
    void writeOut(const QString& text);
    void writeNote(const QString& text, const QColor& color);
    void refreshPrompt();
    void applySettings();
    void historyStep(int dir);
    void onFinished(int code, QProcess::ExitStatus status);
};

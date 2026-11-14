// RunManager.h - "Run" button: compile the buffer with the system C++ compiler, then run it.
#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

class RunManager : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Compiling, Running };
    explicit RunManager(QObject* parent = nullptr);
    ~RunManager() override;

    State state() const { return state_; }
    bool busy() const { return state_ != State::Idle; }
    static QString findCompiler();                   // "" if none is installed
    // Compile 'source' (kept in a temp folder, so unsaved text works) and run the result.
    void buildAndRun(const QString& source, const QString& baseName, const QString& flags, bool run = true);
    void stop();
    void writeInput(const QString& line);

signals:
    void stateChanged();
    void output(const QString& text, bool isError);
    // line / column are 1-based; severity is "error", "warning" or "note"
    void diagnostic(int line, int column, const QString& severity, const QString& text);
    void finished(bool ok, const QString& summary);

private:
    State state_ = State::Idle;
    std::unique_ptr<QTemporaryDir> dir_;
    QProcess* proc_ = nullptr;
    QTimer watchdog_;
    QString srcName_, exePath_, compilerLog_;
    bool run_ = true, stopped_ = false, timedOut_ = false;

    void setState(State s);
    void startRun();
    void parseCompilerOutput(const QString& text);
    void cleanup();
};

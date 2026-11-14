#include "RunManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

RunManager::RunManager(QObject* parent) : QObject(parent) {
    watchdog_.setSingleShot(true);
    connect(&watchdog_, &QTimer::timeout, this, [this] { timedOut_ = true; stop(); });
}

RunManager::~RunManager() {
    if (proc_) { proc_->disconnect(this); proc_->kill(); proc_->waitForFinished(1000); }
}

QString RunManager::findCompiler() {
    for (const char* n : {"g++", "c++", "clang++"}) {
        const QString p = QStandardPaths::findExecutable(QString::fromLatin1(n));
        if (!p.isEmpty()) return p;
    }
    return QString();
}

void RunManager::setState(State s) { state_ = s; emit stateChanged(); }

void RunManager::cleanup() {
    if (proc_) { proc_->deleteLater(); proc_ = nullptr; }
    watchdog_.stop();
    dir_.reset();
}

void RunManager::buildAndRun(const QString& source, const QString& baseName, const QString& flags, bool run) {
    if (busy()) { emit output(tr("Already running. Stop it first.\n"), true); return; }
    const QString cxx = findCompiler();
    if (cxx.isEmpty()) {
        emit output(tr("No C++ compiler found. Install g++ (MinGW-w64 / MSYS2 on Windows, build-essential on Linux, "
                       "Xcode tools on macOS) and make sure it is on your PATH.\n"), true);
        emit finished(false, tr("No compiler"));
        return;
    }
    dir_.reset(new QTemporaryDir);
    if (!dir_->isValid()) {
        emit output(tr("Cannot create a temporary folder: %1\n").arg(dir_->errorString()), true);
        dir_.reset();
        emit finished(false, tr("Temp folder error"));
        return;
    }
    // keep the name of the document so compiler messages look familiar
    QString base = baseName.isEmpty() ? QStringLiteral("untitled") : baseName;
    if (!(base.endsWith(".cpp") || base.endsWith(".cc") || base.endsWith(".cxx"))) base += ".cpp";
    srcName_ = QDir(dir_->path()).filePath(base);
    QString exeName = QStringLiteral("program");
#ifdef Q_OS_WIN
    exeName += QStringLiteral(".exe");
#endif
    exePath_ = QDir(dir_->path()).filePath(exeName);
    QFile f(srcName_);
    if (!f.open(QIODevice::WriteOnly) || f.write(source.toUtf8()) < 0) {
        emit output(tr("Cannot write the temporary source file: %1\n").arg(f.errorString()), true);
        dir_.reset();
        emit finished(false, tr("Temp file error"));
        return;
    }
    f.close();

    run_ = run;
    stopped_ = timedOut_ = false;
    compilerLog_.clear();
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = QProcess::splitCommand(flags);
    args << srcName_ << QStringLiteral("-o") << exePath_;
    emit output(tr("$ %1 %2\n").arg(QFileInfo(cxx).fileName(), args.join(' ')), false);
    connect(proc_, &QProcess::readyRead, this, [this] {
        const QString t = QString::fromLocal8Bit(proc_->readAll());
        compilerLog_ += t;
        emit output(t, true);
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && state_ == State::Compiling) {
            emit output(tr("Could not start the compiler.\n"), true);
            setState(State::Idle);
            cleanup();
            emit finished(false, tr("Compiler did not start"));
        }
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus st) {
        if (stopped_ || timedOut_) {
            const QString why = timedOut_ ? tr("Stopped: compile took more than 2 minutes.") : tr("Stopped.");
            emit output(why + "\n", true);
            setState(State::Idle);
            cleanup();
            emit finished(false, why);
            return;
        }
        if (st != QProcess::NormalExit || code != 0) {
            parseCompilerOutput(compilerLog_);
            setState(State::Idle);
            cleanup();
            emit finished(false, tr("Build failed"));
            return;
        }
        parseCompilerOutput(compilerLog_);       // warnings
        emit output(tr("Build succeeded.\n"), false);
        if (!run_) { setState(State::Idle); cleanup(); emit finished(true, tr("Build succeeded")); return; }
        startRun();
    });
    setState(State::Compiling);
    watchdog_.start(120000);
    proc_->start(cxx, args);
}

void RunManager::startRun() {
    QProcess* old = proc_;
    proc_ = new QProcess(this);
    old->deleteLater();
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    proc_->setWorkingDirectory(dir_->path());
    connect(proc_, &QProcess::readyRead, this, [this] { emit output(QString::fromLocal8Bit(proc_->readAll()), false); });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            emit output(tr("Could not start the program.\n"), true);
            setState(State::Idle);
            cleanup();
            emit finished(false, tr("Program did not start"));
        }
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int code, QProcess::ExitStatus st) {
        QString s;
        if (stopped_) s = tr("Stopped.");
        else if (st == QProcess::CrashExit) s = tr("Program crashed (signal / access violation).");
        else s = tr("Program finished with exit code %1.").arg(code);
        emit output(QStringLiteral("\n[%1]\n").arg(s), st == QProcess::CrashExit);
        setState(State::Idle);
        cleanup();
        emit finished(!stopped_ && st == QProcess::NormalExit && code == 0, s);
    });
    watchdog_.stop();
    setState(State::Running);
    emit output(tr("--- running ---\n"), false);
    proc_->start(exePath_, QStringList());
}

void RunManager::stop() {
    if (!proc_ || !busy()) return;
    stopped_ = true;
    proc_->kill();
}

void RunManager::writeInput(const QString& line) {
    if (state_ != State::Running || !proc_) return;
    proc_->write((line + QLatin1Char('\n')).toLocal8Bit());
}

// "file:12:5: error: message"  (the file part may contain a Windows drive letter "C:")
void RunManager::parseCompilerOutput(const QString& text) {
    static const QRegularExpression re(QStringLiteral("^(.*?):(\\d+):(\\d+):\\s*(fatal error|error|warning|note):\\s*(.*)$"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& l : lines) {
        const QRegularExpressionMatch m = re.match(l.trimmed());
        if (!m.hasMatch()) continue;
        const QString file = QDir::fromNativeSeparators(m.captured(1));
        if (QFileInfo(file).fileName() != QFileInfo(srcName_).fileName()) continue;   // header / library message
        QString sev = m.captured(4);
        if (sev == QLatin1String("fatal error")) sev = QStringLiteral("error");
        emit diagnostic(m.captured(2).toInt(), m.captured(3).toInt(), sev, m.captured(5));
    }
}

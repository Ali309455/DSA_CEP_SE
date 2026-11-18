#include "SyntaxChecker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

void SyntaxChecker::cancel() {
    if (!proc_) return;
    QProcess* p = proc_;
    proc_ = nullptr;
    p->disconnect(this);                                     // nothing from it reaches us any more
    if (p->state() == QProcess::NotRunning) {
        p->deleteLater();
        return;
    }
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p, &QObject::deleteLater);
    p->kill();
}

void SyntaxChecker::check(const QString& path, const std::string& text, quint64 token) {
    cancel();
    const std::string pathStd = path.toUtf8().toStdString();
    if (path.isEmpty() || !isCompilableSource(pathStd)) {
        emit finished(path, token, std::vector<Diagnostic>(), QString());
        return;
    }

    // a temporary copy with the same extension, in its own folder
    const QFileInfo fi(path);
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("devpad-check");
    QDir().mkpath(dir);
    const QString tmp = QDir(dir).filePath(QString("check_%1_%2.%3")
                                               .arg(QCoreApplication::applicationPid())
                                               .arg(++counter_)
                                               .arg(fi.suffix().isEmpty() ? QString("cpp") : fi.suffix()));
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit finished(path, token, std::vector<Diagnostic>(), "Could not write a temporary file for the error check.");
            return;
        }
        f.write(text.data(), static_cast<qint64>(text.size()));
        f.close();
    }

    SyntaxCheckOptions opt;
    opt.includeDir = fi.absolutePath().toUtf8().toStdString();
    const std::string tmpStd = tmp.toUtf8().toStdString();
    const QString program = QString::fromUtf8(syntaxCheckCompiler(tmpStd, opt).c_str());
    QStringList args;
    const DynArray<std::string> a = syntaxCheckArgs(tmpStd, opt);
    for (size_t i = 0; i < a.size(); ++i) args << QString::fromUtf8(a[i].c_str());

    QProcess* proc = new QProcess(this);
    proc_ = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");                               // English messages: "error:" is what the parser looks for
    env.insert("LANG", "C");
    env.remove("LANGUAGE");
    proc->setProcessEnvironment(env);
    if (QFileInfo(fi.absolutePath()).isDir()) proc->setWorkingDirectory(fi.absolutePath());
    QObject::connect(proc, &QObject::destroyed, [tmp]() { QFile::remove(tmp); });

    const QString tmpName = QFileInfo(tmp).fileName();
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, path, token, tmpName, program](int, QProcess::ExitStatus) {
                if (proc != proc_) return;                   // abandoned by a newer check
                proc_ = nullptr;
                const std::string out = QString::fromLocal8Bit(proc->readAll()).toUtf8().toStdString();
                const DynArray<Diagnostic> parsed = parseCompilerOutput(out, program.toUtf8().toStdString());
                std::vector<Diagnostic> list;
                for (size_t i = 0; i < parsed.size(); ++i) {
                    Diagnostic d = parsed[i];
                    if (!d.file.empty() && QFileInfo(QString::fromUtf8(d.file.c_str())).fileName() == tmpName)
                        d.file = path.toUtf8().toStdString();    // the temporary copy IS the user's file
                    list.push_back(d);
                }
                proc->deleteLater();
                emit finished(path, token, list, QString());
            });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, path, token, program](QProcess::ProcessError err) {
        if (proc != proc_ || err != QProcess::FailedToStart) return;
        proc_ = nullptr;
        proc->deleteLater();
        emit finished(path, token, std::vector<Diagnostic>(),
                      "'" + program + "' was not found. Install MinGW-w64 / GCC and add it to PATH to see compiler errors.");
    });
    QTimer::singleShot(20000, proc, [proc]() {               // a runaway template error must not run forever
        if (proc->state() != QProcess::NotRunning) proc->kill();
    });
    proc->start(program, args);
}

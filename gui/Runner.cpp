#include "Runner.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace Runner {

Shell shellKind(const QString& program) {
    QString p = program.trimmed();
    if (p.isEmpty()) {
#ifdef Q_OS_WIN
        return Shell::Cmd;
#else
        return Shell::Posix;
#endif
    }
    const QString b = QFileInfo(p).completeBaseName().toLower();
    if (b == "cmd") return Shell::Cmd;
    if (b == "powershell" || b == "pwsh") return Shell::PowerShell;
    return Shell::Posix;
}

namespace {
QString q(const QString& path, Shell sh) {          // quote a path for the shell
    QString native = path;
    if (sh != Shell::Posix) native.replace('/', '\\');          // cmd and PowerShell want backslashes
    if (sh == Shell::Posix) native.replace('"', "\\\"").replace('$', "\\$").replace('`', "\\`");
    return '"' + native + '"';
}
// command that starts a program given by path
QString exec(const QString& path, Shell sh) {
    if (sh == Shell::PowerShell) return "& " + q(path, sh);
    return q(path, sh);
}
QString python() {
#ifdef Q_OS_WIN
    return "python";
#else
    return "python3";
#endif
}
}  // namespace

QStringList supportedExtensions() {
    return {"c", "cpp", "cc", "cxx", "py", "js", "mjs", "java", "go", "rs", "rb", "php", "lua", "sh", "bat", "cmd", "ps1"};
}

Plan planFor(const QString& filePath, Shell sh, const QString& outDir) {
    Plan plan;
    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();
    plan.extension = ext;
    const QString f = q(filePath, sh);
#ifdef Q_OS_WIN
    const QString exe = QDir(outDir).filePath(fi.completeBaseName() + ".exe");
#else
    const QString exe = QDir(outDir).filePath(fi.completeBaseName() + ".out");
#endif
    const QString out = q(exe, sh);

    if (ext == "c") {
        plan.description = "C (gcc)";
        plan.steps << "gcc " + f + " -O0 -g -o " + out << exec(exe, sh);
    } else if (ext == "cpp" || ext == "cc" || ext == "cxx") {
        plan.description = "C++ (g++)";
        plan.steps << "g++ -std=c++17 " + f + " -O0 -g -o " + out << exec(exe, sh);
    } else if (ext == "py") {
        plan.description = "Python";
        plan.steps << python() + " " + f;
    } else if (ext == "js" || ext == "mjs") {
        plan.description = "JavaScript (node)";
        plan.steps << "node " + f;
    } else if (ext == "java") {
        plan.description = "Java";
        plan.steps << "java " + f;                      // single-file source launch (JDK 11+)
    } else if (ext == "go") {
        plan.description = "Go";
        plan.steps << "go run " + f;
    } else if (ext == "rs") {
        plan.description = "Rust (rustc)";
        plan.steps << "rustc " + f + " -o " + out << exec(exe, sh);
    } else if (ext == "rb") {
        plan.description = "Ruby";
        plan.steps << "ruby " + f;
    } else if (ext == "php") {
        plan.description = "PHP";
        plan.steps << "php " + f;
    } else if (ext == "lua") {
        plan.description = "Lua";
        plan.steps << "lua " + f;
    } else if (ext == "sh") {
        plan.description = "Shell script";
        plan.steps << "bash " + f;
    } else if (ext == "bat" || ext == "cmd") {
        plan.description = "Batch file";
        plan.steps << (sh == Shell::PowerShell ? "& " + f : sh == Shell::Cmd ? "call " + f : "cmd /c " + f);
    } else if (ext == "ps1") {
        plan.description = "PowerShell script";
        plan.steps << (sh == Shell::PowerShell ? "& " + f : "powershell -NoProfile -ExecutionPolicy Bypass -File " + f);
    }
    return plan;
}

}  // namespace Runner

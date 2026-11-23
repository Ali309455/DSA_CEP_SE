#include "Trust.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace Trust {

static const char* kKey = "trust/folders";

#ifdef Q_OS_WIN
static const Qt::CaseSensitivity kCase = Qt::CaseInsensitive;
#else
static const Qt::CaseSensitivity kCase = Qt::CaseSensitive;
#endif

QString normalize(const QString& path) {
    if (path.isEmpty()) return QString();
    const QFileInfo fi(path);
    QString p = fi.exists() ? fi.canonicalFilePath() : QString();
    if (p.isEmpty()) p = QDir::cleanPath(fi.absoluteFilePath());
    p = QDir::fromNativeSeparators(p);
    while (p.size() > 1 && p.endsWith('/') && !p.endsWith(":/")) p.chop(1);
    return p;
}

static bool sameOrInside(const QString& path, const QString& dir) {
    if (dir.isEmpty() || path.isEmpty()) return false;
    if (path.compare(dir, kCase) == 0) return true;
    const QString prefix = dir.endsWith('/') ? dir : dir + '/';
    return path.startsWith(prefix, kCase);
}

QStringList trustedFolders() { return QSettings().value(kKey).toStringList(); }

void setTrustedFolders(const QStringList& dirs) {
    QStringList out;
    for (const QString& d : dirs) {
        const QString n = normalize(d);
        if (n.isEmpty()) continue;
        bool dup = false;
        for (const QString& o : out) dup = dup || o.compare(n, kCase) == 0;
        if (!dup) out << n;
    }
    QSettings().setValue(kKey, out);
}

QString trustedParent(const QString& path) {
    const QString p = normalize(path);
    for (const QString& d : trustedFolders())
        if (sameOrInside(p, d)) return d;
    return QString();
}

bool isTrusted(const QString& path) { return !trustedParent(path).isEmpty(); }

void trustFolder(const QString& dir) {
    QStringList all = trustedFolders();
    all << dir;
    setTrustedFolders(all);
}

void untrustFolder(const QString& dir) {
    const QString n = normalize(dir);
    QStringList all = trustedFolders();
    for (int i = all.size() - 1; i >= 0; --i)
        if (all[i].compare(n, kCase) == 0) all.removeAt(i);
    setTrustedFolders(all);
}

}  // namespace Trust

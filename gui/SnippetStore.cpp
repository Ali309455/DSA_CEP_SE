#include "SnippetStore.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

SnippetStore& SnippetStore::instance() {
    static SnippetStore s;
    if (!s.loaded_) s.load();
    return s;
}

QString SnippetStore::filePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.devpad";
    return QDir(dir).filePath("snippets.txt");
}

void SnippetStore::load() {
    loaded_ = true;                                         // set first: instance() must not recurse
    table_.clear();
    const QString path = filePath();
    if (QFileInfo::exists(path)) {
        const IOResult r = table_.loadFromFile(path.toUtf8().toStdString());
        if (r.ok) return;                                   // the file is the complete list, even if it is empty
        table_.clear();                                     // unreadable file: start from the built-ins
    }
    table_.loadDefaults();
}

bool SnippetStore::save(QString* error) {
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const IOResult r = table_.saveToFile(path.toUtf8().toStdString());
    if (!r.ok && error) *error = QString::fromUtf8(r.message.c_str());
    return r.ok;
}

void SnippetStore::resetToDefaults() {
    table_.clear();
    table_.loadDefaults();
    save();
    emit changed();
}

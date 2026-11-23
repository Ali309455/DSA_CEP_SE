// Trust.h - folder trust ("Restricted Mode").
//
// Code from a folder the user has not trusted is never run by DevPad: such a folder opens in
// Restricted Mode, where its files are read-only (no typing, saving, renaming or deleting), the
// terminal does not open, Run (F5) is off and the compiler check does not run. Trusting a folder
// also trusts everything below it. The list is kept with QSettings ("trust/folders").
#pragma once
#include <QString>
#include <QStringList>

namespace Trust {

// true if 'path' (a file or a folder) is a trusted folder or lies inside one
bool isTrusted(const QString& path);
// the trusted folder that covers 'path', or an empty string
QString trustedParent(const QString& path);

void trustFolder(const QString& dir);
// Removes 'dir' from the list (folders above it that are trusted still cover it).
void untrustFolder(const QString& dir);
QStringList trustedFolders();
void setTrustedFolders(const QStringList& dirs);

// absolute, cleaned, with '/' separators; the form the list is stored in
QString normalize(const QString& path);

}  // namespace Trust

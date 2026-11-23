// TrustDialog.h - the two windows of folder trust (Trust.h keeps the list itself).
//
//   TrustDialog::ask(parent, dir)      "Do you trust the authors of the files in this folder?"
//                                      Returns the folder to trust (dir itself, or its parent when that
//                                      box is ticked), or an empty string to stay in Restricted Mode.
//   TrustDialog::manage(parent, start) File > Manage Trusted Folders: the list, with Add and Remove.
#pragma once
#include <QString>

class QWidget;

namespace TrustDialog {

QString ask(QWidget* parent, const QString& dir);
void manage(QWidget* parent, const QString& startDir);

}  // namespace TrustDialog

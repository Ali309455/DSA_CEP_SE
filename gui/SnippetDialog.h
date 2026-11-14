// SnippetDialog.h - manage snippets (stored in the engine's own hash table).
#pragma once
#include <QDialog>
#include "Snippets.h"

class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QLabel;

class SnippetDialog : public QDialog {
    Q_OBJECT
public:
    SnippetDialog(SnippetTable& table, const QString& userFile, QWidget* parent = nullptr);
signals:
    void changed();
private:
    SnippetTable& t_;
    QString userFile_;
    QListWidget* list_;
    QLineEdit* trigger_;
    QPlainTextEdit* body_;
    QLabel *error_, *stats_;
    void reload();
    void pick();
    void addOrUpdate();
    void removeSelected();
    void importFile();
    void exportFile();
    void persist();
};

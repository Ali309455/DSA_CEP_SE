// SnippetDialog.h - the Snippets window: list, add / update, remove, import, export, reset.
// Edits the shared SnippetStore (engine hash table) and saves it after every change.
#pragma once
#include <QDialog>

class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QLabel;

class SnippetDialog : public QDialog {
    Q_OBJECT
public:
    explicit SnippetDialog(QWidget* parent = nullptr);

private:
    QListWidget* list_ = nullptr;
    QLineEdit* trigger_ = nullptr;
    QPlainTextEdit* body_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* stats_ = nullptr;

    void reload(const QString& select = QString());
    void pick();
    void addOrUpdate();
    void removeSelected();
    void importFile();
    void exportFile();
    void resetDefaults();
    void saved(const QString& selectTrigger);
};

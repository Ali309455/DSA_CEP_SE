// SnippetDialog.h - the Snippets window: add, edit, rename, duplicate and delete snippets.
//
// Left: a searchable list of the triggers (New / Duplicate / Delete under it).
// Right: the trigger and the body of the selected snippet; Save writes it, Revert undoes the edits.
// Import / Export / Reset sit at the bottom. It edits the shared SnippetStore (engine hash table) and
// saves the file after every change, so every open editor expands the new text at once.
#pragma once
#include <QDialog>
#include <QStringList>

class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;

class SnippetDialog : public QDialog {
    Q_OBJECT
public:
    explicit SnippetDialog(QWidget* parent = nullptr);

    // What the window shows (also used by the tests, which drive it without a mouse).
    QStringList shownTriggers() const;       // the list, after the search filter
    QString current() const { return current_; }   // trigger of the snippet being edited; empty for a new one
    bool isDirty() const { return dirty_; }  // the fields differ from the stored snippet
    QString message() const;
    void setFilter(const QString& text);     // search in triggers and bodies
    bool select(const QString& trigger);     // false: no such snippet, or the user kept the unsaved edits
    void startNew();                         // empty fields; Save adds the snippet
    void duplicateCurrent();                 // a copy of the selected snippet as a new, unsaved one
    void setFields(const QString& trigger, const QString& body);
    bool save();                             // add / update / rename; false (with a message) if it is not valid
    bool deleteCurrent(bool ask = true);     // ask: confirm in a message box first
    void revert();                           // back to the stored text

private:
    QListWidget* list_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLineEdit* trigger_ = nullptr;
    QPlainTextEdit* body_ = nullptr;
    QLabel* heading_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* stats_ = nullptr;
    QPushButton* saveBtn_ = nullptr;
    QPushButton* revertBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;
    QPushButton* dupBtn_ = nullptr;

    QString current_;                        // the stored snippet being edited ("" = a new one)
    bool dirty_ = false;
    bool loading_ = false;                   // fields are being filled by the code, not typed

    void reload(const QString& select = QString());
    void load(const QString& trigger);       // fields <- stored snippet
    void markDirty();
    void refreshState();
    bool confirmLeave();                     // unsaved edits: Save / Discard / Cancel
    void say(const QString& text, bool ok);
    void importFile();
    void exportFile();
    void resetDefaults();
    QString stored(const QString& selectTrigger);
};

// SettingsDialog.h - comprehensive preferences dialog for DevPad.
// Tabs: General & Auto-Save, Editor & Font, Keyboard Shortcuts, Snippets (Hash Table), Dictionary (Trie Autocomplete).
#pragma once
#include <QDialog>
#include <QFont>
#include <QString>
#include "Assist.h"
#include "Snippets.h"
#include "Theme.h"

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;

struct EditorSettings {
    QString fontFamily;
    int fontSize = 13;
    QString theme = "Midnight";
    bool autoSave = false;
    int autoSaveIntervalSec = 3;
    bool showUnsavedDot = true;
    bool autoClose = true;
    bool wordWrap = false;
    int tabSize = 4;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const EditorSettings& current,
                   SnippetTable& snippets,
                   const QString& snippetFile,
                   Autocomplete& autocomplete,
                   QWidget* parent = nullptr);

    EditorSettings settings() const;

signals:
    void settingsApplied(const EditorSettings& s);
    void snippetsModified();

private slots:
    void applyChanges();
    void updateFontPreview();
    void filterShortcuts(const QString& query);
    // Snippet tab slots
    void snippetPick();
    void snippetAddOrUpdate();
    void snippetRemove();
    void snippetImport();
    void snippetExport();
    // Dictionary tab slots
    void dictAddWord();
    void dictRemoveWord();
    void dictTestPrefix(const QString& prefix);

private:
    EditorSettings current_;
    SnippetTable& snippets_;
    QString snippetFile_;
    Autocomplete& autocomplete_;

    QTabWidget* tabs_;

    // General tab widgets
    QCheckBox* autoSaveCheck_;
    QSpinBox* autoSaveIntervalSpin_;
    QCheckBox* showUnsavedDotCheck_;
    QCheckBox* autoCloseCheck_;
    QCheckBox* wordWrapCheck_;
    QSpinBox* tabSizeSpin_;

    // Font & Appearance tab widgets
    QFontComboBox* fontCombo_;
    QSpinBox* fontSizeSpin_;
    QComboBox* themeCombo_;
    QPlainTextEdit* previewEdit_;

    // Shortcuts tab widgets
    QLineEdit* shortcutSearch_;
    QTreeWidget* shortcutTree_;

    // Snippets tab widgets
    QListWidget* snipList_;
    QLineEdit* snipTrigger_;
    QPlainTextEdit* snipBody_;
    QLabel* snipStats_;
    QLabel* snipError_;

    // Dictionary tab widgets
    QLineEdit* dictWordInput_;
    QLabel* dictStatsLabel_;
    QLineEdit* dictTestInput_;
    QLabel* dictTestResultLabel_;
    QLabel* dictMsgLabel_;

    void setupGeneralTab();
    void setupFontTab();
    void setupShortcutsTab();
    void setupSnippetsTab();
    void setupDictionaryTab();

    void reloadSnippets();
    void persistSnippets();
    void refreshDictStats();
};

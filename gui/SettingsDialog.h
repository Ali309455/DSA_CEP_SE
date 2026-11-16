// SettingsDialog.h - the Settings window (Ctrl+,).
//
// Sections in one scrolling page (Editor, Appearance, Terminal, General), a navigation list on
// the left that jumps to a section, and a search box that filters the rows. Every control
// applies at once to AppSettings, which stores it and tells the editors, the terminal and the
// theme; there is no OK / Cancel to forget. "Reset to Defaults" puts everything back.
#pragma once
#include <QDialog>
#include <QStringList>
#include <functional>
#include <vector>
#include "Settings.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QVBoxLayout;
class QWidget;
class QFrame;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    QStringList sectionNames() const;
    void showSection(const QString& name);        // scrolls to it and selects it in the list
    void setSearchText(const QString& text);      // what the search box does
    int visibleRowCount() const;                  // rows that pass the current search
    int rowCount() const { return static_cast<int>(rows_.size()); }
    QWidget* control(const QString& title) const; // a row's control (tests drive them)

private:
    struct Row {
        QFrame* frame;
        QFrame* separator;
        QString section;
        QString keywords;                         // lower case: title, description, extra words
        QString title;
        QWidget* control;
    };
    struct Section {
        QString name;
        QWidget* block;
    };
    QLineEdit* search_ = nullptr;
    QListWidget* nav_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QVBoxLayout* page_ = nullptr;
    QLabel* empty_ = nullptr;
    std::vector<Row> rows_;
    std::vector<Section> sections_;
    std::vector<std::function<void(const AppSettings::Values&)>> sync_;   // controls follow the settings
    QVBoxLayout* cardLayout_ = nullptr;
    QString currentSection_;
    bool syncing_ = false;
    bool navFromScroll_ = false;

    void buildUi();
    void beginSection(const QString& name, const QString& blurb);
    QWidget* addRow(const QString& title, const QString& description, QWidget* control, const QString& extraWords = QString());
    void addToggle(const QString& title, const QString& description, bool AppSettings::Values::*field, const QString& words = QString());
    void addStepper(const QString& title, const QString& description, int AppSettings::Values::*field, int lo, int hi, int step,
                    const QString& suffix, const QString& words = QString());
    void addChoice(const QString& title, const QString& description, int AppSettings::Values::*field, const QStringList& labels,
                   const QList<int>& values, const QString& words = QString());
    void edit(const std::function<void(AppSettings::Values&)>& fn);
    void applyFilter();
    void updateNavFromScroll();
};

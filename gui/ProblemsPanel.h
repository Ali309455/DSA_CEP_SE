// ProblemsPanel.h - the "Problems" tab: every error and warning of the current file in one list.
// Clicking a row (or pressing Enter on it) asks the main window to jump there.
#pragma once
#include <QString>
#include <QWidget>
#include <vector>
#include "Diagnostics.h"

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

class ProblemsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProblemsPanel(QWidget* parent = nullptr);

    // Replace the list. 'currentFile' is the file shown for problems that have none of their own
    // (the bracket checker's). Notes are not listed. Sorted by file, line, column.
    void setProblems(const std::vector<Diagnostic>& list, const QString& currentFile);
    void setStatus(const QString& text);       // e.g. "Checking..." or "g++ was not found ..."; "" clears

    int errorCount() const { return errors_; }
    int warningCount() const { return warnings_; }
    int rowCount() const;
    QString rowText(int row) const;            // the message of a row (for tests)
    void activateRow(int row);                 // same as clicking it (for tests)

signals:
    void jumpRequested(const QString& file, int line, int col);   // 1-based; line 0 = no location
    void hideRequested();
    void countsChanged(int errors, int warnings);

private:
    QTreeWidget* tree_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* status_ = nullptr;
    int errors_ = 0;
    int warnings_ = 0;
    void jumpTo(QTreeWidgetItem* it);
};

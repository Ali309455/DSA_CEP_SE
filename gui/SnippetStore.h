// SnippetStore.h - the one snippet table shared by every tab (engine/Snippets, our own HashTable).
//
// The table is loaded once: the user's snippets.txt if it exists (it holds the complete list,
// so a deleted built-in snippet stays deleted), otherwise the built-in C++ snippets.
// The Snippets window edits it and calls save(); every open editor reads the same table.
#pragma once
#include <QObject>
#include <QString>
#include "Snippets.h"

class SnippetStore : public QObject {
    Q_OBJECT
public:
    static SnippetStore& instance();

    const SnippetTable& table() const { return table_; }
    SnippetTable& table() { return table_; }

    QString filePath() const;                 // <config folder>/snippets.txt
    void load();                              // called once at start-up (safe to call again)
    bool save(QString* error = nullptr);      // writes the whole table to filePath()
    void resetToDefaults();                   // built-in snippets only; saved and announced
    void notifyChanged() { emit changed(); }

signals:
    void changed();

private:
    SnippetStore() = default;
    SnippetTable table_;
    bool loaded_ = false;
};

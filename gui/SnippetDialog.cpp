#include "SnippetDialog.h"
#include "Icons.h"
#include "Theme.h"

#include <QFileDialog>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>
#include "SnippetStore.h"

static std::string toStd(const QString& s) { return s.toUtf8().toStdString(); }
static QString fromStd(const std::string& s) { return QString::fromUtf8(s.c_str()); }

SnippetDialog::SnippetDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("snippetDialog");
    setWindowTitle("Snippets");
    resize(780, 520);
    setMinimumSize(620, 420);

    // ---- left: search, list, New / Duplicate / Delete
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Search snippets");
    search_->setClearButtonEnabled(true);
    search_->addAction(Icons::get(Icons::Search), QLineEdit::LeadingPosition);
    list_ = new QListWidget(this);
    list_->setObjectName("snippetList");
    list_->setUniformItemSizes(true);

    QPushButton* newBtn = new QPushButton(Icons::get(Icons::Plus), "New", this);
    newBtn->setObjectName("primary");
    dupBtn_ = new QPushButton("Duplicate", this);
    deleteBtn_ = new QPushButton(Icons::get(Icons::Trash), "Delete", this);
    QHBoxLayout* listButtons = new QHBoxLayout;
    listButtons->setSpacing(6);
    listButtons->addWidget(newBtn);
    listButtons->addWidget(dupBtn_);
    listButtons->addWidget(deleteBtn_);

    QVBoxLayout* left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(search_);
    left->addWidget(list_, 1);
    left->addLayout(listButtons);

    // ---- right: trigger, body, Save / Revert
    heading_ = new QLabel(this);
    heading_->setObjectName("snippetHeading");
    trigger_ = new QLineEdit(this);
    trigger_->setPlaceholderText("trigger: letters, digits and _ (for example  for  or  main)");
    trigger_->setMaxLength(32);
    body_ = new QPlainTextEdit(this);
    body_->setObjectName("snippetBody");
    body_->setFont(Theme::codeFont(10));
    body_->setPlaceholderText("What the trigger expands to.\n\n$0  where the cursor goes\n$$  a real dollar sign");
    body_->setTabChangesFocus(false);
    body_->setTabStopDistance(4 * QFontMetricsF(body_->font()).horizontalAdvance(QLatin1Char(' ')));
    QLabel* hint = new QLabel("Type the trigger in the editor and press Tab (or Edit > Expand Snippet) to expand it.", this);
    hint->setObjectName("snippetHint");
    hint->setWordWrap(true);
    message_ = new QLabel(this);
    message_->setObjectName("snippetMessage");
    message_->setWordWrap(true);

    saveBtn_ = new QPushButton("Save", this);
    saveBtn_->setObjectName("primary");
    revertBtn_ = new QPushButton("Revert", this);
    QHBoxLayout* editButtons = new QHBoxLayout;
    editButtons->addWidget(message_, 1);
    editButtons->addWidget(revertBtn_);
    editButtons->addWidget(saveBtn_);

    QVBoxLayout* right = new QVBoxLayout;
    right->setSpacing(6);
    right->addWidget(heading_);
    right->addWidget(new QLabel("Trigger", this));
    right->addWidget(trigger_);
    right->addWidget(new QLabel("Body", this));
    right->addWidget(body_, 1);
    right->addWidget(hint);
    right->addLayout(editButtons);

    QHBoxLayout* top = new QHBoxLayout;
    top->setSpacing(16);
    QWidget* leftBox = new QWidget(this);
    leftBox->setLayout(left);
    leftBox->setFixedWidth(250);
    left->setContentsMargins(0, 0, 0, 0);
    top->addWidget(leftBox);
    top->addLayout(right, 1);

    // ---- bottom: Import / Export / Reset, Close
    QPushButton* imp = new QPushButton("Import…", this);
    QPushButton* exp = new QPushButton("Export…", this);
    QPushButton* reset = new QPushButton("Reset to built-ins", this);
    QPushButton* close = new QPushButton("Close", this);
    stats_ = new QLabel(this);
    stats_->setObjectName("snippetStats");
    QHBoxLayout* bottom = new QHBoxLayout;
    bottom->addWidget(imp);
    bottom->addWidget(exp);
    bottom->addWidget(reset);
    bottom->addStretch(1);
    bottom->addWidget(close);

    QVBoxLayout* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(18, 16, 18, 14);
    mainLay->setSpacing(10);
    mainLay->addLayout(top, 1);
    mainLay->addWidget(stats_);
    mainLay->addLayout(bottom);

    connect(search_, &QLineEdit::textChanged, this, [this]() { reload(current_); });
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* now) {
        if (loading_) return;
        const QString want = now ? now->text() : QString();
        if (want == current_) return;
        if (!confirmLeave()) {                                 // keep editing: put the selection back
            loading_ = true;
            const QList<QListWidgetItem*> hit = list_->findItems(current_, Qt::MatchExactly);
            list_->setCurrentItem(hit.isEmpty() ? nullptr : hit.first());
            loading_ = false;
            return;
        }
        if (now) load(want);
    });
    connect(trigger_, &QLineEdit::textChanged, this, [this]() { markDirty(); });
    connect(body_, &QPlainTextEdit::textChanged, this, [this]() { markDirty(); });
    connect(newBtn, &QPushButton::clicked, this, [this]() { startNew(); });
    connect(dupBtn_, &QPushButton::clicked, this, [this]() { duplicateCurrent(); });
    connect(deleteBtn_, &QPushButton::clicked, this, [this]() { deleteCurrent(true); });
    connect(saveBtn_, &QPushButton::clicked, this, [this]() { save(); });
    connect(revertBtn_, &QPushButton::clicked, this, [this]() { revert(); });
    connect(imp, &QPushButton::clicked, this, &SnippetDialog::importFile);
    connect(exp, &QPushButton::clicked, this, &SnippetDialog::exportFile);
    connect(reset, &QPushButton::clicked, this, &SnippetDialog::resetDefaults);
    connect(close, &QPushButton::clicked, this, [this]() { if (confirmLeave()) accept(); });

    reload();
    if (list_->count() > 0) { list_->setCurrentRow(0); } else startNew();
}

// ---------------------------------------------------------------- what is shown

QStringList SnippetDialog::shownTriggers() const {
    QStringList l;
    for (int i = 0; i < list_->count(); ++i) l << list_->item(i)->text();
    return l;
}

QString SnippetDialog::message() const { return message_->text(); }

void SnippetDialog::say(const QString& text, bool ok) {
    message_->setStyleSheet(QString("color:%1;").arg((ok ? Theme::Success : Theme::Error).name()));
    message_->setText(text);
}

void SnippetDialog::setFilter(const QString& text) { search_->setText(text); }

// Rebuild the list (filtered by the search box) and select 'select' if it is shown.
void SnippetDialog::reload(const QString& select) {
    const SnippetTable& t = SnippetStore::instance().table();
    const QString filter = search_->text().trimmed();
    const bool wasLoading = loading_;
    loading_ = true;
    list_->clear();
    const DynArray<std::string> keys = t.triggers();
    int shown = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        const QString trig = fromStd(keys[i]);
        if (!filter.isEmpty()) {
            const Snippet* s = t.find(keys[i]);
            const bool hit = trig.contains(filter, Qt::CaseInsensitive) || (s && fromStd(s->body).contains(filter, Qt::CaseInsensitive));
            if (!hit) continue;
        }
        list_->addItem(trig);
        ++shown;
    }
    if (!select.isEmpty()) {
        const QList<QListWidgetItem*> hit = list_->findItems(select, Qt::MatchExactly);
        if (!hit.isEmpty()) list_->setCurrentItem(hit.first());
    }
    loading_ = wasLoading;
    stats_->setText(QString("%1 snippet(s)%2  ·  hash table: %3 buckets, longest chain %4")
                        .arg(t.size())
                        .arg(filter.isEmpty() ? QString() : QString(", %1 shown").arg(shown))
                        .arg(t.bucketCount())
                        .arg(t.longestChain()));
    refreshState();
}

void SnippetDialog::load(const QString& trigger) {
    const Snippet* s = SnippetStore::instance().table().find(toStd(trigger));
    if (!s) return;
    loading_ = true;
    current_ = fromStd(s->trigger);
    trigger_->setText(current_);
    body_->setPlainText(fromStd(s->body));
    loading_ = false;
    dirty_ = false;
    message_->clear();
    refreshState();
}

void SnippetDialog::markDirty() {
    if (loading_) return;
    QString body = body_->toPlainText();
    body.remove(QLatin1Char('\r'));
    const Snippet* s = current_.isEmpty() ? nullptr : SnippetStore::instance().table().find(toStd(current_));
    dirty_ = s ? (trigger_->text() != current_ || body != fromStd(s->body))
               : (!trigger_->text().isEmpty() || !body.isEmpty());
    message_->clear();
    refreshState();
}

void SnippetDialog::refreshState() {
    const bool existing = !current_.isEmpty();
    heading_->setText(existing ? (dirty_ ? "Editing \"" + current_ + "\" (unsaved changes)" : "Snippet \"" + current_ + "\"")
                               : (dirty_ ? "New snippet (not saved yet)" : "New snippet"));
    saveBtn_->setEnabled(dirty_);
    revertBtn_->setEnabled(dirty_);
    deleteBtn_->setEnabled(existing);
    dupBtn_->setEnabled(existing);
}

// Leaving the fields with unsaved edits: offer Save / Discard / Cancel.
bool SnippetDialog::confirmLeave() {
    if (!dirty_) return true;
    QMessageBox box(QMessageBox::Question, "Snippets", "This snippet has unsaved changes.", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    const int r = box.exec();
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save();
    revert();
    return true;
}

// ---------------------------------------------------------------- the actions

bool SnippetDialog::select(const QString& trigger) {
    if (trigger == current_ && !trigger.isEmpty()) return true;
    if (!SnippetStore::instance().table().find(toStd(trigger))) return false;
    if (!confirmLeave()) return false;
    if (!search_->text().isEmpty()) search_->clear();
    reload(trigger);
    load(trigger);
    return true;
}

void SnippetDialog::startNew() {
    if (!confirmLeave()) return;
    loading_ = true;
    list_->clearSelection();
    list_->setCurrentItem(nullptr);
    current_.clear();
    trigger_->clear();
    body_->clear();
    loading_ = false;
    dirty_ = false;
    message_->clear();
    refreshState();
    trigger_->setFocus();
}

void SnippetDialog::duplicateCurrent() {
    if (current_.isEmpty()) { say("Select a snippet first.", false); return; }
    QString body = body_->toPlainText();
    QString name = trigger_->text().trimmed() + "_copy";
    const SnippetTable& t = SnippetStore::instance().table();
    for (int n = 2; t.find(toStd(name)); ++n) name = trigger_->text().trimmed() + "_copy" + QString::number(n);
    loading_ = true;
    list_->setCurrentItem(nullptr);
    current_.clear();
    trigger_->setText(name);
    body_->setPlainText(body);
    loading_ = false;
    dirty_ = true;
    message_->clear();
    refreshState();
    trigger_->setFocus();
    trigger_->selectAll();
}

void SnippetDialog::setFields(const QString& trigger, const QString& body) {
    trigger_->setText(trigger);
    body_->setPlainText(body);
}

void SnippetDialog::revert() {
    if (current_.isEmpty()) {
        loading_ = true;
        trigger_->clear();
        body_->clear();
        loading_ = false;
        dirty_ = false;
        message_->clear();
        refreshState();
    } else {
        load(current_);
    }
}

// After any change: write the file, tell the editors, refresh the list.
// Returns "" or what went wrong with writing the file (the change itself is kept in memory either way).
QString SnippetDialog::stored(const QString& selectTrigger) {
    QString err;
    const bool wrote = SnippetStore::instance().save(&err);
    SnippetStore::instance().notifyChanged();
    reload(selectTrigger);
    return wrote ? QString() : "Changed, but the snippets file could not be saved: " + err;
}

bool SnippetDialog::save() {
    QString body = body_->toPlainText();
    body.remove(QLatin1Char('\r'));
    const QString trig = trigger_->text().trimmed();
    if (trig.isEmpty()) { say("Type a trigger first.", false); return false; }
    SnippetTable& t = SnippetStore::instance().table();
    const bool renamed = !current_.isEmpty() && trig != current_;
    if (trig != current_ && t.find(toStd(trig))) {
        say("There is already a snippet \"" + trig + "\". Select it in the list to edit it, or pick another trigger.", false);
        return false;
    }
    try {
        t.add(toStd(trig), toStd(body));                       // a bad trigger / body or too many snippets throws
        if (renamed) t.remove(toStd(current_));                // the old name goes only after the new one worked
    } catch (const std::exception& ex) {
        say(QString::fromUtf8(ex.what()), false);
        return false;
    }
    const bool fresh = current_.isEmpty();
    loading_ = true;
    current_ = trig;
    loading_ = false;
    const QString err = stored(trig);
    load(trig);
    if (!err.isEmpty()) say(err, false);
    else say(fresh ? "Added \"" + trig + "\"." : renamed ? "Saved as \"" + trig + "\"." : "Saved \"" + trig + "\".", true);
    return true;
}

bool SnippetDialog::deleteCurrent(bool ask) {
    if (current_.isEmpty()) { say("Select a snippet first.", false); return false; }
    const QString name = current_;
    if (ask && QMessageBox::question(this, "Delete snippet", "Delete the snippet \"" + name + "\"?") != QMessageBox::Yes) return false;
    const int row = std::max(0, list_->currentRow());
    SnippetStore::instance().table().remove(toStd(name));
    loading_ = true;
    current_.clear();
    trigger_->clear();
    body_->clear();
    loading_ = false;
    dirty_ = false;
    QString err = stored(QString());
    if (list_->count() > 0) {
        const QString next = list_->item(std::min(row, list_->count() - 1))->text();
        reload(next);
        load(next);
    } else {
        refreshState();
    }
    if (!err.isEmpty()) say(err, false);
    else say("Deleted \"" + name + "\".", true);
    return true;
}

void SnippetDialog::importFile() {
    if (!confirmLeave()) return;
    const QString p = QFileDialog::getOpenFileName(this, "Import snippets");
    if (p.isEmpty()) return;
    const IOResult r = SnippetStore::instance().table().loadFromFile(toStd(p));
    const QString err = stored(current_);
    say(err.isEmpty() ? fromStd(r.message) : err, r.ok && err.isEmpty());
}

void SnippetDialog::exportFile() {
    const QString p = QFileDialog::getSaveFileName(this, "Export snippets", "snippets.txt");
    if (p.isEmpty()) return;
    const IOResult r = SnippetStore::instance().table().saveToFile(toStd(p));
    say(fromStd(r.message), r.ok);
}

void SnippetDialog::resetDefaults() {
    if (QMessageBox::question(this, "Snippets", "Replace all snippets with the built-in C++ ones? Your own snippets will be lost.") !=
        QMessageBox::Yes)
        return;
    SnippetStore::instance().resetToDefaults();
    loading_ = true;
    current_.clear();
    trigger_->clear();
    body_->clear();
    loading_ = false;
    dirty_ = false;
    reload();
    if (list_->count() > 0) {
        const QString first = list_->item(0)->text();
        reload(first);
        load(first);
    }
    say("Built-in snippets restored.", true);
}

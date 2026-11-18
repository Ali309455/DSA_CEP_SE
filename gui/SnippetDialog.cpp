#include "SnippetDialog.h"

#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <exception>
#include "SnippetStore.h"

static std::string toStd(const QString& s) { return s.toUtf8().toStdString(); }
static QString fromStd(const std::string& s) { return QString::fromUtf8(s.c_str()); }

SnippetDialog::SnippetDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Snippets");
    resize(700, 460);

    list_ = new QListWidget(this);
    list_->setMaximumWidth(190);
    trigger_ = new QLineEdit(this);
    trigger_->setPlaceholderText("trigger (letters, digits, _)");
    body_ = new QPlainTextEdit(this);
    body_->setPlaceholderText("Body. $0 = where the cursor goes, $$ = a real dollar sign.");
    body_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    body_->setTabChangesFocus(false);
    message_ = new QLabel(this);
    message_->setWordWrap(true);
    stats_ = new QLabel(this);
    stats_->setStyleSheet("color:#8a8a8a;");

    QPushButton* add = new QPushButton("Add / Update", this);
    QPushButton* rem = new QPushButton("Remove", this);
    QPushButton* imp = new QPushButton("Import…", this);
    QPushButton* exp = new QPushButton("Export…", this);
    QPushButton* reset = new QPushButton("Reset to built-ins", this);
    QPushButton* close = new QPushButton("Close", this);

    QVBoxLayout* right = new QVBoxLayout;
    right->addWidget(new QLabel("Trigger", this));
    right->addWidget(trigger_);
    right->addWidget(new QLabel("Body", this));
    right->addWidget(body_, 1);
    right->addWidget(message_);
    QHBoxLayout* row = new QHBoxLayout;
    row->addWidget(add);
    row->addWidget(rem);
    row->addWidget(imp);
    row->addWidget(exp);
    row->addWidget(reset);
    row->addStretch(1);
    row->addWidget(close);
    right->addLayout(row);
    right->addWidget(stats_);

    QHBoxLayout* mainLay = new QHBoxLayout(this);
    mainLay->addWidget(list_);
    mainLay->addLayout(right, 1);

    connect(list_, &QListWidget::currentItemChanged, this, [this]() { pick(); });
    connect(add, &QPushButton::clicked, this, &SnippetDialog::addOrUpdate);
    connect(rem, &QPushButton::clicked, this, &SnippetDialog::removeSelected);
    connect(imp, &QPushButton::clicked, this, &SnippetDialog::importFile);
    connect(exp, &QPushButton::clicked, this, &SnippetDialog::exportFile);
    connect(reset, &QPushButton::clicked, this, &SnippetDialog::resetDefaults);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    reload();
}

void SnippetDialog::reload(const QString& select) {
    const SnippetTable& t = SnippetStore::instance().table();
    QSignalBlocker block(list_);
    list_->clear();
    const DynArray<std::string> keys = t.triggers();
    for (size_t i = 0; i < keys.size(); ++i) list_->addItem(fromStd(keys[i]));
    stats_->setText(QString("Hash table: %1 snippet(s), %2 buckets, longest chain %3. Type the trigger and press Tab to expand it.")
                        .arg(t.size()).arg(t.bucketCount()).arg(t.longestChain()));
    if (!select.isEmpty()) {
        const QList<QListWidgetItem*> hit = list_->findItems(select, Qt::MatchExactly);
        if (!hit.isEmpty()) list_->setCurrentItem(hit.first());
    }
}

void SnippetDialog::pick() {
    QListWidgetItem* it = list_->currentItem();
    if (!it) return;
    const Snippet* s = SnippetStore::instance().table().find(toStd(it->text()));
    if (!s) return;
    trigger_->setText(fromStd(s->trigger));
    body_->setPlainText(fromStd(s->body));
    message_->clear();
}

// After any change: write the file, tell the editors, refresh the list.
void SnippetDialog::saved(const QString& selectTrigger) {
    QString err;
    if (!SnippetStore::instance().save(&err)) {
        message_->setStyleSheet("color:#e5534b;");
        message_->setText("Changed, but the snippets file could not be saved: " + err);
    }
    SnippetStore::instance().notifyChanged();
    reload(selectTrigger);
}

void SnippetDialog::addOrUpdate() {
    QString body = body_->toPlainText();
    body.remove(QLatin1Char('\r'));
    const QString trig = trigger_->text().trimmed();
    try {
        const bool fresh = SnippetStore::instance().table().add(toStd(trig), toStd(body));
        message_->setStyleSheet("color:#73c991;");
        message_->setText(fresh ? "Added \"" + trig + "\"." : "Updated \"" + trig + "\".");
        saved(trig);
    } catch (const std::exception& ex) {                       // bad trigger / body, too many snippets
        message_->setStyleSheet("color:#e5534b;");
        message_->setText(QString::fromUtf8(ex.what()));
    }
}

void SnippetDialog::removeSelected() {
    QListWidgetItem* it = list_->currentItem();
    if (!it) {
        message_->setStyleSheet("color:#e5534b;");
        message_->setText("Select a snippet first.");
        return;
    }
    const QString name = it->text();
    SnippetStore::instance().table().remove(toStd(name));
    trigger_->clear();
    body_->clear();
    message_->setStyleSheet("color:#73c991;");
    message_->setText("Removed \"" + name + "\".");
    saved(QString());
}

void SnippetDialog::importFile() {
    const QString p = QFileDialog::getOpenFileName(this, "Import snippets");
    if (p.isEmpty()) return;
    const IOResult r = SnippetStore::instance().table().loadFromFile(toStd(p));
    message_->setStyleSheet(r.ok ? "color:#73c991;" : "color:#e5534b;");
    message_->setText(fromStd(r.message));
    saved(QString());
}

void SnippetDialog::exportFile() {
    const QString p = QFileDialog::getSaveFileName(this, "Export snippets", "snippets.txt");
    if (p.isEmpty()) return;
    const IOResult r = SnippetStore::instance().table().saveToFile(toStd(p));
    message_->setStyleSheet(r.ok ? "color:#73c991;" : "color:#e5534b;");
    message_->setText(fromStd(r.message));
}

void SnippetDialog::resetDefaults() {
    if (QMessageBox::question(this, "Snippets", "Replace all snippets with the built-in C++ ones? Your own snippets will be lost.") !=
        QMessageBox::Yes)
        return;
    SnippetStore::instance().resetToDefaults();
    trigger_->clear();
    body_->clear();
    message_->setStyleSheet("color:#73c991;");
    message_->setText("Built-in snippets restored.");
    reload();
}

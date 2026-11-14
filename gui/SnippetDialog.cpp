#include "SnippetDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include "TextUtil.h"

SnippetDialog::SnippetDialog(SnippetTable& table, const QString& userFile, QWidget* parent)
    : QDialog(parent), t_(table), userFile_(userFile) {
    setWindowTitle(tr("Snippets"));
    resize(640, 420);
    list_ = new QListWidget(this);
    list_->setMaximumWidth(180);
    trigger_ = new QLineEdit(this);
    trigger_->setPlaceholderText(tr("trigger (letters, digits, _)"));
    body_ = new QPlainTextEdit(this);
    body_->setPlaceholderText(tr("Body. Use $0 for the cursor position, $$ for a literal $."));
    body_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color:#e5534b;"));
    stats_ = new QLabel(this);
    auto* add = new QPushButton(tr("Add / Update"), this);
    auto* rem = new QPushButton(tr("Remove"), this);
    auto* imp = new QPushButton(tr("Import..."), this);
    auto* exp = new QPushButton(tr("Export..."), this);
    auto* close = new QPushButton(tr("Close"), this);

    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("Trigger"), this));
    right->addWidget(trigger_);
    right->addWidget(new QLabel(tr("Body"), this));
    right->addWidget(body_, 1);
    right->addWidget(error_);
    auto* btns = new QHBoxLayout;
    for (QPushButton* b : {add, rem, imp, exp}) btns->addWidget(b);
    btns->addStretch(1);
    btns->addWidget(close);
    right->addLayout(btns);
    right->addWidget(stats_);
    auto* main = new QHBoxLayout(this);
    main->addWidget(list_);
    main->addLayout(right, 1);

    connect(list_, &QListWidget::currentItemChanged, this, [this] { pick(); });
    connect(add, &QPushButton::clicked, this, &SnippetDialog::addOrUpdate);
    connect(rem, &QPushButton::clicked, this, &SnippetDialog::removeSelected);
    connect(imp, &QPushButton::clicked, this, &SnippetDialog::importFile);
    connect(exp, &QPushButton::clicked, this, &SnippetDialog::exportFile);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    reload();
}

void SnippetDialog::reload() {
    list_->clear();
    const DynArray<std::string> keys = t_.triggers();
    for (size_t i = 0; i < keys.size(); ++i) list_->addItem(TextUtil::fromStd(keys[i]));
    stats_->setText(tr("Hash table: %1 snippet(s), %2 buckets, longest chain %3")
                        .arg(t_.size()).arg(t_.bucketCount()).arg(t_.longestChain()));
}

void SnippetDialog::pick() {
    QListWidgetItem* it = list_->currentItem();
    if (!it) return;
    const Snippet* s = t_.find(TextUtil::toStd(it->text()));
    if (!s) return;
    trigger_->setText(TextUtil::fromStd(s->trigger));
    body_->setPlainText(TextUtil::fromStd(s->body));
    error_->clear();
}

void SnippetDialog::persist() {
    if (userFile_.isEmpty()) return;
    QDir().mkpath(QFileInfo(userFile_).absolutePath());
    const IOResult r = t_.saveToFile(TextUtil::toStd(userFile_));
    if (!r.ok) error_->setText(TextUtil::fromStd(r.message));
}

void SnippetDialog::addOrUpdate() {
    try {
        t_.add(TextUtil::toStd(trigger_->text().trimmed()), TextUtil::toStd(TextUtil::sanitize(body_->toPlainText())));
        error_->clear();
        reload();
        persist();
        emit changed();
    } catch (const std::exception& ex) {          // invalid trigger / body, too many snippets
        error_->setText(QString::fromUtf8(ex.what()));
    }
}

void SnippetDialog::removeSelected() {
    QListWidgetItem* it = list_->currentItem();
    if (!it) { error_->setText(tr("Select a snippet first.")); return; }
    t_.remove(TextUtil::toStd(it->text()));
    error_->clear();
    reload();
    persist();
    emit changed();
}

void SnippetDialog::importFile() {
    const QString p = QFileDialog::getOpenFileName(this, tr("Import snippets"));
    if (p.isEmpty()) return;
    const IOResult r = t_.loadFromFile(TextUtil::toStd(p));
    error_->setText(TextUtil::fromStd(r.message));
    reload();
    persist();
    emit changed();
}

void SnippetDialog::exportFile() {
    const QString p = QFileDialog::getSaveFileName(this, tr("Export snippets"), QStringLiteral("snippets.txt"));
    if (p.isEmpty()) return;
    const IOResult r = t_.saveToFile(TextUtil::toStd(p));
    error_->setText(TextUtil::fromStd(r.message));
}

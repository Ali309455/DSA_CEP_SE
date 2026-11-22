#include "ProblemsPanel.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include "Icons.h"
#include "Theme.h"

namespace {
enum Role { RoleFile = Qt::UserRole, RoleLine, RoleCol };

QIcon dotIcon(const QColor& c) {
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(3, 3, 8, 8));
    p.end();
    return QIcon(pm);
}
}  // namespace

ProblemsPanel::ProblemsPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("terminalPanel");                          // same surface colour as the terminal beside it
    setMinimumHeight(110);

    QVBoxLayout* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    QWidget* head = new QWidget(this);
    head->setObjectName("terminalHeader");
    head->setFixedHeight(32);                              // same header as the terminal's, for a calm tab switch
    QHBoxLayout* h = new QHBoxLayout(head);
    h->setContentsMargins(12, 0, 6, 0);
    h->setSpacing(8);
    // (no "PROBLEMS" title: the tab above already says Problems)
    summary_ = new QLabel(head);
    summary_->setObjectName("terminalChip");
    summary_->setFixedHeight(20);
    status_ = new QLabel(head);
    status_->setObjectName("terminalCwd");
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QToolButton* close = new QToolButton(head);
    close->setIcon(Icons::get(Icons::Close));
    close->setIconSize(QSize(16, 16));
    close->setFixedSize(26, 26);
    close->setToolTip("Hide panel");
    close->setFocusPolicy(Qt::NoFocus);
    h->addWidget(summary_, 0, Qt::AlignVCenter);
    h->addSpacing(4);
    h->addWidget(status_, 1);
    h->addWidget(close);
    v->addWidget(head);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);                                // message | file | line:col
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setIconSize(QSize(14, 14));
    tree_->setObjectName("problemsTree");
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    v->addWidget(tree_, 1);

    connect(close, &QToolButton::clicked, this, &ProblemsPanel::hideRequested);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) { jumpTo(it); });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) { jumpTo(it); });

    setProblems({}, QString());
}

void ProblemsPanel::setProblems(const std::vector<Diagnostic>& listIn, const QString& currentFile) {
    std::vector<Diagnostic> list;
    for (const Diagnostic& d : listIn)
        if (d.severity != Severity::Note) list.push_back(d);
    std::stable_sort(list.begin(), list.end(), [](const Diagnostic& a, const Diagnostic& b) {
        if (a.file != b.file) return a.file < b.file;
        if (a.line != b.line) return a.line < b.line;
        return a.col < b.col;
    });

    const QString keepSelection = tree_->currentItem() ? tree_->currentItem()->text(0) : QString();
    tree_->clear();
    errors_ = warnings_ = 0;
    const QIcon errIcon = dotIcon(Theme::Error);
    const QIcon warnIcon = dotIcon(Theme::Warning);
    for (const Diagnostic& d : list) {
        const bool isError = d.severity == Severity::Error;
        (isError ? errors_ : warnings_)++;
        const QString file = d.file.empty() ? currentFile : QString::fromUtf8(d.file.c_str());
        QTreeWidgetItem* it = new QTreeWidgetItem(tree_);
        it->setIcon(0, isError ? errIcon : warnIcon);
        it->setText(0, QString::fromUtf8(d.message.c_str()));
        it->setText(1, file.isEmpty() ? QString("Untitled") : QFileInfo(file).fileName());
        it->setText(2, d.line == 0 ? QString() : QString("%1:%2").arg(d.line).arg(d.col == 0 ? 1 : d.col));
        it->setToolTip(0, QString::fromUtf8(d.message.c_str()) + "\n[" + QString::fromUtf8(d.source.c_str()) + "]");
        it->setToolTip(1, file);
        it->setForeground(1, QColor(0x8a, 0x8a, 0x8a));
        it->setForeground(2, QColor(0x8a, 0x8a, 0x8a));
        it->setData(0, RoleFile, file);
        it->setData(0, RoleLine, static_cast<int>(d.line));
        it->setData(0, RoleCol, static_cast<int>(d.col));
        if (!keepSelection.isEmpty() && it->text(0) == keepSelection && !tree_->currentItem()) tree_->setCurrentItem(it);
    }
    if (errors_ == 0 && warnings_ == 0) summary_->setText("No problems");
    else summary_->setText(QString("%1 error%2, %3 warning%4")
                               .arg(errors_).arg(errors_ == 1 ? "" : "s").arg(warnings_).arg(warnings_ == 1 ? "" : "s"));
    emit countsChanged(errors_, warnings_);
}

void ProblemsPanel::setStatus(const QString& text) {
    status_->setText(text);
    status_->setToolTip(text);
}

int ProblemsPanel::rowCount() const { return tree_->topLevelItemCount(); }

QString ProblemsPanel::rowText(int row) const {
    QTreeWidgetItem* it = tree_->topLevelItem(row);
    return it ? it->text(0) : QString();
}

void ProblemsPanel::activateRow(int row) {
    if (QTreeWidgetItem* it = tree_->topLevelItem(row)) jumpTo(it);
}

void ProblemsPanel::jumpTo(QTreeWidgetItem* it) {
    if (!it) return;
    emit jumpRequested(it->data(0, RoleFile).toString(), it->data(0, RoleLine).toInt(), it->data(0, RoleCol).toInt());
}

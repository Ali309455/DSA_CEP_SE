#include "TrustDialog.h"

#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include "Icons.h"
#include "Theme.h"
#include "Trust.h"
#include "WinTheme.h"

namespace {

QLabel* iconLabel(Icons::Glyph g, const QColor& c, int size, QWidget* parent) {
    QLabel* l = new QLabel(parent);
    l->setPixmap(Icons::get(g, c, c).pixmap(size, size));
    l->setFixedSize(size, size);
    return l;
}

// One row of the "what changes" list: a coloured mark and a sentence.
QWidget* pointRow(Icons::Glyph g, const QColor& c, const QString& text, QWidget* parent) {
    QWidget* row = new QWidget(parent);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);
    l->addWidget(iconLabel(g, c, 16, row), 0, Qt::AlignTop);
    QLabel* t = new QLabel(text, row);
    t->setObjectName("trustPoint");
    t->setWordWrap(true);
    l->addWidget(t, 1);
    return row;
}

}  // namespace

QString TrustDialog::ask(QWidget* parent, const QString& dir) {
    const QString native = QDir::toNativeSeparators(Trust::normalize(dir));
    const QFileInfo fi(dir);
    const QString parentDir = fi.absolutePath();
    const bool hasParent = !parentDir.isEmpty() && QDir(parentDir) != QDir(dir) && !QDir(dir).isRoot();

    QDialog d(parent);
    d.setObjectName("trustDialog");
    d.setWindowTitle("Trust This Folder?");
    d.setMinimumWidth(520);
    QVBoxLayout* lay = new QVBoxLayout(&d);
    lay->setContentsMargins(24, 22, 24, 18);
    lay->setSpacing(12);

    QHBoxLayout* head = new QHBoxLayout();
    head->setSpacing(12);
    head->addWidget(iconLabel(Icons::Shield, Theme::Warning, 32, &d), 0, Qt::AlignTop);
    QLabel* title = new QLabel("Do you trust the authors of the files in this folder?", &d);
    title->setObjectName("trustHeading");
    title->setWordWrap(true);
    head->addWidget(title, 1);
    lay->addLayout(head);

    QLabel* path = new QLabel(native, &d);
    path->setObjectName("trustPath");
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(path);

    lay->addWidget(pointRow(Icons::Check, Theme::Success,
                            "<b>Trust</b> — edit and save files, use the terminal, run code (F5) and see compiler errors.", &d));
    lay->addWidget(pointRow(Icons::Lock, Theme::Warning,
                            "<b>Restricted Mode</b> — read the code safely: files are read-only and nothing from the folder runs.", &d));

    QCheckBox* parentBox = nullptr;
    if (hasParent) {
        parentBox = new QCheckBox("Also trust every folder in " + QDir::toNativeSeparators(parentDir), &d);
        parentBox->setObjectName("trustParent");
        lay->addWidget(parentBox);
    }

    QLabel* hint = new QLabel("Trust a folder only if you know where its code comes from. You can change this later in "
                              "File > Manage Trusted Folders.", &d);
    hint->setObjectName("trustHint");
    hint->setWordWrap(true);
    lay->addWidget(hint);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton* stay = new QPushButton("Stay in Restricted Mode", &d);
    QPushButton* trust = new QPushButton("Trust Folder", &d);
    trust->setObjectName("primary");
    buttons->addWidget(stay);
    buttons->addWidget(trust);
    lay->addSpacing(4);
    lay->addLayout(buttons);
    trust->setAutoDefault(false);                          // Enter (on the focused Stay button) and Esc never trust by accident
    stay->setFocus();
    QObject::connect(stay, &QPushButton::clicked, &d, &QDialog::reject);
    QObject::connect(trust, &QPushButton::clicked, &d, &QDialog::accept);
    if (parentBox)
        QObject::connect(parentBox, &QCheckBox::toggled, trust, [trust](bool on) { trust->setText(on ? "Trust Parent Folder" : "Trust Folder"); });

    enableDarkTitleBar(&d);
    if (d.exec() != QDialog::Accepted) return QString();
    return parentBox && parentBox->isChecked() ? parentDir : dir;
}

void TrustDialog::manage(QWidget* parent, const QString& startDir) {
    QDialog d(parent);
    d.setObjectName("trustDialog");
    d.setWindowTitle("Trusted Folders");
    d.resize(600, 380);
    QVBoxLayout* lay = new QVBoxLayout(&d);
    lay->setContentsMargins(20, 18, 20, 16);
    lay->setSpacing(10);

    QHBoxLayout* head = new QHBoxLayout();
    head->setSpacing(10);
    head->addWidget(iconLabel(Icons::Shield, Theme::Accent, 24, &d), 0, Qt::AlignVCenter);
    QLabel* title = new QLabel("Trusted Folders", &d);
    title->setObjectName("trustHeading");
    head->addWidget(title, 1);
    lay->addLayout(head);

    QLabel* info = new QLabel("In these folders (and every folder inside them) files can be edited, and the terminal, Run (F5) "
                              "and the compiler check work. Every other folder opens in Restricted Mode.", &d);
    info->setObjectName("trustHint");
    info->setWordWrap(true);
    lay->addWidget(info);

    // the list, or a short message when it is empty
    QStackedWidget* pages = new QStackedWidget(&d);
    QListWidget* list = new QListWidget(pages);
    list->setObjectName("trustedList");
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setIconSize(QSize(16, 16));
    QLabel* empty = new QLabel("No trusted folders yet.\nEvery folder opens in Restricted Mode until you trust it.", pages);
    empty->setObjectName("trustEmpty");
    empty->setAlignment(Qt::AlignCenter);
    pages->addWidget(list);
    pages->addWidget(empty);
    lay->addWidget(pages, 1);

    QHBoxLayout* row = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton("Add Folder…", &d);
    QPushButton* removeBtn = new QPushButton("Remove", &d);
    QPushButton* closeBtn = new QPushButton("Done", &d);
    closeBtn->setObjectName("primary");
    row->addWidget(addBtn);
    row->addWidget(removeBtn);
    row->addStretch(1);
    row->addWidget(closeBtn);
    lay->addLayout(row);

    auto fill = [list, pages, empty, removeBtn]() {
        list->clear();
        for (const QString& f : Trust::trustedFolders()) {
            QListWidgetItem* it = new QListWidgetItem(Icons::folder(false), QDir::toNativeSeparators(f), list);
            it->setData(Qt::UserRole, f);
            it->setToolTip(QDir::toNativeSeparators(f));
        }
        pages->setCurrentWidget(list->count() > 0 ? static_cast<QWidget*>(list) : empty);
        removeBtn->setEnabled(false);
    };
    fill();
    QObject::connect(list, &QListWidget::itemSelectionChanged, &d, [list, removeBtn]() { removeBtn->setEnabled(!list->selectedItems().isEmpty()); });
    QObject::connect(addBtn, &QPushButton::clicked, &d, [&d, startDir, fill]() {
        const QString dir = QFileDialog::getExistingDirectory(&d, "Trust Folder", startDir);
        if (dir.isEmpty()) return;
        Trust::trustFolder(dir);
        fill();
    });
    QObject::connect(removeBtn, &QPushButton::clicked, &d, [list, fill]() {
        for (QListWidgetItem* it : list->selectedItems()) Trust::untrustFolder(it->data(Qt::UserRole).toString());
        fill();
    });
    QObject::connect(closeBtn, &QPushButton::clicked, &d, &QDialog::accept);
    enableDarkTitleBar(&d);
    d.exec();
}

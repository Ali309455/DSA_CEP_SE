#include "WelcomePage.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "Icons.h"
#include "Theme.h"
#include <QScrollArea>
#include <QVBoxLayout>

// links and card icons follow the accent colour chosen in Settings
static QColor linkColor() { return Theme::isDark() ? Theme::Accent.lighter(125) : Theme::Accent.darker(118); }
static QString linkStyle() { return QString("color:%1;text-decoration:none;").arg(linkColor().name()); }

static QString esc(const QString& s) { return s.toHtmlEscaped(); }

// "C:/Users/me/projects/x" -> "~\projects\x" style, so long paths stay readable
static QString shortPath(const QString& path) {
    QString p = QDir::toNativeSeparators(path);
    const QString home = QDir::toNativeSeparators(QDir::homePath());
    if (p.startsWith(home, Qt::CaseInsensitive)) p = "~" + p.mid(home.size());
    return p;
}

static QLabel* heading(const QString& text, QWidget* parent) {
    QLabel* l = new QLabel(text, parent);
    l->setObjectName("welcomeHeading");
    return l;
}

QLabel* WelcomePage::link(const QString& html, QWidget* parent) {
    QLabel* l = new QLabel(html, parent);
    l->setObjectName("welcomeLink");
    l->setTextFormat(Qt::RichText);
    l->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    l->setCursor(Qt::PointingHandCursor);
    connect(l, &QLabel::linkActivated, this, &WelcomePage::activate);
    return l;
}

// A big clickable tile: icon, title, and a hint with the shortcut.
QPushButton* WelcomePage::card(const QString& target, int glyph, const QString& title, const QString& hint, QWidget* parent) {
    QPushButton* b = new QPushButton(parent);
    b->setObjectName("welcomeCard");
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(66);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout* h = new QHBoxLayout(b);
    h->setContentsMargins(14, 10, 14, 10);
    h->setSpacing(12);
    QLabel* icon = new QLabel(b);
    icon->setPixmap(Icons::get(Icons::Glyph(glyph), linkColor(), linkColor()).pixmap(QSize(24, 24)));
    cardIcons_.emplace_back(icon, glyph);
    QVBoxLayout* t = new QVBoxLayout();
    t->setSpacing(1);
    QLabel* l1 = new QLabel(title, b);
    l1->setObjectName("welcomeCardTitle");
    QLabel* l2 = new QLabel(hint, b);
    l2->setObjectName("welcomeCardHint");
    t->addWidget(l1);
    t->addWidget(l2);
    h->addWidget(icon);
    h->addLayout(t, 1);
    for (QLabel* l : {icon, l1, l2}) l->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(b, &QPushButton::clicked, this, [this, target]() { activate(target); });
    return b;
}

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
    setObjectName("welcomePage");
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setObjectName("welcomeScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    QWidget* page = new QWidget(scroll);
    page->setObjectName("welcomeInner");
    scroll->setWidget(page);
    QHBoxLayout* center = new QHBoxLayout(page);
    center->addStretch(1);
    QWidget* column = new QWidget(page);
    column->setMaximumWidth(860);
    column->setMinimumWidth(520);
    center->addWidget(column, 10);
    center->addStretch(1);

    QVBoxLayout* col = new QVBoxLayout(column);
    col->setContentsMargins(28, 60, 28, 28);
    col->setSpacing(4);

    // logo and title
    QHBoxLayout* titleRow = new QHBoxLayout();
    titleRow->setSpacing(18);
    QLabel* logo = new QLabel(column);
    logo->setObjectName("welcomeLogo");
    const qreal dpr = devicePixelRatioF();
    QPixmap lp = Icons::appPixmap(int(64 * dpr));
    lp.setDevicePixelRatio(dpr);
    logo->setPixmap(lp);
    QVBoxLayout* titles = new QVBoxLayout();
    titles->setSpacing(2);
    QLabel* title = new QLabel("DevPad", column);
    title->setObjectName("welcomeTitle");
    QLabel* sub = new QLabel("A code editor built on its own data structures", column);
    sub->setObjectName("welcomeSubtitle");
    titles->addStretch(1);
    titles->addWidget(title);
    titles->addWidget(sub);
    titles->addStretch(1);
    titleRow->addWidget(logo);
    titleRow->addLayout(titles, 1);
    col->addLayout(titleRow);
    col->addSpacing(40);

    // start: cards
    col->addWidget(heading("Start", column));
    QGridLayout* cards = new QGridLayout();
    cards->setHorizontalSpacing(12);
    cards->setVerticalSpacing(12);
    cards->addWidget(card("new", Icons::NewFile, "New File", "Ctrl+N  ·  start with an empty file", column), 0, 0);
    cards->addWidget(card("open", Icons::OpenFile, "Open File…", "Ctrl+O  ·  pick files to edit", column), 0, 1);
    cards->addWidget(card("folder", Icons::Folder, "Open Folder…", "Ctrl+Shift+O  ·  browse it in the Explorer", column), 1, 0);
    quick_ = card("quick", Icons::Palette, "Go to File…", "Ctrl+P  ·  type part of a file name", column);
    cards->addWidget(quick_, 1, 1);
    quick_->hide();
    col->addLayout(cards);
    col->addSpacing(34);

    // recent + shortcuts side by side
    QGridLayout* grid = new QGridLayout();
    grid->setHorizontalSpacing(56);
    col->addLayout(grid);

    QVBoxLayout* left = new QVBoxLayout();
    left->setSpacing(8);
    left->addWidget(heading("Recent", column));
    recentBox_ = new QVBoxLayout();
    recentBox_->setSpacing(7);
    left->addLayout(recentBox_);
    left->addStretch(1);

    QVBoxLayout* right = new QVBoxLayout();
    right->setSpacing(8);
    right->addWidget(heading("Handy shortcuts", column));
    const char* keys[][2] = {
        {"Ctrl+P", "go to a file by name"},
        {"Ctrl+Shift+P", "run any command by name"},
        {"Ctrl+B", "show or hide the Explorer"},
        {"Ctrl+F", "find (KMP search)"},
        {"Ctrl+Space", "autocomplete (Trie)"},
        {"Alt+Up/Down", "move lines"},
        {"Ctrl+/", "toggle comment"},
        {"( [ { \" '", "wrap the selection, again to unwrap"},
    };
    QGridLayout* kg = new QGridLayout();
    kg->setHorizontalSpacing(12);
    kg->setVerticalSpacing(8);
    int row = 0;
    for (const auto& k : keys) {
        QLabel* cap = new QLabel(QString::fromUtf8(k[0]), column);
        cap->setObjectName("welcomeKey");
        QLabel* what = new QLabel(QString::fromUtf8(k[1]), column);
        what->setObjectName("welcomeKeyText");
        kg->addWidget(cap, row, 0, Qt::AlignLeft);
        kg->addWidget(what, row, 1);
        ++row;
    }
    kg->setColumnStretch(1, 1);
    right->addLayout(kg);
    right->addSpacing(6);
    keysLink_ = link(QString(), column);
    settingsLink_ = link(QString(), column);
    terminalLink_ = link(QString(), column);
    right->addWidget(keysLink_);
    right->addWidget(settingsLink_);
    right->addWidget(terminalLink_);
    refreshLinks();
    right->addStretch(1);

    grid->addLayout(left, 0, 0, Qt::AlignTop);
    grid->addLayout(right, 0, 1, Qt::AlignTop);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    col->addSpacing(36);
    QLabel* tip = new QLabel("Tip: drop a file or a folder anywhere on this window to open it.", column);
    tip->setObjectName("welcomeTip");
    col->addWidget(tip);
    col->addStretch(1);

    setRecent({}, {});
}

void WelcomePage::setFolderOpen(bool open) { quick_->setVisible(open); }

void WelcomePage::refreshLinks() {
    auto text = [](const char* target, const char* label, const char* key) {
        return QString("<a href=\"%1\" style=\"%2\">%3</a>&nbsp;&nbsp;<span style=\"color:%5\">%4</span>").arg(target, linkStyle(), label, key, Theme::Dim.name());
    };
    keysLink_->setText(text("keys", "All keyboard shortcuts…", "F1"));
    settingsLink_->setText(text("settings", "Settings…", "Ctrl+,"));
    terminalLink_->setText(text("terminal", "Open terminal", "Ctrl+`"));
}

void WelcomePage::refreshAccent() {
    for (const auto& c : cardIcons_) c.first->setPixmap(Icons::get(Icons::Glyph(c.second), linkColor(), linkColor()).pixmap(QSize(24, 24)));
    refreshLinks();
    setRecent(recentFiles_, recentFolders_);
}

void WelcomePage::setRecent(const QStringList& files, const QStringList& folders) {
    while (QLayoutItem* it = recentBox_->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    shown_.clear();
    recentFiles_ = files;
    recentFolders_ = folders;
    auto add = [&](const QString& kind, const QString& path) {
        const QFileInfo fi(path);
        const QString name = fi.fileName().isEmpty() ? path : fi.fileName();
        const QString where = shortPath(kind == "folder" ? path : fi.absolutePath());
        QWidget* rowW = new QWidget(this);
        QHBoxLayout* row = new QHBoxLayout(rowW);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        QLabel* icon = new QLabel(rowW);
        icon->setPixmap((kind == "folder" ? Icons::folder(false) : Icons::forFile(name)).pixmap(QSize(16, 16)));
        QLabel* l = link(QString("<a href=\"%1:%2\" style=\"%3\">%4</a>&nbsp;&nbsp;<span style=\"color:%6\">%5</span>")
                             .arg(kind, esc(path), linkStyle(), esc(name), esc(where), Theme::Dim.name()),
                         rowW);
        l->setToolTip(QDir::toNativeSeparators(path));
        row->addWidget(icon);
        row->addWidget(l, 1);
        recentBox_->addWidget(rowW);
        shown_ << path;
    };
    int n = 0;
    for (const QString& f : folders)
        if (n < 5 && QFileInfo(f).isDir()) { add("folder", f); ++n; }
    n = 0;
    for (const QString& f : files)
        if (n < 7 && QFileInfo(f).isFile()) { add("file", f); ++n; }
    if (shown_.isEmpty()) {
        QLabel* none = new QLabel("Nothing yet: the files and folders you open will be listed here.", this);
        none->setObjectName("welcomeHint");
        none->setWordWrap(true);
        recentBox_->addWidget(none);
    }
}

void WelcomePage::activate(const QString& link) {
    if (link == "new") emit newFileRequested();
    else if (link == "open") emit openFileRequested();
    else if (link == "folder") emit openFolderRequested();
    else if (link == "quick") emit quickOpenRequested();
    else if (link == "keys") emit shortcutsRequested();
    else if (link == "settings") emit settingsRequested();
    else if (link == "terminal") emit terminalRequested();
    else if (link.startsWith("file:")) emit recentFileRequested(link.mid(5));
    else if (link.startsWith("folder:")) emit recentFolderRequested(link.mid(7));
}

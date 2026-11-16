#include "SettingsDialog.h"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include "Controls.h"
#include "Icons.h"
#include "Theme.h"

namespace {
bool isMonospace(const QString& family) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QFontDatabase::isFixedPitch(family);
#else
    return QFontDatabase().isFixedPitch(family);
#endif
}

QStringList monospaceFamilies() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QStringList all = QFontDatabase::families();
#else
    const QStringList all = QFontDatabase().families();
#endif
    QStringList out;
    for (const QString& f : all)
        if (isMonospace(f) && !f.startsWith('.') && !out.contains(f)) out << f;
    return out;
}
}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("settingsDialog");
    setWindowTitle("Settings");
    setWindowIcon(Icons::app());
    resize(900, 660);
    setMinimumSize(700, 460);
    buildUi();
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        syncing_ = true;
        for (auto& f : sync_) f(AppSettings::instance().values());
        syncing_ = false;
    });
    syncing_ = true;
    for (auto& f : sync_) f(AppSettings::instance().values());
    syncing_ = false;
    applyFilter();
}

void SettingsDialog::edit(const std::function<void(AppSettings::Values&)>& fn) {
    if (syncing_) return;
    AppSettings::Values v = AppSettings::instance().values();
    fn(v);
    AppSettings::instance().apply(v);
}

// ---------------------------------------------------------------- building the page

void SettingsDialog::buildUi() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // header: title, and the search box
    QWidget* head = new QWidget(this);
    QHBoxLayout* hh = new QHBoxLayout(head);
    hh->setContentsMargins(28, 22, 28, 14);
    QVBoxLayout* titles = new QVBoxLayout();
    titles->setSpacing(2);
    QLabel* title = new QLabel("Settings", head);
    title->setObjectName("settingsTitle");
    QLabel* sub = new QLabel("Everything here applies straight away and is remembered.", head);
    sub->setObjectName("settingsSubtitle");
    titles->addWidget(title);
    titles->addWidget(sub);
    hh->addLayout(titles, 1);
    search_ = new QLineEdit(head);
    search_->setPlaceholderText("Search settings");
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(280);
    search_->addAction(Icons::get(Icons::Search, QColor(0x80, 0x80, 0x80)), QLineEdit::LeadingPosition);
    hh->addWidget(search_, 0, Qt::AlignBottom);
    root->addWidget(head);

    // body: navigation | page
    QHBoxLayout* body = new QHBoxLayout();
    body->setContentsMargins(20, 0, 0, 0);
    body->setSpacing(0);
    nav_ = new QListWidget(this);
    nav_->setObjectName("settingsNav");
    nav_->setFixedWidth(176);
    nav_->setIconSize(QSize(18, 18));
    nav_->setFocusPolicy(Qt::NoFocus);
    body->addWidget(nav_);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName("settingsScroll");
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    QWidget* inner = new QWidget(scroll_);
    inner->setObjectName("settingsInner");
    page_ = new QVBoxLayout(inner);
    page_->setContentsMargins(20, 4, 28, 28);
    page_->setSpacing(10);
    scroll_->setWidget(inner);
    body->addWidget(scroll_, 1);
    root->addLayout(body, 1);

    // ---- Editor ----
    beginSection("Editor", "How text is shown and typed.");
    addStepper("Font size", "Size of the code text in points. Ctrl+Wheel zooms and changes this too.", &AppSettings::Values::fontSize, 7, 36, 1,
               " pt", "zoom text");
    {
        Combo* family = new Combo();
        family->addItem("Automatic (best installed)", QString());
        for (const QString& f : monospaceFamilies()) family->addItem(f, f);
        addRow("Font family", "Only fixed-width fonts are listed, so columns stay aligned.", family, "typeface monospace");
        connect(family, QOverload<int>::of(&QComboBox::activated), this, [this, family](int i) {
            const QString f = family->itemData(i).toString();
            edit([&](AppSettings::Values& v) { v.fontFamily = f; });
        });
        sync_.push_back([family](const AppSettings::Values& v) {
            int i = family->findData(v.fontFamily);
            if (i < 0 && !v.fontFamily.isEmpty()) {                      // saved font that is not installed here: show it anyway
                family->addItem(v.fontFamily + " (not installed)", v.fontFamily);
                i = family->count() - 1;
            }
            family->setCurrentIndex(std::max(0, i));
        });
    }
    addChoice("Tab width", "Columns per tab stop. Used by Tab, indent guides and Home / Backspace.", &AppSettings::Values::tabWidth,
              {"2", "4", "8"}, {2, 4, 8}, "indent spaces");
    addToggle("Close brackets and quotes", "Typing ( [ { \" ' ` also types the closing one. Typing the closer steps over it.",
              &AppSettings::Values::autoPairs, "auto pair auto-close");
    addToggle("Wrap selection", "With text selected, typing an opening bracket or quote surrounds it instead of replacing it.",
              &AppSettings::Values::wrapSelection, "surround");
    addToggle("Suggest while typing", "Show the autocomplete list as you type a word. Ctrl+Space still opens it on demand.",
              &AppSettings::Values::autoComplete, "autocomplete completion intellisense popup");
    addChoice("Cursor style", "Shape of the text cursor.", &AppSettings::Values::cursorStyle, {"Line", "Block", "Underline"}, {0, 1, 2}, "caret");
    addToggle("Blinking cursor", "Turn off for a steady cursor.", &AppSettings::Values::cursorBlink, "caret blink");

    // ---- Appearance ----
    beginSection("Appearance", "Colours and what is drawn around the text.");
    {
        int n = 0;
        const AppSettings::Accent* acc = AppSettings::accents(&n);
        QList<QColor> colors;
        QStringList keys, names;
        for (int i = 0; i < n; ++i) { colors << QColor(acc[i].rgb); keys << acc[i].key; names << acc[i].name; }
        Swatches* sw = new Swatches(colors, keys, names);
        addRow("Accent colour", "Used for the active tab, buttons, switches and focus rings.", sw, "color theme highlight");
        connect(sw, &Swatches::valueChanged, this, [this](const QString& k) { edit([&](AppSettings::Values& v) { v.accent = k; }); });
        sync_.push_back([sw](const AppSettings::Values& v) { sw->setValue(v.accent); });
    }
    addToggle("Line numbers", "Show the number gutter on the left of the editor.", &AppSettings::Values::lineNumbers, "gutter");
    addToggle("Indent guides", "Thin vertical lines at every tab stop inside the indentation.", &AppSettings::Values::indentGuides, "lines whitespace");
    addToggle("Highlight current line", "Lightly shade the line the cursor is on.", &AppSettings::Values::highlightLine);
    addToggle("Highlight matching bracket", "Mark the bracket that pairs with the one next to the cursor (uses the bracket checker).",
              &AppSettings::Values::matchBrackets, "parenthesis brace");
    addToggle("Status bar", "The bar at the bottom with cursor position, language and bracket status.", &AppSettings::Values::statusBar,
              "bottom bar footer");

    // ---- Terminal ----
    beginSection("Terminal", "The panel under the editor (Ctrl+`).");
    addStepper("Terminal font size", "Size of the text in the terminal, in points.", &AppSettings::Values::terminalFontSize, 7, 30, 1, " pt",
               "console");
    {
        QLineEdit* shell = new QLineEdit();
        shell->setPlaceholderText(
#ifdef Q_OS_WIN
            "cmd.exe"
#else
            "$SHELL"
#endif
        );
        shell->setFixedWidth(230);
        shell->setClearButtonEnabled(true);
        addRow("Shell", "Program that runs your commands (for example powershell, pwsh, bash, zsh). Empty uses the system default.", shell,
               "command line cmd powershell bash console");
        connect(shell, &QLineEdit::editingFinished, this, [this, shell]() {
            const QString s = shell->text();
            edit([&](AppSettings::Values& v) { v.terminalShell = s; });
        });
        sync_.push_back([shell](const AppSettings::Values& v) { if (!shell->hasFocus()) shell->setText(v.terminalShell); });
    }
    addStepper("Scrollback", "How many lines of output the terminal keeps.", &AppSettings::Values::terminalScrollback, 500, 50000, 500, " lines",
               "history buffer");

    // ---- General ----
    beginSection("General", "Starting up.");
    addToggle("Reopen last session", "Open the files and folder you had open when DevPad was closed.", &AppSettings::Values::restoreSession,
              "restore startup");

    page_->addStretch(1);
    empty_ = new QLabel("No settings match your search.", inner);
    empty_->setObjectName("settingsEmpty");
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setMinimumHeight(120);
    empty_->hide();
    page_->insertWidget(0, empty_);

    // navigation list
    const struct { const char* name; Icons::Glyph icon; } navItems[] = {
        {"Editor", Icons::Editor}, {"Appearance", Icons::Brush}, {"Terminal", Icons::Terminal}, {"General", Icons::Gear}};
    for (const auto& n : navItems) {
        QListWidgetItem* it = new QListWidgetItem(Icons::get(n.icon, QColor(0x9d, 0x9d, 0x9d), QColor(0xff, 0xff, 0xff)), n.name, nav_);
        it->setData(Qt::UserRole, QString(n.name));
    }
    nav_->setCurrentRow(0);
    currentSection_ = "Editor";
    connect(nav_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it, QListWidgetItem*) {
        if (it && !navFromScroll_) showSection(it->data(Qt::UserRole).toString());
    });
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { updateNavFromScroll(); });
    connect(search_, &QLineEdit::textChanged, this, [this]() { applyFilter(); });

    // footer
    QWidget* foot = new QWidget(this);
    foot->setObjectName("settingsFooter");
    QHBoxLayout* fh = new QHBoxLayout(foot);
    fh->setContentsMargins(28, 12, 28, 12);
    QLabel* note = new QLabel("Changes are saved automatically.", foot);
    note->setObjectName("settingsFooterText");
    QPushButton* reset = new QPushButton("Reset to Defaults", foot);
    reset->setAutoDefault(false);
    QPushButton* done = new QPushButton("Done", foot);
    done->setObjectName("primary");
    done->setDefault(true);
    done->setMinimumWidth(88);
    fh->addWidget(note, 1);
    fh->addWidget(reset);
    fh->addWidget(done);
    root->addWidget(foot);
    connect(reset, &QPushButton::clicked, this, []() { AppSettings::instance().resetToDefaults(); });
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
}

void SettingsDialog::beginSection(const QString& name, const QString& blurb) {
    QWidget* block = new QWidget();
    QVBoxLayout* bl = new QVBoxLayout(block);
    bl->setContentsMargins(0, sections_.empty() ? 0 : 14, 0, 0);
    bl->setSpacing(6);
    QLabel* h = new QLabel(name, block);
    h->setObjectName("settingsSection");
    QLabel* d = new QLabel(blurb, block);
    d->setObjectName("settingsSectionDesc");
    QFrame* card = new QFrame(block);
    card->setObjectName("settingsCard");
    cardLayout_ = new QVBoxLayout(card);
    cardLayout_->setContentsMargins(0, 4, 0, 4);
    cardLayout_->setSpacing(0);
    bl->addWidget(h);
    bl->addWidget(d);
    bl->addSpacing(2);
    bl->addWidget(card);
    page_->addWidget(block);
    sections_.push_back({name, block});
    currentSection_ = name;
}

QWidget* SettingsDialog::addRow(const QString& title, const QString& description, QWidget* control, const QString& extraWords) {
    QFrame* sep = new QFrame();
    sep->setObjectName("settingsSep");
    sep->setFixedHeight(1);
    QFrame* row = new QFrame();
    row->setObjectName("settingsRow");
    QHBoxLayout* h = new QHBoxLayout(row);
    h->setContentsMargins(18, 12, 18, 12);
    h->setSpacing(20);
    QVBoxLayout* text = new QVBoxLayout();
    text->setSpacing(3);
    QLabel* t = new QLabel(title, row);
    t->setObjectName("settingsRowTitle");
    QLabel* d = new QLabel(description, row);
    d->setObjectName("settingsRowDesc");
    d->setWordWrap(true);
    text->addWidget(t);
    text->addWidget(d);
    h->addLayout(text, 1);
    h->addWidget(control, 0, Qt::AlignVCenter);
    cardLayout_->addWidget(sep);
    cardLayout_->addWidget(row);
    control->setProperty("settingTitle", title);
    rows_.push_back({row, sep, currentSection_, (title + " " + description + " " + extraWords + " " + currentSection_).toLower(), title, control});
    return control;
}

void SettingsDialog::addToggle(const QString& title, const QString& description, bool AppSettings::Values::*field, const QString& words) {
    Switch* sw = new Switch();
    addRow(title, description, sw, words);
    connect(sw, &QAbstractButton::toggled, this, [this, field](bool on) { edit([&](AppSettings::Values& v) { v.*field = on; }); });
    sync_.push_back([sw, field](const AppSettings::Values& v) {
        QSignalBlocker b(sw);
        sw->setChecked(v.*field);
    });
}

void SettingsDialog::addStepper(const QString& title, const QString& description, int AppSettings::Values::*field, int lo, int hi, int step,
                                const QString& suffix, const QString& words) {
    Stepper* st = new Stepper(lo, hi, step, suffix);
    addRow(title, description, st, words);
    connect(st, &Stepper::valueChanged, this, [this, field](int n) { edit([&](AppSettings::Values& v) { v.*field = n; }); });
    sync_.push_back([st, field](const AppSettings::Values& v) { st->setValue(v.*field); });
}

void SettingsDialog::addChoice(const QString& title, const QString& description, int AppSettings::Values::*field, const QStringList& labels,
                               const QList<int>& values, const QString& words) {
    Segmented* sg = new Segmented(labels, values);
    addRow(title, description, sg, words);
    connect(sg, &Segmented::valueChanged, this, [this, field](int n) { edit([&](AppSettings::Values& v) { v.*field = n; }); });
    sync_.push_back([sg, field](const AppSettings::Values& v) { sg->setValue(v.*field); });
}

// ---------------------------------------------------------------- navigation and search

QStringList SettingsDialog::sectionNames() const {
    QStringList out;
    for (const Section& s : sections_) out << s.name;
    return out;
}

QWidget* SettingsDialog::control(const QString& title) const {
    for (const Row& r : rows_)
        if (r.title == title) return r.control;
    return nullptr;
}

void SettingsDialog::showSection(const QString& name) {
    for (const Section& s : sections_) {
        if (s.name != name) continue;
        if (!search_->text().isEmpty()) search_->clear();           // a search hides sections; jumping to one ends it
        // widgets are laid out lazily: make sure positions are current
        scroll_->widget()->layout()->activate();
        scroll_->verticalScrollBar()->setValue(s.block->y());
        for (int i = 0; i < nav_->count(); ++i)
            if (nav_->item(i)->data(Qt::UserRole).toString() == name) {
                navFromScroll_ = true;
                nav_->setCurrentRow(i);
                navFromScroll_ = false;
            }
        return;
    }
}

void SettingsDialog::updateNavFromScroll() {
    if (!search_->text().isEmpty()) return;
    const int y = scroll_->verticalScrollBar()->value();
    const bool atEnd = y >= scroll_->verticalScrollBar()->maximum() - 2 && y > 0;
    QString current = sections_.front().name;
    for (const Section& s : sections_)
        if (s.block->isVisible() && s.block->y() <= y + 40) current = s.name;
    if (atEnd) current = sections_.back().name;
    for (int i = 0; i < nav_->count(); ++i)
        if (nav_->item(i)->data(Qt::UserRole).toString() == current && nav_->currentRow() != i) {
            navFromScroll_ = true;
            nav_->setCurrentRow(i);
            navFromScroll_ = false;
        }
}

void SettingsDialog::setSearchText(const QString& text) { search_->setText(text); }

void SettingsDialog::applyFilter() {
    const QStringList words = search_->text().toLower().split(' ', Qt::SkipEmptyParts);
    int shown = 0;
    for (const Section& s : sections_) {
        bool firstInSection = true;
        int inSection = 0;
        for (Row& r : rows_) {
            if (r.section != s.name) continue;
            bool match = true;
            for (const QString& w : words) match = match && r.keywords.contains(w);
            r.frame->setVisible(match);
            r.separator->setVisible(match && !firstInSection);
            if (match) { firstInSection = false; ++inSection; ++shown; }
        }
        s.block->setVisible(inSection > 0);
    }
    empty_->setVisible(shown == 0);
}

int SettingsDialog::visibleRowCount() const {
    int n = 0;
    for (const Row& r : rows_) if (!r.frame->isHidden()) ++n;
    return n;
}

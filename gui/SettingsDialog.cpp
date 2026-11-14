#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include "CppHighlighter.h"
#include "TextUtil.h"

SettingsDialog::SettingsDialog(const EditorSettings& current,
                               SnippetTable& snippets,
                               const QString& snippetFile,
                               Autocomplete& autocomplete,
                               QWidget* parent)
    : QDialog(parent),
      current_(current),
      snippets_(snippets),
      snippetFile_(snippetFile),
      autocomplete_(autocomplete) {
    setWindowTitle(tr("Settings & Preferences"));
    resize(760, 540);
    setMinimumSize(640, 440);

    tabs_ = new QTabWidget(this);
    setupGeneralTab();
    setupFontTab();
    setupShortcutsTab();
    setupSnippetsTab();
    setupDictionaryTab();

    auto* btnBox = new QHBoxLayout;
    auto* okBtn = new QPushButton(tr("OK"), this);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    auto* applyBtn = new QPushButton(tr("Apply"), this);
    btnBox->addStretch(1);
    btnBox->addWidget(okBtn);
    btnBox->addWidget(cancelBtn);
    btnBox->addWidget(applyBtn);

    connect(okBtn, &QPushButton::clicked, this, [this] {
        applyChanges();
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsDialog::applyChanges);

    auto* mainLay = new QVBoxLayout(this);
    mainLay->addWidget(tabs_, 1);
    mainLay->addLayout(btnBox);
}

EditorSettings SettingsDialog::settings() const {
    return current_;
}

// ====================================================================== Tab 1: General & Auto-Save

void SettingsDialog::setupGeneralTab() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setSpacing(16);

    // Auto-save group
    auto* autoSaveGroup = new QGroupBox(tr("Auto-Save Options"), page);
    auto* asLay = new QFormLayout(autoSaveGroup);
    autoSaveCheck_ = new QCheckBox(tr("Enable automatic file saving (saves silently while editing)"), autoSaveGroup);
    autoSaveCheck_->setChecked(current_.autoSave);

    autoSaveIntervalSpin_ = new QSpinBox(autoSaveGroup);
    autoSaveIntervalSpin_->setRange(1, 120);
    autoSaveIntervalSpin_->setSuffix(tr(" seconds"));
    autoSaveIntervalSpin_->setValue(current_.autoSaveIntervalSec);
    autoSaveIntervalSpin_->setEnabled(current_.autoSave);

    connect(autoSaveCheck_, &QCheckBox::toggled, autoSaveIntervalSpin_, &QSpinBox::setEnabled);
    asLay->addRow(autoSaveCheck_);
    asLay->addRow(tr("Auto-save delay:"), autoSaveIntervalSpin_);
    lay->addWidget(autoSaveGroup);

    // Visual indicators group
    auto* visualGroup = new QGroupBox(tr("Status & Visual Indicators"), page);
    auto* visLay = new QVBoxLayout(visualGroup);

    showUnsavedDotCheck_ = new QCheckBox(tr("Show unsaved indicator dot ('●') in window title when modified"), visualGroup);
    showUnsavedDotCheck_->setChecked(current_.showUnsavedDot);
    visLay->addWidget(showUnsavedDotCheck_);
    lay->addWidget(visualGroup);

    // Editing behavior group
    auto* editGroup = new QGroupBox(tr("Editor Behavior"), page);
    auto* edLay = new QFormLayout(editGroup);

    autoCloseCheck_ = new QCheckBox(tr("Auto-close brackets (), [], {} and quotes \"\", ''"), editGroup);
    autoCloseCheck_->setChecked(current_.autoClose);
    edLay->addRow(autoCloseCheck_);

    wordWrapCheck_ = new QCheckBox(tr("Soft word wrap long lines"), editGroup);
    wordWrapCheck_->setChecked(current_.wordWrap);
    edLay->addRow(wordWrapCheck_);

    tabSizeSpin_ = new QSpinBox(editGroup);
    tabSizeSpin_->setRange(2, 8);
    tabSizeSpin_->setValue(current_.tabSize);
    tabSizeSpin_->setSuffix(tr(" spaces"));
    edLay->addRow(tr("Tab width:"), tabSizeSpin_);

    lay->addWidget(editGroup);
    lay->addStretch(1);

    tabs_->addTab(page, tr("General"));
}

// ====================================================================== Tab 2: Font & Appearance

void SettingsDialog::setupFontTab() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);

    auto* formLay = new QFormLayout;

    fontCombo_ = new QFontComboBox(page);
    fontCombo_->setFontFilters(QFontComboBox::MonospacedFonts);
    if (!current_.fontFamily.isEmpty()) {
        fontCombo_->setCurrentFont(QFont(current_.fontFamily));
    }

    fontSizeSpin_ = new QSpinBox(page);
    fontSizeSpin_->setRange(6, 48);
    fontSizeSpin_->setValue(current_.fontSize);
    fontSizeSpin_->setSuffix(tr(" pt"));

    themeCombo_ = new QComboBox(page);
    themeCombo_->addItems({"Midnight", "Daylight", "Nord", "Solarized Light"});
    themeCombo_->setCurrentText(current_.theme);

    formLay->addRow(tr("Font Family (Monospaced):"), fontCombo_);
    formLay->addRow(tr("Font Size:"), fontSizeSpin_);
    formLay->addRow(tr("Color Theme:"), themeCombo_);

    lay->addLayout(formLay);

    // Preview
    auto* prevLabel = new QLabel(tr("Live Preview:"), page);
    previewEdit_ = new QPlainTextEdit(page);
    previewEdit_->setReadOnly(true);
    previewEdit_->setMaximumHeight(150);
    previewEdit_->setPlainText(
        "// DevPad C++ Code Editor\n"
        "#include <iostream>\n\n"
        "int main() {\n"
        "    std::cout << \"Hello from DevPad!\" << std::endl;\n"
        "    return 0;\n"
        "}\n");
    new CppHighlighter(previewEdit_->document());

    lay->addWidget(prevLabel);
    lay->addWidget(previewEdit_, 1);

    connect(fontCombo_, &QFontComboBox::currentFontChanged, this, &SettingsDialog::updateFontPreview);
    connect(fontSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::updateFontPreview);
    connect(themeCombo_, &QComboBox::currentTextChanged, this, &SettingsDialog::updateFontPreview);

    updateFontPreview();
    tabs_->addTab(page, tr("Appearance & Font"));
}

void SettingsDialog::updateFontPreview() {
    QFont f = fontCombo_->currentFont();
    f.setPointSize(fontSizeSpin_->value());
    previewEdit_->setFont(f);

    const Theme t = Theme::byName(themeCombo_->currentText());
    previewEdit_->setStyleSheet(
        QStringLiteral("background: %1; color: %2; border: 1px solid %3; border-radius: 4px;")
            .arg(t.bg.name(), t.fg.name(), t.accent.name()));
}

// ====================================================================== Tab 3: Shortcuts

void SettingsDialog::setupShortcutsTab() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);

    shortcutSearch_ = new QLineEdit(page);
    shortcutSearch_->setPlaceholderText(tr("Search keyboard shortcuts…"));
    shortcutSearch_->setClearButtonEnabled(true);
    lay->addWidget(shortcutSearch_);

    shortcutTree_ = new QTreeWidget(page);
    shortcutTree_->setHeaderLabels({tr("Command"), tr("Shortcut"), tr("Category"), tr("Description")});
    shortcutTree_->setRootIsDecorated(false);
    shortcutTree_->setSortingEnabled(true);
    shortcutTree_->header()->setStretchLastSection(true);
    shortcutTree_->setColumnWidth(0, 190);
    shortcutTree_->setColumnWidth(1, 140);
    shortcutTree_->setColumnWidth(2, 110);

    auto addSc = [this](const QString& cmd, const QString& sc, const QString& cat, const QString& desc) {
        auto* it = new QTreeWidgetItem(shortcutTree_);
        it->setText(0, cmd);
        it->setText(1, sc);
        it->setText(2, cat);
        it->setText(3, desc);
    };

    // File
    addSc(tr("New Document"), QStringLiteral("Ctrl+N"), tr("File"), tr("Create an empty document"));
    addSc(tr("Open File"), QStringLiteral("Ctrl+O"), tr("File"), tr("Open a C++ or text file"));
    addSc(tr("Open Folder"), QStringLiteral("Ctrl+Shift+O"), tr("File"), tr("Open directory in sidebar explorer"));
    addSc(tr("Save File"), QStringLiteral("Ctrl+S"), tr("File"), tr("Save changes to disk"));
    addSc(tr("Save As"), QStringLiteral("Ctrl+Shift+S"), tr("File"), tr("Save under a new filename"));
    addSc(tr("Quit"), QStringLiteral("Ctrl+Q"), tr("File"), tr("Exit DevPad"));

    // Edit
    addSc(tr("Undo"), QStringLiteral("Ctrl+Z"), tr("Edit"), tr("Revert last action via engine stack"));
    addSc(tr("Redo"), QStringLiteral("Ctrl+Y"), tr("Edit"), tr("Reapply undone edit via engine stack"));
    addSc(tr("Cut"), QStringLiteral("Ctrl+X"), tr("Edit"), tr("Cut selection to clipboard"));
    addSc(tr("Copy"), QStringLiteral("Ctrl+C"), tr("Edit"), tr("Copy selection to clipboard"));
    addSc(tr("Paste"), QStringLiteral("Ctrl+V"), tr("Edit"), tr("Paste from clipboard"));
    addSc(tr("Select All"), QStringLiteral("Ctrl+A"), tr("Edit"), tr("Select entire buffer"));
    addSc(tr("Find"), QStringLiteral("Ctrl+F"), tr("Edit"), tr("Open KMP text search"));
    addSc(tr("Replace"), QStringLiteral("Ctrl+H"), tr("Edit"), tr("Open KMP search and replace"));
    addSc(tr("Find Next"), QStringLiteral("F3 / Enter"), tr("Edit"), tr("Jump to next match"));
    addSc(tr("Find Previous"), QStringLiteral("Shift+F3"), tr("Edit"), tr("Jump to previous match"));
    addSc(tr("Go to Line"), QStringLiteral("Ctrl+G"), tr("Edit"), tr("Jump directly to 1-based line number"));
    addSc(tr("Toggle Comment"), QStringLiteral("Ctrl+/"), tr("Edit"), tr("Comment or uncomment selected lines"));
    addSc(tr("Duplicate Line"), QStringLiteral("Ctrl+Shift+D"), tr("Edit"), tr("Duplicate current line or selection"));
    addSc(tr("Delete Line"), QStringLiteral("Ctrl+Shift+K"), tr("Edit"), tr("Delete current line completely"));
    addSc(tr("Indent Selection"), QStringLiteral("Tab"), tr("Edit"), tr("Indent selected block by 4 spaces"));
    addSc(tr("Unindent Selection"), QStringLiteral("Shift+Tab"), tr("Edit"), tr("Unindent selected block"));

    // DSA Engine
    addSc(tr("Match Bracket"), QStringLiteral("Ctrl+M"), tr("DSA Engine"), tr("Jump cursor to matching partner bracket"));
    addSc(tr("Autocomplete Suggest"), QStringLiteral("Ctrl+Space"), tr("DSA Engine"), tr("Trigger Trie keyword/doc suggestions"));
    addSc(tr("Expand Snippet"), QStringLiteral("Ctrl+J / Tab"), tr("DSA Engine"), tr("Expand hash table snippet trigger"));
    addSc(tr("Next Problem"), QStringLiteral("F8"), tr("DSA Engine"), tr("Jump to next bracket/compiler issue"));

    // Build & Run
    addSc(tr("Build & Run"), QStringLiteral("F5"), tr("Build & Run"), tr("Compile buffer and run executable"));
    addSc(tr("Stop Program"), QStringLiteral("Shift+F5"), tr("Build & Run"), tr("Terminate running background process"));
    addSc(tr("Build Only"), QStringLiteral("F7"), tr("Build & Run"), tr("Compile without executing"));

    // View
    addSc(tr("Toggle Explorer"), QStringLiteral("Ctrl+B"), tr("View"), tr("Show or hide folder tree sidebar"));
    addSc(tr("Zoom In"), QStringLiteral("Ctrl++ / Ctrl+WheelUp"), tr("View"), tr("Increase editor font scale"));
    addSc(tr("Zoom Out"), QStringLiteral("Ctrl+- / Ctrl+WheelDown"), tr("View"), tr("Decrease editor font scale"));
    addSc(tr("Preferences / Settings"), QStringLiteral("Ctrl+,"), tr("View"), tr("Open this settings panel"));

    lay->addWidget(shortcutTree_, 1);
    connect(shortcutSearch_, &QLineEdit::textChanged, this, &SettingsDialog::filterShortcuts);

    tabs_->addTab(page, tr("Keyboard Shortcuts"));
}

void SettingsDialog::filterShortcuts(const QString& query) {
    const QString q = query.trimmed().toLower();
    for (int i = 0; i < shortcutTree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = shortcutTree_->topLevelItem(i);
        bool match = q.isEmpty() ||
                     it->text(0).toLower().contains(q) ||
                     it->text(1).toLower().contains(q) ||
                     it->text(2).toLower().contains(q) ||
                     it->text(3).toLower().contains(q);
        it->setHidden(!match);
    }
}

// ====================================================================== Tab 4: Snippets (Hash Table)

void SettingsDialog::setupSnippetsTab() {
    auto* page = new QWidget(this);
    auto* hlay = new QHBoxLayout(page);

    auto* leftBox = new QVBoxLayout;
    leftBox->addWidget(new QLabel(tr("Snippet Triggers:"), page));
    snipList_ = new QListWidget(page);
    snipList_->setMaximumWidth(160);
    leftBox->addWidget(snipList_, 1);

    auto* rightBox = new QVBoxLayout;
    rightBox->addWidget(new QLabel(tr("Trigger:"), page));
    snipTrigger_ = new QLineEdit(page);
    snipTrigger_->setPlaceholderText(tr("e.g. for, if, class, main"));
    rightBox->addWidget(snipTrigger_);

    rightBox->addWidget(new QLabel(tr("Body ($0 for cursor position, $$ for literal $):"), page));
    snipBody_ = new QPlainTextEdit(page);
    snipBody_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    rightBox->addWidget(snipBody_, 1);

    snipError_ = new QLabel(page);
    snipError_->setStyleSheet(QStringLiteral("color: #e06c75;"));
    rightBox->addWidget(snipError_);

    auto* btnRow = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add / Update"), page);
    auto* remBtn = new QPushButton(tr("Remove"), page);
    auto* impBtn = new QPushButton(tr("Import…"), page);
    auto* expBtn = new QPushButton(tr("Export…"), page);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    btnRow->addWidget(impBtn);
    btnRow->addWidget(expBtn);
    btnRow->addStretch(1);
    rightBox->addLayout(btnRow);

    snipStats_ = new QLabel(page);
    snipStats_->setStyleSheet(QStringLiteral("font-size: 11px; color: #8a919d;"));
    rightBox->addWidget(snipStats_);

    hlay->addLayout(leftBox);
    hlay->addLayout(rightBox, 1);

    connect(snipList_, &QListWidget::currentItemChanged, this, &SettingsDialog::snippetPick);
    connect(addBtn, &QPushButton::clicked, this, &SettingsDialog::snippetAddOrUpdate);
    connect(remBtn, &QPushButton::clicked, this, &SettingsDialog::snippetRemove);
    connect(impBtn, &QPushButton::clicked, this, &SettingsDialog::snippetImport);
    connect(expBtn, &QPushButton::clicked, this, &SettingsDialog::snippetExport);

    reloadSnippets();
    tabs_->addTab(page, tr("Snippets (Hash Table)"));
}

void SettingsDialog::reloadSnippets() {
    snipList_->clear();
    const DynArray<std::string> keys = snippets_.triggers();
    for (size_t i = 0; i < keys.size(); ++i) snipList_->addItem(TextUtil::fromStd(keys[i]));
    snipStats_->setText(tr("DSA Hash Table: %1 snippet(s), %2 buckets, longest chain: %3")
                            .arg(snippets_.size()).arg(snippets_.bucketCount()).arg(snippets_.longestChain()));
}

void SettingsDialog::snippetPick() {
    QListWidgetItem* it = snipList_->currentItem();
    if (!it) return;
    const Snippet* s = snippets_.find(TextUtil::toStd(it->text()));
    if (!s) return;
    snipTrigger_->setText(TextUtil::fromStd(s->trigger));
    snipBody_->setPlainText(TextUtil::fromStd(s->body));
    snipError_->clear();
}

void SettingsDialog::snippetAddOrUpdate() {
    try {
        snippets_.add(TextUtil::toStd(snipTrigger_->text().trimmed()),
                      TextUtil::toStd(TextUtil::sanitize(snipBody_->toPlainText())));
        snipError_->clear();
        reloadSnippets();
        persistSnippets();
        emit snippetsModified();
    } catch (const std::exception& ex) {
        snipError_->setText(QString::fromUtf8(ex.what()));
    }
}

void SettingsDialog::snippetRemove() {
    QListWidgetItem* it = snipList_->currentItem();
    if (!it) return;
    snippets_.remove(TextUtil::toStd(it->text()));
    reloadSnippets();
    persistSnippets();
    snipTrigger_->clear();
    snipBody_->clear();
    emit snippetsModified();
}

void SettingsDialog::snippetImport() {
    const QString p = QFileDialog::getOpenFileName(this, tr("Import snippets"), QString(), tr("Text files (*.txt);;All files (*)"));
    if (p.isEmpty()) return;
    const IOResult r = snippets_.loadFromFile(TextUtil::toStd(p));
    if (!r.ok) snipError_->setText(TextUtil::fromStd(r.message));
    else { snipError_->clear(); reloadSnippets(); persistSnippets(); emit snippetsModified(); }
}

void SettingsDialog::snippetExport() {
    const QString p = QFileDialog::getSaveFileName(this, tr("Export snippets"), QStringLiteral("snippets.txt"), tr("Text files (*.txt);;All files (*)"));
    if (p.isEmpty()) return;
    const IOResult r = snippets_.saveToFile(TextUtil::toStd(p));
    if (!r.ok) snipError_->setText(TextUtil::fromStd(r.message));
    else snipError_->setText(tr("Saved to '%1'.").arg(p));
}

void SettingsDialog::persistSnippets() {
    if (snippetFile_.isEmpty()) return;
    QDir().mkpath(QFileInfo(snippetFile_).absolutePath());
    snippets_.saveToFile(TextUtil::toStd(snippetFile_));
}

// ====================================================================== Tab 5: Dictionary (Trie Autocomplete)

void SettingsDialog::setupDictionaryTab() {
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setSpacing(14);

    // Stats
    dictStatsLabel_ = new QLabel(page);
    dictStatsLabel_->setStyleSheet(QStringLiteral("font-weight: 600; padding: 6px; background: palette(midlight); border-radius: 4px;"));
    lay->addWidget(dictStatsLabel_);

    // Add / remove word group
    auto* wordGroup = new QGroupBox(tr("Manage Autocomplete Trie Dictionary"), page);
    auto* wgLay = new QVBoxLayout(wordGroup);

    auto* row1 = new QHBoxLayout;
    dictWordInput_ = new QLineEdit(wordGroup);
    dictWordInput_->setPlaceholderText(tr("Type a word to add or remove (e.g. std::vector, myVariable)"));
    auto* addWordBtn = new QPushButton(tr("Add Word (dictadd)"), wordGroup);
    auto* remWordBtn = new QPushButton(tr("Remove Word (dictdel)"), wordGroup);
    row1->addWidget(dictWordInput_, 1);
    row1->addWidget(addWordBtn);
    row1->addWidget(remWordBtn);
    wgLay->addLayout(row1);

    dictMsgLabel_ = new QLabel(wordGroup);
    wgLay->addWidget(dictMsgLabel_);
    lay->addWidget(wordGroup);

    // Test Prefix Suggestion group
    auto* testGroup = new QGroupBox(tr("Interactive Trie Prefix Search (complete <prefix>)"), page);
    auto* tgLay = new QVBoxLayout(testGroup);

    dictTestInput_ = new QLineEdit(testGroup);
    dictTestInput_->setPlaceholderText(tr("Type a prefix to test Trie suggestions live…"));
    dictTestResultLabel_ = new QLabel(tr("Suggestions: (type above)"), testGroup);
    dictTestResultLabel_->setStyleSheet(QStringLiteral("padding: 8px; font-family: monospace;"));
    dictTestResultLabel_->setWordWrap(true);

    tgLay->addWidget(dictTestInput_);
    tgLay->addWidget(dictTestResultLabel_);
    lay->addWidget(testGroup);

    lay->addStretch(1);

    connect(addWordBtn, &QPushButton::clicked, this, &SettingsDialog::dictAddWord);
    connect(remWordBtn, &QPushButton::clicked, this, &SettingsDialog::dictRemoveWord);
    connect(dictTestInput_, &QLineEdit::textChanged, this, &SettingsDialog::dictTestPrefix);

    refreshDictStats();
    tabs_->addTab(page, tr("Dictionary (Trie)"));
}

void SettingsDialog::refreshDictStats() {
    dictStatsLabel_->setText(tr("Autocomplete Trie Stats: %1 built-in/user dictionary words  ·  %2 open-document words")
                                 .arg(autocomplete_.dictionarySize())
                                 .arg(autocomplete_.documentWordCount()));
}

void SettingsDialog::dictAddWord() {
    const QString w = dictWordInput_->text().trimmed();
    if (w.isEmpty()) return;
    if (autocomplete_.addWord(TextUtil::toStd(w))) {
        dictMsgLabel_->setText(tr("Added '%1' to autocomplete dictionary Trie.").arg(w));
        dictMsgLabel_->setStyleSheet(QStringLiteral("color: #98c379;"));
        dictWordInput_->clear();
        refreshDictStats();
        if (!dictTestInput_->text().isEmpty()) dictTestPrefix(dictTestInput_->text());
    } else {
        dictMsgLabel_->setText(tr("Cannot add '%1' (too short, too long or invalid characters).").arg(w));
        dictMsgLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
    }
}

void SettingsDialog::dictRemoveWord() {
    const QString w = dictWordInput_->text().trimmed();
    if (w.isEmpty()) return;
    if (autocomplete_.removeWord(TextUtil::toStd(w))) {
        dictMsgLabel_->setText(tr("Removed '%1' from dictionary Trie.").arg(w));
        dictMsgLabel_->setStyleSheet(QStringLiteral("color: #98c379;"));
        dictWordInput_->clear();
        refreshDictStats();
        if (!dictTestInput_->text().isEmpty()) dictTestPrefix(dictTestInput_->text());
    } else {
        dictMsgLabel_->setText(tr("Word '%1' not found in dictionary Trie.").arg(w));
        dictMsgLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
    }
}

void SettingsDialog::dictTestPrefix(const QString& prefix) {
    const std::string p = TextUtil::toStd(prefix.trimmed());
    if (p.empty()) {
        dictTestResultLabel_->setText(tr("Suggestions: (type above)"));
        return;
    }
    const DynArray<std::string> sug = autocomplete_.suggest(p, 10);
    if (sug.empty()) {
        dictTestResultLabel_->setText(tr("No matching words starting with '%1'").arg(prefix));
    } else {
        QStringList items;
        for (size_t i = 0; i < sug.size(); ++i) items << TextUtil::fromStd(sug[i]);
        dictTestResultLabel_->setText(tr("Suggestions for '%1':  %2").arg(prefix, items.join(QStringLiteral("   •   "))));
    }
}

// ====================================================================== Apply

void SettingsDialog::applyChanges() {
    current_.autoSave = autoSaveCheck_->isChecked();
    current_.autoSaveIntervalSec = autoSaveIntervalSpin_->value();
    current_.showUnsavedDot = showUnsavedDotCheck_->isChecked();
    current_.autoClose = autoCloseCheck_->isChecked();
    current_.wordWrap = wordWrapCheck_->isChecked();
    current_.tabSize = tabSizeSpin_->value();
    current_.fontFamily = fontCombo_->currentFont().family();
    current_.fontSize = fontSizeSpin_->value();
    current_.theme = themeCombo_->currentText();

    emit settingsApplied(current_);
}

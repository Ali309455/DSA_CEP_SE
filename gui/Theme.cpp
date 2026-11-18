#include "Theme.h"

#include <QApplication>
#include <algorithm>
#include "Settings.h"
#include <QFontDatabase>
#include <QPalette>
#include <QStringList>
#include <QStyleFactory>

namespace Theme {

static QString pick(const QStringList& wanted) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QStringList have = QFontDatabase::families();
#else
    const QStringList have = QFontDatabase().families();
#endif
    for (const QString& w : wanted)
        for (const QString& h : have)
            if (h.compare(w, Qt::CaseInsensitive) == 0) return h;
    return QString();
}

QFont uiFont() {
    QFont f = QApplication::font();
    const QString fam = pick({"Segoe UI Variable Text", "Segoe UI", "Inter", "SF Pro Text", "Noto Sans", "Ubuntu", "Cantarell"});
    if (!fam.isEmpty()) f.setFamily(fam);
    f.setPointSizeF(9.75);
    return f;
}

QFont codeFont(int pointSize, const QString& family) {
    static const QString fam = pick({"Cascadia Code", "Cascadia Mono", "JetBrains Mono", "Fira Code", "Source Code Pro",
                                     "Consolas", "SF Mono", "Menlo", "DejaVu Sans Mono", "Liberation Mono"});
    const QString chosen = family.isEmpty() ? QString() : pick({family});     // a font that is no longer installed falls back
    QFont f = !chosen.isEmpty() ? QFont(chosen) : fam.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(fam);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPointSize(pointSize);
    return f;
}

// ---------------------------------------------------------------- the themes

namespace {

struct Scheme {
    const char* id;
    const char* name;
    bool dark;
    // surfaces and text
    unsigned chrome, editor, surface, border, text, muted, dim, heading;
    // controls and cards
    unsigned raised, cardBorder, inputBorder, popup, popupBorder, rowHover, pressed, button, buttonHover, keycap, keycapBorder, treeHover;
    // editor
    unsigned currentLine, selection, findMatch, gutterNumber, gutterCurrent, indentGuide;
    // text, keyword, type, string, number, comment, preprocessor
    unsigned syntax[7];
    // status colours and line icons
    unsigned error, warning, success, iconNormal, iconActive;
};

const Scheme kSchemes[] = {
    // Midnight: the original DevPad dark look
    {"midnight", "Midnight", true,
     0x181818, 0x1f1f1f, 0x2b2b2b, 0x2b2b2b, 0xcccccc, 0x9d9d9d, 0x6e6e6e, 0xf0f0f0,
     0x252526, 0x303030, 0x3a3a3a, 0x1f1f1f, 0x383838, 0x2e2e2e, 0x353535, 0x2d2d2d, 0x363636, 0x333333, 0x454545, 0x2a2d2e,
     0x262626, 0x264f78, 0x624a1c, 0x6e7681, 0xcccccc, 0x333333,
     {0xd4d4d4, 0xc678dd, 0xe5c07b, 0x98c379, 0xd19a66, 0x7f848e, 0x56b6c2},
     0xf14c4c, 0xcca700, 0x73c991, 0x9d9d9d, 0xe8e8e8},

    // Daylight: light, GitHub-like
    {"daylight", "Daylight", false,
     0xf3f4f6, 0xffffff, 0xffffff, 0xdfe3e8, 0x24292f, 0x57606a, 0x8c959f, 0x1f2328,
     0xf6f8fa, 0xd8dee4, 0xcdd3da, 0xffffff, 0xd0d7de, 0xe6eaef, 0xdae0e7, 0xf0f2f5, 0xe4e8ed, 0xeef1f4, 0xc3cad2, 0xe9edf2,
     0xf0f4ff, 0xb6d4fb, 0xffe58f, 0x8c959f, 0x1f2328, 0xe3e7ec,
     {0x24292f, 0xcf222e, 0x953800, 0x0a3069, 0x0550ae, 0x6e7781, 0x0b7285},
     0xd1242f, 0x9a6700, 0x1a7f37, 0x6e7781, 0x1f2328},

    // Nord: dark blue-grey
    {"nord", "Nord", true,
     0x242933, 0x2e3440, 0x3b4252, 0x2b303b, 0xd8dee9, 0xa0aabf, 0x616e88, 0xeceff4,
     0x2b303b, 0x3b4252, 0x4c566a, 0x2b303b, 0x434c5e, 0x3b4252, 0x434c5e, 0x3b4252, 0x434c5e, 0x3b4252, 0x4c566a, 0x343b49,
     0x3b4252, 0x434c5e, 0x5a5326, 0x4c566a, 0xeceff4, 0x3b4252,
     {0xd8dee9, 0x81a1c1, 0x8fbcbb, 0xa3be8c, 0xb48ead, 0x616e88, 0x88c0d0},
     0xbf616a, 0xebcb8b, 0xa3be8c, 0xa0aabf, 0xeceff4},

    // Solarized Light: warm light
    {"solarized-light", "Solarized Light", false,
     0xeee8d5, 0xfdf6e3, 0xfdf6e3, 0xddd6c1, 0x586e75, 0x657b83, 0x93a1a1, 0x073642,
     0xf5eedc, 0xddd6c1, 0xcfc8b3, 0xfdf6e3, 0xcfc8b3, 0xe6dfca, 0xdcd5bf, 0xeee8d5, 0xe6dfca, 0xeee8d5, 0xcfc8b3, 0xebe4d0,
     0xf5eedc, 0xc9d8e0, 0xf5e9a0, 0x93a1a1, 0x073642, 0xe6dfca,
     {0x586e75, 0x859900, 0xb58900, 0x2aa198, 0xd33682, 0x93a1a1, 0x6c71c4},
     0xdc322f, 0xb58900, 0x859900, 0x657b83, 0x073642},
};
const int kSchemeCount = static_cast<int>(sizeof(kSchemes) / sizeof(kSchemes[0]));

QColor rgb(unsigned v) { return QColor(static_cast<int>((v >> 16) & 0xff), static_cast<int>((v >> 8) & 0xff), static_cast<int>(v & 0xff)); }

const Scheme& find(const QString& id) {
    for (const Scheme& s : kSchemes)
        if (id == QLatin1String(s.id)) return s;
    return kSchemes[0];
}

QString g_current = QStringLiteral("midnight");
bool g_dark = true;

QColor mix(const QColor& a, const QColor& b, qreal t) {      // t = share of a
    return QColor::fromRgbF(a.redF() * t + b.redF() * (1 - t), a.greenF() * t + b.greenF() * (1 - t), a.blueF() * t + b.blueF() * (1 - t));
}

void updateCaret() { Caret = g_dark ? Accent.lighter(122) : Accent.darker(112); }

// Copy a theme into the colour variables (no widgets are touched here).
void assign(const QString& id) {
    const Scheme& s = find(id);
    g_current = QString::fromLatin1(s.id);
    g_dark = s.dark;
    Chrome = rgb(s.chrome); Editor = rgb(s.editor); Surface = rgb(s.surface); Border = rgb(s.border);
    Text = rgb(s.text); Muted = rgb(s.muted); Dim = rgb(s.dim); Heading = rgb(s.heading);
    Raised = rgb(s.raised); CardBorder = rgb(s.cardBorder); InputBorder = rgb(s.inputBorder);
    Popup = rgb(s.popup); PopupBorder = rgb(s.popupBorder); RowHover = rgb(s.rowHover); Pressed = rgb(s.pressed);
    Button = rgb(s.button); ButtonHover = rgb(s.buttonHover); Keycap = rgb(s.keycap); KeycapBorder = rgb(s.keycapBorder);
    TreeHover = rgb(s.treeHover);
    CurrentLine = rgb(s.currentLine); Selection = rgb(s.selection); FindMatch = rgb(s.findMatch);
    GutterNumber = rgb(s.gutterNumber); GutterCurrent = rgb(s.gutterCurrent); IndentGuide = rgb(s.indentGuide);
    for (int i = 0; i < 7; ++i) Syntax[i] = rgb(s.syntax[i]);
    Error = rgb(s.error); Warning = rgb(s.warning); Success = rgb(s.success);
    IconNormal = rgb(s.iconNormal); IconActive = rgb(s.iconActive);
    updateCaret();
}

void applyPalette() {
    QPalette p;
    p.setColor(QPalette::Window, Chrome);
    p.setColor(QPalette::WindowText, Text);
    p.setColor(QPalette::Base, Editor);
    p.setColor(QPalette::AlternateBase, Chrome);
    p.setColor(QPalette::Text, Syntax[0]);
    p.setColor(QPalette::Button, Button);
    p.setColor(QPalette::ButtonText, Text);
    p.setColor(QPalette::Highlight, Selection);
    p.setColor(QPalette::HighlightedText, g_dark ? QColor(Qt::white) : Heading);
    p.setColor(QPalette::ToolTipBase, Raised);
    p.setColor(QPalette::ToolTipText, Text);
    p.setColor(QPalette::PlaceholderText, Dim);
    p.setColor(QPalette::Link, Accent);
    p.setColor(QPalette::Light, g_dark ? InputBorder : QColor(Qt::white));
    p.setColor(QPalette::Mid, CardBorder);
    p.setColor(QPalette::Dark, g_dark ? Chrome.darker(125) : InputBorder);
    p.setColor(QPalette::Disabled, QPalette::Text, Dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, Dim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, Dim);
    qApp->setPalette(p);
}

// Re-style everything that is already on screen after a theme or accent change.
void restyle() {
    if (!qApp) return;
    if (!qApp->styleSheet().isEmpty()) {
        applyPalette();
        qApp->setStyleSheet(styleSheet());
    }
    notifier().changed();
}

// Keep the theme and the accent in step with the settings (the settings window, the toggle ...).
void syncWithSettings() {
    const AppSettings::Values& v = AppSettings::instance().values();
    const QColor accent(AppSettings::accentRgb(v.accent));
    const QString wanted = isKnownScheme(v.theme) ? v.theme : defaultScheme();
    if (wanted == g_current && accent == Accent) return;
    Accent = accent;
    assign(wanted);
    restyle();
}

}  // namespace

int schemeCount() { return kSchemeCount; }
SchemeInfo schemeInfo(int i) {
    const Scheme& s = kSchemes[std::max(0, std::min(kSchemeCount - 1, i))];
    return {QString::fromLatin1(s.id), QString::fromLatin1(s.name), s.dark};
}
QStringList schemeIds() {
    QStringList l;
    for (const Scheme& s : kSchemes) l << QString::fromLatin1(s.id);
    return l;
}
bool isKnownScheme(const QString& id) {
    for (const Scheme& s : kSchemes)
        if (id == QLatin1String(s.id)) return true;
    return false;
}
QString schemeName(const QString& id) { return QString::fromLatin1(find(id).name); }
QString defaultScheme() { return QStringLiteral("midnight"); }
QString currentScheme() { return g_current; }
bool isDark() { return g_dark; }

QString toggledScheme(const QString& id) {
    if (id == QLatin1String("midnight")) return QStringLiteral("daylight");
    if (id == QLatin1String("daylight")) return QStringLiteral("midnight");
    if (id == QLatin1String("nord")) return QStringLiteral("solarized-light");
    if (id == QLatin1String("solarized-light")) return QStringLiteral("nord");
    return QStringLiteral("midnight");
}

Notifier& notifier() {
    static Notifier n;
    return n;
}

void setScheme(const QString& id) {
    const QString wanted = isKnownScheme(id) ? id : defaultScheme();
    if (wanted == g_current) return;
    assign(wanted);
    restyle();
}

// ---------------------------------------------------------------- derived colours

QColor accentHover() { return g_dark ? Accent.lighter(118) : Accent.darker(112); }
QColor accentSoft() { return mix(Accent, Editor, g_dark ? 0.22 : 0.20); }
QColor accentDeep() { return g_dark ? Accent.darker(138) : mix(Accent, Popup, 0.24); }
QColor selectionText() { return g_dark ? QColor(Qt::white) : Heading; }

void loadAccent() {
    const AppSettings::Values& v = AppSettings::instance().values();
    Accent = QColor(AppSettings::accentRgb(v.accent));
    assign(isKnownScheme(v.theme) ? v.theme : defaultScheme());
    static bool hooked = false;
    if (!hooked && qApp) {
        hooked = true;
        QObject::connect(&AppSettings::instance(), &AppSettings::changed, qApp, []() { syncWithSettings(); });
    }
}

void setAccent(const QColor& c) {
    Accent = c;
    updateCaret();
    restyle();
}

// ---------------------------------------------------------------- style sheet

static QString extraStyleSheet() {
    return QString(R"QSS(
/* ---- shared controls (settings window) ---- */
#combo { background: @surface@; border: 1px solid @inputBorder@; border-radius: 6px; padding: 5px 30px 5px 10px; color: @text@; min-height: 20px; }
#combo:hover { border-color: @keycapBorder@; }
#combo:focus, #combo:on { border-color: @accent@; }
#combo::drop-down { border: 0; width: 0; }
#combo::down-arrow { image: none; width: 0; }
QComboBox QAbstractItemView { background: @popup@; border: 1px solid @popupBorder@; border-radius: 6px; padding: 4px; outline: 0;
                              color: @text@; selection-background-color: @deep@; }
QComboBox QAbstractItemView::item { min-height: 26px; padding: 0 8px; border-radius: 5px; }
QComboBox QAbstractItemView::item:selected { background: @deep@; color: @selfg@; }
#stepper, #segmented { background: @surface@; border: 1px solid @inputBorder@; border-radius: 8px; }
#stepperValue { color: @text@; font-weight: 600; background: transparent; }
QToolButton#stepperButton { border: 0; border-radius: 6px; color: @text@; font-size: 15px; background: transparent; }
QToolButton#stepperButton:hover { background: @buttonHover@; }
QToolButton#stepperButton:disabled { color: @dim@; background: transparent; }
QToolButton#segment { border: 0; border-radius: 6px; padding: 3px 12px; color: @muted@; background: transparent; }
QToolButton#segment:hover { background: @pressed@; color: @text@; }
QToolButton#segment:checked { background: @accent@; color: @segfg@; font-weight: 600; border: 0; }

QSplitter::handle { background: transparent; }
QSplitter::handle:vertical { border-top: 1px solid @border@; }
QSplitter::handle:hover { background: @acc20@; }

/* ---- settings window ---- */
#settingsDialog { background: @editor@; }
#settingsTitle { font-size: 22px; font-weight: 600; color: @heading@; }
#settingsSubtitle { color: @muted@; }
QListWidget#settingsNav { background: transparent; border: 0; outline: 0; }
QListWidget#settingsNav::item { padding: 9px 12px; border-radius: 8px; color: @muted@; margin: 1px 0; }
QListWidget#settingsNav::item:hover { background: @rowHover@; color: @text@; }
QListWidget#settingsNav::item:selected { background: @acc12@; color: @heading@; }
#settingsScroll, #settingsInner { background: transparent; border: 0; }
#settingsSection { color: @heading@; font-size: 15px; font-weight: 600; }
#settingsSectionDesc { color: @muted@; font-size: 12px; }
#settingsCard { background: @raised@; border: 1px solid @cardBorder@; border-radius: 12px; }
#settingsRow { background: transparent; }
#settingsSep { background: @cardBorder@; border: 0; }
#settingsRowTitle { color: @heading@; font-weight: 600; font-size: 13px; background: transparent; }
#settingsRowDesc { color: @muted@; font-size: 12px; background: transparent; }
#settingsFooter { background: @chrome@; border-top: 1px solid @border@; }
#settingsFooterText { color: @dim@; }
#settingsEmpty { color: @dim@; font-size: 13px; }

/* ---- keyboard shortcuts window ---- */
#shortcutsDialog { background: @editor@; }
#shortcutsTitle { font-size: 22px; font-weight: 600; color: @heading@; }
#shortcutsSubtitle { color: @muted@; }
#shortcutsScroll, #shortcutsInner { background: transparent; border: 0; }
#shortcutCard { background: @raised@; border: 1px solid @cardBorder@; border-radius: 12px; }
#shortcutCategory { color: @accent@; font-size: 11px; font-weight: 700; background: transparent; }
#shortcutDesc { color: @text@; font-size: 13px; background: transparent; }
#shortcutNote { color: @dim@; font-size: 12px; background: transparent; }
#keyCap { background: @keycap@; color: @heading@; border: 1px solid @keycapBorder@; border-bottom: 2px solid @keycapBorder@; border-radius: 5px;
          padding: 1px 7px; font-size: 11px; font-weight: 600; }
#keyOr { color: @dim@; font-size: 11px; background: transparent; }
#keyPlus { color: @dim@; font-size: 11px; background: transparent; }
#shortcutsFooter { background: @chrome@; border-top: 1px solid @border@; }

/* ---- problems panel ---- */
QTreeWidget#problemsTree { background: @editor@; }

/* ---- terminal panel ---- */
#terminalPanel { background: @editor@; }
#terminalHeader { background: @chrome@; border-top: 1px solid @border@; border-bottom: 1px solid @border@; }
#terminalTitle { color: @subhead@; font-weight: 600; font-size: 11px; background: transparent; }
#terminalChip { background: @surface@; color: @muted@; border-radius: 9px; padding: 1px 9px; font-size: 11px; }
#terminalChipBusy { background: @acc20@; color: @heading@; border-radius: 9px; padding: 1px 9px; font-size: 11px; }
#terminalCwd { color: @dim@; font-size: 12px; background: transparent; }
QPlainTextEdit#terminalOutput { background: @editor@; color: @code@; border: 0; padding: 6px 10px 2px 10px; selection-background-color: @selection@; }
#terminalPromptRow { background: @editor@; }
#terminalPrompt { color: @accent@; font-weight: 600; background: transparent; }
QLineEdit#terminalInput { background: transparent; border: 0; padding: 4px 2px 6px 2px; color: @heading@; }
QLineEdit#terminalInput:focus { border: 0; }
)QSS");
}

QString styleSheet() {
    QString s = QString(R"QSS(
QMainWindow, QDialog, QMessageBox, QInputDialog { background: @editor@; }
QWidget { color: @text@; }
QMainWindow::separator { background: @border@; width: 1px; height: 1px; }
QMainWindow::separator:hover { background: @accent@; }

QToolTip { background: @raised@; color: @text@; border: 1px solid @inputBorder@; padding: 5px 8px; border-radius: 6px; }

/* menus */
QMenuBar { background: @chrome@; color: @text@; border-bottom: 1px solid @border@; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 5px; }
QMenuBar::item:selected { background: @rowHover@; }
QMenuBar::item:pressed { background: @pressed@; }
QMenu { background: @popup@; color: @text@; border: 1px solid @popupBorder@; padding: 5px; }
QMenu::item { padding: 6px 32px 6px 22px; border-radius: 5px; }
QMenu::item:selected { background: @deep@; color: @selfg@; }
QMenu::item:disabled { color: @dim@; }
QMenu::separator { height: 1px; background: @cardBorder@; margin: 5px 8px; }
QMenu::icon { padding-left: 8px; }

/* tabs */
QTabWidget::pane { border: 0; background: @editor@; }
QTabBar { background: @chrome@; }
QTabBar::tab { background: @chrome@; color: @muted@; padding: 8px 10px 8px 12px; min-width: 70px;
               border: 0; border-right: 1px solid @border@; border-bottom: 1px solid @border@; border-top: 2px solid transparent; }
QTabBar::tab:hover:!selected { background: @tabHover@; color: @text@; }
QTabBar::tab:selected { background: @editor@; color: @heading@; border-top: 2px solid @accent@; border-bottom: 1px solid @editor@; }
QTabBar QToolButton { background: @chrome@; border: 0; }
QToolButton#tabClose { background: transparent; border: 0; border-radius: 4px; padding: 2px; }
QToolButton#tabClose:hover { background: @pressed@; }

/* status bar */
QStatusBar { background: @chrome@; color: @muted@; border-top: 1px solid @border@; min-height: 24px; }
QStatusBar::item { border: 0; }
QStatusBar QLabel, QStatusBar QPushButton { color: @muted@; padding: 1px 8px; background: transparent; border: 0; border-radius: 4px; }
QStatusBar QPushButton:hover { background: @rowHover@; color: @text@; }

/* inputs and buttons */
QLineEdit { background: @surface@; color: @text@; border: 1px solid @inputBorder@; border-radius: 6px; padding: 5px 8px;
            selection-background-color: @selection@; }
QLineEdit:focus { border: 1px solid @accent@; }
QPushButton { background: @button@; color: @text@; border: 1px solid @inputBorder@; border-radius: 6px; padding: 5px 14px; }
QPushButton:hover { background: @buttonHover@; border-color: @keycapBorder@; }
QPushButton:pressed { background: @pressed@; }
QPushButton:default, QPushButton#primary { background: @accent@; color: #ffffff; border: 1px solid @accent@; }
QPushButton:default:hover, QPushButton#primary:hover { background: @hover@; border-color: @hover@; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 3px; }
QToolButton:hover { background: @rowHover@; }
QToolButton:pressed { background: @pressed@; }
QToolButton:checked { background: @acc20@; border: 1px solid @acc55@; }

/* lists and trees */
QTreeView, QListView, QListWidget { background: @chrome@; color: @text@; border: 0; outline: 0; }
QTreeView::item, QListView::item { padding: 3px 2px; border: 0; }
QTreeView::item:hover, QListView::item:hover { background: @treeHover@; }
QTreeView::item:selected, QListView::item:selected { background: @soft@; color: @selfg@; }
QTreeView::item:selected:!active, QListView::item:selected:!active { background: @pressed@; color: @text@; }
QTreeView::branch { background: transparent; }

/* scroll bars: thin, no arrows */
QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle:vertical { background: rgba(121, 121, 121, 0.35); min-height: 32px; border-radius: 4px; margin: 2px 3px; }
QScrollBar::handle:horizontal { background: rgba(121, 121, 121, 0.35); min-width: 32px; border-radius: 4px; margin: 3px 2px; }
QScrollBar::handle:hover { background: rgba(150, 150, 150, 0.55); }
QScrollBar::handle:pressed { background: rgba(170, 170, 170, 0.65); }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: 0; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QAbstractScrollArea::corner { background: transparent; border: 0; }

/* activity bar */
QToolBar#activityBar { background: @chrome@; border: 0; border-right: 1px solid @border@; padding: 6px 0; spacing: 2px; }
QToolBar#activityBar QToolButton { border: 0; border-left: 2px solid transparent; border-radius: 0; padding: 10px 11px; margin: 0; background: transparent; }
QToolBar#activityBar QToolButton:checked { border-left: 2px solid @accent@; background: transparent; }
QToolBar#activityBar QToolButton:hover { background: transparent; }

/* explorer */
#fileExplorer, #explorerHeader { background: @chrome@; }
#explorerTitle { color: @subhead@; font-weight: 600; font-size: 11px; letter-spacing: 1px; }
#explorerHint { color: @muted@; }
QToolButton#explorerButton { border-radius: 5px; padding: 3px; }
QPushButton#explorerOpen { background: @accent@; color: #ffffff; border: 0; padding: 7px; border-radius: 6px; }
QPushButton#explorerOpen:hover { background: @hover@; }

/* find bar */
#findBar { background: @chrome@; border-top: 1px solid @border@; }
#findCount { color: @muted@; }

/* welcome page */
#welcomePage, #welcomeScroll, #welcomeInner { background: @editor@; }
#welcomeTitle { color: @heading@; font-size: 30px; font-weight: 600; }
#welcomeSubtitle { color: @muted@; font-size: 14px; }
#welcomeHeading { color: @heading@; font-size: 14px; font-weight: 600; padding-bottom: 6px; }
#welcomeLink { font-size: 13px; }
#welcomeHint, #welcomeTip { color: @dim@; }
QPushButton#welcomeCard { background: @raised@; border: 1px solid @cardBorder@; border-radius: 10px; padding: 14px 16px;
                           text-align: left; color: @text@; font-size: 13px; }
QPushButton#welcomeCard:hover { background: @rowHover@; border: 1px solid @accent@; }
#welcomeKey { background: @keycap@; color: @text@; border: 1px solid @keycapBorder@; border-bottom: 2px solid @keycapBorder@; border-radius: 4px;
              padding: 1px 6px; font-size: 11px; }
#welcomeKeyText { color: @muted@; }
#welcomeCardTitle { color: @heading@; font-weight: 600; font-size: 13px; background: transparent; }
#welcomeCardHint { color: @muted@; font-size: 12px; background: transparent; }
#welcomeRecentPath { color: @dim@; }
)QSS");
    s += extraStyleSheet();

    auto rgba = [](const QColor& c, double a) { return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(a); };
    struct Token { const char* key; QString value; };
    const Token tokens[] = {
        {"@chrome@", Chrome.name()},           {"@editor@", Editor.name()},           {"@surface@", Surface.name()},
        {"@border@", Border.name()},           {"@accent@", Accent.name()},           {"@text@", Text.name()},
        {"@muted@", Muted.name()},             {"@dim@", Dim.name()},                 {"@heading@", Heading.name()},
        {"@raised@", Raised.name()},           {"@cardBorder@", CardBorder.name()},   {"@inputBorder@", InputBorder.name()},
        {"@popup@", Popup.name()},             {"@popupBorder@", PopupBorder.name()}, {"@rowHover@", RowHover.name()},
        {"@pressed@", Pressed.name()},         {"@button@", Button.name()},           {"@buttonHover@", ButtonHover.name()},
        {"@keycap@", Keycap.name()},           {"@keycapBorder@", KeycapBorder.name()}, {"@treeHover@", TreeHover.name()},
        {"@selection@", Selection.name()},     {"@code@", Syntax[0].name()},
        {"@deep@", accentDeep().name()},       {"@hover@", accentHover().name()},     {"@soft@", accentSoft().name()},
        {"@selfg@", selectionText().name()},   {"@segfg@", g_dark ? QStringLiteral("#0f0f0f") : QStringLiteral("#ffffff")},
        {"@tabHover@", mix(Chrome, Editor, 0.5).name()},
        {"@subhead@", mix(Text, Muted, 0.6).name()},
        {"@acc20@", rgba(Accent, 0.20)},       {"@acc55@", rgba(Accent, 0.55)},       {"@acc12@", rgba(Accent, 0.12)},
    };
    for (const Token& t : tokens) s.replace(QLatin1String(t.key), t.value);
    return s;
}

QString popupListStyle() {
    return QString("QListWidget{background:%1;color:%2;border:1px solid %3;border-radius:6px;outline:0;padding:3px;}"
                   "QListWidget::item{padding:3px 8px;border-radius:4px;}"
                   "QListWidget::item:selected{background:%4;color:%5;}")
        .arg(Raised.name(), Syntax[0].name(), InputBorder.name(), accentDeep().name(), selectionText().name());
}

void apply() {
    loadAccent();
    qApp->setStyle(QStyleFactory::create("Fusion"));
    applyPalette();
    qApp->setFont(uiFont());
    qApp->setStyleSheet(styleSheet());
}

}  // namespace Theme

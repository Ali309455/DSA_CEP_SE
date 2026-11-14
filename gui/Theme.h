// Theme.h – colours for the editor, syntax highlighter, and application chrome.
// Modern VS Code / JetBrains IDE aesthetic with four built-in themes.
#pragma once
#include <QColor>
#include <QPalette>
#include <QString>

struct Theme {
    // ── Identity ────────────────────────────────────────────────────────────
    QString name;
    bool dark = true;

    // ── Editor colours ───────────────────────────────────────────────────────
    QColor bg, fg, gutterBg, gutterFg, gutterActive;
    QColor curLine, selection, bracketMatch;
    QColor error, warning, findMatch, findCurrent;

    // ── Syntax colours ───────────────────────────────────────────────────────
    QColor keyword, type, string, comment, number, preproc, function;

    // ── Chrome / UI colours ──────────────────────────────────────────────────
    QColor chromeBg, chromeFg, accent, popupBg, popupSel;

    // ════════════════════════════════════════════════════════════════════════
    //  Built-in themes
    // ════════════════════════════════════════════════════════════════════════

    // ── Midnight (OneDark-inspired) ─────────────────────────────────────────
    static Theme dark_() {
        Theme t;
        t.name = "Midnight"; t.dark = true;
        // Editor
        t.bg         = "#1e2127"; t.fg          = "#abb2bf";
        t.gutterBg   = "#1a1d23"; t.gutterFg    = "#565c64"; t.gutterActive = "#c8ccd4";
        t.curLine    = "#282c34"; t.selection   = "#3e4a6e"; t.bracketMatch = "#4b5d3a";
        t.error      = "#e06c75"; t.warning     = "#e5c07b";
        t.findMatch  = "#5a4a1a"; t.findCurrent = "#8a6d1f";
        // Syntax
        t.keyword  = "#c678dd"; t.type    = "#e5c07b"; t.string  = "#98c379";
        t.comment  = "#7f848e"; t.number  = "#d19a66"; t.preproc = "#56b6c2";
        t.function = "#61afef";
        // Chrome
        t.chromeBg = "#21252b"; t.chromeFg = "#abb2bf";
        t.accent   = "#528bff";
        t.popupBg  = "#2c313a"; t.popupSel = "#3a4254";
        return t;
    }

    // ── Daylight (GitHub Light-inspired) ────────────────────────────────────
    static Theme light_() {
        Theme t;
        t.name = "Daylight"; t.dark = false;
        // Editor
        t.bg         = "#ffffff"; t.fg          = "#1f2328";
        t.gutterBg   = "#f6f8fa"; t.gutterFg    = "#8c959f"; t.gutterActive = "#1f2328";
        t.curLine    = "#f0f4ff"; t.selection   = "#b6cdf5"; t.bracketMatch = "#cfe8c4";
        t.error      = "#d1242f"; t.warning     = "#9a6700";
        t.findMatch  = "#fff1a8"; t.findCurrent = "#ffd33d";
        // Syntax
        t.keyword  = "#cf222e"; t.type    = "#953800"; t.string  = "#0a3069";
        t.comment  = "#8c959f"; t.number  = "#0550ae"; t.preproc = "#0b7285";
        t.function = "#8250df";
        // Chrome
        t.chromeBg = "#f6f8fa"; t.chromeFg = "#1f2328";
        t.accent   = "#0969da";
        t.popupBg  = "#ffffff"; t.popupSel = "#dbeafe";
        return t;
    }

    // ── Nord (Arctic, North-bluish) ─────────────────────────────────────────
    static Theme nord_() {
        Theme t;
        t.name = "Nord"; t.dark = true;
        // Editor – Polar Night + Snow Storm
        t.bg         = "#2e3440"; t.fg          = "#d8dee9";
        t.gutterBg   = "#272c36"; t.gutterFg    = "#4c566a"; t.gutterActive = "#eceff4";
        t.curLine    = "#3b4252"; t.selection   = "#434c5e"; t.bracketMatch = "#4b5d6e";
        t.error      = "#bf616a"; t.warning     = "#ebcb8b";
        t.findMatch  = "#4a4320"; t.findCurrent = "#6b6020";
        // Syntax – Frost + Aurora
        t.keyword  = "#81a1c1"; t.type    = "#8fbcbb"; t.string  = "#a3be8c";
        t.comment  = "#616e88"; t.number  = "#b48ead"; t.preproc = "#88c0d0";
        t.function = "#88c0d0";
        // Chrome
        t.chromeBg = "#242933"; t.chromeFg = "#d8dee9";
        t.accent   = "#5e81ac";
        t.popupBg  = "#3b4252"; t.popupSel = "#434c5e";
        return t;
    }

    // ── Solarized Light ─────────────────────────────────────────────────────
    static Theme solarized_light_() {
        Theme t;
        t.name = "Solarized Light"; t.dark = false;
        // Editor
        t.bg         = "#fdf6e3"; t.fg          = "#657b83";
        t.gutterBg   = "#eee8d5"; t.gutterFg    = "#93a1a1"; t.gutterActive = "#073642";
        t.curLine    = "#eee8d5"; t.selection   = "#c9d8e0"; t.bracketMatch = "#c5dfc5";
        t.error      = "#dc322f"; t.warning     = "#b58900";
        t.findMatch  = "#f5e9a0"; t.findCurrent = "#e8d44d";
        // Syntax
        t.keyword  = "#859900"; t.type    = "#b58900"; t.string  = "#2aa198";
        t.comment  = "#93a1a1"; t.number  = "#d33682"; t.preproc = "#6c71c4";
        t.function = "#268bd2";
        // Chrome
        t.chromeBg = "#eee8d5"; t.chromeFg = "#586e75";
        t.accent   = "#268bd2";
        t.popupBg  = "#fdf6e3"; t.popupSel = "#c9d8e0";
        return t;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Lookup
    // ════════════════════════════════════════════════════════════════════════
    static Theme byName(const QString& n) {
        if (n == "Daylight")        return light_();
        if (n == "Nord")            return nord_();
        if (n == "Solarized Light") return solarized_light_();
        return dark_(); // default: Midnight
    }

    // ════════════════════════════════════════════════════════════════════════
    //  QPalette
    // ════════════════════════════════════════════════════════════════════════
    QPalette palette() const {
        QPalette p;
        p.setColor(QPalette::Window,          chromeBg);
        p.setColor(QPalette::WindowText,      chromeFg);
        p.setColor(QPalette::Base,            bg);
        p.setColor(QPalette::AlternateBase,   curLine);
        p.setColor(QPalette::Text,            fg);
        p.setColor(QPalette::Button,          chromeBg);
        p.setColor(QPalette::ButtonText,      chromeFg);
        p.setColor(QPalette::ToolTipBase,     popupBg);
        p.setColor(QPalette::ToolTipText,     fg);
        p.setColor(QPalette::Highlight,       accent);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, gutterFg);
        p.setColor(QPalette::Disabled, QPalette::Text,       gutterFg);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, gutterFg);
        p.setColor(QPalette::Disabled, QPalette::WindowText, gutterFg);
        return p;
    }

    // ════════════════════════════════════════════════════════════════════════
    //  Global QSS stylesheet generator
    // ════════════════════════════════════════════════════════════════════════
    static QString appStyleSheet(const Theme& t) {
        // ── Derived solid colours (no rgba – not supported in QSS) ──────────
        // Accent tints: blend accent toward chromeBg at different strengths
        auto blend = [](QColor a, QColor b, int pct) -> QString {
            // pct = how much of `a` to keep (0–100)
            int r = (a.red()   * pct + b.red()   * (100 - pct)) / 100;
            int g = (a.green() * pct + b.green() * (100 - pct)) / 100;
            int bl= (a.blue()  * pct + b.blue()  * (100 - pct)) / 100;
            return QColor(r, g, bl).name();
        };

        const QString cbg   = t.chromeBg.name();
        const QString cfg   = t.chromeFg.name();
        const QString acc   = t.accent.name();
        const QString pbg   = t.popupBg.name();
        const QString psel  = t.popupSel.name();
        const QString ebg   = t.bg.name();
        const QString efg   = t.fg.name();
        const QString ggfg  = t.gutterFg.name();
        const QString ggbg  = t.gutterBg.name();
        const QString sel   = t.selection.name();

        // accent at ~15% opacity over chromeBg
        const QString accTint15 = blend(t.accent, t.chromeBg, 15);
        // accent at ~30% opacity over chromeBg
        const QString accTint30 = blend(t.accent, t.chromeBg, 30);
        // accent hover (full)
        const QString accHover  = acc;
        // accent pressed (slightly darker)
        const QString accPress  = t.accent.darker(115).name();
        // accent at 90% over chromeBg (button bg)
        const QString accBtn    = blend(t.accent, t.chromeBg, 90);
        // menu hover bg (accent at 80% opacity over chromeBg for menu item)
        const QString menuHover = acc;
        // gutter fg at ~60% over popupBg (line edit border)
        const QString leBorder  = blend(t.gutterFg, t.popupBg, 60);
        // scrollbar handle: gutterFg at 70% over bg
        const QString sbHandle  = blend(t.gutterFg, t.bg, 70);
        const QString sbHover   = blend(t.gutterFg, t.bg, 90);
        // dock title lighter shade
        const QString dockTitle = t.dark
            ? t.chromeBg.lighter(115).name()
            : t.chromeBg.darker(108).name();
        // tab hover bg
        const QString tabHover  = blend(t.accent, t.chromeBg, 10);
        // tree item hover
        const QString treeHover = blend(t.accent, t.bg, 10);
        // toolbar btn hover
        const QString tbBtnHov  = accTint15;

        QString ss;
        ss.reserve(8192);

        // ── QMainWindow / QWidget base ───────────────────────────────────────
        ss += QString(
            "QMainWindow, QWidget {"
            "  background: %1;"
            "  color: %2;"
            "}\n"
        ).arg(cbg, cfg);

        // ── QMenuBar ─────────────────────────────────────────────────────────
        ss += QString(
            "QMenuBar {"
            "  background: %1;"
            "  color: %2;"
            "  border-bottom: 1px solid %3;"
            "  padding: 2px 0;"
            "}"
            "QMenuBar::item {"
            "  background: transparent;"
            "  padding: 4px 10px;"
            "  border-radius: 3px;"
            "}"
            "QMenuBar::item:selected, QMenuBar::item:pressed {"
            "  background: %4;"
            "  color: white;"
            "}\n"
        ).arg(cbg, cfg, accTint30, menuHover);

        // ── QMenu ─────────────────────────────────────────────────────────────
        ss += QString(
            "QMenu {"
            "  background: %1;"
            "  color: %2;"
            "  border: 1px solid %3;"
            "  border-radius: 4px;"
            "  padding: 4px 0;"
            "}"
            "QMenu::item {"
            "  padding: 5px 24px 5px 12px;"
            "  border-radius: 2px;"
            "}"
            "QMenu::item:selected {"
            "  background: %4;"
            "  color: white;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background: %3;"
            "  margin: 3px 8px;"
            "}\n"
        ).arg(pbg, efg, leBorder, menuHover);

        // ── QToolBar ─────────────────────────────────────────────────────────
        ss += QString(
            "QToolBar {"
            "  background: %1;"
            "  border: none;"
            "  border-bottom: 1px solid %2;"
            "  padding: 2px 4px;"
            "  spacing: 2px;"
            "}"
            "QToolBar::separator {"
            "  width: 1px;"
            "  background: %3;"
            "  margin: 4px 2px;"
            "}\n"
        ).arg(cbg, accTint30, ggbg);

        // ── QToolButton ───────────────────────────────────────────────────────
        ss += QString(
            "QToolButton {"
            "  background: transparent;"
            "  border: none;"
            "  border-radius: 3px;"
            "  padding: 3px 5px;"
            "  color: %1;"
            "}"
            "QToolButton:hover {"
            "  background: %2;"
            "}"
            "QToolButton:pressed {"
            "  background: %3;"
            "}\n"
        ).arg(cfg, tbBtnHov, accTint30);

        // ── QDockWidget ───────────────────────────────────────────────────────
        ss += QString(
            "QDockWidget {"
            "  color: %1;"
            "  titlebar-close-icon: none;"
            "}"
            "QDockWidget::title {"
            "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "    stop:0 %2, stop:1 %3);"
            "  border-bottom: 1px solid %4;"
            "  font-weight: 600;"
            "  padding: 4px 8px;"
            "  color: %1;"
            "}\n"
        ).arg(cfg, cbg, dockTitle, accTint30);

        // ── QTabWidget / QTabBar ──────────────────────────────────────────────
        ss += QString(
            "QTabWidget::pane {"
            "  border: none;"
            "  background: %1;"
            "}"
            "QTabBar {"
            "  background: %2;"
            "}"
            "QTabBar::tab {"
            "  background: transparent;"
            "  color: %3;"
            "  padding: 6px 16px;"
            "  border: none;"
            "  border-bottom: 2px solid transparent;"
            "  min-width: 80px;"
            "}"
            "QTabBar::tab:selected {"
            "  color: %4;"
            "  border-bottom: 2px solid %5;"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  background: %6;"
            "}\n"
        ).arg(ebg, cbg, ggfg, efg, acc, tabHover);

        // ── QStatusBar ────────────────────────────────────────────────────────
        ss += QString(
            "QStatusBar {"
            "  background: %1;"
            "  color: %2;"
            "  border-top: 1px solid %3;"
            "  font-size: 11px;"
            "}"
            "QStatusBar::item {"
            "  border: none;"
            "}"
            "QStatusBar QLabel {"
            "  padding: 0 6px;"
            "  color: %2;"
            "}\n"
        ).arg(cbg, cfg, accTint30);

        // ── QPlainTextEdit / QTextEdit ────────────────────────────────────────
        ss += QString(
            "QPlainTextEdit, QTextEdit {"
            "  background: %1;"
            "  color: %2;"
            "  border: none;"
            "  selection-background-color: %3;"
            "  selection-color: %2;"
            "}\n"
        ).arg(ebg, efg, sel);

        // ── QTreeView ─────────────────────────────────────────────────────────
        ss += QString(
            "QTreeView {"
            "  background: %1;"
            "  color: %2;"
            "  border: none;"
            "  alternate-background-color: %1;"
            "  outline: none;"
            "}"
            "QTreeView::item:hover {"
            "  background: %3;"
            "}"
            "QTreeView::item:selected {"
            "  background: %4;"
            "  color: %2;"
            "}\n"
        ).arg(ebg, efg, treeHover, sel);

        // ── QLineEdit ─────────────────────────────────────────────────────────
        ss += QString(
            "QLineEdit {"
            "  background: %1;"
            "  color: %2;"
            "  border: 1px solid %3;"
            "  border-radius: 4px;"
            "  padding: 3px 6px;"
            "  selection-background-color: %4;"
            "}"
            "QLineEdit:focus {"
            "  border: 1px solid %5;"
            "}\n"
        ).arg(pbg, efg, leBorder, sel, acc);

        // ── QPushButton ───────────────────────────────────────────────────────
        ss += QString(
            "QPushButton {"
            "  background: %1;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 4px;"
            "  padding: 4px 12px;"
            "}"
            "QPushButton:hover {"
            "  background: %2;"
            "}"
            "QPushButton:pressed {"
            "  background: %3;"
            "}"
            "QPushButton:disabled {"
            "  background: %4;"
            "  color: %5;"
            "}\n"
        ).arg(accBtn, accHover, accPress, ggbg, ggfg);

        // ── QScrollBar ────────────────────────────────────────────────────────
        ss += QString(
            "QScrollBar:vertical {"
            "  background: %1;"
            "  width: 8px;"
            "  margin: 0;"
            "  border: none;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: %2;"
            "  min-height: 24px;"
            "  border-radius: 4px;"
            "}"
            "QScrollBar::handle:vertical:hover {"
            "  background: %3;"
            "}"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
            "  height: 0; width: 0; border: none; background: none;"
            "}"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
            "  background: none;"
            "}"
            "QScrollBar:horizontal {"
            "  background: %1;"
            "  height: 8px;"
            "  margin: 0;"
            "  border: none;"
            "}"
            "QScrollBar::handle:horizontal {"
            "  background: %2;"
            "  min-width: 24px;"
            "  border-radius: 4px;"
            "}"
            "QScrollBar::handle:horizontal:hover {"
            "  background: %3;"
            "}"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
            "  height: 0; width: 0; border: none; background: none;"
            "}"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
            "  background: none;"
            "}\n"
        ).arg(ebg, sbHandle, sbHover);

        // ── QListWidget (completion popup) ────────────────────────────────────
        ss += QString(
            "QListWidget {"
            "  background: %1;"
            "  color: %2;"
            "  border: 1px solid %3;"
            "  border-radius: 4px;"
            "  outline: none;"
            "}"
            "QListWidget::item {"
            "  padding: 2px 8px;"
            "  border-radius: 2px;"
            "}"
            "QListWidget::item:selected, QListWidget::item:hover:selected {"
            "  background: %4;"
            "  color: %2;"
            "}\n"
        ).arg(pbg, efg, acc, psel);

        // ── QSplitter ─────────────────────────────────────────────────────────
        ss += QString(
            "QSplitter::handle {"
            "  background: %1;"
            "  width: 1px;"
            "  height: 1px;"
            "}\n"
        ).arg(ggbg);

        return ss;
    }
};

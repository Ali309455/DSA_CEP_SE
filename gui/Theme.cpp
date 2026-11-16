#include "Theme.h"

#include <QApplication>
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

QFont codeFont(int pointSize) {
    static const QString fam = pick({"Cascadia Code", "Cascadia Mono", "JetBrains Mono", "Fira Code", "Source Code Pro",
                                     "Consolas", "SF Mono", "Menlo", "DejaVu Sans Mono", "Liberation Mono"});
    QFont f = fam.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(fam);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPointSize(pointSize);
    return f;
}

QString styleSheet() {
    // %1 chrome  %2 editor  %3 surface  %4 border  %5 accent  %6 text  %7 muted  %8 dim
    QString s = R"QSS(
QMainWindow, QDialog, QMessageBox, QInputDialog { background: %2; }
QWidget { color: %6; }
QMainWindow::separator { background: %4; width: 1px; height: 1px; }
QMainWindow::separator:hover { background: %5; }

QToolTip { background: #252526; color: %6; border: 1px solid #3c3c3c; padding: 5px 8px; border-radius: 6px; }

/* menus */
QMenuBar { background: %1; color: %6; border-bottom: 1px solid %4; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 5px; }
QMenuBar::item:selected { background: #2e2e2e; }
QMenuBar::item:pressed { background: #353535; }
QMenu { background: #1f1f1f; color: %6; border: 1px solid #383838; padding: 5px; }
QMenu::item { padding: 6px 32px 6px 22px; border-radius: 5px; }
QMenu::item:selected { background: #2f5fbf; color: #ffffff; }
QMenu::item:disabled { color: %8; }
QMenu::separator { height: 1px; background: #333333; margin: 5px 8px; }
QMenu::icon { padding-left: 8px; }

/* tabs */
QTabWidget::pane { border: 0; background: %2; }
QTabBar { background: %1; }
QTabBar::tab { background: %1; color: %7; padding: 8px 10px 8px 12px; min-width: 70px;
               border: 0; border-right: 1px solid %4; border-bottom: 1px solid %4; border-top: 2px solid transparent; }
QTabBar::tab:hover:!selected { background: #1c1c1c; color: %6; }
QTabBar::tab:selected { background: %2; color: #ffffff; border-top: 2px solid %5; border-bottom: 1px solid %2; }
QTabBar QToolButton { background: %1; border: 0; }
QToolButton#tabClose { background: transparent; border: 0; border-radius: 4px; padding: 2px; }
QToolButton#tabClose:hover { background: #3a3a3a; }

/* status bar */
QStatusBar { background: %1; color: %7; border-top: 1px solid %4; min-height: 24px; }
QStatusBar::item { border: 0; }
QStatusBar QLabel, QStatusBar QPushButton { color: %7; padding: 1px 8px; background: transparent; border: 0; border-radius: 4px; }
QStatusBar QPushButton:hover { background: #2a2a2a; color: %6; }

/* inputs and buttons */
QLineEdit { background: %3; color: %6; border: 1px solid #3a3a3a; border-radius: 6px; padding: 5px 8px;
            selection-background-color: #264f78; }
QLineEdit:focus { border: 1px solid %5; }
QPushButton { background: #2d2d2d; color: %6; border: 1px solid #3a3a3a; border-radius: 6px; padding: 5px 14px; }
QPushButton:hover { background: #363636; border-color: #454545; }
QPushButton:pressed { background: #2a2a2a; }
QPushButton:default, QPushButton#primary { background: %5; color: #ffffff; border: 1px solid %5; }
QPushButton:default:hover, QPushButton#primary:hover { background: #6b9fff; border-color: #6b9fff; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 3px; }
QToolButton:hover { background: #2e2e2e; }
QToolButton:pressed { background: #353535; }
QToolButton:checked { background: rgba(79, 140, 255, 0.20); border: 1px solid rgba(79, 140, 255, 0.55); }

/* lists and trees */
QTreeView, QListView, QListWidget { background: %1; color: %6; border: 0; outline: 0; }
QTreeView::item, QListView::item { padding: 3px 2px; border: 0; }
QTreeView::item:hover, QListView::item:hover { background: #2a2d2e; }
QTreeView::item:selected, QListView::item:selected { background: #233a5e; color: #ffffff; }
QTreeView::item:selected:!active, QListView::item:selected:!active { background: #2b2f36; color: %6; }
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
QToolBar#activityBar { background: %1; border: 0; border-right: 1px solid %4; padding: 6px 0; spacing: 2px; }
QToolBar#activityBar QToolButton { border: 0; border-left: 2px solid transparent; border-radius: 0; padding: 10px 11px; margin: 0; background: transparent; }
QToolBar#activityBar QToolButton:checked { border-left: 2px solid %5; background: transparent; }
QToolBar#activityBar QToolButton:hover { background: transparent; }

/* explorer */
#fileExplorer, #explorerHeader { background: %1; }
#explorerTitle { color: #bbbbbb; font-weight: 600; font-size: 11px; letter-spacing: 1px; }
#explorerHint { color: %7; }
QToolButton#explorerButton { border-radius: 5px; padding: 3px; }
QPushButton#explorerOpen { background: %5; color: #ffffff; border: 0; padding: 7px; border-radius: 6px; }
QPushButton#explorerOpen:hover { background: #6b9fff; }

/* find bar */
#findBar { background: %1; border-top: 1px solid %4; }
#findCount { color: %7; }

/* welcome page */
#welcomePage, #welcomeScroll, #welcomeInner { background: %2; }
#welcomeTitle { color: #f0f0f0; font-size: 30px; font-weight: 600; }
#welcomeSubtitle { color: %7; font-size: 14px; }
#welcomeHeading { color: #e6e6e6; font-size: 14px; font-weight: 600; padding-bottom: 6px; }
#welcomeLink { font-size: 13px; }
#welcomeHint, #welcomeTip { color: %8; }
QPushButton#welcomeCard { background: #252526; border: 1px solid #303030; border-radius: 10px; padding: 14px 16px;
                           text-align: left; color: %6; font-size: 13px; }
QPushButton#welcomeCard:hover { background: #2a2b2e; border: 1px solid %5; }
#welcomeKey { background: #2b2b2b; color: %6; border: 1px solid #3a3a3a; border-bottom: 2px solid #3a3a3a; border-radius: 4px;
              padding: 1px 6px; font-size: 11px; }
#welcomeKeyText { color: %7; }
)QSS";
    return s.arg(Chrome.name(), Editor.name(), Surface.name(), Border.name(), Accent.name(), Text.name(), Muted.name(), Dim.name());
}

void apply() {
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, Chrome);
    p.setColor(QPalette::WindowText, Text);
    p.setColor(QPalette::Base, Editor);
    p.setColor(QPalette::AlternateBase, Chrome);
    p.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
    p.setColor(QPalette::Button, QColor(0x2d, 0x2d, 0x2d));
    p.setColor(QPalette::ButtonText, Text);
    p.setColor(QPalette::Highlight, QColor(0x26, 0x4f, 0x78));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(0x25, 0x25, 0x26));
    p.setColor(QPalette::ToolTipText, Text);
    p.setColor(QPalette::PlaceholderText, Dim);
    p.setColor(QPalette::Link, Accent);
    p.setColor(QPalette::Light, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::Mid, QColor(0x30, 0x30, 0x30));
    p.setColor(QPalette::Dark, QColor(0x12, 0x12, 0x12));
    p.setColor(QPalette::Disabled, QPalette::Text, Dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, Dim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, Dim);
    qApp->setPalette(p);
    qApp->setFont(uiFont());
    qApp->setStyleSheet(styleSheet());
}

}  // namespace Theme

// Icons.h - every icon DevPad uses, drawn with QPainter (no image files, sharp at any screen scale).
//
//   Icons::get(Icons::Search)       line icons for toolbars and buttons (dim normally, bright on
//                                   hover / when checked), coloured by the current theme
//   Icons::forFile("main.cpp")      coloured badge per file type, used in the Explorer and on tabs
//   Icons::folder(open)             folder, closed or open
//   Icons::app()                    the application icon
//
// Call them from the GUI thread only (they build QPixmaps); results are cached.
#pragma once
#include <QColor>
#include <QIcon>
#include <QString>
#include "Outline.h"

namespace Icons {

enum Glyph {
    Files, Search, Palette, Keyboard,                   // activity bar
    NewFile, NewFolder, Refresh, Collapse,              // explorer toolbar
    Close, ChevronRight, ChevronDown, ArrowUp, ArrowDown,
    CaseSensitive, WholeWord,
    Check, Warning, Folder, FolderOpen, File, OpenFile,
    Terminal, Gear, Trash, Stop, Editor, Brush, Plus, Play,     // terminal panel, settings window
    Shield, Lock                                                // Restricted Mode (folder trust)
};

// A line icon in the colours of the current theme (Theme::IconNormal at rest, Theme::IconActive when hovered
// or checked). It is drawn on demand, so it follows a theme change without being rebuilt.
QIcon get(Glyph g);

// A line icon in fixed colours. 'normal' is the colour at rest; hovered and checked states use 'active'.
QIcon get(Glyph g, QColor normal, QColor active = QColor(0xe8, 0xe8, 0xe8));

// Coloured file-type badge ("++" for C++, "H" for headers, "{}" for JSON, ...).
QIcon forFile(const QString& fileName);

// Filled folder icon.
QIcon folder(bool open);

// Badge for an Outline symbol: N namespace, C class, S struct, U union, E enum, f function (engine SymbolKind).
QIcon forSymbol(SymbolKind kind);

// The DevPad application icon (rounded gradient square with braces).
QIcon app();
QPixmap appPixmap(int size);

}  // namespace Icons

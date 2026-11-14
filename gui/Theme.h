// Theme.h - colours for the editor, the syntax highlighter and the application chrome.
#pragma once
#include <QColor>
#include <QPalette>
#include <QString>

struct Theme {
    QString name;
    bool dark = true;
    QColor bg, fg, gutterBg, gutterFg, gutterActive, curLine, selection;
    QColor bracketMatch, error, warning, findMatch, findCurrent;
    QColor keyword, type, string, comment, number, preproc, function;
    QColor chromeBg, chromeFg, accent, popupBg, popupSel;

    static Theme dark_() {
        Theme t;
        t.name = "Midnight"; t.dark = true;
        t.bg = "#1e2127"; t.fg = "#d7dae0"; t.gutterBg = "#1e2127"; t.gutterFg = "#5c6370"; t.gutterActive = "#c8ccd4";
        t.curLine = "#262a33"; t.selection = "#3b4f7a"; t.bracketMatch = "#4b5d3a"; t.error = "#f0524f"; t.warning = "#e5c07b";
        t.findMatch = "#5a4a1a"; t.findCurrent = "#8a6d1f";
        t.keyword = "#c678dd"; t.type = "#e5c07b"; t.string = "#98c379"; t.comment = "#7f8794"; t.number = "#d19a66";
        t.preproc = "#56b6c2"; t.function = "#61afef";
        t.chromeBg = "#21252b"; t.chromeFg = "#d7dae0"; t.accent = "#4d78cc"; t.popupBg = "#2b303b"; t.popupSel = "#3b4f7a";
        return t;
    }
    static Theme light_() {
        Theme t;
        t.name = "Daylight"; t.dark = false;
        t.bg = "#fbfbfd"; t.fg = "#24292f"; t.gutterBg = "#f3f4f7"; t.gutterFg = "#9aa1ad"; t.gutterActive = "#24292f";
        t.curLine = "#eef2fb"; t.selection = "#b6cdf5"; t.bracketMatch = "#cfe8c4"; t.error = "#d1242f"; t.warning = "#b08800";
        t.findMatch = "#fff1a8"; t.findCurrent = "#ffd33d";
        t.keyword = "#a626a4"; t.type = "#986801"; t.string = "#2e7d32"; t.comment = "#8a919d"; t.number = "#b35900";
        t.preproc = "#0b7285"; t.function = "#1a5fb4";
        t.chromeBg = "#eceef2"; t.chromeFg = "#24292f"; t.accent = "#2f6fdd"; t.popupBg = "#ffffff"; t.popupSel = "#cfe0fb";
        return t;
    }
    static Theme byName(const QString& n) { return n == "Daylight" ? light_() : dark_(); }

    QPalette palette() const {
        QPalette p;
        p.setColor(QPalette::Window, chromeBg);
        p.setColor(QPalette::WindowText, chromeFg);
        p.setColor(QPalette::Base, bg);
        p.setColor(QPalette::AlternateBase, curLine);
        p.setColor(QPalette::Text, fg);
        p.setColor(QPalette::Button, chromeBg);
        p.setColor(QPalette::ButtonText, chromeFg);
        p.setColor(QPalette::ToolTipBase, popupBg);
        p.setColor(QPalette::ToolTipText, fg);
        p.setColor(QPalette::Highlight, accent);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, gutterFg);
        p.setColor(QPalette::Disabled, QPalette::Text, gutterFg);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, gutterFg);
        p.setColor(QPalette::Disabled, QPalette::WindowText, gutterFg);
        return p;
    }
};

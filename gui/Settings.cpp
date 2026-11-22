#include "Settings.h"

#include <QSettings>
#include <algorithm>
#include "Theme.h"

bool AppSettings::Values::operator==(const Values& o) const {
    return fontSize == o.fontSize && fontFamily == o.fontFamily && tabWidth == o.tabWidth && autoPairs == o.autoPairs &&
           wrapSelection == o.wrapSelection && autoComplete == o.autoComplete && cursorStyle == o.cursorStyle &&
           cursorBlink == o.cursorBlink && theme == o.theme && accent == o.accent && lineNumbers == o.lineNumbers &&
           indentGuides == o.indentGuides && highlightLine == o.highlightLine && matchBrackets == o.matchBrackets &&
           statusBar == o.statusBar && terminalFontSize == o.terminalFontSize && terminalShell == o.terminalShell &&
           terminalScrollback == o.terminalScrollback && restoreSession == o.restoreSession && autoSave == o.autoSave &&
           autoSaveDelay == o.autoSaveDelay && wordWrap == o.wordWrap && breadcrumbs == o.breadcrumbs;
}

static const AppSettings::Accent kAccents[] = {
    {"blue", "Blue", 0x4f8cff},   {"violet", "Violet", 0x9b7bff}, {"teal", "Teal", 0x2fc4b2},
    {"green", "Green", 0x5fbf6a}, {"orange", "Orange", 0xf0913a}, {"pink", "Pink", 0xf06292},
};

const AppSettings::Accent* AppSettings::accents(int* count) {
    if (count) *count = static_cast<int>(sizeof(kAccents) / sizeof(kAccents[0]));
    return kAccents;
}

unsigned AppSettings::accentRgb(const QString& key) {
    for (const Accent& a : kAccents)
        if (key == QLatin1String(a.key)) return a.rgb;
    return kAccents[0].rgb;
}

AppSettings::Values AppSettings::sanitized(Values v) {
    v.fontSize = std::max(7, std::min(36, v.fontSize));
    if (v.tabWidth != 2 && v.tabWidth != 4 && v.tabWidth != 8) v.tabWidth = 4;
    v.cursorStyle = std::max(0, std::min(2, v.cursorStyle));
    v.autoSaveDelay = std::max(1, std::min(300, v.autoSaveDelay));
    v.terminalFontSize = std::max(7, std::min(30, v.terminalFontSize));
    v.terminalScrollback = std::max(200, std::min(100000, v.terminalScrollback));
    bool known = false;
    for (const Accent& a : kAccents) known = known || v.accent == QLatin1String(a.key);
    if (!known) v.accent = QStringLiteral("blue");
    if (!Theme::isKnownScheme(v.theme)) v.theme = Theme::defaultScheme();
    v.fontFamily = v.fontFamily.trimmed();
    v.terminalShell = v.terminalShell.trimmed();
    return v;
}

AppSettings& AppSettings::instance() {
    static AppSettings s;
    return s;
}

AppSettings::AppSettings() { load(); }

void AppSettings::reload() {
    const Values old = v_;
    load();
    if (v_ != old) emit changed();
}

void AppSettings::load() {
    QSettings s;
    s.beginGroup("prefs");
    Values d, v;
    v.fontSize = s.value("fontSize", d.fontSize).toInt();
    v.fontFamily = s.value("fontFamily", d.fontFamily).toString();
    v.tabWidth = s.value("tabWidth", d.tabWidth).toInt();
    v.autoPairs = s.value("autoPairs", d.autoPairs).toBool();
    v.wrapSelection = s.value("wrapSelection", d.wrapSelection).toBool();
    v.autoComplete = s.value("autoComplete", d.autoComplete).toBool();
    v.cursorStyle = s.value("cursorStyle", d.cursorStyle).toInt();
    v.cursorBlink = s.value("cursorBlink", d.cursorBlink).toBool();
    v.theme = s.value("theme", d.theme).toString();
    v.accent = s.value("accent", d.accent).toString();
    v.lineNumbers = s.value("lineNumbers", d.lineNumbers).toBool();
    v.indentGuides = s.value("indentGuides", d.indentGuides).toBool();
    v.highlightLine = s.value("highlightLine", d.highlightLine).toBool();
    v.matchBrackets = s.value("matchBrackets", d.matchBrackets).toBool();
    v.statusBar = s.value("statusBar", d.statusBar).toBool();
    v.wordWrap = s.value("wordWrap", d.wordWrap).toBool();
    v.breadcrumbs = s.value("breadcrumbs", d.breadcrumbs).toBool();
    v.terminalFontSize = s.value("terminalFontSize", d.terminalFontSize).toInt();
    v.terminalShell = s.value("terminalShell", d.terminalShell).toString();
    v.terminalScrollback = s.value("terminalScrollback", d.terminalScrollback).toInt();
    v.restoreSession = s.value("restoreSession", d.restoreSession).toBool();
    v.autoSave = s.value("autoSave", d.autoSave).toBool();
    v.autoSaveDelay = s.value("autoSaveDelay", d.autoSaveDelay).toInt();
    v_ = sanitized(v);
}

void AppSettings::save() const {
    QSettings s;
    s.beginGroup("prefs");
    s.setValue("fontSize", v_.fontSize);
    s.setValue("fontFamily", v_.fontFamily);
    s.setValue("tabWidth", v_.tabWidth);
    s.setValue("autoPairs", v_.autoPairs);
    s.setValue("wrapSelection", v_.wrapSelection);
    s.setValue("autoComplete", v_.autoComplete);
    s.setValue("cursorStyle", v_.cursorStyle);
    s.setValue("cursorBlink", v_.cursorBlink);
    s.setValue("theme", v_.theme);
    s.setValue("accent", v_.accent);
    s.setValue("lineNumbers", v_.lineNumbers);
    s.setValue("indentGuides", v_.indentGuides);
    s.setValue("highlightLine", v_.highlightLine);
    s.setValue("matchBrackets", v_.matchBrackets);
    s.setValue("statusBar", v_.statusBar);
    s.setValue("wordWrap", v_.wordWrap);
    s.setValue("breadcrumbs", v_.breadcrumbs);
    s.setValue("terminalFontSize", v_.terminalFontSize);
    s.setValue("terminalShell", v_.terminalShell);
    s.setValue("terminalScrollback", v_.terminalScrollback);
    s.setValue("restoreSession", v_.restoreSession);
    s.setValue("autoSave", v_.autoSave);
    s.setValue("autoSaveDelay", v_.autoSaveDelay);
}

void AppSettings::apply(const Values& in) {
    const Values v = sanitized(in);
    if (v == v_) return;
    v_ = v;
    save();
    emit changed();
}

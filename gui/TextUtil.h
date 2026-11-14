// TextUtil.h - glue between Qt strings (UTF-16, QString) and the engine (bytes, std::string).
//
// The engine counts columns in BYTES (UTF-8). Qt counts them in UTF-16 units. For plain
// ASCII they are equal; for "é" (2 bytes) or an emoji (4 bytes, 2 UTF-16 units) they are not.
// Every conversion between the two worlds lives here so the rule is in one place.
#pragma once
#include <QChar>
#include <QString>
#include <string>
#include "Document.h"

namespace TextUtil {

// UTF-8 size of s[from, to) in bytes. A lone surrogate is encoded by Qt as U+FFFD (3 bytes).
inline int utf8Size(const QString& s, int from, int to) {
    int n = 0;
    for (int i = from; i < to; ++i) {
        const ushort u = s.at(i).unicode();
        if (u < 0x80) n += 1;
        else if (u < 0x800) n += 2;
        else if (QChar::isHighSurrogate(u) && i + 1 < to && QChar::isLowSurrogate(s.at(i + 1).unicode())) { n += 4; ++i; }
        else n += 3;
    }
    return n;
}

// Byte column -> UTF-16 column inside one line.
inline int byteToU16(const QString& line, size_t byteCol) {
    size_t bytes = 0;
    int i = 0;
    while (i < line.size() && bytes < byteCol) {
        const ushort u = line.at(i).unicode();
        if (u < 0x80) bytes += 1;
        else if (u < 0x800) bytes += 2;
        else if (QChar::isHighSurrogate(u) && i + 1 < line.size() && QChar::isLowSurrogate(line.at(i + 1).unicode())) { bytes += 4; ++i; }
        else bytes += 3;
        ++i;
    }
    return i;
}

inline size_t u16ToByte(const QString& line, int col16) { return static_cast<size_t>(utf8Size(line, 0, col16)); }

inline std::string toStd(const QString& s) { return s.toUtf8().toStdString(); }
inline QString fromStd(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<int>(s.size())); }

// The text of a Document the way the widget shows it: lines joined by '\n', no trailing '\n'.
inline QString documentText(const Document& d) {
    std::string s = d.toString();
    if (!s.empty() && s[s.size() - 1] == '\n') s.erase(s.size() - 1);
    return fromStd(s);
}

// Walk 'from' forward over s[begin, end) (the '\n' character starts a new line).
inline Pos advancePos(Pos from, const QString& s, int begin, int end) {
    Pos p = from;
    for (int i = begin; i < end; ++i) {
        const QChar c = s.at(i);
        if (c == QChar('\n')) { ++p.line; p.col = 0; continue; }
        const ushort u = c.unicode();
        if (u < 0x80) p.col += 1;
        else if (u < 0x800) p.col += 2;
        else if (QChar::isHighSurrogate(u) && i + 1 < end && QChar::isLowSurrogate(s.at(i + 1).unicode())) { p.col += 4; ++i; }
        else p.col += 3;
    }
    return p;
}

// Make pasted / dropped / loaded text safe for the engine: one kind of line break ('\n'),
// no NUL, no unpaired surrogates (they become U+FFFD).
inline QString sanitize(const QString& in) {
    QString out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        const ushort u = c.unicode();
        if (u == '\r') { if (i + 1 < in.size() && in.at(i + 1) == QChar('\n')) ++i; out.append('\n'); }
        else if (u == 0x2028 || u == 0x2029) out.append('\n');
        else if (u == 0) continue;
        else if (QChar::isHighSurrogate(u)) {
            if (i + 1 < in.size() && QChar::isLowSurrogate(in.at(i + 1).unicode())) { out.append(c); out.append(in.at(++i)); }
            else out.append(QChar(0xFFFD));
        } else if (QChar::isLowSurrogate(u)) out.append(QChar(0xFFFD));
        else out.append(c);
    }
    return out;
}

// Common prefix / suffix of two strings that never cuts a surrogate pair in half.
inline void commonAffixes(const QString& a, const QString& b, int& prefix, int& suffix) {
    const int m = qMin(a.size(), b.size());
    int p = 0;
    while (p < m && a.at(p) == b.at(p)) ++p;
    if (p > 0 && QChar::isHighSurrogate(a.at(p - 1).unicode())) --p;
    int s = 0;
    const int ms = m - p;
    while (s < ms && a.at(a.size() - 1 - s) == b.at(b.size() - 1 - s)) ++s;
    if (s > 0 && QChar::isLowSurrogate(a.at(a.size() - s).unicode())) --s;
    prefix = p;
    suffix = s;
}

}  // namespace TextUtil

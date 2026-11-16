#include "Icons.h"

#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace Icons {
namespace {

const int kSizes[] = {16, 20, 24, 32, 48};

// All glyphs are designed on a 24 x 24 grid and scaled to the requested size.
QPixmap canvas(int size, qreal dpr) {
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    return pm;
}

QFont boldFont(qreal pixelSize) {
    QFont f;
    f.setFamilies({"Segoe UI", "Inter", "Helvetica Neue", "Arial", "DejaVu Sans"});
    f.setPixelSize(qMax(1, int(pixelSize + 0.5)));
    f.setWeight(QFont::Bold);
    return f;
}

// text centred in a box, as a path (crisp, and the same on every system)
void drawText(QPainter& p, const QString& text, const QRectF& box, qreal px, const QColor& c, bool bold = true) {
    QFont f = boldFont(px);
    if (!bold) f.setWeight(QFont::DemiBold);
    QPainterPath path;
    path.addText(0, 0, f, text);
    const QRectF r = path.boundingRect();
    p.save();
    p.translate(box.center().x() - r.center().x(), box.center().y() - r.center().y());
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(path);
    p.restore();
}

void paintGlyph(QPainter& p, Glyph g, const QColor& c) {
    QPen pen(c, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
    auto plus = [&](qreal x, qreal y, qreal r) { line(x - r, y, x + r, y); line(x, y - r, x, y + r); };
    QPainterPath path;
    switch (g) {
        case Files:       // two pages, the front one with a folded corner
            path.moveTo(9, 3.5); path.lineTo(15, 3.5); path.lineTo(19.5, 8); path.lineTo(19.5, 17.5); path.lineTo(9, 17.5); path.closeSubpath();
            path.moveTo(15, 3.5); path.lineTo(15, 8); path.lineTo(19.5, 8);
            path.moveTo(6, 7); path.lineTo(5, 7); path.lineTo(5, 21); path.lineTo(15.5, 21); path.lineTo(15.5, 20.5);
            p.drawPath(path);
            break;
        case Search:
            p.drawEllipse(QPointF(10.5, 10.5), 6, 6);
            line(15, 15, 20, 20);
            break;
        case Palette:     // command palette: window with a prompt
            p.drawRoundedRect(QRectF(3, 4.5, 18, 15), 3, 3);
            path.moveTo(7, 10); path.lineTo(9.5, 12.25); path.lineTo(7, 14.5);
            p.drawPath(path);
            line(12, 14.5, 16.5, 14.5);
            break;
        case Keyboard:
            p.drawRoundedRect(QRectF(2.5, 6, 19, 12), 2.5, 2.5);
            for (int i = 0; i < 4; ++i) line(6 + i * 4, 10, 6.01 + i * 4, 10);
            for (int i = 0; i < 3; ++i) line(8 + i * 4, 13, 8.01 + i * 4, 13);
            line(8, 15.5, 16, 15.5);
            pen.setWidthF(2.0); p.setPen(pen);
            for (int i = 0; i < 4; ++i) line(6 + i * 4, 10, 6.01 + i * 4, 10);
            for (int i = 0; i < 3; ++i) line(8 + i * 4, 13, 8.01 + i * 4, 13);
            break;
        case File:
        case NewFile:
        case OpenFile:
            path.moveTo(g == NewFile ? 12 : 18.5, 20.5);
            path.lineTo(5.5, 20.5); path.lineTo(5.5, 3.5); path.lineTo(13, 3.5); path.lineTo(18.5, 9); path.lineTo(18.5, g == NewFile ? 12.5 : 20.5);
            path.moveTo(13, 3.5); path.lineTo(13, 9); path.lineTo(18.5, 9);
            p.drawPath(path);
            if (g == NewFile) plus(17.5, 17.5, 3.5);
            if (g == OpenFile) { line(9, 13.5, 15, 13.5); line(9, 16.5, 13, 16.5); }
            break;
        case NewFolder:
        case Folder:
            path.moveTo(g == NewFolder ? 12 : 21, 19);
            path.lineTo(3, 19); path.lineTo(3, 5); path.lineTo(9, 5); path.lineTo(11, 7.5); path.lineTo(21, 7.5); path.lineTo(21, g == NewFolder ? 12 : 19);
            p.drawPath(path);
            if (g == NewFolder) plus(17.5, 17.5, 3.5);
            break;
        case FolderOpen:
            path.moveTo(3, 19); path.lineTo(3, 5); path.lineTo(9, 5); path.lineTo(11, 7.5); path.lineTo(19, 7.5); path.lineTo(19, 10);
            path.moveTo(3, 19); path.lineTo(6.5, 10.5); path.lineTo(22, 10.5); path.lineTo(18.5, 19); path.closeSubpath();
            p.drawPath(path);
            break;
        case Refresh: {
            p.drawArc(QRectF(4.5, 4.5, 15, 15), 100 * 16, 280 * 16);
            path.moveTo(13.6, 1.8); path.lineTo(10.6, 4.6); path.lineTo(13.6, 7.4);
            p.drawPath(path);
            break;
        }
        case Collapse:
            p.drawRoundedRect(QRectF(7.5, 7.5, 13, 13), 2, 2);
            line(11, 14, 17, 14);
            path.moveTo(4, 16.5); path.lineTo(4, 5.5); path.quadTo(4, 4, 5.5, 4); path.lineTo(16.5, 4);
            p.drawPath(path);
            break;
        case Close:
            line(7, 7, 17, 17);
            line(17, 7, 7, 17);
            break;
        case ChevronRight:
            path.moveTo(9.5, 6.5); path.lineTo(15, 12); path.lineTo(9.5, 17.5);
            p.drawPath(path);
            break;
        case ChevronDown:
            path.moveTo(6.5, 9.5); path.lineTo(12, 15); path.lineTo(17.5, 9.5);
            p.drawPath(path);
            break;
        case ArrowUp:
            line(12, 19, 12, 5.5);
            path.moveTo(6.5, 11); path.lineTo(12, 5.5); path.lineTo(17.5, 11);
            p.drawPath(path);
            break;
        case ArrowDown:
            line(12, 5, 12, 18.5);
            path.moveTo(6.5, 13); path.lineTo(12, 18.5); path.lineTo(17.5, 13);
            p.drawPath(path);
            break;
        case CaseSensitive:
            drawText(p, "Aa", QRectF(0, 1, 24, 22), 13, c);
            break;
        case WholeWord:
            drawText(p, "ab", QRectF(0, -1, 24, 20), 12.5, c);
            line(4, 19, 20, 19);
            line(4, 16.5, 4, 19);
            line(20, 16.5, 20, 19);
            break;
        case Check:
            path.moveTo(5, 12.5); path.lineTo(10, 17.5); path.lineTo(19, 7);
            p.drawPath(path);
            break;
        case Warning:
            path.moveTo(12, 3.5); path.lineTo(21.5, 20); path.lineTo(2.5, 20); path.closeSubpath();
            p.drawPath(path);
            line(12, 9.5, 12, 14);
            line(12, 17, 12, 17.01);
            break;
    }
}

QPixmap renderGlyph(Glyph g, int size, const QColor& c, qreal dpr) {
    QPixmap pm = canvas(size, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    paintGlyph(p, g, c);
    return pm;
}

struct Badge {
    QString text;       // empty: a small picture instead of letters
    QColor bg;
    QColor fg;
    int kind;           // 0 letters, 1 text lines, 2 picture, 3 plain document
};

Badge badgeFor(const QString& fileName) {
    const QString lower = fileName.toLower();
    const QString ext = QFileInfo(lower).suffix();
    const QColor white(Qt::white), ink(0x1e, 0x1e, 0x1e);
    if (lower == "cmakelists.txt" || ext == "cmake") return {"CM", QColor(0x16, 0xa3, 0x4a), white, 0};
    if (lower.startsWith(".git")) return {"G", QColor(0xf0, 0x50, 0x32), white, 0};
    if (lower == "makefile") return {"M", QColor(0x6b, 0x72, 0x80), white, 0};
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c++") return {"++", QColor(0x3b, 0x82, 0xf6), white, 0};
    if (ext == "c") return {"C", QColor(0x63, 0x66, 0xf1), white, 0};
    if (ext == "h" || ext == "hpp" || ext == "hh" || ext == "hxx") return {"H", QColor(0xa8, 0x55, 0xf7), white, 0};
    if (ext == "py") return {"Py", QColor(0xfa, 0xcc, 0x15), ink, 0};
    if (ext == "js" || ext == "mjs" || ext == "cjs") return {"JS", QColor(0xf7, 0xdf, 0x1e), ink, 0};
    if (ext == "ts" || ext == "tsx") return {"TS", QColor(0x31, 0x78, 0xc6), white, 0};
    if (ext == "json") return {"{}", QColor(0xf5, 0x9e, 0x0b), ink, 0};
    if (ext == "md" || ext == "markdown") return {"M", QColor(0x0e, 0xa5, 0xe9), white, 0};
    if (ext == "html" || ext == "htm" || ext == "xml" || ext == "ui") return {"<>", QColor(0xe3, 0x4c, 0x26), white, 0};
    if (ext == "css" || ext == "scss" || ext == "qss") return {"#", QColor(0x26, 0x65, 0xf1), white, 0};
    if (ext == "java") return {"J", QColor(0xf8, 0x98, 0x20), white, 0};
    if (ext == "cs") return {"C#", QColor(0x7c, 0x3a, 0xed), white, 0};
    if (ext == "rs") return {"R", QColor(0xde, 0xa5, 0x84), ink, 0};
    if (ext == "go") return {"Go", QColor(0x00, 0xad, 0xd8), white, 0};
    if (ext == "sh" || ext == "bat" || ext == "cmd" || ext == "ps1") return {">_", QColor(0x22, 0xc5, 0x5e), ink, 0};
    if (ext == "yml" || ext == "yaml") return {"Y", QColor(0xcb, 0x17, 0x1e), white, 0};
    if (ext == "toml" || ext == "ini" || ext == "cfg" || ext == "conf") return {QString(), QColor(0x64, 0x74, 0x8b), white, 1};
    if (ext == "pdf") return {"P", QColor(0xef, 0x44, 0x44), white, 0};
    if (ext == "zip" || ext == "7z" || ext == "rar" || ext == "gz" || ext == "tar") return {"Z", QColor(0x78, 0x71, 0x6c), white, 0};
    if (ext == "exe" || ext == "dll" || ext == "o" || ext == "obj" || ext == "a" || ext == "lib" || ext == "so")
        return {"01", QColor(0x52, 0x52, 0x5b), white, 0};
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "bmp" || ext == "ico" || ext == "svg" || ext == "webp")
        return {QString(), QColor(0x14, 0xb8, 0xa6), white, 2};
    if (ext == "txt" || ext == "log" || ext == "csv") return {QString(), QColor(0x8b, 0x94, 0x9e), white, 1};
    return {QString(), QColor(0x8b, 0x94, 0x9e), white, 3};
}

QPixmap renderBadge(const Badge& b, int size, qreal dpr) {
    QPixmap pm = canvas(size, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    if (b.kind == 3) {                                   // plain document outline
        paintGlyph(p, File, b.bg);
        return pm;
    }
    const QRectF box(3, 3, 18, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(b.bg);
    p.drawRoundedRect(box, 4.5, 4.5);
    if (b.kind == 0) {
        const int n = b.text.size();
        const qreal px = n <= 1 ? 12.5 : (n == 2 ? 10.5 : 8);
        drawText(p, b.text, box.adjusted(0, 0.5, 0, 0), px, b.fg);
    } else if (b.kind == 1) {                            // text lines
        QPen pen(b.fg, 1.8);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(QPointF(7, 8.5), QPointF(17, 8.5));
        p.drawLine(QPointF(7, 12), QPointF(17, 12));
        p.drawLine(QPointF(7, 15.5), QPointF(13, 15.5));
    } else {                                             // picture: sun and mountains
        p.setBrush(b.fg);
        p.drawEllipse(QPointF(15.5, 8.5), 2, 2);
        QPainterPath m;
        m.moveTo(5, 18); m.lineTo(10, 11); m.lineTo(13.5, 15.5); m.lineTo(15.5, 13); m.lineTo(19, 18); m.closeSubpath();
        p.drawPath(m);
    }
    return pm;
}

QPixmap renderFolder(bool open, int size, qreal dpr) {
    QPixmap pm = canvas(size, dpr);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    const QColor back(0xc9, 0x92, 0x2e), front(0xe8, 0xb3, 0x4b);
    QPainterPath b;
    b.moveTo(2.5, 6); b.quadTo(2.5, 4, 4.5, 4); b.lineTo(9, 4); b.lineTo(11, 6.5); b.lineTo(19.5, 6.5);
    b.quadTo(21.5, 6.5, 21.5, 8.5); b.lineTo(21.5, 18); b.quadTo(21.5, 20, 19.5, 20); b.lineTo(4.5, 20); b.quadTo(2.5, 20, 2.5, 18);
    b.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(back);
    p.drawPath(b);
    QPainterPath f;
    if (open) {
        f.moveTo(5.5, 10); f.lineTo(23, 10); f.lineTo(20, 20); f.lineTo(2.5, 20); f.closeSubpath();
    } else {
        f.addRoundedRect(QRectF(2.5, 8.5, 19, 11.5), 2, 2);
    }
    p.setBrush(front);
    p.drawPath(f);
    return pm;
}

QColor shade(const QColor& c, int alpha) { QColor x = c; x.setAlpha(alpha); return x; }

}  // namespace

QIcon get(Glyph g, QColor normal, QColor active) {
    static QHash<QString, QIcon> cache;
    const QString key = QString("%1/%2/%3").arg(int(g)).arg(normal.name(QColor::HexArgb), active.name(QColor::HexArgb));
    auto it = cache.find(key);
    if (it != cache.end()) return *it;
    QIcon icon;
    for (int s : kSizes) {
        for (qreal dpr : {1.0, 2.0}) {
            icon.addPixmap(renderGlyph(g, s, normal, dpr), QIcon::Normal, QIcon::Off);
            icon.addPixmap(renderGlyph(g, s, active, dpr), QIcon::Active, QIcon::Off);
            icon.addPixmap(renderGlyph(g, s, active, dpr), QIcon::Normal, QIcon::On);
            icon.addPixmap(renderGlyph(g, s, active, dpr), QIcon::Active, QIcon::On);
            icon.addPixmap(renderGlyph(g, s, active, dpr), QIcon::Selected, QIcon::Off);
            icon.addPixmap(renderGlyph(g, s, shade(normal, 90), dpr), QIcon::Disabled, QIcon::Off);
        }
    }
    cache.insert(key, icon);
    return icon;
}

QIcon forFile(const QString& fileName) {
    static QHash<QString, QIcon> cache;
    const Badge b = badgeFor(QFileInfo(fileName).fileName());
    const QString key = b.text + "|" + b.bg.name() + "|" + QString::number(b.kind);
    auto it = cache.find(key);
    if (it != cache.end()) return *it;
    QIcon icon;
    for (int s : kSizes)
        for (qreal dpr : {1.0, 2.0}) {
            const QPixmap pm = renderBadge(b, s, dpr);
            icon.addPixmap(pm, QIcon::Normal);
            icon.addPixmap(pm, QIcon::Selected);
            icon.addPixmap(pm, QIcon::Active);
        }
    cache.insert(key, icon);
    return icon;
}

QIcon folder(bool open) {
    static QIcon cache[2];
    QIcon& icon = cache[open ? 1 : 0];
    if (!icon.isNull()) return icon;
    for (int s : kSizes)
        for (qreal dpr : {1.0, 2.0}) {
            const QPixmap pm = renderFolder(open, s, dpr);
            icon.addPixmap(pm, QIcon::Normal);
            icon.addPixmap(pm, QIcon::Selected);
            icon.addPixmap(pm, QIcon::Active);
        }
    return icon;
}

QPixmap appPixmap(int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size;
    const QRectF box(s * 0.04, s * 0.04, s * 0.92, s * 0.92);
    QLinearGradient grad(box.topLeft(), box.bottomRight());
    grad.setColorAt(0.0, QColor(0x4f, 0x8c, 0xff));
    grad.setColorAt(1.0, QColor(0x7c, 0x4d, 0xff));
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRoundedRect(box, s * 0.22, s * 0.22);
    // soft highlight on the upper half
    QLinearGradient shine(box.topLeft(), QPointF(box.left(), box.center().y()));
    shine.setColorAt(0.0, QColor(255, 255, 255, 46));
    shine.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(shine);
    p.drawRoundedRect(box, s * 0.22, s * 0.22);
    // braces, drawn as strokes
    QPen pen(Qt::white, s * 0.075);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto brace = [&](bool left) {
        const qreal x = left ? s * 0.33 : s * 0.67, d = left ? 1 : -1;
        const qreal top = s * 0.28, mid = s * 0.5, bot = s * 0.72, w = s * 0.08;
        QPainterPath b;
        b.moveTo(x + d * w, top);
        b.quadTo(x, top, x, top + s * 0.07);
        b.lineTo(x, mid - s * 0.06);
        b.quadTo(x, mid, x - d * w, mid);
        b.quadTo(x, mid, x, mid + s * 0.06);
        b.lineTo(x, bot - s * 0.07);
        b.quadTo(x, bot, x + d * w, bot);
        p.drawPath(b);
    };
    brace(true);
    brace(false);
    // the cursor between them
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 220));
    if (size >= 32) p.drawRoundedRect(QRectF(s * 0.475, s * 0.38, s * 0.05, s * 0.24), s * 0.025, s * 0.025);
    return pm;
}

QIcon app() {
    static QIcon icon;
    if (icon.isNull())
        for (int s : {16, 24, 32, 48, 64, 128, 256}) icon.addPixmap(appPixmap(s));
    return icon;
}

}  // namespace Icons

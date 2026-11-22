#include "Breadcrumbs.h"
#include <algorithm>
#include <QMouseEvent>
#include <QPainter>
#include "Theme.h"

namespace {
const int kPad = 6;          // inside a crumb, left and right
const int kIcon = 14;
const int kGap = 4;          // icon to text
const int kSep = 14;         // width of the " › " between crumbs
const int kLeft = 8;
}  // namespace

Breadcrumbs::Breadcrumbs(QWidget* parent) : QWidget(parent) {
    setObjectName("breadcrumbs");
    setMouseTracking(true);
    setFixedHeight(barHeight());
    setCursor(Qt::PointingHandCursor);
}

QString Breadcrumbs::text() const {
    QStringList parts;
    for (const Crumb& c : crumbs_) parts << c.text;
    return parts.join(QStringLiteral(" › "));
}

void Breadcrumbs::setCrumbs(const QVector<Crumb>& crumbs) {
    bool same = crumbs.size() == crumbs_.size();
    for (int i = 0; same && i < crumbs.size(); ++i)
        same = crumbs[i].text == crumbs_[i].text && crumbs[i].kind == crumbs_[i].kind && crumbs[i].symbol == crumbs_[i].symbol &&
               crumbs[i].path == crumbs_[i].path;
    if (same) return;
    crumbs_ = crumbs;
    hover_ = -1;
    layoutCrumbs();
    setToolTip(text());
    update();
}

// Widths from the right: the crumbs nearest the cursor matter most, so leading ones are dropped first.
void Breadcrumbs::layoutCrumbs() {
    rects_.fill(QRect(), crumbs_.size());
    const QFontMetrics fm(font());
    QVector<int> w(crumbs_.size());
    for (int i = 0; i < crumbs_.size(); ++i)
        w[i] = kPad * 2 + (crumbs_[i].icon.isNull() ? 0 : kIcon + kGap) + fm.horizontalAdvance(crumbs_[i].text);
    const int avail = width() - kLeft * 2;
    const int ellipsisW = fm.horizontalAdvance(QStringLiteral("…")) + kSep + kPad;
    int first = 0, total = 0;
    for (int i = 0; i < crumbs_.size(); ++i) total += w[i] + (i ? kSep : 0);
    while (first < crumbs_.size() - 1 && total + (first ? ellipsisW : 0) > avail) {
        total -= w[first] + kSep;
        ++first;
    }
    elided_ = first > 0;
    int x = kLeft + (elided_ ? ellipsisW : 0);
    const int h = height() - 4;
    for (int i = first; i < crumbs_.size(); ++i) {
        if (i > first) x += kSep;
        rects_[i] = QRect(x, 2, std::min(w[i], std::max(20, width() - x - kLeft)), h);
        x += w[i];
    }
}

void Breadcrumbs::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    layoutCrumbs();
}

int Breadcrumbs::crumbAt(const QPoint& p) const {
    for (int i = 0; i < rects_.size(); ++i)
        if (!rects_[i].isNull() && rects_[i].contains(p)) return i;
    return -1;
}

void Breadcrumbs::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::Editor);
    const QFontMetrics fm(font());
    p.setRenderHint(QPainter::Antialiasing, true);
    int firstShown = -1;
    for (int i = 0; i < rects_.size(); ++i) {
        const QRect r = rects_[i];
        if (r.isNull()) continue;
        if (firstShown < 0) firstShown = i;
        if (i > firstShown) {                                   // separator before this crumb
            p.setPen(Theme::Dim);
            p.drawText(QRect(r.left() - kSep, r.top(), kSep, r.height()), Qt::AlignCenter, QStringLiteral("›"));
        }
        if (i == hover_) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::RowHover);
            p.drawRoundedRect(r, 4, 4);
        }
        int x = r.left() + kPad;
        if (!crumbs_[i].icon.isNull()) {
            crumbs_[i].icon.paint(&p, QRect(x, r.top() + (r.height() - kIcon) / 2, kIcon, kIcon));
            x += kIcon + kGap;
        }
        const bool last = i == crumbs_.size() - 1;
        p.setPen(i == hover_ || last ? Theme::Text : Theme::Muted);
        const QRect tr(x, r.top(), r.right() - x - kPad + 1, r.height());
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(crumbs_[i].text, Qt::ElideRight, tr.width()));
    }
    if (elided_ && firstShown >= 0) {
        p.setPen(Theme::Dim);
        p.drawText(QRect(kLeft, 0, rects_[firstShown].left() - kLeft, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("…  ›"));
    }
    p.setPen(Theme::Border);                                      // a hairline under the bar
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void Breadcrumbs::mouseMoveEvent(QMouseEvent* e) {
    const int h = crumbAt(e->pos());
    if (h != hover_) { hover_ = h; update(); }
}

void Breadcrumbs::leaveEvent(QEvent*) {
    if (hover_ != -1) { hover_ = -1; update(); }
}

void Breadcrumbs::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int i = crumbAt(e->pos());
    if (i >= 0) emit crumbClicked(i, mapToGlobal(QPoint(rects_[i].left(), height())));
}

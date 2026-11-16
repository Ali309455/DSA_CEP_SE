#include "Controls.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include "Theme.h"

// ---------------------------------------------------------------- Switch

Switch::Switch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Switch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(1, 1, width() - 2, height() - 2);
    const bool on = isChecked();
    QColor track = on ? Theme::Accent : QColor(0x3a, 0x3a, 0x3a);
    if (!isEnabled()) track.setAlphaF(0.4);
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    const qreal d = r.height() - 6;
    const qreal x = on ? r.right() - 3 - d : r.left() + 3;
    p.setBrush(on ? QColor(Qt::white) : QColor(0xc8, 0xc8, 0xc8));
    p.drawEllipse(QRectF(x, r.top() + 3, d, d));
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Theme::Accent.lighter(130), 1.5));
        p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), r.height() / 2 + 1, r.height() / 2 + 1);
    }
}

// ---------------------------------------------------------------- Stepper

Stepper::Stepper(int min, int max, int step, const QString& suffix, QWidget* parent)
    : QWidget(parent), min_(min), max_(max), step_(step), value_(min), suffix_(suffix) {
    setObjectName("stepper");
    QHBoxLayout* h = new QHBoxLayout(this);
    h->setContentsMargins(2, 2, 2, 2);
    h->setSpacing(0);
    auto makeButton = [this](const QString& text) {
        QToolButton* b = new QToolButton(this);
        b->setObjectName("stepperButton");
        b->setText(text);
        b->setFixedSize(28, 26);
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoRepeat(true);
        b->setAutoRepeatDelay(350);
        b->setAutoRepeatInterval(60);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    minus_ = makeButton(QString::fromUtf8("\xe2\x88\x92"));     // U+2212 minus
    plus_ = makeButton(QStringLiteral("+"));
    label_ = new QLabel(this);
    label_->setObjectName("stepperValue");
    label_->setAlignment(Qt::AlignCenter);
    label_->setMinimumWidth(52);
    h->addWidget(minus_);
    h->addWidget(label_, 1);
    h->addWidget(plus_);
    connect(minus_, &QToolButton::clicked, this, [this]() { stepBy(-1); });
    connect(plus_, &QToolButton::clicked, this, [this]() { stepBy(+1); });
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    refresh();
}

void Stepper::refresh() {
    label_->setText(QString::number(value_) + suffix_);
    minus_->setEnabled(value_ > min_);
    plus_->setEnabled(value_ < max_);
}

void Stepper::setValue(int v) {
    value_ = std::max(min_, std::min(max_, v));
    refresh();
}

void Stepper::stepBy(int direction) {
    const int before = value_;
    setValue(value_ + direction * step_);
    if (value_ != before) emit valueChanged(value_);
}

void Stepper::wheelEvent(QWheelEvent* e) { e->ignore(); }

// ---------------------------------------------------------------- Segmented

Segmented::Segmented(const QStringList& labels, const QList<int>& values, QWidget* parent) : QWidget(parent), values_(values) {
    setObjectName("segmented");
    QHBoxLayout* h = new QHBoxLayout(this);
    h->setContentsMargins(2, 2, 2, 2);
    h->setSpacing(2);
    QButtonGroup* g = new QButtonGroup(this);
    g->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        QToolButton* b = new QToolButton(this);
        b->setObjectName("segment");
        b->setText(labels[i]);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumSize(44, 26);
        b->setFocusPolicy(Qt::NoFocus);
        g->addButton(b, i);
        h->addWidget(b);
        buttons_.append(b);
    }
    connect(g, &QButtonGroup::idClicked, this, [this](int id) { emit valueChanged(values_.value(id)); });
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

int Segmented::value() const {
    for (int i = 0; i < buttons_.size(); ++i)
        if (buttons_[i]->isChecked()) return values_.value(i);
    return values_.isEmpty() ? 0 : values_.first();
}

void Segmented::setValue(int v) {
    for (int i = 0; i < buttons_.size(); ++i) {
        QSignalBlocker b(buttons_[i]);
        buttons_[i]->setChecked(values_.value(i) == v);
    }
}

void Segmented::choose(int index) {
    if (index < 0 || index >= buttons_.size()) return;
    buttons_[index]->click();
}

// ---------------------------------------------------------------- Swatches

namespace {
class Dot : public QAbstractButton {
public:
    Dot(const QColor& c, QWidget* parent) : QAbstractButton(parent), color_(c) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(26, 26);
        setFocusPolicy(Qt::NoFocus);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, height() / 2.0);
        if (isChecked()) {
            p.setPen(QPen(color_, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, 12, 12);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(underMouse() && !isChecked() ? color_.lighter(115) : color_);
        p.drawEllipse(c, isChecked() ? 8 : 9, isChecked() ? 8 : 9);
        if (isChecked()) {
            QPen tick(QColor(0x14, 0x14, 0x14), 1.8);
            tick.setCapStyle(Qt::RoundCap);
            tick.setJoinStyle(Qt::RoundJoin);
            p.setPen(tick);
            QPainterPath t;
            t.moveTo(c.x() - 3.2, c.y() + 0.2);
            t.lineTo(c.x() - 0.8, c.y() + 2.6);
            t.lineTo(c.x() + 3.4, c.y() - 2.4);
            p.drawPath(t);
        }
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent*) override { update(); }
#else
    void enterEvent(QEvent*) override { update(); }
#endif
    void leaveEvent(QEvent*) override { update(); }
private:
    QColor color_;
};
}  // namespace

Swatches::Swatches(const QList<QColor>& colors, const QStringList& keys, const QStringList& names, QWidget* parent)
    : QWidget(parent), keys_(keys) {
    QHBoxLayout* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    QButtonGroup* g = new QButtonGroup(this);
    g->setExclusive(true);
    for (int i = 0; i < colors.size(); ++i) {
        Dot* d = new Dot(colors[i], this);
        d->setToolTip(names.value(i));
        g->addButton(d, i);
        h->addWidget(d);
        buttons_.append(d);
    }
    connect(g, &QButtonGroup::idClicked, this, [this](int id) { emit valueChanged(keys_.value(id)); });
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QString Swatches::value() const {
    for (int i = 0; i < buttons_.size(); ++i)
        if (buttons_[i]->isChecked()) return keys_.value(i);
    return QString();
}

void Swatches::setValue(const QString& key) {
    for (int i = 0; i < buttons_.size(); ++i) {
        QSignalBlocker b(buttons_[i]);
        buttons_[i]->setChecked(keys_.value(i) == key);
        buttons_[i]->update();
    }
}

void Swatches::choose(const QString& key) {
    const int i = keys_.indexOf(key);
    if (i >= 0) buttons_[i]->click();
}

// ---------------------------------------------------------------- Combo

Combo::Combo(QWidget* parent) : QComboBox(parent) {
    setObjectName("combo");
    setView(new QListView(this));                 // lets the style sheet style the items
    setCursor(Qt::PointingHandCursor);
    setMinimumContentsLength(16);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMaxVisibleItems(12);
}

void Combo::paintEvent(QPaintEvent* e) {
    QComboBox::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(0x9d, 0x9d, 0x9d), 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const qreal cx = width() - 16, cy = height() / 2.0;
    QPainterPath path;
    path.moveTo(cx - 4, cy - 1.6);
    path.lineTo(cx, cy + 2.4);
    path.lineTo(cx + 4, cy - 1.6);
    p.drawPath(path);
}

// Controls.h - the small custom controls of the settings window, drawn in the app's theme:
//   Switch     on / off pill
//   Stepper    [ - ]  value  [ + ] for numbers in a range
//   Segmented  a row of exclusive options ("2 | 4 | 8")
//   Swatches   a row of round colour choices
//   Combo      a drop-down with a drawn chevron
// They only draw and report changes; the settings window decides what a change means.
#pragma once
#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QList>
#include <QWheelEvent>
#include <QStringList>
#include <QWidget>

class QLabel;
class QToolButton;

class Switch : public QAbstractButton {
    Q_OBJECT
public:
    explicit Switch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(38, 22); }

protected:
    void paintEvent(QPaintEvent*) override;
};

class Stepper : public QWidget {
    Q_OBJECT
public:
    Stepper(int min, int max, int step, const QString& suffix, QWidget* parent = nullptr);
    int value() const { return value_; }
    void setValue(int v);                 // clamps; emits nothing (use for syncing from outside)
    void stepBy(int direction);           // what the buttons do; emits valueChanged
signals:
    void valueChanged(int v);

protected:
    void wheelEvent(QWheelEvent*) override;

private:
    int min_, max_, step_, value_;
    QString suffix_;
    QLabel* label_;
    QToolButton* minus_;
    QToolButton* plus_;
    void refresh();
};

class Segmented : public QWidget {
    Q_OBJECT
public:
    // options: text shown; the value reported is the option's index in 'values'
    Segmented(const QStringList& labels, const QList<int>& values, QWidget* parent = nullptr);
    int value() const;
    void setValue(int v);                 // silent
    void choose(int index);               // like a click; emits valueChanged
signals:
    void valueChanged(int v);

private:
    QList<int> values_;
    QList<QAbstractButton*> buttons_;
};

class Swatches : public QWidget {
    Q_OBJECT
public:
    Swatches(const QList<QColor>& colors, const QStringList& keys, const QStringList& names, QWidget* parent = nullptr);
    QString value() const;
    void setValue(const QString& key);    // silent
    void choose(const QString& key);      // like a click; emits valueChanged
signals:
    void valueChanged(const QString& key);

private:
    QList<QAbstractButton*> buttons_;
    QStringList keys_;
};

class Combo : public QComboBox {
    Q_OBJECT
public:
    explicit Combo(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }   // scrolling the page must not change a setting
};

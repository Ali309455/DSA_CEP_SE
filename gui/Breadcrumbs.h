// Breadcrumbs.h - the path bar at the top of the editor:   src › gui › CodeView.cpp › CodeView › paintEvent()
//
// Folders, then the file, then the symbols around the cursor (from engine/Outline.h), outermost first.
// The bar only draws and reports clicks; CodeView fills it and decides what a click does:
//   folder  -> show it in the Explorer      file -> a menu of every symbol in the file
//   symbol  -> jump to it
// When the bar is too narrow, crumbs are dropped from the left and a "…" takes their place.
#pragma once
#include <QIcon>
#include <QVector>
#include <QWidget>

class Breadcrumbs : public QWidget {
    Q_OBJECT
public:
    enum Kind { Folder, File, Symbol };
    struct Crumb {
        QString text;
        QIcon icon;
        Kind kind = Folder;
        QString path;          // Folder: the folder's absolute path
        long symbol = -1;      // Symbol: index into CodeView::symbols()
    };

    explicit Breadcrumbs(QWidget* parent = nullptr);
    void setCrumbs(const QVector<Crumb>& crumbs);          // repaints only if something changed
    const QVector<Crumb>& crumbs() const { return crumbs_; }
    QString text() const;                                  // "a › b.cpp › Foo" (tests, tooltips)
    static int barHeight() { return 24; }

signals:
    void crumbClicked(int index, const QPoint& globalBelow);   // globalBelow: where a menu for it should open

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QVector<Crumb> crumbs_;
    QVector<QRect> rects_;     // rects_[i]: where crumb i is drawn (empty = dropped for lack of room)
    int hover_ = -1;
    bool elided_ = false;
    void layoutCrumbs();
    int crumbAt(const QPoint& p) const;
};

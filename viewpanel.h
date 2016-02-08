#ifndef VIEWPANEL_H
#define VIEWPANEL_H

#include <QObject>
#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPixmap>
#include <QGraphicsPixmapItem>

class ViewPanel : public QGraphicsView
{

    Q_OBJECT

public:
    ViewPanel(QWidget *parent);

    QGraphicsPixmapItem* showPixmap(QPixmap *pixmap);

signals:
    void zoomWasChanged();

protected slots:
//    virtual void mouseMoveEvent(QMouseEvent* event);
//    virtual void mousePressEvent(QMouseEvent* event);
//    virtual void mouseReleaseEvent(QMouseEvent* event);
//    virtual void mouseDoubleClickEvent(QMouseEvent* event);
    virtual void wheelEvent(QWheelEvent* event);
//    virtual void keyPressEvent(QKeyEvent* event);
//    virtual void resizeEvent(QResizeEvent *event);

private:
    QGraphicsScene *viewScene;
};

#endif // VIEWPANEL_H

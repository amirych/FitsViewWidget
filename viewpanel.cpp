#include "viewpanel.h"

#include <QDebug>
#include <QWheelEvent>

ViewPanel::ViewPanel(QWidget *parent): QGraphicsView(parent)
{
    viewScene = new QGraphicsScene(this);

    this->setScene(viewScene);
}


QGraphicsPixmapItem* ViewPanel::showPixmap(const QPixmap *pixmap, const QPointF &center, qreal scale)
{
    viewScene->clear();
    viewScene->setSceneRect(-1.0*pixmap->size().width(),-1.0*pixmap->size().height(),2.0*pixmap->size().width(),2.0*pixmap->size().height());

    QGraphicsPixmapItem* item = viewScene->addPixmap(*pixmap);
    item->setPos(-center.x(), -center.y());

//    item->setPos(-0.5*pixmap->size().width(),-0.5*pixmap->size().height());

    if ( scale <= 0 ) { // show entire image
        this->fitInView(item,Qt::KeepAspectRatio);
    } else {
        this->scale(scale,scale);
    }

    return item;
}


void ViewPanel::wheelEvent(QWheelEvent *event)
{
    qDebug() << "WHEEL!!!";
//    if ( !currentImage_buffer ) return;
    int numDegrees = event->delta() / 8;

    int numSteps = numDegrees / 15; // see QWheelEvent documentation

    qreal factor = 1.0+qreal(numSteps)*0.1;
    qDebug() << "factor(wheel) = " << factor;

    this->scale(factor,factor);
    qDebug() << mapToScene(viewport()->rect()).boundingRect();
}


#include "viewpanel.h"

#include <QDebug>
#include <QWheelEvent>
#include <QList>

ViewPanel::ViewPanel(QWidget *parent): QGraphicsView(parent),
    currentPixmapItem(nullptr)
{
    viewScene = new QGraphicsScene(this);

    this->setScene(viewScene);
}


QGraphicsPixmapItem* ViewPanel::showPixmap(const QPixmap *pixmap, const QPointF &center, qreal scale)
{
    viewScene->clear();
    currentPixmapItem = nullptr;
    viewScene->setSceneRect(-1.0*pixmap->size().width(),-1.0*pixmap->size().height(),2.0*pixmap->size().width(),2.0*pixmap->size().height());

    currentPixmapItem = viewScene->addPixmap(*pixmap);

    QPointF cc = center - QPointF(-0.5,-0.5); // FITS coordinates begin from (1,1) and origin is at the center of pixel
    currentPixmapItem->setPos(-cc);

//    item->setPos(-0.5*pixmap->size().width(), -0.5*pixmap->size().height());


    if ( scale <= 0 ) { // show entire image
        this->fitInView(currentPixmapItem,Qt::KeepAspectRatio);
    } else {
        this->scale(scale,scale);
    }

    return currentPixmapItem;
}


void ViewPanel::wheelEvent(QWheelEvent *event)
{
//    qDebug() << "WHEEL!!!";
//    if ( !currentImage_buffer ) return;
    int numDegrees = event->delta() / 8;

    int numSteps = numDegrees / 15; // see QWheelEvent documentation

    qreal factor = 1.0+qreal(numSteps)*0.1;
//    qDebug() << "factor(wheel) = " << factor;

    this->scale(factor,factor);
    emit zoomWasChanged(factor);
//    qDebug() << mapToScene(viewport()->rect()).boundingRect();
}


void ViewPanel::mouseMoveEvent(QMouseEvent *event)
{
    if ( currentPixmapItem == nullptr ) return;

    QPointF pos =  mapToScene( event->pos() );

    pos = currentPixmapItem->mapFromScene(pos);

    emit cursorPos(pos);
}

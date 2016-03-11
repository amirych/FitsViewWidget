#include "FitsViewWidget.h"

#include<random>
#include<algorithm>
#include<cmath>

#include<QImage>
#include<QDebug>
#include<QPointF>
#include<QVBoxLayout>

#include<fitsio.h>

static void random_sample(std::vector<double> &sample, size_t max_nelem)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, sample.size());

    std::vector<double> new_sample;

    size_t idx;
    for ( size_t i = 0; i < max_nelem; ++i ) {
        idx = dis(gen);
        new_sample.push_back(sample[idx]);
    }

    sample = new_sample;
}


static int robust_sigma(std::vector<double> &sample, double *sigma, double *median = nullptr)
{
    const double eps = 1.0E-20;

    double med,mad;

    std::sort(sample.begin(),sample.end());

    // compute median
    if ( (sample.size() % 2) == 1 ) {
        med = sample[sample.size()/2];
    } else {
        med = (sample[sample.size()/2-1] + sample[sample.size()/2])/2.0;
    }

    if ( median != nullptr ) *median = med;

    // compute median absolute deviation
    for ( size_t i = 0; i < sample.size(); ++i ) {
        sample[i] = abs(sample[i]-med);
    }
    std::sort(sample.begin(),sample.end());
    if ( (sample.size() % 2) == 1 ) {
        mad = sample[sample.size()/2];
    } else {
        mad = (sample[sample.size()/2-1] + sample[sample.size()/2])/2.0;
    }

    if ( (mad/0.6745) < eps ) { // try mean absolute deviation
        mad =  0.0;
        for ( size_t i = 0; i < sample.size(); ++i ) {
            mad += sample[i];
        }
        mad /= sample.size();

        if ( (mad/0.8) < eps ) {
            *sigma = 0.0;
            return 1;
        }

    } else mad /= 0.6745;

    // biweighting
    std::vector<double> u2(sample.size());
    std::vector<size_t> idx;

    mad *= 36*mad;
    for ( size_t i = 0; i < sample.size(); ++i ) {
        u2[i] = sample[i]*sample[i]/mad;
        if ( u2[i] <= 1.0 ) {
            idx.push_back(i);
        }
    }

    if ( idx.size() < 3 ) {
        *sigma = 0.0;
        return 1;
    }

    double num = 0.0;
    double denum = 0.0;
    for ( size_t i = 0; i < idx.size(); ++i ) {
        num += sample[idx[i]]*sample[idx[i]]*std::pow(1.0-u2[idx[i]],4);
        denum += (1.0-u2[idx[i]])*(1.0-5.0*u2[idx[i]]);
    }

    *sigma = num/(denum*(denum-1.0))*sample.size();

    if ( *sigma > 0 ) {
      *sigma = sqrt(*sigma);
    } else {
      *sigma = 0.0;
      return 1;
    }

    return 0;
}


            /*  CONSTRUCTOR AND DESTRUCTOR  */

FitsViewWidget::FitsViewWidget(QWidget *parent): QWidget(parent),
    rubberBand(nullptr), rubberBandOrigin(QPointF(0,0)), rubberBandEnd(QPointF(0,0)),
    rubberBandPen(QPen(QBrush(Qt::SolidPattern),0,Qt::DashLine)),
    rubberBandIsActive(false), rubberBandIsShown(false),
    currentError(FitsViewWidget::OK),
    currentFilename(""), imageIsLoaded(false),
    currentImage_buffer(std::unique_ptr<double[]>()), currentScaledImage_buffer(std::unique_ptr<uchar[]>()),
    currentImage_npix(0),
    lowCutSigmas(2.0), highCutSigmas(5.0),
    currentLowCut(0.0), currentHighCut(0.0),
    currentCT(QVector<QRgb>(FITS_VIEW_COLOR_TABLE_LENGTH)), currentCT_name(FitsViewWidget::CT_NEGBW),
    currentPixmap(QPixmap()), currentZoomFactor(0.0), zoomIncrement(2.0),
    maxSampleLength(FITS_VIEW_MAX_SAMPLE_LENGTH),
    currentViewedSubImageCenter(QPointF(0,0))
{

    currentImage_dim[0] = 0, currentImage_dim[1] = 0;

    generateCT(currentCT_name);
    generateCT(CT_BW);
//    setColorTable(FitsViewWidget::CT_NEGBW);

    view = new ViewPanel(this);
//    view = new QGraphicsView(this);
//    scene = new QGraphicsScene(this);
//    view->setScene(scene);

    QTransform tr(1.0,0.0,0.0,-1.0,0.0,0.0); // reflection about x-axis to put
    view->setTransform(tr);                  // the origin to bottom-left conner

    view->setCursor(Qt::CrossCursor);

//    view->setMouseTracking(true);
    setMouseTracking(true);
    setFocusProxy(view);

    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContentsOnFirstShow);

    rubberBandPen.setColor("red");

    resizeTimer = new QTimer(this);
    connect(resizeTimer,SIGNAL(timeout()),this,SLOT(resizeTimeout()));

    connect(this,SIGNAL(ColorTableIsChanged(FitsViewWidget::ColorTable)),this,SLOT(showImage()));
    connect(view,SIGNAL(zoomWasChanged(qreal)),this,SLOT(changeZoom(qreal)));
    connect(this,SIGNAL(zoomIsChanged(qreal)),this,SLOT(changeZoom(qreal)));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(view);
    layout->setMargin(0);
}


FitsViewWidget::~FitsViewWidget()
{
}


            /*  PUBLIC SLOTS  */

void FitsViewWidget::load(const QString fits_filename, const bool autoscale)
{
    imageIsLoaded = false;

    QString str = fits_filename.trimmed();
    if ( str.isEmpty() || str.isNull() ) return;

    fitsfile *FITS_fptr;

    int fits_status = 0;
    currentError = FitsViewWidget::OK;

    // THE ONLY 2D-images is support now!!!
    int maxdim = 2;
    long naxes[maxdim];
    int naxis, bitpix;
    LONGLONG nelem = 1;

    char* filename = fits_filename.toLocal8Bit().data();

    try {
        fits_open_image(&FITS_fptr, filename, READONLY, &fits_status);
        if ( fits_status ) throw fits_status;

        fits_read_imghdr(FITS_fptr, maxdim, NULL, &bitpix, &naxis, naxes, NULL, NULL, NULL, &fits_status);
        if ( fits_status ) throw fits_status;

        for ( int i = 0; i < maxdim; ++i ) {
            nelem *= naxes[i];
            currentImage_dim[i] = naxes[i];
        }

        currentImage_npix = nelem;
        currentImage_buffer = std::unique_ptr<double[]>(new double[currentImage_npix]);
        double *buffer = currentImage_buffer.get();

        fits_read_img(FITS_fptr, TDOUBLE, 1, nelem, NULL, (void*) buffer, NULL, &fits_status);
        if ( fits_status ) throw fits_status;

        fits_close_file(FITS_fptr, &fits_status);
        if ( fits_status ) throw fits_status;

    } catch (std::bad_alloc &ex) {
        currentImage_buffer = nullptr;
        currentError = FitsViewWidget::MemoryError;
        throw currentError;
    } catch (int err) {
        currentError = err;
        emit fitsViewError(currentError);
        fits_close_file(FITS_fptr, &fits_status);

        currentImage_npix = 0;

        return;
    }

    imageIsLoaded = true;

    double *buffer = currentImage_buffer.get();
    auto minmax = std::minmax_element(buffer,buffer+currentImage_npix);
    currentImageMinVal = *minmax.first;
    currentImageMaxVal = *minmax.second;

    currentLowCut = currentImageMinVal;
    currentHighCut = currentImageMaxVal;

    if ( autoscale ) {
//        double lcut,hcut;
        std::vector<double> sample(buffer,buffer+currentImage_npix);

        computeCuts(sample,&currentLowCut,&currentHighCut);
        rescale(currentLowCut,currentHighCut);
    }

//    // redefine scene size
////    scene->setSceneRect(-1.0*currentImage_dim[0],-1.0*currentImage_dim[1],2.0*currentImage_dim[0],2.0*currentImage_dim[1]);
//    view->setSceneRect(-1.0*currentImage_dim[0],-1.0*currentImage_dim[1],2.0*currentImage_dim[0],2.0*currentImage_dim[1]);

//    // compute zoom factor for entire image viewing
////    qDebug() << view->viewport()->width();
//    qreal xzoom = 1.0*(view->viewport()->width()-2.0*FITS_VIEW_IMAGE_MARGIN)/currentImage_dim[0];
//    qreal yzoom = 1.0*(view->viewport()->height()-2.0*FITS_VIEW_IMAGE_MARGIN)/currentImage_dim[1];

//    qDebug() << xzoom << yzoom;
//    currentZoomFactor = ( xzoom < yzoom ) ? xzoom : yzoom;

    currentViewedSubImage.setWidth(currentImage_dim[0]);
    currentViewedSubImage.setHeight(currentImage_dim[1]);
    currentViewedSubImageCenter = QPointF(0.5*currentImage_dim[0],0.5*currentImage_dim[1]);
    currentZoomFactor = 0.0; // full image

//    qDebug() << "cuts: " << currentLowCut << ", " << currentHighCut;
}


void FitsViewWidget::rescale(const double lcuts, const double hcuts)
{
    if ( (currentImage_buffer == nullptr) || (currentImage_npix == 0) ) return;

    currentError = FitsViewWidget::OK;

    if ( lcuts >= hcuts ) {
        currentError = FitsViewWidget::BadCutValue;
        emit fitsViewError(currentError);
        return;
    }

    if ( lcuts >= currentImageMaxVal ) {
        currentError = FitsViewWidget::BadCutValue;
        emit fitsViewError(currentError);
        return;

    }

    if ( hcuts <= currentImageMinVal ) {
        currentError = FitsViewWidget::BadCutValue;
        emit fitsViewError(currentError);
        return;

    }

    try {
        currentScaledImage_buffer = std::unique_ptr<uchar[]>(new uchar[currentImage_npix]);
    } catch (std::bad_alloc &ex) {
        currentError = FitsViewWidget::MemoryError;
        emit fitsViewError(currentError);
        return;
    }

    if ( lcuts < currentImageMinVal ) currentLowCut = currentImageMinVal; else currentLowCut = lcuts;
    if ( hcuts > currentImageMaxVal ) currentHighCut = currentImageMaxVal; else currentHighCut = hcuts;


    double range = currentHighCut-currentLowCut;
    double scaled_val;
    uchar max_val = 255; // 8-bit indexed image

    for ( size_t i = 0; i < currentImage_npix; ++i ) {
        if ( currentImage_buffer[i] <= currentLowCut ) {
            currentScaledImage_buffer[i] = 0;
            continue;
        }
        if ( currentImage_buffer[i] >= currentHighCut ) {
            currentScaledImage_buffer[i] = max_val;
            continue;
        }
        scaled_val = (currentImage_buffer[i]-currentLowCut)/range;
        currentScaledImage_buffer[i] = static_cast<uchar>(std::lround(scaled_val*max_val));
//        if ( (i > 5000) && (i < 5030) ) qDebug() << currentScaledImage_buffer[i];
    }

    emit cutsAreChanged(currentLowCut,currentHighCut);
}


void FitsViewWidget::showImage()
{
    if ( !currentImage_buffer ) return;

    // convert to pixmap
    QImage im = QImage(currentScaledImage_buffer.get(),currentImage_dim[0],currentImage_dim[1],currentImage_dim[0],QImage::Format_Indexed8);
    im.setColorTable(currentCT);

    currentPixmap = QPixmap::fromImage(im);

//    fitsImagePixmapItem = view->showPixmap(&currentPixmap,QPointF(0.5*currentImage_dim[0],0.5*currentImage_dim[1]),currentZoomFactor);
    fitsImagePixmapItem = view->showPixmap(&currentPixmap,currentViewedSubImageCenter,currentZoomFactor);


//    view->fitInView(fitsImagePixmapItem,Qt::KeepAspectRatio);

//    view->centerOn(currentViewedSubImageCenter);

//    setZoom(currentZoomFactor);

    currentZoomFactor = view->transform().m11();
//    view->scale(currentZoomFactor,currentZoomFactor);
//    scale(0.66348,0.66348);

    qDebug() << currentZoomFactor;
    qDebug() << view->transform();

//    currentViewedSubImage = view->mapToScene(view->viewport()->rect()).boundingRect();
//    if ( currentViewedSubImage.width() > currentImage_dim[0] ) currentViewedSubImage.setWidth(currentImage_dim[0]);
//    if ( currentViewedSubImage.height() > currentImage_dim[1] ) currentViewedSubImage.setHeight(currentImage_dim[1]);

//    qDebug() << 1.0*view->viewport()->width()/currentImage_dim[0] << 1.0*viewport()->height()/currentImage_dim[1];
//    int lm,tm,rm,bm;
//    view->viewport()->getContentsMargins(&lm,&tm,&rm,&bm);
//    qDebug() << lm << tm << rm << bm;
//    qDebug() << 1.0*(view->viewport()->width()-4)/currentImage_dim[0] << 1.0*(view->viewport()->height()-4)/currentImage_dim[1];
}


            /*  PUBLIC METHODS  */

int FitsViewWidget::getError() const
{
    return currentError;
}


bool FitsViewWidget::isImageLoaded() const
{
    return imageIsLoaded;
}


QString FitsViewWidget::getCurrentFilename() const
{
    return currentFilename;
}


void FitsViewWidget::getCuts(double *lcuts, double *hcuts)
{
    if ( lcuts != nullptr ) *lcuts = currentLowCut;
    if ( hcuts != nullptr ) *hcuts = currentHighCut;
}


void FitsViewWidget::setCutSigma(const double lcut_sigmas, const double hcut_sigmas)
{
    if ( lcut_sigmas > 0.0 ) lowCutSigmas = lcut_sigmas;
    if ( hcut_sigmas > 0.0 ) highCutSigmas = hcut_sigmas;
}


void FitsViewWidget::setColorTable(FitsViewWidget::ColorTable ct)
{
    generateCT(ct);
    if ( currentError != FitsViewWidget::OK ) return;
    currentCT_name = ct;

    if ( !currentImage_buffer ) return;

    QImage im = QImage(currentScaledImage_buffer.get(),currentImage_dim[0],currentImage_dim[1],currentImage_dim[0],QImage::Format_Indexed8);
    im.setColorTable(currentCT);

    currentPixmap = QPixmap::fromImage(im);
    fitsImagePixmapItem->setPixmap(currentPixmap);

    emit ColorTableIsChanged(ct);
}


FitsViewWidget::ColorTable FitsViewWidget::getColorTable() const
{
    return currentCT_name;
}


void FitsViewWidget::setMaxSampleLength(size_t nelem)
{
    maxSampleLength = nelem;
}


void FitsViewWidget::centerOn(qreal x, qreal y)
{
    currentViewedSubImageCenter.setX(x);
    currentViewedSubImageCenter.setY(y);

    // convert from FITS image pixel cordinates to the scene ones
    QPointF cen = QPointF(x-0.5,y-0.5);
    cen = fitsImagePixmapItem->mapToScene(currentViewedSubImageCenter);

//    view->centerOn(x,y);
    view->centerOn(cen);
    qDebug() << "recentering: " << cen;
}


void FitsViewWidget::centerOn(QPointF &pos)
{
    centerOn(pos.x(),pos.y());
}


QPointF FitsViewWidget::getImageCenter() const
{
    return currentViewedSubImageCenter;
}


void FitsViewWidget::setRubberBandPen(const QPen &pen)
{
    if ( rubberBand ) rubberBand->setPen(pen);
}


void FitsViewWidget::setZoom(const qreal zoom_factor)
{
    if ( !currentScaledImage_buffer ) return;

    if ( zoom_factor <= 0.0 ) return;

    currentZoomFactor = zoom_factor;
    QTransform tr(zoom_factor,0.0,0.0,-zoom_factor,0.0,0.0);
    view->setTransform(tr);
}


void FitsViewWidget::incrementZoom(const qreal zoom_inc)
{
    if ( !currentScaledImage_buffer ) return;

    if ( zoom_inc <= 0.0 ) return;

    qDebug() << "inc: " << zoom_inc;

    view->scale(zoom_inc,zoom_inc);

    emit zoomIsChanged(zoom_inc);

//    currentZoomFactor *= zoom_inc;

//    // recompute current viewed sub-image
//    currentViewedSubImage = view->mapToScene(view->viewport()->rect()).boundingRect();
//    if ( currentViewedSubImage.width() > currentImage_dim[0] ) currentViewedSubImage.setWidth(currentImage_dim[0]);
//    if ( currentViewedSubImage.height() > currentImage_dim[1] ) currentViewedSubImage.setHeight(currentImage_dim[1]);

//    qDebug() << view->transform();
}


qreal FitsViewWidget::getZoom() const
{
    return currentZoomFactor;
}


void FitsViewWidget::zoomFitInView()
{
    // compute zoom factor for entire image viewing
    qreal xzoom = 1.0*(view->viewport()->width()-2.0*FITS_VIEW_IMAGE_MARGIN)/currentImage_dim[0];
    qreal yzoom = 1.0*(view->viewport()->height()-2.0*FITS_VIEW_IMAGE_MARGIN)/currentImage_dim[1];

    // FITS coordinate system starts from (1,1) and its origin is at the center of pixel
    currentViewedSubImageCenter = QPointF(0.5*currentImage_dim[0]+0.5,0.5*currentImage_dim[1]+0.5);
    currentViewedSubImage.setWidth(currentImage_dim[0]);
    currentViewedSubImage.setHeight(currentImage_dim[1]);

    centerOn(currentViewedSubImageCenter);

    setZoom(( xzoom < yzoom ) ? xzoom : yzoom);
}


        /*  PROTECTED METHODS  */


void FitsViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if ( !currentScaledImage_buffer ) return;
}


void FitsViewWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if ( !currentScaledImage_buffer ) return;

    currentViewedSubImageCenter =  view->mapToScene( event->pos() );

    view->centerOn(currentViewedSubImageCenter);

    qDebug() << "doubleClick (mouse pos): " << event->pos();
    qDebug() << "doubleClick (imcenter scene): " << currentViewedSubImageCenter;
    qDebug() << "doubleClick: (image pixel)" << fitsImagePixmapItem->mapFromScene(currentViewedSubImageCenter);

    // convert to pixels coordinates
    currentViewedSubImageCenter = fitsImagePixmapItem->mapFromScene(currentViewedSubImageCenter) + QPointF(0.5,0.5);


    qreal incr;

    if ( event->button() == Qt::LeftButton ) {
            incr = zoomIncrement;
    }
    if ( event->button() == Qt::RightButton ) {
            incr = 1.0/zoomIncrement;
    }

    incrementZoom(incr);
}


//void FitsViewWidget::wheelEvent(QWheelEvent *event)
//{
//    qDebug() << "WHEEL!!!";
//    if ( !currentImage_buffer ) return;
//    int numDegrees = event->delta() / 8;

//    int numSteps = numDegrees / 15; // see QWheelEvent documentation

//    qreal factor = 1.0+qreal(numSteps)*0.1;
//    qDebug() << "factor(wheel) = " << factor;
//    incrementZoom(factor);
//}


void FitsViewWidget::mousePressEvent(QMouseEvent *event)
{
    if ( !currentImage_buffer ) return;

    if ( event->button() == Qt::LeftButton ) {
        if ( rubberBandIsShown ) {
//            scene->removeItem(rubberBand);
            rubberBandIsShown = false;
            emit RegionWasDeselected();
        }

        rubberBandOrigin = view->mapToScene(event->pos());
    }
}


void FitsViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
}


void FitsViewWidget::keyPressEvent(QKeyEvent *event)
{
    if ( !currentScaledImage_buffer ) return;

    switch ( event->key() ) {
        case Qt::Key_Escape: {
            zoomFitInView();
            break;
        }
        default: {
            qDebug() << "Pressed key: " << event->key();
        }
    }
}


void FitsViewWidget::resizeEvent(QResizeEvent *event)
{
    if ( !currentScaledImage_buffer ) return;

    if ( event->oldSize().width() < 0 || event->oldSize().height() < 0 ) return; // initial resizing (by calling show())

    resizeTimer->stop();
    resizeTimer->start(FITS_VIEW_DEFAULT_RESIZE_TIMEOUT);
}



        /*  PRIVATE SLOTS  */

void FitsViewWidget::resizeTimeout()
{
    resizeTimer->stop();

//    QRectF rr = view->mapToScene(view->geometry()).boundingRect();

    centerOn(currentViewedSubImageCenter);
    view->invalidateScene();

    return;

//    if ( (rr.height() < currentViewedSubImage.height()) || (rr.width() < currentViewedSubImage.width()) ) {
//        qreal hfactor = 1.0*rr.height()/currentViewedSubImage.height();
//        qreal wfactor = 1.0*rr.width()/currentViewedSubImage.width();

//        qreal factor = (hfactor > wfactor) ? wfactor : hfactor;
//        incrementZoom(factor);
//    } else {
//        setZoom(currentZoomFactor);
//    }

}

void FitsViewWidget::changeZoom(qreal factor)
{
    currentZoomFactor *= factor;

    // recompute current viewed sub-image
    currentViewedSubImage = view->mapToScene(view->viewport()->rect()).boundingRect();
    if ( currentViewedSubImage.width() > currentImage_dim[0] ) currentViewedSubImage.setWidth(currentImage_dim[0]);
    if ( currentViewedSubImage.height() > currentImage_dim[1] ) currentViewedSubImage.setHeight(currentImage_dim[1]);
}

        /*  PRIVATE METHODS  */


void FitsViewWidget::computeCuts(std::vector<double> &sample, double *lcut, double *hcut)
{
    if ( lcut == nullptr ) return;
    if ( hcut == nullptr ) return;

    double sigma, median;

    random_sample(sample,maxSampleLength);

    int status = robust_sigma(sample,&sigma,&median);
    if ( status ) return; // pixel distribution is weird

    *lcut = median - lowCutSigmas*sigma;
    *hcut = median + highCutSigmas*sigma;
}


void FitsViewWidget::generateCT(FitsViewWidget::ColorTable ct)
{
    int j;
    qreal ct_step = 255.0/(FITS_VIEW_COLOR_TABLE_LENGTH-1);

    switch (ct) {
        case FitsViewWidget::CT_BW: { // black-and-white (grayscale)
            for ( int i = 0; i < FITS_VIEW_COLOR_TABLE_LENGTH; ++i ) {
                j = i*ct_step;
                currentCT[i] = qRgb(j,j,j);
            }
            break;
        }
        case FitsViewWidget::CT_NEGBW: { // negative grayscale
            for ( int i = 0; i < FITS_VIEW_COLOR_TABLE_LENGTH; ++i ) {
                j = 255-i*ct_step;
                if ( j < 0 ) j = 0;
                currentCT[i] = qRgb(j,j,j);
            }
            break;
        }
        default: {
            currentError = FitsViewWidget::BadColorTable;
            emit fitsViewError(currentError);
        }
    }
}


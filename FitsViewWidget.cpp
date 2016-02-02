#include "FitsViewWidget.h"

#include<random>
#include<algorithm>
#include<cmath>

#include<QImage>
#include<QDebug>
#include<QPointF>

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

FitsViewWidget::FitsViewWidget(QWidget *parent): QGraphicsView(parent),
    currentError(FitsViewWidget::OK),
    currentFilename(""), imageIsLoaded(false),
    currentImage_buffer(std::unique_ptr<double[]>()), currentScaledImage_buffer(std::unique_ptr<uchar[]>()),
    currentImage_npix(0),
    lowCutSigmas(2.0), highCutSigmas(5.0),
    currentLowCut(0.0), currentHighCut(0.0),
    currentCT(QVector<QRgb>(FITS_VIEW_COLOR_TABLE_LENGTH)), currentCT_name(FitsViewWidget::CT_NEGBW),
    currentPixmap(QPixmap()), currentZoomFactor(1.0), zoomIncrement(2.0),
    maxSampleLength(FITS_VIEW_MAX_SAMPLE_LENGTH),
    oldSize(QSize(-1,-1))
{

    currentImage_dim[0] = 0, currentImage_dim[1] = 0;

    generateCT(FitsViewWidget::CT_NEGBW);

    scene = new QGraphicsScene(this);
    setScene(scene);

    QTransform tr(1.0,0.0,0.0,-1.0,0.0,0.0); // reflection about x-axis to put
    this->setTransform(tr);                  // the origin to bottom-left conner

    setCursor(Qt::CrossCursor);

    setMouseTracking(true);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContentsOnFirstShow);

    resizeTimer = new QTimer(this);
    connect(resizeTimer,SIGNAL(timeout()),this,SLOT(resizeTimeout()));
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
    // convert to pixmap
    QImage im = QImage(currentScaledImage_buffer.get(),currentImage_dim[0],currentImage_dim[1],currentImage_dim[0],QImage::Format_Indexed8);
    im.setColorTable(currentCT);

    currentPixmap = QPixmap::fromImage(im);

    scene->clear();

//    scene->setSceneRect(0,0,currentImage_dim[0],currentImage_dim[1]);
    scene->setSceneRect(-1.0*currentImage_dim[0],-1.0*currentImage_dim[1],2.0*currentImage_dim[0],2.0*currentImage_dim[1]);

    fitsImagePixmapItem = scene->addPixmap(currentPixmap);
    fitsImagePixmapItem->setPos(-0.5*currentImage_dim[0],-0.5*currentImage_dim[1]);

    centerOn(0,0);
//    this->centerOn(fitsImagePixmapItem);
    this->fitInView(fitsImagePixmapItem,Qt::KeepAspectRatio);
    this->scale(currentZoomFactor,currentZoomFactor);

    currentViewedSubImage = mapToScene(viewport()->rect()).boundingRect();
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



        /*  PROTECTED METHODS  */

void FitsViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if ( !currentScaledImage_buffer ) return;
}


void FitsViewWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if ( !currentScaledImage_buffer ) return;

    QPointF pos =  mapToScene( event->pos() );

    centerOn(pos);

    if ( event->button() == Qt::LeftButton ) {
            scale(zoomIncrement,zoomIncrement);
            currentZoomFactor *= zoomIncrement;
    }
    if ( event->button() == Qt::RightButton ) {
            currentZoomFactor /= zoomIncrement;
            if ( currentZoomFactor < 1.0 ) {
                currentZoomFactor = 1.0;
            } else {
                scale(1.0/zoomIncrement,1.0/zoomIncrement);
            }
    }

    qDebug() << pos << "(" << mapFromScene(event->pos()) << ")";
}


void FitsViewWidget::wheelEvent(QWheelEvent *event)
{
    if ( !currentScaledImage_buffer ) return;
    int numDegrees = event->delta() / 8;

    int numSteps = numDegrees / 15; // see QWheelEvent documentation

    qreal factor = 1.0+qreal(numSteps)*0.1;
    currentZoomFactor *= factor;
    if ( currentZoomFactor < 1.0 ) {
        currentZoomFactor = 1.0;
        fitInView(fitsImagePixmapItem,Qt::KeepAspectRatio);
        return;
    }
    scale(factor,factor);
}


void FitsViewWidget::mousePressEvent(QMouseEvent *event)
{
}


void FitsViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
}


void FitsViewWidget::keyPressEvent(QKeyEvent *event)
{
}


void FitsViewWidget::resizeEvent(QResizeEvent *event)
{
    if ( !currentScaledImage_buffer ) return;
    if ( (event->oldSize().width() < 0) || (event->oldSize().height() < 0) ) {
        oldSize = event->size();
        return;
    }


//    qreal hfactor = 1.0*event->size().height()/event->oldSize().height();
//    qreal wfactor = 1.0*event->size().width()/event->oldSize().width();

//    qreal factor = (hfactor >= wfactor) ? wfactor : hfactor;

//    scale(factor,factor);

//    qDebug() << event->size() << event->oldSize();
//    qDebug() << "Factor = " << factor;

    resizeTimer->stop();
    resizeTimer->start(FITS_VIEW_DEFAULT_RESIZE_TIMEOUT);
}


void FitsViewWidget::showEvent(QShowEvent *event)
{
    oldSize = this->size();
}

        /*  PRIVATE SLOTS  */

void FitsViewWidget::resizeTimeout()
{
    resizeTimer->stop();

//    qreal hfactor = 1.0*this->size().height()/oldSize.height();
//    qreal wfactor = 1.0*this->size().width()/oldSize.width();

//    qreal factor = (hfactor > wfactor) ? wfactor : hfactor;

//    qreal ratio = 1.0*this->size().height()/this->size().width();
//    qreal old_ratio = 1.0*oldSize.height()/oldSize.width();

//    factor *= ratio/old_ratio;

    qreal factor = 1.0*this->size().width()/oldSize.width();

    qreal ratio = 1.0*currentViewedSubImage.width()/currentViewedSubImage.height();

    if ( currentViewedSubImage.height()*factor < currentViewedSubImage.width()*factor/ratio ) {
        factor = 1.0*this->size().height()/oldSize.height()/ratio;
    }

    scale(factor,factor);

    qDebug() << this->size() << oldSize << "; factor = " << factor << " (ratio = " << ratio << ")";
    qDebug() << currentViewedSubImage;
//    qDebug() << mapToScene(viewport()->rect()).boundingRect();

    oldSize = this->size();
    currentViewedSubImage = mapToScene(viewport()->rect()).boundingRect();
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



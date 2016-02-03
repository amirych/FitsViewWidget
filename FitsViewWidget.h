#ifndef FITSVIEWWIDGET_H
#define FITSVIEWWIDGET_H

#include "fitsviewwidget_global.h"

#include<memory>
#include<QWidget>
#include<QGraphicsView>
#include<QGraphicsScene>
#include<QGraphicsItem>
#include<QVector>
#include<vector>
#include<QRgb>
#include<QPixmap>
#include<QPointer>
#include<QMouseEvent>
#include<QTimer>
#include<QRectF>
#include<QPointF>


#define FITS_VIEW_COLOR_TABLE_LENGTH 256
#define FITS_VIEW_MAX_SAMPLE_LENGTH 10000
//#define FITS_VIEW_DEFAULT_RESIZE_TIMEOUT 1000 // 1/4 second
#define FITS_VIEW_DEFAULT_RESIZE_TIMEOUT 250 // 1/4 second

class FITSVIEWWIDGETSHARED_EXPORT FitsViewWidget: public QGraphicsView
{

    Q_OBJECT

public:
    enum ColorTable {CT_BW, CT_NEGBW};
    enum Error {OK, MemoryError = 10000, BadColorTable, BadCutValue};

    FitsViewWidget(QWidget *parent = nullptr);

    ~FitsViewWidget();

    int getError() const;

    bool isImageLoaded() const;

    void getCuts(double *lcuts, double *hcuts);

    QString getCurrentFilename() const;

    void setCutSigma(const double lcut_sigmas, const double hcut_sigmas);

    void setColorTable(FitsViewWidget::ColorTable ct);
    FitsViewWidget::ColorTable getColorTable() const;

    void setMaxSampleLength(size_t nelem);

public slots:
    void load(const QString fits_filename, const bool autoscale = true);
    void rescale(const double lcuts, const double hcuts);
    void showImage();

signals:
    void fitsViewError(int err);
    void cutsAreChanged(double lcut, double hcut);
    void ColorTableIsChanged(FitsViewWidget::ColorTable ct);

protected:
    virtual void mouseMoveEvent(QMouseEvent* event);
    virtual void mousePressEvent(QMouseEvent* event);
    virtual void mouseReleaseEvent(QMouseEvent* event);
    virtual void mouseDoubleClickEvent(QMouseEvent* event);
    virtual void wheelEvent(QWheelEvent* event);
    virtual void keyPressEvent(QKeyEvent* event);
    virtual void resizeEvent(QResizeEvent *event);
    virtual void showEvent(QShowEvent* event);


private slots:
    void resizeTimeout();

private:
    int currentError;
    QString currentFilename;

    bool imageIsLoaded;
    std::unique_ptr<double[]> currentImage_buffer;
    std::unique_ptr<uchar[]> currentScaledImage_buffer;
    size_t currentImage_npix;
    size_t currentImage_dim[2];
    double currentImageMinVal;
    double currentImageMaxVal;

    void computeCuts(std::vector<double> &sample, double *lcut, double *hcut);
    double lowCutSigmas, highCutSigmas;
    double currentLowCut,currentHighCut;

    void generateCT(FitsViewWidget::ColorTable ct);
    QVector<QRgb> currentCT;
    ColorTable currentCT_name;

    QPixmap currentPixmap;
    QPointer<QGraphicsScene> scene;
    QGraphicsPixmapItem *fitsImagePixmapItem;
    qreal currentZoomFactor;
    qreal zoomIncrement;

    size_t maxSampleLength;

    QPointer<QTimer> resizeTimer;
    QSize oldSize;
    QRectF currentViewedSubImage;
    QPointF currentViewedSubImageCenter;
};

#endif // FITSVIEWWIDGET_H

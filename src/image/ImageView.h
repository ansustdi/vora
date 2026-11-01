#pragma once

#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

class QMovie;
class QVariantAnimation;

// Zoom/pan image canvas: fit modes, zoom-to-cursor, animated images, crop overlay,
// histogram, transient toasts and a fade transition for slideshows.
class ImageView : public QWidget
{
    Q_OBJECT
public:
    enum FitMode { Fit, Fill, Actual, FitWidth, Free };

    explicit ImageView(QWidget *parent = nullptr);

    void setImage(const QImage &image, const QString &path, bool animated, bool fade);
    void replaceImage(const QImage &image); // after an edit; keeps the fit mode
    void clear();
    QImage image() const { return m_image; }
    bool isAnimated() const { return m_movie != nullptr; }

    void setFitMode(FitMode mode);
    FitMode fitMode() const { return m_fit; }
    void zoomBy(double factor, QPointF anchor = QPointF(-1, -1));
    double zoomPercent() const;

    void setOverlayText(const QString &text);
    void showToast(const QString &text, int ms = 1500);
    // Stop an animation and close its file (rename / delete need the handle gone); the current
    // frame stays on screen.
    void releaseFile();
    void setHistogramVisible(bool on);
    bool histogramVisible() const { return m_histVisible; }

    void setCropMode(bool on, double ratio = 0);
    bool cropMode() const { return m_crop; }
    QRect cropRect() const;

signals:
    void zoomChanged(double percent);
    void pixelHovered(QPoint pos, QColor color);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    bool event(QEvent *) override;

private:
    void applyFit();
    void clampOffset();
    void setPixmapFrom(const QImage &img);
    void computeHistogram();
    void requestHighQuality();
    QRectF imageRect() const;
    QPointF toImage(QPointF widgetPos) const;
    QPointF toWidget(QPointF imagePos) const;
    void drawCheckerboard(QPainter &p, const QRectF &r);
    void drawCrop(QPainter &p);
    void drawHistogram(QPainter &p);
    void drawLabel(QPainter &p, const QString &text, Qt::Alignment align);
    void emitZoom();

    QImage m_image;
    QPixmap m_pix;
    QPixmap m_hq;          // pre-scaled pixmap for smooth downscaling
    double m_hqScale = -1;
    QTimer m_hqTimer;

    QPixmap m_prevPix;     // for fade transition
    QRectF m_prevRect;
    double m_fade = 1.0;
    QVariantAnimation *m_fadeAnim;

    QMovie *m_movie = nullptr;

    double m_scale = 1.0;  // logical px per image px
    QPointF m_offset;      // widget position of the image's top-left
    FitMode m_fit = Fit;

    bool m_panning = false;
    QPointF m_lastPos;

    QString m_overlay;
    QString m_toast;
    QTimer m_toastTimer;

    bool m_histVisible = false;
    QVector<int> m_hist[3];
    int m_histMax = 1;

    bool m_crop = false;
    double m_cropRatio = 0;
    QRectF m_cropImg;      // in image coordinates
    QPointF m_cropStart;
    bool m_cropDragging = false;
};

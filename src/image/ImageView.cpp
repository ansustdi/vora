#include "ImageView.h"

#include <QGestureEvent>
#include <QMovie>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPinchGesture>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <cmath>

ImageView::ImageView(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_AcceptTouchEvents);
    grabGesture(Qt::PinchGesture);

    m_hqTimer.setSingleShot(true);
    connect(&m_hqTimer, &QTimer::timeout, this, [this] {
        if (m_image.isNull() || m_movie)
            return;
        const double dpr = devicePixelRatioF();
        const QSize target = (QSizeF(m_image.size()) * m_scale * dpr).toSize();
        if (target.isEmpty())
            return;
        m_hq = QPixmap::fromImage(m_image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        m_hq.setDevicePixelRatio(dpr);
        m_hqScale = m_scale;
        update();
    });

    m_toastTimer.setSingleShot(true);
    connect(&m_toastTimer, &QTimer::timeout, this, [this] {
        m_toast.clear();
        update();
    });

    m_fadeAnim = new QVariantAnimation(this);
    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->setDuration(350);
    connect(m_fadeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_fade = v.toDouble();
        update();
    });
    connect(m_fadeAnim, &QVariantAnimation::finished, this, [this] {
        m_fade = 1.0;
        m_prevPix = QPixmap();
        update();
    });
}

void ImageView::setImage(const QImage &image, const QString &path, bool animated, bool fade)
{
    m_fadeAnim->stop();
    if (fade && !m_pix.isNull()) {
        m_prevPix = (m_hqScale == m_scale && !m_hq.isNull()) ? m_hq : m_pix;
        m_prevRect = imageRect();
        m_fade = 0.0;
    } else {
        m_prevPix = QPixmap();
        m_fade = 1.0;
    }

    if (m_movie) {
        m_movie->stop();
        m_movie->deleteLater();
        m_movie = nullptr;
    }

    m_image = image;
    setPixmapFrom(image);
    m_crop = false;
    m_cropImg = QRectF();

    if (animated) {
        m_movie = new QMovie(path, QByteArray(), this);
        connect(m_movie, &QMovie::frameChanged, this, [this] {
            m_image = m_movie->currentImage();
            m_pix = QPixmap::fromImage(m_image);
            update();
        });
        m_movie->start();
    }

    computeHistogram();
    if (m_fit == Free)
        m_fit = Fit;
    applyFit();
    if (m_fade < 1.0)
        m_fadeAnim->start();
}

void ImageView::releaseFile()
{
    if (!m_movie)
        return;
    m_movie->stop();
    delete m_movie; // now, not deleteLater: the file must be closed before the caller continues
    m_movie = nullptr;
}

void ImageView::replaceImage(const QImage &image)
{
    m_crop = false; // a crop rectangle drawn on the previous pixels means nothing now
    m_cropDragging = false;
    m_image = image;
    setPixmapFrom(image);
    computeHistogram();
    if (m_fit == Free) {
        clampOffset();
        requestHighQuality();
        emitZoom();
        update();
    } else {
        applyFit();
    }
}

void ImageView::clear()
{
    if (m_movie) {
        m_movie->stop();
        m_movie->deleteLater();
        m_movie = nullptr;
    }
    m_image = QImage();
    m_pix = QPixmap();
    m_hq = QPixmap();
    m_prevPix = QPixmap();
    m_crop = false;
    m_overlay.clear();
    update();
}

void ImageView::setPixmapFrom(const QImage &img)
{
    m_pix = QPixmap::fromImage(img);
    m_hq = QPixmap();
    m_hqScale = -1;
}

void ImageView::setFitMode(FitMode mode)
{
    m_fit = mode;
    applyFit();
}

void ImageView::applyFit()
{
    if (m_image.isNull() || m_fit == Free)
        return;
    const double W = width(), H = height();
    const double iw = m_image.width(), ih = m_image.height();
    const double actual = 1.0 / devicePixelRatioF();
    switch (m_fit) {
    case Fit: m_scale = qMin(actual, qMin(W / iw, H / ih)); break; // never upscale small images
    case Fill: m_scale = qMax(W / iw, H / ih); break;
    case Actual: m_scale = actual; break;
    case FitWidth: m_scale = W / iw; break;
    case Free: break;
    }
    m_offset = QPointF((W - iw * m_scale) / 2, (H - ih * m_scale) / 2);
    if (m_fit == FitWidth && ih * m_scale > H)
        m_offset.setY(0);
    clampOffset();
    requestHighQuality();
    emitZoom();
    update();
}

void ImageView::clampOffset()
{
    if (m_image.isNull())
        return;
    const double dw = m_image.width() * m_scale, dh = m_image.height() * m_scale;
    double x = m_offset.x(), y = m_offset.y();
    x = dw <= width() ? (width() - dw) / 2 : qBound(width() - dw, x, 0.0);
    y = dh <= height() ? (height() - dh) / 2 : qBound(height() - dh, y, 0.0);
    m_offset = QPointF(x, y);
}

void ImageView::zoomBy(double factor, QPointF anchor)
{
    if (m_image.isNull())
        return;
    if (anchor.x() < 0)
        anchor = QRectF(rect()).center();
    const double ns = qBound(0.01, m_scale * factor, 64.0);
    if (qFuzzyCompare(ns, m_scale))
        return;
    const QPointF ip = (anchor - m_offset) / m_scale;
    m_scale = ns;
    m_offset = anchor - ip * ns;
    m_fit = Free;
    clampOffset();
    requestHighQuality();
    emitZoom();
    update();
}

double ImageView::zoomPercent() const
{
    return m_scale * devicePixelRatioF() * 100.0;
}

void ImageView::emitZoom()
{
    emit zoomChanged(zoomPercent());
}

void ImageView::requestHighQuality()
{
    if (!m_movie && m_scale * devicePixelRatioF() < 0.999) {
        m_hqTimer.start(120);
    } else {
        m_hqTimer.stop();
        m_hq = QPixmap();
        m_hqScale = -1;
    }
}

QRectF ImageView::imageRect() const
{
    return QRectF(m_offset, QSizeF(m_image.size()) * m_scale);
}

QPointF ImageView::toImage(QPointF widgetPos) const
{
    return (widgetPos - m_offset) / m_scale;
}

QPointF ImageView::toWidget(QPointF imagePos) const
{
    return imagePos * m_scale + m_offset;
}

void ImageView::setOverlayText(const QString &text)
{
    m_overlay = text;
    update();
}

void ImageView::showToast(const QString &text, int ms)
{
    m_toast = text;
    m_toastTimer.start(ms);
    update();
}

void ImageView::setHistogramVisible(bool on)
{
    m_histVisible = on;
    update();
}

void ImageView::computeHistogram()
{
    for (auto &h : m_hist)
        h.fill(0, 256);
    m_histMax = 1;
    if (m_image.isNull())
        return;
    const QImage small = m_image.scaled(256, 256, Qt::KeepAspectRatio, Qt::FastTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < small.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            ++m_hist[0][qRed(line[x])];
            ++m_hist[1][qGreen(line[x])];
            ++m_hist[2][qBlue(line[x])];
        }
    }
    // Ignore the extreme bins so clipped blacks/whites don't flatten the graph.
    for (const auto &h : m_hist)
        for (int i = 1; i < 255; ++i)
            m_histMax = qMax(m_histMax, h[i]);
}

void ImageView::setCropMode(bool on, double ratio)
{
    m_crop = on && !m_image.isNull() && !m_movie;
    m_cropRatio = ratio;
    m_cropImg = QRectF();
    m_cropDragging = false;
    setCursor(m_crop ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

QRect ImageView::cropRect() const
{
    return m_cropImg.toAlignedRect().intersected(m_image.rect());
}

// ---------------------------------------------------------------- painting

void ImageView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 18, 18));

    if (!m_image.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (m_fade < 1.0 && !m_prevPix.isNull()) {
            p.setOpacity(1.0 - m_fade);
            p.drawPixmap(m_prevRect, m_prevPix, QRectF(m_prevPix.rect()));
            p.setOpacity(m_fade);
        }

        const QRectF r = imageRect();
        if (m_image.hasAlphaChannel())
            drawCheckerboard(p, r);

        if (m_hqScale == m_scale && !m_hq.isNull()) {
            p.drawPixmap(QPointF(std::round(r.x()), std::round(r.y())), m_hq);
        } else {
            // Nearest-neighbour when zoomed in a lot, for pixel peeping.
            p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale * devicePixelRatioF() < 4.0);
            p.drawPixmap(r, m_pix, QRectF(m_pix.rect()));
        }
        p.setOpacity(1.0);

        if (m_crop)
            drawCrop(p);
        if (m_histVisible)
            drawHistogram(p);
    }

    if (!m_overlay.isEmpty())
        drawLabel(p, m_overlay, Qt::AlignTop | Qt::AlignLeft);
    if (!m_toast.isEmpty())
        drawLabel(p, m_toast, Qt::AlignTop | Qt::AlignHCenter);
}

void ImageView::drawCheckerboard(QPainter &p, const QRectF &r)
{
    static QPixmap tile = [] {
        QPixmap t(16, 16);
        t.fill(QColor(100, 100, 100));
        QPainter tp(&t);
        tp.fillRect(0, 0, 8, 8, QColor(70, 70, 70));
        tp.fillRect(8, 8, 8, 8, QColor(70, 70, 70));
        return t;
    }();
    p.fillRect(r.intersected(QRectF(rect())), QBrush(tile));
}

void ImageView::drawCrop(QPainter &p)
{
    const QRectF r = imageRect();
    QPainterPath outside;
    outside.addRect(r);
    QRectF c;
    if (!m_cropImg.isEmpty()) {
        c = QRectF(toWidget(m_cropImg.topLeft()), toWidget(m_cropImg.bottomRight()));
        QPainterPath inside;
        inside.addRect(c);
        outside = outside.subtracted(inside);
    }
    p.fillPath(outside, QColor(0, 0, 0, 150));

    if (c.isEmpty()) {
        drawLabel(p, tr("Drag to select · Enter apply · Esc cancel"), Qt::AlignBottom | Qt::AlignHCenter);
        return;
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 90), 1));
    for (int i = 1; i < 3; ++i) {
        const double x = c.left() + c.width() * i / 3.0;
        const double y = c.top() + c.height() * i / 3.0;
        p.drawLine(QPointF(x, c.top()), QPointF(x, c.bottom()));
        p.drawLine(QPointF(c.left(), y), QPointF(c.right(), y));
    }
    p.setPen(QPen(Qt::white, 1.5, Qt::DashLine));
    p.drawRect(c);
    const QRect cr = cropRect();
    drawLabel(p, tr("%1 × %2 · Enter apply · Esc cancel").arg(cr.width()).arg(cr.height()),
              Qt::AlignBottom | Qt::AlignHCenter);
}

void ImageView::drawHistogram(QPainter &p)
{
    const QRectF box(width() - 276, height() - 126, 266, 116);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(box, 6, 6);
    const QRectF g = box.adjusted(5, 5, -5, -5);
    const QColor colors[3] = {QColor(255, 60, 60, 150), QColor(60, 255, 60, 150), QColor(80, 120, 255, 150)};
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    for (int c = 0; c < 3; ++c) {
        QPolygonF poly;
        poly << g.bottomLeft();
        for (int i = 0; i < 256; ++i) {
            const double h = qMin(1.0, double(m_hist[c][i]) / m_histMax) * g.height();
            poly << QPointF(g.left() + g.width() * i / 255.0, g.bottom() - h);
        }
        poly << g.bottomRight();
        p.setBrush(colors[c]);
        p.drawPolygon(poly);
    }
    p.restore();
}

void ImageView::drawLabel(QPainter &p, const QString &text, Qt::Alignment align)
{
    p.save();
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 1.05);
    p.setFont(f);
    const QFontMetrics fm(f);
    const QRect tr = fm.boundingRect(QRect(0, 0, width() - 40, 2000), Qt::TextWordWrap, text);
    QRectF box(0, 0, tr.width() + 20, tr.height() + 12);
    const double m = 10;
    if (align & Qt::AlignLeft)
        box.moveLeft(m);
    else
        box.moveLeft((width() - box.width()) / 2);
    if (align & Qt::AlignBottom)
        box.moveBottom(height() - m);
    else
        box.moveTop(m + ((align & Qt::AlignHCenter) && !m_overlay.isEmpty() ? 34 : 0));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 160));
    p.drawRoundedRect(box, 6, 6);
    p.setPen(QColor(235, 235, 235));
    p.drawText(box.adjusted(10, 6, -10, -6), Qt::TextWordWrap, text);
    p.restore();
}

// ---------------------------------------------------------------- input

void ImageView::resizeEvent(QResizeEvent *)
{
    if (m_fit != Free) {
        applyFit();
    } else {
        clampOffset();
        update();
    }
}

void ImageView::wheelEvent(QWheelEvent *e)
{
    if (m_image.isNull())
        return;
    // Touchpad two-finger scroll pans; mouse wheel (or Ctrl+scroll) zooms to the cursor.
    if (!e->pixelDelta().isNull() && !(e->modifiers() & Qt::ControlModifier)) {
        m_offset += e->pixelDelta();
        clampOffset();
        update();
    } else {
        const double steps = e->angleDelta().y() / 120.0;
        zoomBy(std::pow(1.2, steps), e->position());
    }
    e->accept();
}

void ImageView::mousePressEvent(QMouseEvent *e)
{
    if (m_crop && e->button() == Qt::LeftButton) {
        const QPointF ip = toImage(e->position());
        m_cropStart = QPointF(qBound(0.0, ip.x(), double(m_image.width())), qBound(0.0, ip.y(), double(m_image.height())));
        m_cropImg = QRectF(m_cropStart, QSizeF(0, 0));
        m_cropDragging = true;
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPos = e->position();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    e->ignore();
}

void ImageView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_cropDragging) {
        const QPointF ip = toImage(e->position());
        const QPointF cur(qBound(0.0, ip.x(), double(m_image.width())), qBound(0.0, ip.y(), double(m_image.height())));
        double dx = cur.x() - m_cropStart.x(), dy = cur.y() - m_cropStart.y();
        if (m_cropRatio > 0 && dx != 0 && dy != 0) {
            double w = std::abs(dx), h = std::abs(dy);
            if (w / h > m_cropRatio)
                w = h * m_cropRatio;
            else
                h = w / m_cropRatio;
            dx = std::copysign(w, dx);
            dy = std::copysign(h, dy);
        }
        m_cropImg = QRectF(m_cropStart, QPointF(m_cropStart.x() + dx, m_cropStart.y() + dy))
                        .normalized()
                        .intersected(QRectF(QPointF(0, 0), QSizeF(m_image.size())));
        update();
    } else if (m_panning) {
        m_offset += e->position() - m_lastPos;
        m_lastPos = e->position();
        clampOffset();
        update();
    }

    if (!m_image.isNull()) {
        const QPointF ip = toImage(e->position());
        const QPoint px(int(std::floor(ip.x())), int(std::floor(ip.y())));
        if (m_image.rect().contains(px))
            emit pixelHovered(px, m_image.pixelColor(px));
    }
    e->ignore(); // let the main window see moves (fullscreen chrome)
}

void ImageView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_cropDragging) {
        m_cropDragging = false;
    } else if (m_panning) {
        m_panning = false;
        setCursor(m_crop ? Qt::CrossCursor : Qt::ArrowCursor);
    }
    e->accept();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *e)
{
    // Double-click toggles fullscreen (handled by the main window), like PotPlayer.
    // Fit / 100% stay on the 0 / 1 keys.
    if (m_crop)
        e->accept();
    else
        e->ignore();
}

bool ImageView::event(QEvent *e)
{
    if (e->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1.0 + g->value(), g->position());
            return true;
        }
    } else if (e->type() == QEvent::Gesture) {
        auto *ge = static_cast<QGestureEvent *>(e);
        if (auto *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture))) {
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)
                zoomBy(pinch->scaleFactor(), mapFromGlobal(pinch->centerPoint().toPoint()));
            return true;
        }
    }
    return QWidget::event(e);
}

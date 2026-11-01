#include "ThumbStrip.h"

#include "Exif.h"
#include "FolderList.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QWheelEvent>
#include <QtConcurrent>

static constexpr int kThumb = 160;
static constexpr int kVideoRole = Qt::UserRole + 1;

// Rounded thumbnails with an accent ring for the current file; film glyph for videos.
class ThumbDelegate : public QStyledItemDelegate
{
public:
    ThumbDelegate(ThumbStrip *strip)
        : QStyledItemDelegate(strip), m_strip(strip) {}

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        const bool grid = m_strip->isGrid();

        QRectF cell = QRectF(opt.rect).adjusted(4, 4, -4, -4);
        QRectF tile = cell;
        if (grid)
            tile.setHeight(cell.width());

        QPainterPath clip;
        clip.addRoundedRect(tile, 8, 8);
        p->fillPath(clip, QColor(255, 255, 255, hover ? 22 : 10));

        if (index.data(kVideoRole).toBool()) {
            const int s = int(tile.width() * 0.34);
            p->drawPixmap(QPointF(tile.center().x() - s / 2.0, tile.center().y() - s / 2.0),
                          Theme::pixmap(isAudioFile(index.data(Qt::UserRole).toString()) ? "music" : "film", s, p->device()->devicePixelRatioF(), QColor(255, 255, 255, 140)));
        } else {
            const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            const QPixmap pm = icon.pixmap(QSize(kThumb, kThumb));
            if (!pm.isNull()) {
                const QSizeF fit = QSizeF(pm.size()).scaled(tile.size() - QSizeF(8, 8), Qt::KeepAspectRatio);
                const QRectF target(tile.center().x() - fit.width() / 2, tile.center().y() - fit.height() / 2,
                                    fit.width(), fit.height());
                QPainterPath r;
                r.addRoundedRect(target, 5, 5);
                p->setClipPath(r);
                p->drawPixmap(target, pm, QRectF(pm.rect()));
                p->setClipping(false);
            }
        }

        if (selected) {
            p->setPen(QPen(Theme::kAccent, 2.5));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(tile.adjusted(1.25, 1.25, -1.25, -1.25), 8, 8);
        } else if (!hover) {
            p->fillPath(clip, QColor(0, 0, 0, 70)); // dim the others a little
        }

        if (grid) {
            const QRectF textRect(cell.left(), tile.bottom() + 4, cell.width(), cell.bottom() - tile.bottom() - 4);
            p->setPen(selected ? QColor(255, 255, 255) : QColor(170, 172, 180));
            p->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
                        opt.fontMetrics.elidedText(index.data(Qt::ToolTipRole).toString(), Qt::ElideMiddle, int(cell.width())));
        }
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return m_strip->gridSize();
    }

private:
    ThumbStrip *m_strip;
};
static constexpr int kStripHeight = 122;

static QString cacheDir()
{
    static const QString dir = [] {
        const QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs";
        QDir().mkpath(d);
        return d;
    }();
    return dir;
}

ThumbStrip::ThumbStrip(QWidget *parent)
    : QListWidget(parent)
    , m_generation(std::make_shared<std::atomic<int>>(0))
{
    setFocusPolicy(Qt::NoFocus);
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet("QListWidget { background: #121215; border-top: 1px solid #222327; }");
    setItemDelegate(new ThumbDelegate(this));
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover);
    setGrid(false);

    connect(this, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) { emit fileActivated(item->data(Qt::UserRole).toString()); });
}

ThumbStrip::~ThumbStrip()
{
    ++*m_generation; // stop background jobs
}

void ThumbStrip::setGrid(bool grid)
{
    m_grid = grid;
    setFlow(QListView::LeftToRight);
    setWrapping(grid);
    if (grid) {
        setIconSize(QSize(kThumb, kThumb));
        setGridSize(QSize(kThumb + 24, kThumb + 46));
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    } else {
        setIconSize(QSize(96, 96));
        setGridSize(QSize(108, 108));
        setFixedHeight(kStripHeight);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    if (currentItem())
        scrollToItem(currentItem(), QAbstractItemView::PositionAtCenter);
}

void ThumbStrip::setFiles(const QStringList &files)
{
    if (files == m_files)
        return;
    m_files = files;
    ++*m_generation;
    clear();
    m_items.clear();

    QStringList images;
    for (const QString &f : files) {
        auto *item = new QListWidgetItem(this);
        item->setData(Qt::UserRole, f);
        item->setToolTip(QFileInfo(f).fileName());
        if (mediaTypeOf(f) == MediaType::Image)
            images << f;
        else
            item->setData(kVideoRole, true);
        m_items.insert(f, item);
    }
    startJobs(images);
}

void ThumbStrip::refresh(const QString &path)
{
    if (m_items.contains(path))
        startJobs({path});
}

void ThumbStrip::setCurrent(const QString &path)
{
    QListWidgetItem *item = m_items.value(path);
    if (!item)
        return;
    setCurrentItem(item);
    scrollToItem(item, QAbstractItemView::PositionAtCenter);
}

void ThumbStrip::wheelEvent(QWheelEvent *e)
{
    if (!m_grid && e->angleDelta().x() == 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->angleDelta().y());
        e->accept();
        return;
    }
    QListWidget::wheelEvent(e);
}

QImage ThumbStrip::makeThumb(const QString &path)
{
    const QFileInfo fi(path);
    const QByteArray key = QCryptographicHash::hash(
        (fi.absoluteFilePath() + QString::number(fi.lastModified().toMSecsSinceEpoch()) + QString::number(fi.size())).toUtf8(),
        QCryptographicHash::Md5).toHex();
    const QString cached = cacheDir() + "/" + key + ".png";

    QImage img(cached);
    if (!img.isNull())
        return img;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid())
        reader.setScaledSize(size.scaled(kThumb, kThumb, Qt::KeepAspectRatio)); // fast DCT scaling for JPEG
    if (!reader.read(&img) && Exif::isRaw(path))
        img = Exif::rawPreview(path);
    if (img.isNull())
        return {};
    if (img.width() > kThumb || img.height() > kThumb)
        img = img.scaled(kThumb, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img.save(cached, "PNG");
    return img;
}

void ThumbStrip::startJobs(const QStringList &paths)
{
    const int gen = *m_generation;
    auto counter = m_generation;
    QPointer<ThumbStrip> self(this);
    (void)QtConcurrent::run([paths, gen, counter, self] {
        for (const QString &p : paths) {
            if (*counter != gen)
                return;
            const QImage t = makeThumb(p);
            QMetaObject::invokeMethod(qApp, [self, gen, p, t] {
                if (self)
                    self->onThumb(gen, p, t);
            }, Qt::QueuedConnection);
        }
    });
}

void ThumbStrip::onThumb(int generation, const QString &path, const QImage &thumb)
{
    if (generation != *m_generation || thumb.isNull())
        return;
    if (QListWidgetItem *item = m_items.value(path))
        item->setIcon(QIcon(QPixmap::fromImage(thumb)));
}

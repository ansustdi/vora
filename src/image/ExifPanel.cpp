#include "ExifPanel.h"

#include "Exif.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHeaderView>
#include <QLocale>
#include <QPainter>
#include <QTreeWidget>
#include <QVBoxLayout>

ExifPanel::ExifPanel(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(320);
    m_tree = new QTreeWidget(this);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setWordWrap(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setStyleSheet("QTreeWidget { background: #1a1a1a; }");

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(m_tree);
}

void ExifPanel::clear()
{
    m_tree->clear();
    m_pixel = nullptr;
    m_path.clear();
}

static QTreeWidgetItem *section(QTreeWidget *tree, const QString &title)
{
    auto *s = new QTreeWidgetItem(tree, {title});
    QFont f = s->font(0);
    f.setBold(true);
    s->setFont(0, f);
    s->setFirstColumnSpanned(true);
    s->setExpanded(true);
    return s;
}

void ExifPanel::showFile(const QString &path, const QSize &size, const QString &format)
{
    if (path == m_path)
        return;
    clear();
    m_path = path;
    const QFileInfo fi(path);
    const QLocale loc;

    auto *file = section(m_tree, tr("File"));
    new QTreeWidgetItem(file, {tr("Name"), fi.fileName()});
    new QTreeWidgetItem(file, {tr("Folder"), fi.absolutePath()});
    new QTreeWidgetItem(file, {tr("Size"), loc.formattedDataSize(fi.size())});
    new QTreeWidgetItem(file, {tr("Dimensions"), QString("%1 × %2 (%3 MP)").arg(size.width()).arg(size.height())
                                                    .arg(size.width() * double(size.height()) / 1e6, 0, 'f', 1)});
    new QTreeWidgetItem(file, {tr("Format"), format});
    new QTreeWidgetItem(file, {tr("Modified"), loc.toString(fi.lastModified(), QLocale::ShortFormat)});

    m_pixel = new QTreeWidgetItem(file, {tr("Pixel"), "—"});

    const Exif::Info info = Exif::read(path);
    if (!info.summary.isEmpty()) {
        auto *cam = section(m_tree, tr("Camera"));
        for (const auto &kv : info.summary)
            new QTreeWidgetItem(cam, {kv.first, kv.second});
    }
    if (!info.all.isEmpty()) {
        auto *all = section(m_tree, tr("All tags (%1)").arg(info.all.size()));
        all->setExpanded(false);
        for (const auto &kv : info.all)
            new QTreeWidgetItem(all, {kv.first, kv.second});
    } else if (!Exif::available()) {
        auto *s = section(m_tree, tr("EXIF"));
        new QTreeWidgetItem(s, {tr("n/a"), tr("built without exiv2")});
    }
}

void ExifPanel::setPixel(QPoint pos, const QColor &color)
{
    if (!m_pixel || !isVisible())
        return;
    QPixmap swatch(14, 14);
    swatch.fill(color);
    m_pixel->setIcon(1, QIcon(swatch));
    m_pixel->setText(1, QString("%1, %2  %3  rgb(%4, %5, %6)")
                            .arg(pos.x()).arg(pos.y())
                            .arg(color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb).toUpper())
                            .arg(color.red()).arg(color.green()).arg(color.blue()));
}

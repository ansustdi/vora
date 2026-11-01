#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

// Side panel: file info, camera/EXIF summary, all tags, and the pixel under the cursor.
class ExifPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ExifPanel(QWidget *parent = nullptr);

    void showFile(const QString &path, const QSize &size, const QString &format);
    void setPixel(QPoint pos, const QColor &color);
    void clear();

private:
    QTreeWidget *m_tree;
    QTreeWidgetItem *m_pixel = nullptr;
    QString m_path;
};

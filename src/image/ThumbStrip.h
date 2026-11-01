#pragma once

#include <QListWidget>
#include <atomic>
#include <memory>

// Filmstrip / grid of the folder's files. Thumbnails are built on the thread pool
// and cached on disk (keyed by path + mtime + size).
class ThumbStrip : public QListWidget
{
    Q_OBJECT
public:
    explicit ThumbStrip(QWidget *parent = nullptr);
    ~ThumbStrip() override;

    void setFiles(const QStringList &files);
    void setCurrent(const QString &path);
    void refresh(const QString &path);
    void setGrid(bool grid);
    bool isGrid() const { return m_grid; }

signals:
    void fileActivated(const QString &path);

protected:
    void wheelEvent(QWheelEvent *e) override;

private:
    void startJobs(const QStringList &paths);
    void onThumb(int generation, const QString &path, const QImage &thumb);
    static QImage makeThumb(const QString &path);

    QStringList m_files;
    QHash<QString, QListWidgetItem *> m_items;
    std::shared_ptr<std::atomic<int>> m_generation;
    bool m_grid = false;
};

#pragma once

#include <QDialog>
#include <QImage>

class QCheckBox;
class QSpinBox;

// Pure QImage edit operations.
namespace Edit {

QImage rotate(const QImage &img, int clockwiseDegrees);
QImage flip(const QImage &img, Qt::Orientation orientation);
QImage crop(const QImage &img, const QRect &rect);
QImage resize(const QImage &img, const QSize &size);

} // namespace Edit

class ResizeDialog : public QDialog
{
    Q_OBJECT
public:
    ResizeDialog(const QSize &current, QWidget *parent = nullptr);
    QSize result() const;

private:
    QSize m_orig;
    QSpinBox *m_w;
    QSpinBox *m_h;
    QSpinBox *m_pct;
    QCheckBox *m_keep;
    bool m_updating = false;
};

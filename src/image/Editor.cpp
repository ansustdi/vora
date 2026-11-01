#include "Editor.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QTransform>

namespace Edit {

QImage rotate(const QImage &img, int clockwiseDegrees)
{
    return img.transformed(QTransform().rotate(clockwiseDegrees));
}

QImage flip(const QImage &img, Qt::Orientation orientation)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return img.flipped(orientation == Qt::Horizontal ? Qt::Horizontal : Qt::Vertical);
#else
    return img.mirrored(orientation == Qt::Horizontal, orientation == Qt::Vertical);
#endif
}

QImage crop(const QImage &img, const QRect &rect)
{
    return img.copy(rect.intersected(img.rect()));
}

QImage resize(const QImage &img, const QSize &size)
{
    return img.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace Edit

ResizeDialog::ResizeDialog(const QSize &current, QWidget *parent)
    : QDialog(parent)
    , m_orig(current)
{
    setWindowTitle(tr("Resize image"));
    m_w = new QSpinBox(this);
    m_h = new QSpinBox(this);
    m_pct = new QSpinBox(this);
    for (QSpinBox *s : {m_w, m_h})
        s->setRange(1, 65535);
    m_pct->setRange(1, 1000);
    m_pct->setSuffix(" %");
    m_w->setValue(current.width());
    m_h->setValue(current.height());
    m_pct->setValue(100);
    m_keep = new QCheckBox(tr("Keep aspect ratio"), this);
    m_keep->setChecked(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *form = new QFormLayout(this);
    form->addRow(tr("Width"), m_w);
    form->addRow(tr("Height"), m_h);
    form->addRow(tr("Scale"), m_pct);
    form->addRow(m_keep);
    form->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_w, &QSpinBox::valueChanged, this, [this](int w) {
        if (m_updating || !m_keep->isChecked())
            return;
        m_updating = true;
        m_h->setValue(qMax(1, qRound(w * double(m_orig.height()) / m_orig.width())));
        m_pct->setValue(qRound(100.0 * w / m_orig.width()));
        m_updating = false;
    });
    connect(m_h, &QSpinBox::valueChanged, this, [this](int h) {
        if (m_updating || !m_keep->isChecked())
            return;
        m_updating = true;
        m_w->setValue(qMax(1, qRound(h * double(m_orig.width()) / m_orig.height())));
        m_pct->setValue(qRound(100.0 * h / m_orig.height()));
        m_updating = false;
    });
    connect(m_pct, &QSpinBox::valueChanged, this, [this](int pct) {
        if (m_updating)
            return;
        m_updating = true;
        m_w->setValue(qMax(1, qRound(m_orig.width() * pct / 100.0)));
        m_h->setValue(qMax(1, qRound(m_orig.height() * pct / 100.0)));
        m_updating = false;
    });
}

QSize ResizeDialog::result() const
{
    return QSize(m_w->value(), m_h->value());
}

#include "IconButton.h"

#include "Theme.h"

#include <QPainter>

IconButton::IconButton(const QString &icon, int size, int iconSize, QWidget *parent)
    : QAbstractButton(parent)
    , m_icon(icon)
    , m_size(size)
    , m_iconSize(iconSize)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(size, size);
}

void IconButton::setIconName(const QString &icon)
{
    if (icon == m_icon)
        return;
    m_icon = icon;
    update();
}

void IconButton::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void IconButton::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

void IconButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_hover || isDown() || isChecked()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, isDown() ? 46 : 28));
        p.drawEllipse(rect().adjusted(1, 1, -1, -1));
    }
    const QColor c = !isEnabled() ? QColor(255, 255, 255, 90)
                   : isChecked()  ? Theme::kAccent.lighter(130)
                   : m_hover      ? QColor(255, 255, 255)
                                  : QColor(255, 255, 255, 215);
    const QPixmap pm = Theme::pixmap(m_icon, m_iconSize, devicePixelRatioF(), c);
    const QPointF topLeft((width() - m_iconSize) / 2.0, (height() - m_iconSize) / 2.0);
    p.drawPixmap(topLeft, pm);
}

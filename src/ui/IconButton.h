#pragma once

#include <QAbstractButton>

// Round, borderless button showing a tinted SVG icon; soft highlight on hover.
class IconButton : public QAbstractButton
{
    Q_OBJECT
public:
    IconButton(const QString &icon, int size = 36, int iconSize = 20, QWidget *parent = nullptr);

    void setIconName(const QString &icon);
    QSize sizeHint() const override { return {m_size, m_size}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QString m_icon;
    int m_size;
    int m_iconSize;
    bool m_hover = false;
};

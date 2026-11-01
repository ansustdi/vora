#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

class QApplication;
class QWidget;

// Colours, the app-wide stylesheet and tinted SVG icons from :/icons.
namespace Theme {

inline const QColor kAccent(61, 139, 255);
inline const QColor kBackground(17, 17, 19);
inline const QColor kSurface(28, 29, 33);
inline const QColor kText(230, 230, 232);
inline const QColor kMuted(150, 152, 160);

void apply(QApplication &app);

// Dark title bar / caption colour (Windows 10/11); no-op elsewhere.
void styleWindow(QWidget *window);

// Renders :/icons/<name>.svg at `logicalSize`, tinted with `color`, for the given device pixel ratio.
QPixmap pixmap(const QString &name, int logicalSize, qreal dpr, const QColor &color = Qt::white);
QIcon icon(const QString &name, const QColor &color = QColor(220, 220, 224));

} // namespace Theme

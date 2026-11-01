#include "Theme.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QStyleFactory>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace Theme {

static QString css()
{
    const QString accent = kAccent.name();
    const QString accentHover = kAccent.lighter(115).name();
    return QStringLiteral(R"(
QToolTip { color: #e8e8ea; background: #1e1f23; border: 1px solid #33343a; padding: 5px 9px; border-radius: 6px; }

QMenu { background: #1c1d21; border: 1px solid #2e2f35; border-radius: 10px; padding: 6px; color: #e6e6e8; }
QMenu::item { padding: 7px 30px 7px 28px; border-radius: 6px; margin: 1px 0; }
QMenu::item:selected { background: %1; color: white; }
QMenu::item:disabled { color: #66676d; }
QMenu::separator { height: 1px; background: #2e2f35; margin: 5px 10px; }
QMenu::indicator { width: 14px; height: 14px; left: 8px; }
QMenu::indicator:checked { image: none; background: %1; border-radius: 4px; border: 3px solid #1c1d21; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #3a3b42; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: #3a3b42; border-radius: 4px; min-width: 30px; margin: 2px; }
QScrollBar::handle:hover { background: #4a4b53; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QDialog { background: #18191c; }
QPushButton { background: #2a2b31; border: 1px solid #36373e; border-radius: 7px; padding: 6px 16px; color: #e6e6e8; }
QPushButton:hover { background: #33343b; }
QPushButton:pressed { background: #24252a; }
QPushButton:default { border-color: %1; }
QPushButton:disabled { color: #66676d; }
QPushButton:checked { background: %1; border-color: %1; color: white; }

QPushButton#pill { background: %1; border: none; border-radius: 19px; padding: 10px 24px; font-weight: 600; color: white; }
QPushButton#pill:hover { background: %2; }
QPushButton#pillGhost { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.10); border-radius: 19px; padding: 10px 24px; font-weight: 600; color: #e6e6e8; }
QPushButton#pillGhost:hover { background: rgba(255,255,255,0.12); }
QPushButton#recentItem { text-align: left; background: transparent; border: none; padding: 8px 12px; border-radius: 8px; color: #cfd0d4; }
QPushButton#recentItem:hover { background: rgba(255,255,255,0.06); color: white; }

QLineEdit, QSpinBox { background: #141518; border: 1px solid #34353c; border-radius: 7px; padding: 5px 8px; selection-background-color: %1; color: #e6e6e8; }
QLineEdit:focus, QSpinBox:focus { border-color: %1; }

QTabWidget::pane { border: 1px solid #2c2d33; border-radius: 8px; top: -1px; background: #1b1c20; }
QTabBar::tab { background: transparent; padding: 8px 16px; color: #9a9ba2; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: white; border-bottom: 2px solid %1; }
QTabBar::tab:hover { color: #dcdce0; }

QPushButton#chip { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.10); border-radius: 14px; padding: 5px 12px; color: #e6e6e8; }
QPushButton#chip:hover { background: rgba(255,255,255,0.13); }
QPushButton#chip:checked { background: rgba(61,139,255,0.22); border-color: %1; color: white; }
QPushButton#chip:disabled { color: #6a6b72; background: rgba(255,255,255,0.03); }
QPushButton#chipPrimary { background: %1; border: none; border-radius: 14px; padding: 5px 14px; color: white; font-weight: 600; }
QPushButton#chipPrimary:hover { background: %2; }

QProgressBar { background: rgba(255,255,255,0.10); border: none; border-radius: 2px; min-height: 4px; max-height: 4px; }
QProgressBar::chunk { background: %1; border-radius: 2px; }

QLineEdit#searchField { border-radius: 19px; padding: 8px 14px; background: #141518; border: 1px solid #33343b; }
QLineEdit#searchField:focus { border-color: %1; }

QListWidget#results { background: transparent; border: none; outline: none; }
QListWidget#results::item { border-radius: 12px; margin: 2px 0; }
QListWidget#results::item:hover { background: rgba(255,255,255,0.04); }
QListWidget#results::item:selected { background: rgba(61,139,255,0.13); }

QComboBox { background: #1c1d21; border: 1px solid #34353c; border-radius: 7px; padding: 5px 10px; color: #e6e6e8; }
QComboBox:hover { border-color: #4a4b53; }
QComboBox QAbstractItemView { background: #1c1d21; border: 1px solid #2e2f35; selection-background-color: %1; color: #e6e6e8; outline: none; }

QWidget#downloadsPanel { background: #15161a; border-left: 1px solid #25262b; }

QTableWidget, QTreeWidget { background: #16171a; border: none; gridline-color: #26272b; alternate-background-color: #1a1b1f; }
QTableWidget::item:selected, QTreeWidget::item:selected { background: %1; color: white; }
QHeaderView::section { background: #1c1d21; color: #a0a1a8; border: none; border-bottom: 1px solid #2c2d33; padding: 6px 8px; }
)").arg(accent, accentHover);
}

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark); // dark title bars for dialogs too
#endif

    QPalette p;
    p.setColor(QPalette::Window, QColor(24, 25, 28));
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, QColor(20, 21, 24));
    p.setColor(QPalette::AlternateBase, QColor(30, 31, 35));
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, QColor(42, 43, 49));
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, kAccent.lighter(120));
    p.setColor(QPalette::ToolTipBase, QColor(30, 31, 35));
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::PlaceholderText, QColor(110, 111, 118));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 111, 118));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 111, 118));
    app.setPalette(p);

    QFont f = app.font();
#ifdef Q_OS_WIN
    f.setFamilies({"Segoe UI Variable Text", "Segoe UI"});
#endif
    f.setPointSizeF(10);
    app.setFont(f);

    app.setStyleSheet(css());
}

void styleWindow(QWidget *window)
{
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = RGB(kBackground.red(), kBackground.green(), kBackground.blue());
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR (Win11) */, &caption, sizeof(caption));
    const COLORREF text = RGB(200, 200, 205);
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
#else
    Q_UNUSED(window);
#endif
}

QPixmap pixmap(const QString &name, int logicalSize, qreal dpr, const QColor &color)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QString("%1|%2|%3|%4").arg(name).arg(logicalSize).arg(dpr).arg(color.rgba());
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;

    const int px = qRound(logicalSize * dpr);
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QSvgRenderer svg(QString(":/icons/%1.svg").arg(name));
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        svg.render(&p);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(img.rect(), color);
    }
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    cache.insert(key, pm);
    return pm;
}

QIcon icon(const QString &name, const QColor &color)
{
    QIcon ic;
    for (qreal dpr : {1.0, 1.5, 2.0})
        ic.addPixmap(pixmap(name, 16, dpr, color));
    return ic;
}

} // namespace Theme

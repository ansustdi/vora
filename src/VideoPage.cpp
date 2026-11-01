#include "VideoPage.h"

#include "ControlBar.h"
#include "MpvWidget.h"
#include "ui/Theme.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPainter>
#include <QPropertyAnimation>
#include <QVariantAnimation>

// Big round icon in the middle of the video that grows and fades out.
class FlashIcon : public QWidget
{
public:
    explicit FlashIcon(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(132, 132);
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        m_anim.setDuration(550);
        QObject::connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_t = v.toDouble();
            update();
        });
        QObject::connect(&m_anim, &QVariantAnimation::finished, this, &QWidget::hide);
        hide();
    }

    void flash(const QString &icon)
    {
        m_icon = icon;
        m_t = 0;
        show();
        raise();
        m_anim.stop();
        m_anim.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(1.0 - m_t);
        const double r = 40 + 18 * m_t;
        const QPointF c = QRectF(rect()).center();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawEllipse(c, r, r);
        const int s = 34;
        p.drawPixmap(QPointF(c.x() - s / 2.0 + (m_icon == "play" ? 3 : 0), c.y() - s / 2.0),
                     Theme::pixmap(m_icon, s, devicePixelRatioF()));
    }

private:
    QVariantAnimation m_anim;
    QString m_icon;
    double m_t = 1;
};

// Opaque centre panel: icon + title + subtitle.
class CenterMessage : public QWidget
{
public:
    explicit CenterMessage(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

    void set(const QString &icon, const QString &title, const QString &subtitle)
    {
        m_icon = icon;
        m_title = title;
        m_subtitle = subtitle;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRadialGradient bg(QPointF(width() / 2.0, height() * 0.42), qMax(width(), height()) * 0.7);
        bg.setColorAt(0, QColor(32, 36, 48));
        bg.setColorAt(1, QColor(12, 12, 14));
        p.fillRect(rect(), bg);

        const double cy = height() * 0.42;
        const int iconSize = 72;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 14));
        p.drawEllipse(QPointF(width() / 2.0, cy), 64, 64);
        p.drawPixmap(QPointF(width() / 2.0 - iconSize / 2.0, cy - iconSize / 2.0),
                     Theme::pixmap(m_icon, iconSize, devicePixelRatioF(), QColor(255, 255, 255, 200)));

        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.6);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(236, 236, 240));
        const QRectF titleRect(40, cy + 84, width() - 80, 40);
        p.drawText(titleRect, Qt::AlignHCenter | Qt::AlignTop,
                   QFontMetrics(f).elidedText(m_title, Qt::ElideMiddle, int(titleRect.width())));

        QFont s = font();
        p.setFont(s);
        p.setPen(QColor(150, 152, 160));
        p.drawText(QRectF(40, cy + 124, width() - 80, 60), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_subtitle);
    }

private:
    QString m_icon, m_title, m_subtitle;
};

// Dimmed overlay over the last frame with Replay (and Back to YouTube) buttons.
class EndScreen : public QWidget
{
public:
    explicit EndScreen(QWidget *parent)
        : QWidget(parent)
    {
        m_title = new QLabel(this);
        m_title->setAlignment(Qt::AlignCenter);
        QFont tf = m_title->font();
        tf.setPointSizeF(tf.pointSizeF() * 1.35);
        tf.setWeight(QFont::DemiBold);
        m_title->setFont(tf);
        QPalette tp = m_title->palette();
        tp.setColor(QPalette::WindowText, QColor(240, 240, 244));
        m_title->setPalette(tp);

        replay = new QPushButton(Theme::icon("replay", Qt::white), tr("  Replay"), this);
        replay->setObjectName("pill");
        replay->setIconSize(QSize(20, 20));
        replay->setToolTip(tr("Watch again (Space)"));
        youtube = new QPushButton(Theme::icon("youtube", QColor(255, 90, 90)), tr("  Back to YouTube"), this);
        youtube->setObjectName("pillGhost");
        youtube->setIconSize(QSize(20, 20));
        youtube->setToolTip(tr("Back to search results (Esc)"));
        for (QPushButton *b : {replay, youtube}) {
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            b->setMinimumHeight(44);
        }

        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(12);
        buttons->addStretch();
        buttons->addWidget(replay);
        buttons->addWidget(youtube);
        buttons->addStretch();

        auto *lay = new QVBoxLayout(this);
        lay->addStretch(5);
        lay->addWidget(m_title);
        lay->addSpacing(18);
        lay->addLayout(buttons);
        lay->addStretch(6);
        hide();
    }

    void set(const QString &title, bool offerYouTube, const QString &replayText)
    {
        replay->setText(replayText.isEmpty() ? tr("  Replay") : "  " + replayText);
        m_title->setText(fontMetrics().elidedText(title, Qt::ElideMiddle, qMax(200, width() - 120)));
        youtube->setVisible(offerYouTube);
    }

    QPushButton *replay;
    QPushButton *youtube;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 165));
    }
    // Swallow clicks so they don't reach the main window's click-to-pause / double-click-fullscreen.
    void mousePressEvent(QMouseEvent *e) override { e->accept(); }
    void mouseReleaseEvent(QMouseEvent *e) override { e->accept(); }
    void mouseDoubleClickEvent(QMouseEvent *e) override { e->accept(); }

private:
    QLabel *m_title;
};

VideoPage::VideoPage(MpvWidget *mpv, ControlBar *bar, QWidget *parent)
    : QWidget(parent)
    , m_mpv(mpv)
    , m_bar(bar)
{
    setMouseTracking(true);
    mpv->setParent(this);
    m_message = new CenterMessage(this);
    m_end = new EndScreen(this);
    connect(m_end->replay, &QPushButton::clicked, this, &VideoPage::replayRequested);
    connect(m_end->youtube, &QPushButton::clicked, this, &VideoPage::backToYouTubeRequested);
    bar->setParent(this);
    m_flash = new FlashIcon(this);

    m_fx = new QGraphicsOpacityEffect(bar);
    m_fx->setOpacity(1.0);
    bar->setGraphicsEffect(m_fx);
    m_fade = new QPropertyAnimation(m_fx, "opacity", this);
    m_fade->setDuration(180);
    connect(m_fade, &QPropertyAnimation::finished, this, [this] {
        if (!m_visible)
            m_bar->hide();
    });

    m_hide.setSingleShot(true);
    m_hide.setInterval(2200);
    connect(&m_hide, &QTimer::timeout, this, [this] {
        if (m_pinned || m_bar->isInteracting()) {
            m_hide.start();
            return;
        }
        setControlsVisible(false);
    });
}

void VideoPage::resizeEvent(QResizeEvent *)
{
    relayout();
}

void VideoPage::relayout()
{
    m_mpv->setGeometry(rect());
    m_message->setGeometry(rect());
    // End screen leaves the control bar usable (seek back, repeat, next file).
    const int bh = m_bar->sizeHint().height();
    m_end->setGeometry(0, 0, width(), height() - bh);
    m_bar->setGeometry(0, height() - bh, width(), bh);
    m_flash->move((width() - m_flash->width()) / 2, (height() - m_flash->height()) / 2);
    m_bar->raise();
    m_flash->raise();
}

void VideoPage::setControlsVisible(bool visible)
{
    if (visible == m_visible)
        return;
    m_visible = visible;
    m_fade->stop();
    m_fade->setStartValue(m_fx->opacity());
    m_fade->setEndValue(visible ? 1.0 : 0.0);
    if (visible)
        m_bar->show();
    m_fade->start();
    // Keep subtitles clear of the control bar while it is shown.
    const int liftPct = visible && height() > 0 ? qBound(0, int(100.0 * (m_bar->height() - 24) / height()), 40) : 0;
    m_mpv->setProp("sub-pos", QString::number(100 - liftPct));
    const Qt::CursorShape cursor = visible ? Qt::ArrowCursor : Qt::BlankCursor;
    m_mpv->setCursor(cursor);
    setCursor(cursor);
}

void VideoPage::poke()
{
    setControlsVisible(true);
    m_hide.start();
}

void VideoPage::setPinned(bool pinned)
{
    m_pinned = pinned;
    if (pinned)
        setControlsVisible(true);
    m_hide.start();
}

void VideoPage::flash(const QString &icon)
{
    m_flash->flash(icon);
}

void VideoPage::showMessage(const QString &icon, const QString &title, const QString &subtitle)
{
    m_message->set(icon, title, subtitle);
    m_message->show();
    relayout();
}

void VideoPage::clearMessage()
{
    m_message->hide();
}

void VideoPage::showEndScreen(const QString &title, bool offerYouTube, const QString &replayText)
{
    m_end->set(title, offerYouTube, replayText);
    m_end->show();
    m_end->raise();
    m_bar->raise();
    setPinned(true); // keep controls visible on the end screen
}

void VideoPage::hideEndScreen()
{
    m_end->hide();
}

bool VideoPage::endScreenVisible() const
{
    return m_end->isVisible();
}

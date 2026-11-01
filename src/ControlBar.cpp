#include "ControlBar.h"

#include "ui/IconButton.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>

static QString fmtTime(double sec, bool hours)
{
    const int s = qMax(0, int(sec));
    if (hours)
        return QString("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// QLabel that elides in the middle instead of growing the layout.
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override { return {20, QLabel::minimumSizeHint().height()}; }
    QSize sizeHint() const override { return {20, QLabel::sizeHint().height()}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(rect(), int(alignment()), fontMetrics().elidedText(text(), Qt::ElideMiddle, width()));
    }
};

// ======================================================================== SeekBar

SeekBar::SeekBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(44);
    setFocusPolicy(Qt::NoFocus);
}

void SeekBar::setDuration(double sec)
{
    m_duration = qMax(0.0, sec);
    update();
}

void SeekBar::setPosition(double sec)
{
    if (m_dragging)
        return;
    m_position = sec;
    update();
}

void SeekBar::setCache(double sec)
{
    m_cache = sec;
    update();
}

void SeekBar::setChapters(const QVector<Chapter> &chapters)
{
    m_chapters = chapters;
    update();
}

QRectF SeekBar::trackRect() const
{
    const double h = (m_hover || m_dragging) ? 6 : 4;
    return QRectF(8, height() - 12 - h / 2, width() - 16, h);
}

double SeekBar::timeAt(double x) const
{
    const QRectF t = trackRect();
    return qBound(0.0, (x - t.left()) / t.width(), 1.0) * m_duration;
}

double SeekBar::xOf(double sec) const
{
    const QRectF t = trackRect();
    return t.left() + (m_duration > 0 ? qBound(0.0, sec / m_duration, 1.0) : 0.0) * t.width();
}

void SeekBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    const QRectF t = trackRect();
    const double r = t.height() / 2;
    p.setBrush(QColor(255, 255, 255, 50));
    p.drawRoundedRect(t, r, r);
    if (m_duration <= 0)
        return;

    if (m_cache > m_position) {
        QRectF c = t;
        c.setRight(xOf(m_cache));
        p.setBrush(QColor(255, 255, 255, 60));
        p.drawRoundedRect(c, r, r);
    }
    QRectF played = t;
    played.setRight(xOf(m_position));
    p.setBrush(Theme::kAccent);
    p.drawRoundedRect(played, r, r);

    p.setBrush(QColor(0, 0, 0, 170));
    for (const Chapter &ch : m_chapters)
        if (ch.time > 0.5)
            p.drawRect(QRectF(xOf(ch.time) - 1, t.top(), 2, t.height()));

    if (m_hover || m_dragging) {
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(xOf(m_position), t.center().y()), 7, 7);
    }

    if (m_hover || m_dragging) {
        const double at = m_dragging ? m_position : timeAt(m_hoverX);
        QString text = fmtTime(at, m_duration >= 3600);
        for (const Chapter &ch : m_chapters)
            if (ch.time <= at && !ch.title.isEmpty())
                text = fmtTime(at, m_duration >= 3600) + "  ·  " + ch.title;
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.92);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const QFontMetrics fm(f);
        const double bw = fm.horizontalAdvance(text) + 18, bh = 22;
        const double cx = m_dragging ? xOf(m_position) : m_hoverX;
        QRectF bubble(qBound(0.0, cx - bw / 2, width() - bw), 0, bw, bh);
        p.setBrush(QColor(20, 21, 24, 235));
        p.drawRoundedRect(bubble, 6, 6);
        p.setPen(QColor(240, 240, 242));
        p.drawText(bubble, Qt::AlignCenter, text);
    }
}

void SeekBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_duration <= 0)
        return;
    m_dragging = true;
    m_position = timeAt(e->position().x());
    emit seekRequested(m_position, false);
    update();
}

void SeekBar::mouseMoveEvent(QMouseEvent *e)
{
    m_hoverX = e->position().x();
    if (m_dragging) {
        m_position = timeAt(m_hoverX);
        emit seekRequested(m_position, false);
    }
    update();
}

void SeekBar::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        emit seekRequested(m_position, true);
    }
    update();
}

void SeekBar::wheelEvent(QWheelEvent *e)
{
    if (m_duration > 0 && e->angleDelta().y() != 0) {
        const double step = e->angleDelta().y() > 0 ? 5 : -5;
        emit seekRequested(qBound(0.0, m_position + step, m_duration), true);
    }
    e->accept();
}

void SeekBar::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void SeekBar::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

// ======================================================================== VolumeBar

VolumeBar::VolumeBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(86, 30);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void VolumeBar::setVolume(double v)
{
    if (m_dragging)
        return;
    m_volume = v;
    setToolTip(tr("Volume %1%").arg(qRound(v)));
    update();
}

void VolumeBar::setFromX(double x)
{
    m_volume = qRound(qBound(0.0, (x - 6) / (width() - 12), 1.0) * 150);
    emit volumeRequested(m_volume);
    update();
}

void VolumeBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QRectF t(6, height() / 2.0 - 2, width() - 12, 4);
    p.setBrush(QColor(255, 255, 255, 50));
    p.drawRoundedRect(t, 2, 2);
    QRectF fill = t;
    fill.setWidth(t.width() * qBound(0.0, m_volume / 150.0, 1.0));
    p.setBrush(m_volume > 100 ? QColor(255, 176, 64) : QColor(255, 255, 255, 235));
    p.drawRoundedRect(fill, 2, 2);
    p.setBrush(QColor(255, 255, 255, 120));
    p.drawRect(QRectF(t.left() + t.width() * 100.0 / 150.0 - 0.5, t.top() - 3, 1, t.height() + 6));
    if (m_hover || m_dragging) {
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(fill.right(), t.center().y()), 6, 6);
    }
}

void VolumeBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    setFromX(e->position().x());
}

void VolumeBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging)
        setFromX(e->position().x());
}

void VolumeBar::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
    update();
}

void VolumeBar::wheelEvent(QWheelEvent *e)
{
    if (e->angleDelta().y() != 0) {
        m_volume = qBound(0.0, m_volume + (e->angleDelta().y() > 0 ? 5 : -5), 150.0);
        emit volumeRequested(m_volume);
        update();
    }
    e->accept();
}

void VolumeBar::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void VolumeBar::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

// ======================================================================== ControlBar

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);

    m_seek = new SeekBar(this);
    auto *prev = new IconButton("prev", 36, 18, this);
    m_play = new IconButton("play", 44, 26, this);
    auto *next = new IconButton("next", 36, 18, this);
    m_audio = new IconButton("audio", 36, 19, this);
    m_subs = new IconButton("subtitles", 36, 20, this);
    m_quality = new QPushButton(this);
    m_quality->setObjectName("qualityBadge");
    m_quality->setCursor(Qt::PointingHandCursor);
    m_quality->setFocusPolicy(Qt::NoFocus);
    m_quality->setToolTip(tr("Video quality"));
    m_quality->setStyleSheet("QPushButton#qualityBadge { background: rgba(255,255,255,0.12); color: white;"
                             " border: 1px solid rgba(255,255,255,0.22); border-radius: 6px; padding: 2px 8px;"
                             " font-weight: 600; }"
                             "QPushButton#qualityBadge:hover { background: rgba(255,255,255,0.22); }");
    m_quality->hide();
    m_repeat = new IconButton("repeat", 36, 19, this);
    m_repeat->setCheckable(true);
    m_repeat->setToolTip(tr("Loop: this video, or the whole playlist when playing one (R)"));
    m_mute = new IconButton("volume", 36, 20, this);
    m_volumeBar = new VolumeBar(this);
    auto *settings = new IconButton("settings", 36, 19, this);
    m_full = new IconButton("fullscreen", 36, 19, this);

    prev->setToolTip(tr("Previous video / file (PgUp)"));
    m_play->setToolTip(tr("Play / Pause (Space)"));
    next->setToolTip(tr("Next video / file (PgDn)"));
    m_audio->setToolTip(tr("Audio track (A)"));
    m_subs->setToolTip(tr("Subtitles (S) · search online Ctrl+J"));
    m_mute->setToolTip(tr("Mute (M)"));
    settings->setToolTip(tr("Settings (Ctrl+P)"));
    m_full->setToolTip(tr("Fullscreen (Enter)"));

    m_time = new QLabel("0:00 / 0:00", this);
    m_time->setCursor(Qt::PointingHandCursor);
    m_time->setToolTip(tr("Click: elapsed / remaining"));
    m_time->installEventFilter(this);
    QPalette tp = m_time->palette();
    tp.setColor(QPalette::WindowText, QColor(242, 242, 244));
    m_time->setPalette(tp);
    QFont tf = m_time->font();
    tf.setWeight(QFont::DemiBold);
    m_time->setFont(tf);

    m_title = new ElidedLabel(this);
    m_title->setAlignment(Qt::AlignCenter);
    QPalette lp = m_title->palette();
    lp.setColor(QPalette::WindowText, QColor(185, 186, 194));
    m_title->setPalette(lp);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    row->addWidget(prev);
    row->addWidget(m_play);
    row->addWidget(next);
    row->addSpacing(10);
    row->addWidget(m_time);
    row->addSpacing(16);
    row->addWidget(m_title, 1);
    row->addSpacing(16);
    row->addWidget(m_quality);
    row->addSpacing(4);
    row->addWidget(m_repeat);
    row->addWidget(m_audio);
    row->addWidget(m_subs);
    row->addSpacing(6);
    row->addWidget(m_mute);
    row->addWidget(m_volumeBar);
    row->addSpacing(6);
    row->addWidget(settings);
    row->addWidget(m_full);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 30, 12, 8);
    lay->setSpacing(0);
    lay->addWidget(m_seek);
    lay->addLayout(row);

    connect(prev, &IconButton::clicked, this, &ControlBar::prevClicked);
    connect(m_play, &IconButton::clicked, this, &ControlBar::playPauseClicked);
    connect(next, &IconButton::clicked, this, &ControlBar::nextClicked);
    connect(m_mute, &IconButton::clicked, this, &ControlBar::muteClicked);
    connect(m_subs, &IconButton::clicked, this, &ControlBar::subsClicked);
    connect(m_repeat, &IconButton::toggled, this, &ControlBar::repeatToggled);
    connect(m_quality, &QPushButton::clicked, this, &ControlBar::qualityClicked);
    connect(m_audio, &IconButton::clicked, this, &ControlBar::audioClicked);
    connect(settings, &IconButton::clicked, this, &ControlBar::settingsClicked);
    connect(m_full, &IconButton::clicked, this, &ControlBar::fullscreenClicked);
    connect(m_seek, &SeekBar::seekRequested, this, &ControlBar::seekRequested);
    connect(m_volumeBar, &VolumeBar::volumeRequested, this, &ControlBar::volumeRequested);
}

void ControlBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0.0, QColor(0, 0, 0, 0));
    g.setColorAt(0.45, QColor(0, 0, 0, 120));
    g.setColorAt(1.0, QColor(0, 0, 0, 215));
    p.fillRect(rect(), g);
}

void ControlBar::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updateTitle();
}

bool ControlBar::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_time && e->type() == QEvent::MouseButtonPress) {
        m_showRemaining = !m_showRemaining;
        updateTime();
        return true;
    }
    return QWidget::eventFilter(obj, e);
}

bool ControlBar::isInteracting() const
{
    return m_seek->isDragging() || underMouse();
}

QWidget *ControlBar::subsButton() const
{
    return m_subs;
}

QWidget *ControlBar::audioButton() const
{
    return m_audio;
}

QString ControlBar::formatTime(double sec, bool hours)
{
    return fmtTime(sec, hours);
}

void ControlBar::updateTime()
{
    const bool hours = m_duration >= 3600;
    if (m_live) {
        m_time->setText("<span style='color:#ff4d4d'>●</span> LIVE");
        return;
    }
    if (m_duration <= 0) {
        m_time->setText(formatTime(m_position, hours));
        return;
    }
    const QString left = m_showRemaining ? "−" + formatTime(m_duration - m_position, hours) : formatTime(m_position, hours);
    m_time->setText(left + "  /  " + formatTime(m_duration, hours));
}

void ControlBar::updateTitle()
{
    m_title->setText(m_fullTitle);
    m_title->setToolTip(m_fullTitle);
    m_title->update();
}

void ControlBar::setTitle(const QString &title)
{
    m_fullTitle = title;
    updateTitle();
}

void ControlBar::setDuration(double sec)
{
    m_duration = sec;
    m_seek->setDuration(sec);
    updateTime();
}

void ControlBar::setPosition(double sec)
{
    m_position = sec;
    m_seek->setPosition(sec);
    updateTime();
}

void ControlBar::setCache(double sec)
{
    m_seek->setCache(sec);
}

void ControlBar::setChapters(const QVector<Chapter> &chapters)
{
    m_seek->setChapters(chapters);
}

void ControlBar::setPaused(bool paused)
{
    m_play->setIconName(paused ? "play" : "pause");
}

void ControlBar::updateVolumeIcon()
{
    m_mute->setIconName(m_muted || m_volume <= 0 ? "mute" : m_volume < 50 ? "volume-low" : "volume");
}

void ControlBar::setVolume(double volume)
{
    m_volume = volume;
    m_volumeBar->setVolume(volume);
    updateVolumeIcon();
}

void ControlBar::setMuted(bool muted)
{
    m_muted = muted;
    updateVolumeIcon();
}

void ControlBar::setQuality(const QString &label)
{
    m_quality->setText(label);
    m_quality->setVisible(!label.isEmpty());
}

QWidget *ControlBar::qualityButton() const
{
    return m_quality;
}

void ControlBar::setRepeat(bool on)
{
    QSignalBlocker block(m_repeat);
    m_repeat->setChecked(on);
}

void ControlBar::setLive(bool live)
{
    m_live = live;
    setProperty("live", live);
    m_seek->setVisible(!live); // nothing to seek in a live stream
    updateTime();
}

void ControlBar::setFullscreen(bool fullscreen)
{
    m_full->setIconName(fullscreen ? "fullscreen-exit" : "fullscreen");
    m_full->setToolTip(fullscreen ? tr("Exit fullscreen (Esc)") : tr("Fullscreen (Enter)"));
}

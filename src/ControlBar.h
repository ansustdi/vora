#pragma once

#include <QVector>
#include <QWidget>

class IconButton;
class QLabel;

struct Chapter
{
    double time = 0;
    QString title;
};

// Slim seek bar: buffered range, chapter marks, hover time bubble, handle on hover.
class SeekBar : public QWidget
{
    Q_OBJECT
public:
    explicit SeekBar(QWidget *parent = nullptr);

    void setDuration(double sec);
    void setPosition(double sec);
    void setCache(double sec);
    void setChapters(const QVector<Chapter> &chapters);
    bool isDragging() const { return m_dragging; }
    QSize sizeHint() const override { return {200, 30}; }

signals:
    // exact=false while dragging (fast keyframe seek), true on release.
    void seekRequested(double sec, bool exact);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF trackRect() const;
    double timeAt(double x) const;
    double xOf(double sec) const;

    double m_duration = 0;
    double m_position = 0;
    double m_cache = 0;
    QVector<Chapter> m_chapters;
    bool m_hover = false;
    bool m_dragging = false;
    double m_hoverX = 0;
};

// Compact volume slider (0–150 %, tick at 100 %).
class VolumeBar : public QWidget
{
    Q_OBJECT
public:
    explicit VolumeBar(QWidget *parent = nullptr);
    void setVolume(double v);
    QSize sizeHint() const override { return {86, 30}; }

signals:
    void volumeRequested(double v);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void setFromX(double x);

    double m_volume = 100;
    bool m_dragging = false;
    bool m_hover = false;
};

// Overlay controls drawn over the video with a bottom gradient.
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);

    void setDuration(double sec);
    void setPosition(double sec);
    void setCache(double sec);
    void setChapters(const QVector<Chapter> &chapters);
    void setPaused(bool paused);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setFullscreen(bool fullscreen);
    void setTitle(const QString &title);
    void setLive(bool live);
    void setRepeat(bool on);
    void setQuality(const QString &label); // empty hides the badge
    QWidget *qualityButton() const;

    bool isInteracting() const;
    QWidget *subsButton() const;
    QWidget *audioButton() const;

signals:
    void playPauseClicked();
    void prevClicked();
    void nextClicked();
    void seekRequested(double sec, bool exact);
    void volumeRequested(double volume);
    void muteClicked();
    void fullscreenClicked();
    void subsClicked();
    void audioClicked();
    void settingsClicked();
    void repeatToggled(bool on);
    void qualityClicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    static QString formatTime(double sec, bool hours);
    void updateTime();
    void updateTitle();
    void updateVolumeIcon();

    SeekBar *m_seek;
    VolumeBar *m_volumeBar;
    IconButton *m_play;
    IconButton *m_mute;
    IconButton *m_subs;
    IconButton *m_audio;
    IconButton *m_full;
    IconButton *m_repeat;
    class QPushButton *m_quality;
    QLabel *m_time;
    QLabel *m_title;
    QString m_fullTitle;
    double m_duration = 0;
    double m_position = 0;
    double m_volume = 100;
    bool m_muted = false;
    bool m_showRemaining = false;
    bool m_live = false;
};

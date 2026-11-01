#pragma once

#include <QTimer>
#include <QWidget>

class ControlBar;
class MpvWidget;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class FlashIcon;
class CenterMessage;
class EndScreen;

// Video surface with auto-hiding overlay controls, a centre play/pause flash and
// a centre message (audio-only file, playback error).
class VideoPage : public QWidget
{
    Q_OBJECT
public:
    VideoPage(MpvWidget *mpv, ControlBar *bar, QWidget *parent = nullptr);

    void poke();                    // mouse activity: show controls, restart the hide timer
    void setPinned(bool pinned);    // paused / idle: keep controls visible
    void flash(const QString &icon);
    void showMessage(const QString &icon, const QString &title, const QString &subtitle = {});
    void clearMessage();

    // Shown when playback reaches the end and nothing plays next.
    void showEndScreen(const QString &title, bool offerYouTube, const QString &replayText = {});
    void hideEndScreen();
    bool endScreenVisible() const;

signals:
    void replayRequested();
    void backToYouTubeRequested();

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void relayout();
    void setControlsVisible(bool visible);

    MpvWidget *m_mpv;
    ControlBar *m_bar;
    FlashIcon *m_flash;
    CenterMessage *m_message;
    EndScreen *m_end;
    QGraphicsOpacityEffect *m_fx;
    QPropertyAnimation *m_fade;
    QTimer m_hide;
    bool m_pinned = true;
    bool m_visible = true;
};

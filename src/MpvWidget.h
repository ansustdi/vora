#pragma once

#include <QOpenGLWidget>
#include <QStringList>
#include <QVariant>

#include <mpv/client.h>
#include <mpv/render_gl.h>

// QOpenGLWidget hosting libmpv via the render API (portable, no native window handles).
class MpvWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    bool command(const QStringList &args);
    void setProp(const char *name, const QString &value);
    void setProp(const char *name, double value);
    void setFlag(const char *name, bool value);
    QString getString(const char *name) const;
    double getDouble(const char *name, double def = 0) const;
    bool getFlag(const char *name) const;
    QVariant getNode(const char *name) const;
    void showText(const QString &text, int ms = 2000);

    // A stream yt-dlp already resolved. The next loadfile of `url` plays `edl` instead (swapped in
    // by an on_load hook that runs before mpv's ytdl hook), so the page URL stays the file's path:
    // resume positions, media-title and the HTTP identity all stay per-file.
    struct PendingStream
    {
        QString url, edl, title, userAgent;
        QStringList headers;
    };
    void setPendingStream(const PendingStream &p) { m_pending = p; }

signals:
    void positionChanged(double sec);
    void durationChanged(double sec);
    void pauseChanged(bool paused);
    void volumeChanged(double volume);
    void muteChanged(bool muted);
    void trackListChanged();
    void fileLoaded();
    void eofReached();
    void cacheChanged(double sec);
    void videoHeightChanged(int height);
    void chaptersChanged();
    void loadFailed(const QString &reason);

protected:
    void initializeGL() override;
    void paintGL() override;

private slots:
    void onMpvEvents();
    void maybeUpdate();

private:
    void handleEvent(mpv_event *event);
    static void onWakeup(void *ctx);
    static void onUpdate(void *ctx);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_gl = nullptr;
    QByteArray m_vf; // auto-inserted pixel format conversion, see handleEvent
    PendingStream m_pending;
    bool m_fileReady = false; // between FILE_LOADED and the next START_FILE: eof-reached is about this file
};

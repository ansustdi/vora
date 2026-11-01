#include "MpvWidget.h"

#include "stream/YtDlp.h"

#include <QDebug>
#include <QDir>
#include <QOpenGLContext>
#include <QStandardPaths>
#include <stdexcept>
#include <vector>

static QVariant nodeToVariant(const mpv_node *n)
{
    switch (n->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(n->u.string);
    case MPV_FORMAT_FLAG:
        return bool(n->u.flag);
    case MPV_FORMAT_INT64:
        return qlonglong(n->u.int64);
    case MPV_FORMAT_DOUBLE:
        return n->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        for (int i = 0; i < n->u.list->num; ++i)
            list << nodeToVariant(&n->u.list->values[i]);
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < n->u.list->num; ++i)
            map.insert(QString::fromUtf8(n->u.list->keys[i]), nodeToVariant(&n->u.list->values[i]));
        return map;
    }
    default:
        return {};
    }
}

static void *getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return nullptr;
    return reinterpret_cast<void *>(ctx->getProcAddress(QByteArray(name)));
}

MpvWidget::MpvWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);

    m_mpv = mpv_create();
    if (!m_mpv)
        throw std::runtime_error("could not create mpv context");

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString watchLater = dataDir + "/watch_later";
    QDir().mkpath(watchLater);
    const QString shots = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Vora";

    mpv_set_option_string(m_mpv, "config", "no");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    // Copy-back hw decoding (GPU decodes, frames copied to RAM): avoids GPU->OpenGL interop entirely.
    mpv_set_option_string(m_mpv, "hwdec", "auto-copy-safe");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "osc", "no");
    // Page URLs (YouTube, Twitch, Kick…) are resolved by yt-dlp through mpv's built-in ytdl hook.
    mpv_set_option_string(m_mpv, "ytdl", "yes");
    const QByteArray ytdlOpt = ("ytdl_hook-ytdl_path=" + YtDlp::pathForMpv()).toUtf8();
    mpv_set_option_string(m_mpv, "script-opts", ytdlOpt.constData());
    const QByteArray ytdlRaw = YtDlp::rawOptions().toUtf8();
    if (!ytdlRaw.isEmpty())
        mpv_set_option_string(m_mpv, "ytdl-raw-options", ytdlRaw.constData());
    // YouTube answers 403 to open-ended or large range requests; fetch HTTP in 1 MiB ranges.
    mpv_set_option_string(m_mpv, "stream-lavf-o", "request_size=1048576");
    mpv_set_option_string(m_mpv, "sub-auto", "fuzzy");
    // Release groups often ship subtitles in a Subs/ folder next to the video.
    const QByteArray subDirs = QStringList({"subs", "Subs", "subtitles", "Subtitles", "sub", "Sub"}).join(QDir::listSeparator()).toUtf8();
    mpv_set_option_string(m_mpv, "sub-file-paths", subDirs.constData());
    mpv_set_option_string(m_mpv, "osd-font-size", "30");
    mpv_set_option_string(m_mpv, "osd-border-size", "1.5");
    mpv_set_option_string(m_mpv, "osd-margin-x", "28");
    mpv_set_option_string(m_mpv, "osd-margin-y", "22");
    mpv_set_option_string(m_mpv, "osd-bar-h", "2.5");
    mpv_set_option_string(m_mpv, "osd-duration", "1400");
    mpv_set_option_string(m_mpv, "sub-border-size", "2.5");
    mpv_set_option_string(m_mpv, "volume-max", "150");
    // option names changed across mpv versions; unknown ones are ignored
    const QByteArray wl = QDir::toNativeSeparators(watchLater).toUtf8();
    mpv_set_option_string(m_mpv, "watch-later-dir", wl.constData());
    mpv_set_option_string(m_mpv, "watch-later-directory", wl.constData());
    const QByteArray sd = QDir::toNativeSeparators(shots).toUtf8();
    mpv_set_option_string(m_mpv, "screenshot-dir", sd.constData());
    mpv_set_option_string(m_mpv, "screenshot-directory", sd.constData());

    // Debug hooks: VORA_MPV_LOG=<file>, VORA_MPV_OPTS="opt=value;opt2=value"
    if (const QByteArray log = qgetenv("VORA_MPV_LOG"); !log.isEmpty())
        mpv_set_option_string(m_mpv, "log-file", log.constData());
    for (const QByteArray &kv : qgetenv("VORA_MPV_OPTS").split(';')) {
        const int eq = kv.indexOf('=');
        if (eq > 0)
            mpv_set_option_string(m_mpv, kv.left(eq).trimmed().constData(), kv.mid(eq + 1).trimmed().constData());
    }

    if (mpv_initialize(m_mpv) < 0)
        throw std::runtime_error("could not initialize mpv context");

    mpv_request_log_messages(m_mpv, "warn");

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NONE);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "demuxer-cache-time", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "chapter-list", MPV_FORMAT_NONE);
    mpv_observe_property(m_mpv, 0, "video-params/h", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "video-dec-params/pixelformat", MPV_FORMAT_STRING);

    // Lower values run first. Lua hooks get "priority - 50", so ytdl_hook's 10 is really -40.
    mpv_hook_add(m_mpv, 0, "on_load", -60);

    mpv_set_wakeup_callback(m_mpv, MpvWidget::onWakeup, this);
}

MpvWidget::~MpvWidget()
{
    // mpv's threads must not post events to this object once it's gone.
    mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    if (m_gl)
        mpv_render_context_set_update_callback(m_gl, nullptr, nullptr);
    makeCurrent();
    if (m_gl)
        mpv_render_context_free(m_gl);
    doneCurrent();
    mpv_terminate_destroy(m_mpv);
}

bool MpvWidget::command(const QStringList &args)
{
    QList<QByteArray> utf8;
    std::vector<const char *> argv;
    utf8.reserve(args.size());
    for (const QString &a : args)
        utf8 << a.toUtf8();
    for (const QByteArray &b : utf8)
        argv.push_back(b.constData());
    argv.push_back(nullptr);
    const int err = mpv_command(m_mpv, argv.data());
    if (err < 0)
        qWarning() << "mpv command failed" << args << mpv_error_string(err);
    return err >= 0;
}

void MpvWidget::setProp(const char *name, const QString &value)
{
    mpv_set_property_string(m_mpv, name, value.toUtf8().constData());
}

void MpvWidget::setProp(const char *name, double value)
{
    mpv_set_property(m_mpv, name, MPV_FORMAT_DOUBLE, &value);
}

void MpvWidget::setFlag(const char *name, bool value)
{
    int flag = value ? 1 : 0;
    mpv_set_property(m_mpv, name, MPV_FORMAT_FLAG, &flag);
}

QString MpvWidget::getString(const char *name) const
{
    char *s = mpv_get_property_string(m_mpv, name);
    if (!s)
        return {};
    QString r = QString::fromUtf8(s);
    mpv_free(s);
    return r;
}

double MpvWidget::getDouble(const char *name, double def) const
{
    double v = def;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_DOUBLE, &v) < 0)
        return def;
    return v;
}

bool MpvWidget::getFlag(const char *name) const
{
    int flag = 0;
    mpv_get_property(m_mpv, name, MPV_FORMAT_FLAG, &flag);
    return flag != 0;
}

QVariant MpvWidget::getNode(const char *name) const
{
    mpv_node node;
    if (mpv_get_property(m_mpv, name, MPV_FORMAT_NODE, &node) < 0)
        return {};
    QVariant v = nodeToVariant(&node);
    mpv_free_node_contents(&node);
    return v;
}

void MpvWidget::showText(const QString &text, int ms)
{
    command({"show-text", text, QString::number(ms)});
}

void MpvWidget::initializeGL()
{
    mpv_opengl_init_params glInit{};
    glInit.get_proc_address = getProcAddress;
    glInit.get_proc_address_ctx = nullptr;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    if (mpv_render_context_create(&m_gl, m_mpv, params) < 0)
        throw std::runtime_error("failed to initialize mpv GL context");
    mpv_render_context_set_update_callback(m_gl, MpvWidget::onUpdate, this);
}

void MpvWidget::paintGL()
{
    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{static_cast<int>(defaultFramebufferObject()),
                       static_cast<int>(width() * dpr), static_cast<int>(height() * dpr), 0};
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_gl, params);
}

void MpvWidget::onWakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "onMpvEvents", Qt::QueuedConnection);
}

void MpvWidget::onUpdate(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "maybeUpdate", Qt::QueuedConnection);
}

void MpvWidget::maybeUpdate()
{
    // When minimized Qt skips paintGL, but mpv still needs frames consumed.
    if (window()->isMinimized()) {
        makeCurrent();
        paintGL();
        context()->swapBuffers(context()->surface());
        doneCurrent();
    } else {
        update();
    }
}

void MpvWidget::onMpvEvents()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(event);
    }
}

void MpvWidget::handleEvent(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *prop = static_cast<mpv_event_property *>(event->data);
        const QByteArray name(prop->name);
        if (prop->format == MPV_FORMAT_DOUBLE) {
            const double v = *static_cast<double *>(prop->data);
            if (name == "time-pos")
                emit positionChanged(v);
            else if (name == "duration")
                emit durationChanged(v);
            else if (name == "volume")
                emit volumeChanged(v);
            else if (name == "demuxer-cache-time")
                emit cacheChanged(v);
            else if (name == "video-params/h")
                emit videoHeightChanged(int(v));
        } else if (prop->format == MPV_FORMAT_FLAG) {
            const bool v = *static_cast<int *>(prop->data) != 0;
            if (name == "pause")
                emit pauseChanged(v);
            else if (name == "mute")
                emit muteChanged(v);
            else if (name == "eof-reached" && v && m_fileReady) // not a late one from the previous file
                emit eofReached();
        } else if (prop->format == MPV_FORMAT_STRING && name == "video-dec-params/pixelformat") {
            // Semi-planar frames (nv12/p010, what hw decoders output) render with colour stripe
            // corruption through the libmpv OpenGL path inside Qt (NVIDIA, mpv 0.41), while planar
            // yuv420p renders correctly. De-interleave chroma on the CPU: cheap and keeps bit depth.
            const QByteArray fmt(*static_cast<char **>(prop->data));
            const QByteArray want = fmt == "nv12" ? "format=yuv420p"
                                  : fmt == "p010" ? "format=yuv420p10"
                                                  : QByteArray();
            if (want != m_vf) {
                m_vf = want;
                mpv_set_property_string(m_mpv, "vf", want.constData());
            }
        } else if (name == "track-list") {
            emit trackListChanged();
        } else if (name == "chapter-list") {
            emit chaptersChanged();
        }
        break;
    }
    case MPV_EVENT_START_FILE:
        m_fileReady = false;
        break;
    case MPV_EVENT_FILE_LOADED:
        m_fileReady = true;
        emit fileLoaded();
        break;
    case MPV_EVENT_HOOK: {
        auto *hook = static_cast<mpv_event_hook *>(event->data);
        if (QByteArray(hook->name) == "on_load") {
            const PendingStream p = m_pending;
            m_pending = {};
            if (!p.url.isEmpty() && getString("path") == p.url) {
                setProp("stream-open-filename", p.edl);
                if (!p.title.isEmpty())
                    setProp("file-local-options/force-media-title", p.title);
                if (!p.userAgent.isEmpty())
                    setProp("file-local-options/user-agent", p.userAgent);
                QStringList headers; // mpv's list option: commas inside a value aren't escapable
                for (const QString &h : p.headers)
                    if (!h.contains(','))
                        headers << h;
                if (!headers.isEmpty())
                    setProp("file-local-options/http-header-fields", headers.join(','));
            }
        }
        mpv_hook_continue(m_mpv, hook->id);
        break;
    }
    case MPV_EVENT_END_FILE: {
        auto *end = static_cast<mpv_event_end_file *>(event->data);
        if (end->reason == MPV_END_FILE_REASON_ERROR)
            emit loadFailed(QString::fromUtf8(mpv_error_string(end->error)));
        break;
    }
    case MPV_EVENT_LOG_MESSAGE: {
        auto *msg = static_cast<mpv_event_log_message *>(event->data);
        qWarning().noquote() << "[mpv]" << msg->prefix << QString::fromUtf8(msg->text).trimmed();
        break;
    }
    default:
        break;
    }
}

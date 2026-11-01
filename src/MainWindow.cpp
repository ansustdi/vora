#include "MainWindow.h"

#include "ControlBar.h"
#include "MpvWidget.h"
#include "Platform.h"
#include "Settings.h"
#include "SettingsDialog.h"
#include "VideoPage.h"
#include "WelcomePage.h"
#include "image/Editor.h"
#include "image/Exif.h"
#include "image/ExifPanel.h"
#include "image/ImageView.h"
#include "image/ThumbStrip.h"
#include "subs/SubtitleManager.h"
#include "subs/SubtitleSearchDialog.h"
#include "subs/SubtitlePickerDialog.h"
#include "stream/OpenStreamDialog.h"
#include "stream/YouTube.h"
#include "stream/YouTubePage.h"
#include "stream/YtDlp.h"
#include "stream/StreamResolver.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCollator>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>

// VORA_HWDEC=<mpv hwdec value> overrides the setting (debugging decoder issues).
static QString hwdecMode()
{
    const QString env = qEnvironmentVariable("VORA_HWDEC");
    if (!env.isEmpty())
        return env;
    return Settings::hwdec() ? QStringLiteral("auto-copy-safe") : QStringLiteral("no");
}

static bool isUrl(const QString &path)
{
    return path.contains("://") && !QFileInfo::exists(path);
}

static bool isJpeg(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Vora");
    setWindowIcon(QIcon(":/icon.svg"));
    resize(1180, 720);
    setAcceptDrops(true);
    Theme::styleWindow(this);
    setFocusPolicy(Qt::StrongFocus);

    m_mpv = new MpvWidget;
    m_controls = new ControlBar;
    m_videoPage = new VideoPage(m_mpv, m_controls);

    m_imageView = new ImageView;
    m_thumbs = new ThumbStrip;
    m_exif = new ExifPanel;
    m_imagePage = new QWidget;
    m_imageColumn = new QVBoxLayout;
    m_imageColumn->setContentsMargins(0, 0, 0, 0);
    m_imageColumn->setSpacing(0);
    m_imageColumn->addWidget(m_imageView, 1);
    m_imageColumn->addWidget(m_thumbs, 0);
    auto *imageRow = new QHBoxLayout(m_imagePage);
    imageRow->setContentsMargins(0, 0, 0, 0);
    imageRow->setSpacing(0);
    imageRow->addLayout(m_imageColumn, 1);
    imageRow->addWidget(m_exif);

    m_welcome = new WelcomePage;
    connect(m_welcome, &WelcomePage::openFileClicked, this, &MainWindow::openFileDialog);
    connect(m_welcome, &WelcomePage::openFolderClicked, this, &MainWindow::openFolderDialog);
    connect(m_welcome, &WelcomePage::youtubeClicked, this, &MainWindow::showYouTube);
    connect(m_welcome, &WelcomePage::recentClicked, this, &MainWindow::openPath);

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_welcome);
    m_stack->addWidget(m_videoPage);
    m_stack->addWidget(m_imagePage);

    m_downloads = new DownloadManager(this);
    m_playlists = new PlaylistStore(this);
    m_resolver = new StreamResolver(this);
    m_youtube = new YouTubePage(m_downloads, m_playlists);
    connect(m_youtube, &YouTubePage::playQueueRequested, this, &MainWindow::playQueue);
    connect(m_youtube, &YouTubePage::prefetchRequested, this,
            [this](const QString &url) { m_resolver->prefetch(url, currentStreamFormat()); });
    m_stack->addWidget(m_youtube);
    connect(m_youtube, &YouTubePage::playRequested, this,
            [this](const QString &urlOrPath, const QString &) { playFromYouTube(urlOrPath); });
    connect(m_youtube, &YouTubePage::backRequested, this, &MainWindow::leaveYouTube);
    setCentralWidget(m_stack);

    m_loader = new ImageLoader(this);
    m_subs = new SubtitleManager(m_mpv, this);

    m_thumbs->setVisible(Settings::showThumbs());
    m_exif->setVisible(Settings::showExif());
    m_list.setSort(FolderList::Sort(Settings::sortMode()));

    m_mpv->setProp("volume", Settings::volume());
    m_mpv->setProp("loop-file", Settings::repeat() ? QStringLiteral("inf") : QStringLiteral("no"));
    m_controls->setRepeat(Settings::repeat());
    applySubtitleLanguages();
    m_mpv->setProp("hwdec", hwdecMode());

    // --- video wiring
    connect(m_mpv, &MpvWidget::positionChanged, m_controls, &ControlBar::setPosition);
    connect(m_videoPage, &VideoPage::replayRequested, this, &MainWindow::replay);
    connect(m_videoPage, &VideoPage::backToYouTubeRequested, this, &MainWindow::showYouTube);
    connect(m_controls, &ControlBar::repeatToggled, this, &MainWindow::setRepeat);
    connect(m_mpv, &MpvWidget::videoHeightChanged, this, &MainWindow::updateQualityBadge);
    connect(m_controls, &ControlBar::qualityClicked, this, [this] {
        QMenu menu(this);
        fillQualityMenu(&menu);
        QWidget *b = m_controls->qualityButton();
        menu.exec(b->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    connect(m_mpv, &MpvWidget::durationChanged, m_controls, &ControlBar::setDuration);
    connect(m_mpv, &MpvWidget::pauseChanged, this, [this](bool paused) {
        m_controls->setPaused(paused);
        m_videoPage->setPinned(paused);
    });
    connect(m_mpv, &MpvWidget::cacheChanged, m_controls, &ControlBar::setCache);
    connect(m_mpv, &MpvWidget::chaptersChanged, this, &MainWindow::updateChapters);
    connect(m_mpv, &MpvWidget::loadFailed, this, [this](const QString &reason) {
        if (!hadVideo())
            return;
        if (isUrl(m_current))
            m_videoPage->showMessage("alert", tr("Can't play this stream"),
                                     YtDlp::needsResolver(m_current)
                                         ? tr("The channel may be offline, the video private or region-locked, "
                                              "or yt-dlp needs an update.")
                                         : reason);
        else
            m_videoPage->showMessage("alert", tr("Can't play %1").arg(QFileInfo(m_current).fileName()), reason);
        m_videoPage->setPinned(true);
        m_controls->setPaused(true);
        m_controls->setDuration(0);
        if (queueActive()) {
            // A private / removed video shouldn't stop the whole list; give up after 3 in a row.
            const int failed = m_queueIndex;
            const bool more = failed + 1 < m_queue.size() || Settings::repeat();
            if (++m_queueFailures < 3 && more) {
                m_videoPage->showMessage("alert", tr("Skipping a video that can't be played"), reason);
                QTimer::singleShot(1500, this, [this, failed] {
                    if (queueActive() && m_queueIndex == failed && m_mode == Mode::Video)
                        playQueueIndex(failed + 1 < m_queue.size() ? failed + 1 : 0);
                });
            }
        }
    });
    connect(m_mpv, &MpvWidget::volumeChanged, m_controls, &ControlBar::setVolume);
    connect(m_mpv, &MpvWidget::muteChanged, m_controls, &ControlBar::setMuted);
    connect(m_mpv, &MpvWidget::fileLoaded, this, &MainWindow::onFileLoaded);
    connect(m_mpv, &MpvWidget::eofReached, this, &MainWindow::onEof);

    connect(m_controls, &ControlBar::playPauseClicked, this, &MainWindow::togglePause);
    connect(m_controls, &ControlBar::prevClicked, this, [this] { navigate(-1); });
    connect(m_controls, &ControlBar::nextClicked, this, [this] { navigate(1); });
    connect(m_controls, &ControlBar::seekRequested, this, [this](double s, bool exact) {
        userSeek({"seek", QString::number(s, 'f', 3), exact ? "absolute+exact" : "absolute+keyframes"});
    });
    connect(m_controls, &ControlBar::volumeRequested, this, [this](double v) { m_mpv->setProp("volume", v); });
    connect(m_controls, &ControlBar::muteClicked, this, [this] { m_mpv->command({"cycle", "mute"}); });
    connect(m_controls, &ControlBar::fullscreenClicked, this, &MainWindow::toggleFullscreen);
    connect(m_controls, &ControlBar::settingsClicked, this, &MainWindow::openSettings);
    connect(m_controls, &ControlBar::subsClicked, this, [this] {
        QMenu menu(this);
        fillSubsMenu(&menu);
        QWidget *b = m_controls->subsButton();
        menu.exec(b->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    connect(m_controls, &ControlBar::audioClicked, this, [this] {
        QMenu menu(this);
        fillTrackMenu(&menu, "audio");
        QWidget *b = m_controls->audioButton();
        menu.exec(b->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    connect(m_subs, &SubtitleManager::status, this, &MainWindow::osd);
    connect(m_subs, &SubtitleManager::subtitlesFound, this, [this](const QVector<SubResult> &ranked) {
        if (m_mode != Mode::Video)
            return;
        if (m_subPicker)
            m_subPicker->close();
        auto *dlg = new SubtitlePickerDialog(ranked, QFileInfo(m_current).fileName(), this);
        connect(dlg, &SubtitlePickerDialog::chosen, m_subs, &SubtitleManager::downloadAndLoad);
        m_subPicker = dlg;
        dlg->show(); // non-modal: the video keeps playing
    });

    // --- image wiring
    connect(m_loader, &ImageLoader::ready, this, &MainWindow::onImageReady);
    connect(m_imageView, &ImageView::zoomChanged, this, &MainWindow::updateImageOverlay);
    connect(m_imageView, &ImageView::pixelHovered, m_exif, &ExifPanel::setPixel);
    connect(m_thumbs, &ThumbStrip::fileActivated, this, [this](const QString &path) {
        if (m_thumbs->isGrid())
            setGrid(false);
        if (path != m_current)
            openPath(path);
    });

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(1800);
    connect(&m_hideTimer, &QTimer::timeout, this, [this] {
        if (m_thumbs->underMouse()) {
            m_hideTimer.start();
            return;
        }
        m_chromeShown = false;
        updateChrome();
    });
    connect(&m_slideTimer, &QTimer::timeout, this, &MainWindow::slideshowStep);
    m_toast = new QLabel(this);
    m_toast->setObjectName("toast");
    m_toast->setStyleSheet("#toast { background: rgba(20,20,24,220); color: white; border-radius: 8px;"
                           " padding: 8px 16px; font-size: 13px; }");
    m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_toast->hide();
    m_toastTimer.setSingleShot(true);
    connect(&m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);
    m_clickTimer.setSingleShot(true);
    m_clickTimer.setInterval(QApplication::doubleClickInterval());
    connect(&m_clickTimer, &QTimer::timeout, this, &MainWindow::togglePause);

    qApp->installEventFilter(this);
    YtDlp::updateInBackground();
    showWelcome();

    // Restore last size/position only now: a saved maximized/fullscreen state fires changeEvent,
    // which touches the widgets created above. Never start in fullscreen.
    if (const QByteArray geo = Settings::value("ui/geometry").toByteArray(); !geo.isEmpty()) {
        restoreGeometry(geo);
        if (windowState() & Qt::WindowFullScreen)
            setWindowState(windowState() & ~Qt::WindowFullScreen);
    }
    m_ready = true;
}

// ======================================================================== opening

void MainWindow::openPath(const QString &path)
{
    if (isUrl(path)) {
        openStream(path);
        return;
    }
    QFileInfo fi(path);
    if (fi.isDir()) {
        FolderList probe;
        const QFileInfoList entries = QDir(path).entryInfoList(QDir::Files);
        for (const QFileInfo &e : entries) {
            const MediaType t = mediaTypeOf(e.fileName());
            if (t == MediaType::Image || t == MediaType::Video) {
                probe.setCurrent(e.absoluteFilePath());
                openPath(probe.files().first());
                return;
            }
        }
        osd(tr("No media files in %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (!fi.exists())
        return;

    const QString abs = fi.absoluteFilePath();
    switch (mediaTypeOf(abs)) {
    case MediaType::Image:
        openImage(abs);
        break;
    case MediaType::Video:
        openVideo(abs);
        break;
    case MediaType::Subtitle:
        if (m_mode == Mode::Video) {
            m_mpv->command({"sub-add", abs, "select"});
            osd(tr("Subtitle: %1").arg(fi.fileName()));
        }
        break;
    case MediaType::None:
        if (QImageReader(abs).canRead())
            openImage(abs);
        else
            openVideo(abs);
        break;
    }
}

void MainWindow::openVideo(const QString &path, bool saveState)
{
    if (m_mode == Mode::Image && !maybeSaveImage())
        return;
    stopSlideshow();
    if (m_subPicker)
        m_subPicker->close(); // belongs to the previous file
    if (hadVideo() && saveState && !m_finishedCurrent && Settings::resume())
        m_mpv->command({"write-watch-later-config"});
    m_finishedCurrent = false;
    if (m_mode == Mode::Image || (m_mode == Mode::YouTube && m_youtubeReturn == Mode::Image))
        m_imageView->clear();
    if (m_thumbs->isGrid())
        setGrid(false);

    m_current = path;
    m_subs->setVideo(QFileInfo::exists(path) ? path : QString());
    if (QFileInfo::exists(path))
        m_list.setCurrent(path);
    m_mode = Mode::Video;
    m_stack->setCurrentWidget(m_videoPage);
    m_videoPage->clearMessage();
    m_videoPage->hideEndScreen();
    m_openedFromYouTube = false;
    m_controls->setQuality({});
    m_queueEnded = false;
    if (!m_queueAdvancing) {
        m_queue.clear();
        m_queueIndex = -1;
    }
    m_mpv->setProp("loop-file", !m_queueAdvancing && Settings::repeat() ? QStringLiteral("inf") : QStringLiteral("no"));
    m_controls->setTitle(QFileInfo(path).fileName());
    m_controls->setChapters({});
    m_controls->setCache(0);
    m_controls->setDuration(0);
    m_controls->setPosition(0);
    addRecent(path);

    m_mpv->setFlag("resume-playback", Settings::resume());
    m_mpv->setProp("ytdl-format", currentStreamFormat());
    m_resolvedJson.clear();
    m_resolvedSubs.clear();
    m_controls->setLive(false);
    if (isUrl(path)) {
        // yt-dlp needs a few seconds to resolve page URLs: say so instead of a black screen.
        const QUrl u(path);
        m_videoPage->showMessage("link", tr("Opening stream…"), u.host() + u.path());
        m_controls->setTitle(u.host());
    }
    const ResolvedStream *ready = isUrl(path) ? m_resolver->ready(path, currentStreamFormat()) : nullptr;
    if (ready) {
        // Resolved ahead of time (hovered / next in playlist): skip yt-dlp, start right away.
        m_resolvedJson = ready->json;
        m_resolvedSubs = ready->subtitles;
        m_mpv->setPendingStream({path, StreamResolver::edl(*ready), ready->title, ready->userAgent, ready->headers});
    } else {
        m_mpv->setPendingStream({});
    }
    m_mpv->command({"loadfile", path});
    m_mpv->setFlag("pause", false);
    setWindowTitle((isUrl(path) ? QUrl(path).host() : QFileInfo(path).fileName()) + " — Vora");
    updateChrome();
}

void MainWindow::openImage(const QString &path)
{
    if (m_mode == Mode::Image && path == m_current && m_dirty)
        return; // already showing it, with edits that a reload would throw away
    if (m_mode == Mode::Image && path != m_current && !maybeSaveImage())
        return;
    if (hadVideo()) {
        if (m_subPicker)
            m_subPicker->close();
        if (Settings::resume() && !m_finishedCurrent)
            m_mpv->command({"write-watch-later-config"});
        m_mpv->command({"stop"});
    }
    m_subs->setVideo({});
    m_queue.clear();
    m_queueIndex = -1;

    m_current = path;
    m_list.setCurrent(path);
    m_mode = Mode::Image;
    m_stack->setCurrentWidget(m_imagePage);
    resetEditState();

    m_thumbs->setFiles(m_list.files());
    m_thumbs->setCurrent(path);
    m_loader->request(path);
    addRecent(path);
    setWindowTitle(QFileInfo(path).fileName() + " — Vora");
    updateChrome();
}

void MainWindow::openYouTube(const QString &query)
{
    showYouTube();
    if (!query.isEmpty())
        m_youtube->search(query);
}

void MainWindow::openPlaylists()
{
    showYouTube();
    m_youtube->showPlaylists();
}

bool MainWindow::playSavedPlaylist(const QString &name)
{
    for (int i = 0; i < m_playlists->count(); ++i) {
        const Playlist &p = m_playlists->at(i);
        if ((name.isEmpty() || p.name.compare(name, Qt::CaseInsensitive) == 0) && !p.items.isEmpty()) {
            playQueue(p.items, 0, p.name);
            return true;
        }
    }
    openPlaylists();
    osd(tr("No playlist named “%1”").arg(name));
    return false;
}

void MainWindow::startDownload(const QString &url, const QString &kind)
{
    showYouTube();
    YtDlp::ensure(this, [this, url, kind](bool ok) {
        if (ok)
            m_downloads->add(url, url, kind == "mp3" || kind == "m4a" ? kind : QStringLiteral("video"));
    });
}

bool MainWindow::hadVideo() const
{
    return m_mode == Mode::Video || (m_mode == Mode::YouTube && m_youtubeReturn == Mode::Video);
}

void MainWindow::showYouTube()
{
    if (m_mode == Mode::YouTube) {
        m_youtube->focusSearch();
        return;
    }
    if (m_mode == Mode::Image && !maybeSaveImage())
        return;
    stopSlideshow();
    m_resumeAfterYouTube = m_mode == Mode::Video && !m_mpv->getFlag("pause") && !m_videoPage->endScreenVisible();
    if (m_mode == Mode::Video)
        m_mpv->setFlag("pause", true); // keep the position; resume with Back / Esc
    m_youtubeReturn = m_mode;
    m_titleBeforeYouTube = windowTitle();
    m_mode = Mode::YouTube;
    m_youtube->setCanGoBack(true);
    m_stack->setCurrentWidget(m_youtube);
    setWindowTitle("YouTube — Vora");
    updateChrome();
    m_youtube->focusSearch();
}

void MainWindow::leaveYouTube()
{
    if (m_mode != Mode::YouTube)
        return;
    switch (m_youtubeReturn) {
    case Mode::Video:
        m_mode = Mode::Video;
        m_stack->setCurrentWidget(m_videoPage);
        setWindowTitle(m_titleBeforeYouTube);
        if (m_resumeAfterYouTube)
            m_mpv->setFlag("pause", false);
        break;
    case Mode::Image:
        m_mode = Mode::Image;
        m_stack->setCurrentWidget(m_imagePage);
        setWindowTitle(m_titleBeforeYouTube);
        break;
    default:
        showWelcome();
        break;
    }
    setFocus();
    updateChrome();
}

void MainWindow::downloadCurrent(const QString &kind)
{
    if (!isUrl(m_current))
        return;
    QString title = m_mpv->getString("media-title");
    if (title.isEmpty())
        title = m_current;
    m_downloads->add(m_current, title, kind);
    osd(kind == "video" ? tr("Downloading video… (YouTube ▸ Downloads, Ctrl+Y)")
                        : tr("Downloading audio… (YouTube ▸ Downloads, Ctrl+Y)"));
}

void MainWindow::showWelcome()
{
    m_mode = Mode::Empty;
    m_current.clear();
    m_welcome->setRecent(recentFiles());
    m_stack->setCurrentWidget(m_welcome);
    setWindowTitle("Vora");
    updateChrome();
}

void MainWindow::navigate(int delta)
{
    if (m_mode == Mode::Image) {
        const QString next = m_list.step(m_current, delta, MediaType::Image, true);
        if (!next.isEmpty() && next != m_current)
            openImage(next);
    } else if (m_mode == Mode::Video && queueActive()) {
        const int target = m_queueIndex + delta;
        if (target < 0 || target >= m_queue.size())
            osd(delta > 0 ? tr("Last video in “%1”").arg(m_queueName) : tr("First video in “%1”").arg(m_queueName));
        else
            playQueueIndex(target);
    } else if (m_mode == Mode::Video) {
        if (!QFileInfo::exists(m_current)) { // a stream outside a playlist has no neighbours
            osd(tr("Not playing a playlist"));
            return;
        }
        const QString next = folderVideo(delta);
        if (next.isEmpty())
            osd(delta > 0 ? tr("Last file in folder") : tr("First file in folder"));
        else if (next != m_current)
            openVideo(next);
    }
}

void MainWindow::navigateEdge(bool last)
{
    if (m_mode != Mode::Image)
        return;
    const QString p = m_list.edge(MediaType::Image, last);
    if (!p.isEmpty() && p != m_current)
        openImage(p);
}

void MainWindow::openFileDialog()
{
    const QString start = m_current.isEmpty()
                              ? Settings::value("ui/lastDir", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString()
                              : QFileInfo(m_current).absolutePath();
    const QString all = mediaNameFilters(MediaType::Video).join(' ') + ' ' + mediaNameFilters(MediaType::Image).join(' ');
    const QString filter = tr("Media (%1);;Videos (%2);;Images (%3);;All files (*)")
                               .arg(all, mediaNameFilters(MediaType::Video).join(' '), mediaNameFilters(MediaType::Image).join(' '));
    const QString path = QFileDialog::getOpenFileName(this, tr("Open"), start, filter);
    if (path.isEmpty())
        return;
    Settings::setValue("ui/lastDir", QFileInfo(path).absolutePath());
    openPath(path);
}

void MainWindow::openFolderDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open folder"),
                                                          Settings::value("ui/lastDir").toString());
    if (dir.isEmpty())
        return;
    Settings::setValue("ui/lastDir", dir);
    openPath(dir);
}

void MainWindow::openUrlDialog()
{
    OpenStreamDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
        openStream(dlg.url());
}

void MainWindow::openStream(const QString &url)
{
    if (!YtDlp::needsResolver(url)) {
        openVideo(url);
        return;
    }
    YtDlp::ensure(this, [this, url](bool ok) {
        if (ok)
            openVideo(url);
    });
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_mpv->setProp("hwdec", hwdecMode());
    m_subs->reloadSettings();
    applySubtitleLanguages();
    if (m_slideTimer.isActive())
        m_slideTimer.start(Settings::slideshowSeconds() * 1000);
}

// ======================================================================== chrome

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        if (m_wasMaximized)
            showMaximized();
        else
            showNormal();
    } else {
        m_wasMaximized = isMaximized();
        showFullScreen();
    }
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange && m_ready) {
        m_chromeShown = false;
        updateChrome();
    }
    QMainWindow::changeEvent(e);
}

void MainWindow::updateChrome()
{
    const bool fs = isFullScreen();
    const bool chrome = !fs || m_chromeShown;
    m_controls->setFullscreen(fs);

    const bool grid = m_thumbs->isGrid();
    m_imageView->setVisible(!grid);
    m_thumbs->setVisible(grid || (Settings::showThumbs() && chrome));
    m_imageColumn->setStretchFactor(m_thumbs, grid ? 1 : 0);
    m_exif->setVisible(Settings::showExif() && !fs);
}

void MainWindow::revealChrome()
{
    // Fullscreen image mode: only the bottom edge reveals the filmstrip (avoids re-layout while panning).
    if (!isFullScreen() || m_mode != Mode::Image || mapFromGlobal(QCursor::pos()).y() < height() - 160)
        return;
    if (!m_chromeShown) {
        m_chromeShown = true;
        updateChrome();
    }
    m_hideTimer.start();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    // Only events for widgets in this window (not menus, dialogs, tooltips).
    const bool ours = obj->isWidgetType() && static_cast<QWidget *>(obj)->window() == this;
    if (!ours)
        return QMainWindow::eventFilter(obj, e);
    switch (e->type()) {
    case QEvent::MouseMove:
        // Moving over an inactive window still shows the controls, like PotPlayer.
        if (m_mode == Mode::Video)
            m_videoPage->poke();
        else
            revealChrome();
        break;
    case QEvent::MouseButtonPress:
        m_swallowRelease = false; // a fresh click: nothing left over from a double-click
        break;
    case QEvent::KeyPress: {
        // Global shortcuts the YouTube search field would otherwise eat (Ctrl+Y = redo, Alt+Bksp = undo).
        auto *k = static_cast<QKeyEvent *>(e);
        const auto mods = k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        if (k->key() == Qt::Key_Y && mods == Qt::ControlModifier) {
            m_mode == Mode::YouTube ? leaveYouTube() : showYouTube();
            return true;
        }
        if (m_mode == Mode::YouTube && k->key() == Qt::Key_Backspace && mods == Qt::AltModifier) {
            leaveYouTube();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::osd(const QString &text)
{
    if (m_mode == Mode::Video)
        m_mpv->showText(text, 3000);
    else if (m_mode == Mode::Image)
        m_imageView->showToast(text, 3000);
    else
        showToast(text);
}

void MainWindow::showToast(const QString &text)
{
    m_toast->setText(text);
    m_toast->adjustSize();
    m_toast->move((width() - m_toast->width()) / 2, height() - m_toast->height() - 48);
    m_toast->raise();
    m_toast->show();
    m_toastTimer.start(3000);
}

void MainWindow::setMediaTitle(const QString &title)
{
    const QString t = title + " — Vora";
    if (m_mode == Mode::YouTube)
        m_titleBeforeYouTube = t; // shown again when leaving the YouTube page
    else
        setWindowTitle(t);
}

QString MainWindow::folderVideo(int delta) const
{
    // The sort order picked for photos (date / size) shouldn't reorder episodes.
    FolderList byName = m_list;
    byName.setSort(FolderList::ByName);
    return byName.step(m_current, delta, MediaType::Video, false);
}

void MainWindow::userSeek(const QStringList &cmd)
{
    if (m_videoPage->endScreenVisible()) {
        m_videoPage->hideEndScreen(); // seeking back from the end screen continues watching
        m_finishedCurrent = false;
    }
    m_mpv->command(cmd);
}

void MainWindow::playFromYouTube(const QString &urlOrPath)
{
    // yt-dlp may have to be downloaded first; set the flag only once the video is really opened.
    auto open = [this, urlOrPath] {
        openPath(urlOrPath);
        m_openedFromYouTube = true;
    };
    if (!isUrl(urlOrPath) || !YtDlp::needsResolver(urlOrPath)) {
        open();
        return;
    }
    YtDlp::ensure(this, [open](bool ok) {
        if (ok)
            open();
    });
}

// ======================================================================== input

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    const int key = e->key();
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool cropping = m_mode == Mode::Image && m_imageView->cropMode();

    if (ctrl && key == Qt::Key_L && m_mode != Mode::Image) { // Ctrl+L rotates in the image viewer
        openPlaylists();
        return;
    }
    if (m_mode == Mode::YouTube) {
        if (key == Qt::Key_Escape || (key == Qt::Key_Backspace && (e->modifiers() & Qt::AltModifier)))
            leaveYouTube();
        else if ((key == Qt::Key_F11) || (key == Qt::Key_Return && (e->modifiers() & Qt::AltModifier)))
            toggleFullscreen();
        else if (ctrl && key == Qt::Key_Q)
            close();
        else if (ctrl && key == Qt::Key_P)
            openSettings();
        else
            QMainWindow::keyPressEvent(e);
        return;
    }
    if (ctrl && key == Qt::Key_O) {
        shift ? openFolderDialog() : openFileDialog();
        return;
    }
    if (ctrl && key == Qt::Key_U) {
        openUrlDialog();
        return;
    }
    if (ctrl && key == Qt::Key_P) {
        openSettings();
        return;
    }
    if (ctrl && key == Qt::Key_Q) {
        close();
        return;
    }
    if (key == Qt::Key_Escape) {
        if (cropping)
            m_imageView->setCropMode(false);
        else if (m_mode == Mode::Image && m_thumbs->isGrid())
            setGrid(false);
        else if (m_slideTimer.isActive())
            stopSlideshow();
        else if (isFullScreen())
            toggleFullscreen();
        else if (m_mode == Mode::Video && m_videoPage->endScreenVisible() && m_openedFromYouTube)
            showYouTube(); // finished a YouTube video: Esc goes back to the results
        else
            showMinimized(); // PotPlayer: Esc in a window minimizes
        return;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && cropping) {
        applyCrop();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter || (key == Qt::Key_F && !ctrl) || key == Qt::Key_F11) {
        toggleFullscreen();
        return;
    }

    if (m_mode == Mode::Video)
        videoKey(e);
    else if (m_mode == Mode::Image)
        imageKey(e);
    else
        QMainWindow::keyPressEvent(e);
}

void MainWindow::videoKey(QKeyEvent *e)
{
    const int key = e->key();
    const auto mods = e->modifiers();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const double seekStep = ctrl ? 30 : shift ? 1 : (mods & Qt::AltModifier) ? 60 : 5;

    switch (key) {
    case Qt::Key_Space:
        togglePause();
        break;
    case Qt::Key_Left:
        userSeek({"seek", QString::number(-seekStep), shift ? "exact" : "relative"});
        break;
    case Qt::Key_Right:
        userSeek({"seek", QString::number(seekStep), shift ? "exact" : "relative"});
        break;
    case Qt::Key_Up:
        m_mpv->command({"add", "volume", "5"});
        showVolumeOsd();
        break;
    case Qt::Key_Down:
        m_mpv->command({"add", "volume", "-5"});
        showVolumeOsd();
        break;
    case Qt::Key_M:
        m_mpv->command({"cycle", "mute"});
        osd(m_mpv->getFlag("mute") ? tr("Muted") : tr("Unmuted"));
        break;
    case Qt::Key_PageUp:
        navigate(-1);
        break;
    case Qt::Key_PageDown:
        navigate(1);
        break;
    case Qt::Key_Home:
        userSeek({"seek", "0", "absolute"});
        break;
    case Qt::Key_Z:
        m_mpv->setProp("speed", 1.0);
        osd(tr("Speed 1.00x"));
        break;
    case Qt::Key_X:
    case Qt::Key_C: {
        const double s = qBound(0.1, m_mpv->getDouble("speed", 1) + (key == Qt::Key_C ? 0.1 : -0.1), 4.0);
        m_mpv->setProp("speed", s);
        osd(tr("Speed %1x").arg(s, 0, 'f', 2));
        break;
    }
    case Qt::Key_Comma:
        m_mpv->command({"frame-back-step"});
        break;
    case Qt::Key_Period:
        m_mpv->command({"frame-step"});
        break;
    case Qt::Key_S:
        m_mpv->command({"cycle", "sid"});
        osd(tr("Subtitle: %1").arg(trackLabel("sub")));
        break;
    case Qt::Key_A:
        m_mpv->command({"cycle", "aid"});
        osd(tr("Audio: %1").arg(trackLabel("audio")));
        break;
    case Qt::Key_V:
        m_mpv->command({"cycle", "sub-visibility"});
        break;
    case Qt::Key_R:
        if (!ctrl)
            setRepeat(!Settings::repeat()); // in a playlist loop-file is always "no"
        break;
    case Qt::Key_BracketLeft:
    case Qt::Key_BracketRight:
        m_mpv->command({"add", "sub-delay", key == Qt::Key_BracketLeft ? "-0.1" : "0.1"});
        osd(tr("Subtitle delay %1 ms").arg(qRound(m_mpv->getDouble("sub-delay") * 1000)));
        break;
    case Qt::Key_J:
        if (ctrl)
            openSubtitleSearch();
        break;
    case Qt::Key_D:
        if (ctrl)
            m_subs->searchAndLoad(true);
        break;
    case Qt::Key_E:
        if (ctrl) {
            m_mpv->command({"screenshot"});
            osd(tr("Screenshot saved to Pictures/Vora"));
        }
        break;
    case Qt::Key_I:
        m_mpv->command({"show-text",
                        "${filename}\n${video-codec} ${width}x${height} @ ${container-fps} fps\n"
                        "${audio-codec-name} ${audio-params/channel-count}ch\nhwdec: ${hwdec-current}",
                        "4000"});
        break;
    default:
        QMainWindow::keyPressEvent(e);
    }
}

void MainWindow::imageKey(QKeyEvent *e)
{
    const int key = e->key();
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;

    switch (key) {
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        navigate(-1);
        break;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        navigate(1);
        break;
    case Qt::Key_Home:
        navigateEdge(false);
        break;
    case Qt::Key_End:
        navigateEdge(true);
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_imageView->zoomBy(1.25);
        break;
    case Qt::Key_Minus:
        m_imageView->zoomBy(0.8);
        break;
    case Qt::Key_0:
        m_imageView->setFitMode(ImageView::Fit);
        break;
    case Qt::Key_1:
        m_imageView->setFitMode(ImageView::Actual);
        break;
    case Qt::Key_2:
        m_imageView->setFitMode(ImageView::Fill);
        break;
    case Qt::Key_3:
    case Qt::Key_W:
        m_imageView->setFitMode(ImageView::FitWidth);
        break;
    case Qt::Key_L:
        if (ctrl)
            applyEdit(Edit::rotate(m_imageView->image(), -90), false, -90);
        break;
    case Qt::Key_R:
        if (ctrl)
            applyEdit(Edit::rotate(m_imageView->image(), 90), false, 90);
        break;
    case Qt::Key_H:
        if (ctrl)
            m_imageView->setHistogramVisible(!m_imageView->histogramVisible());
        else
            applyEdit(Edit::flip(m_imageView->image(), Qt::Horizontal), true, 0);
        break;
    case Qt::Key_V:
        applyEdit(Edit::flip(m_imageView->image(), Qt::Vertical), true, 0);
        break;
    case Qt::Key_C:
        if (ctrl && shift)
            copyPath();
        else if (ctrl)
            copyImage();
        else
            startCrop(0);
        break;
    case Qt::Key_E:
        if (ctrl && shift)
            Platform::revealInFileManager(m_current);
        else if (ctrl)
            resizeImage();
        break;
    case Qt::Key_Z:
        if (ctrl)
            undoEdit();
        break;
    case Qt::Key_S:
        if (ctrl && shift)
            saveImageAs();
        else if (ctrl)
            saveImage();
        else
            toggleSlideshow();
        break;
    case Qt::Key_F5:
        toggleSlideshow();
        break;
    case Qt::Key_Delete:
        deleteFile();
        break;
    case Qt::Key_F2:
        renameFile();
        break;
    case Qt::Key_I:
        setExifVisible(!Settings::showExif());
        break;
    case Qt::Key_T:
        setThumbsVisible(!Settings::showThumbs());
        break;
    case Qt::Key_G:
        setGrid(!m_thumbs->isGrid());
        break;
    case Qt::Key_O:
        m_showOverlay = !m_showOverlay;
        updateImageOverlay();
        break;
    default:
        QMainWindow::keyPressEvent(e);
    }
}

void MainWindow::wheelEvent(QWheelEvent *e)
{
    if (m_mode == Mode::Video) {
        const int steps = e->angleDelta().y() / 120;
        if (steps) {
            m_mpv->command({"add", "volume", QString::number(steps * 5)});
            showVolumeOsd();
        }
        e->accept();
        return;
    }
    QMainWindow::wheelEvent(e);
}

bool MainWindow::overVideo(const QPoint &globalPos) const
{
    // Clicks on the control bar (incl. its transparent gradient) are not clicks on the video.
    if (m_mode != Mode::Video || m_controls->isHidden())
        return m_mode == Mode::Video;
    return !m_controls->rect().contains(m_controls->mapFromGlobal(globalPos));
}

void MainWindow::mouseReleaseEvent(QMouseEvent *e)
{
    // PotPlayer: single click = play/pause. Wait out the double-click interval so a
    // double-click (fullscreen) doesn't also pause.
    if (e->button() == Qt::LeftButton && m_mode == Mode::Video && overVideo(e->globalPosition().toPoint())) {
        if (m_swallowRelease)
            m_swallowRelease = false;
        else
            m_clickTimer.start();
    }
    QMainWindow::mouseReleaseEvent(e);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_mode == Mode::Empty) {
        openFileDialog();
        return;
    }
    if (m_mode == Mode::YouTube)
        return;
    if (m_mode == Mode::Video && !overVideo(e->globalPosition().toPoint()))
        return;
    m_clickTimer.stop();
    m_swallowRelease = m_mode == Mode::Video; // the release that ends this double-click
    toggleFullscreen();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QList<QUrl> urls = e->mimeData()->urls();
    if (urls.isEmpty())
        return;
    const QUrl url = urls.first();
    openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_mode == Mode::Image && !maybeSaveImage()) {
        e->ignore();
        return;
    }
    int activeDownloads = 0;
    for (int id : m_downloads->ids())
        if (const DownloadJob *j = m_downloads->job(id))
            activeDownloads += j->state == DownloadJob::Running || j->state == DownloadJob::Queued;
    if (activeDownloads > 0
        && QMessageBox::question(this, tr("Downloads in progress"),
                                 tr("%n download(s) still running. Quit and cancel them?", nullptr, activeDownloads))
               != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    if (hadVideo() && Settings::resume() && !m_finishedCurrent)
        m_mpv->command({"write-watch-later-config"});
    Settings::setValue("video/volume", m_mpv->getDouble("volume", 100));
    Settings::setValue("ui/geometry", saveGeometry());
    e->accept();
}

// ======================================================================== menus

void MainWindow::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    auto add = [&](const QString &text, const QString &keys, auto fn) {
        QAction *a = menu.addAction(text);
        if (!keys.isEmpty())
            a->setShortcut(QKeySequence(keys));
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    add(tr("Open file…"), "Ctrl+O", [this] { openFileDialog(); });
    add(tr("Open folder…"), "Ctrl+Shift+O", [this] { openFolderDialog(); });
    add(tr("Open stream (YouTube, Twitch, Kick…)"), "Ctrl+U", [this] { openUrlDialog(); });
    add(m_mode == Mode::YouTube ? tr("Back to player") : tr("YouTube: search && download…"), "Ctrl+Y", [this] {
        m_mode == Mode::YouTube ? leaveYouTube() : showYouTube();
    });
    add(tr("My playlists…"), "Ctrl+L", [this] {
        showYouTube();
        m_youtube->showPlaylists();
    });
    menu.addSeparator();
    if (m_mode == Mode::Video)
        buildVideoMenu(&menu);
    else if (m_mode == Mode::Image)
        buildImageMenu(&menu);
    menu.addSeparator();
    add(isFullScreen() ? tr("Exit fullscreen") : tr("Fullscreen"), "Enter", [this] { toggleFullscreen(); });
    add(tr("Settings…"), "Ctrl+P", [this] { openSettings(); });
    add(tr("Exit"), "Ctrl+Q", [this] { close(); });
    menu.exec(e->globalPos());
}

void MainWindow::fillTrackMenu(QMenu *menu, const QString &type)
{
    const char *prop = type == "sub" ? "sid" : "aid";
    if (type == "sub") {
        QAction *off = menu->addAction(tr("Off"));
        off->setCheckable(true);
        off->setChecked(m_mpv->getString("sid") == "no");
        connect(off, &QAction::triggered, this, [this] { m_mpv->setProp("sid", QStringLiteral("no")); });
    }
    const QVariantList tracks = m_mpv->getNode("track-list").toList();
    for (const QVariant &v : tracks) {
        const QVariantMap t = v.toMap();
        if (t.value("type").toString() != type)
            continue;
        const qlonglong id = t.value("id").toLongLong();
        QStringList parts;
        if (!t.value("lang").toString().isEmpty())
            parts << t.value("lang").toString();
        if (!t.value("title").toString().isEmpty())
            parts << t.value("title").toString();
        if (!t.value("codec").toString().isEmpty())
            parts << "[" + t.value("codec").toString() + "]";
        if (t.value("external").toBool())
            parts << tr("(external)");
        QAction *a = menu->addAction(QString("%1: %2").arg(id).arg(parts.join(' ')));
        a->setCheckable(true);
        a->setChecked(t.value("selected").toBool());
        connect(a, &QAction::triggered, this, [this, prop, id] { m_mpv->setProp(prop, QString::number(id)); });
    }
    if (menu->isEmpty())
        menu->addAction(tr("No tracks"))->setEnabled(false);
}

void MainWindow::fillSubsMenu(QMenu *menu)
{
    fillTrackMenu(menu, "sub");
    menu->addSeparator();
    connect(menu->addAction(tr("Load subtitle file…")), &QAction::triggered, this, &MainWindow::loadSubtitleFile);
    QAction *search = menu->addAction(tr("Search online…"));
    search->setShortcut(QKeySequence("Ctrl+J"));
    connect(search, &QAction::triggered, this, &MainWindow::openSubtitleSearch);
    QAction *best = menu->addAction(tr("Download best match now"));
    best->setShortcut(QKeySequence("Ctrl+D"));
    connect(best, &QAction::triggered, this, [this] { m_subs->searchAndLoad(true); });
    menu->addSeparator();
    QAction *later = menu->addAction(tr("Delay −0.1 s"));
    later->setShortcut(QKeySequence("["));
    connect(later, &QAction::triggered, this, [this] { m_mpv->command({"add", "sub-delay", "-0.1"}); });
    QAction *sooner = menu->addAction(tr("Delay +0.1 s"));
    sooner->setShortcut(QKeySequence("]"));
    connect(sooner, &QAction::triggered, this, [this] { m_mpv->command({"add", "sub-delay", "0.1"}); });
    QAction *vis = menu->addAction(tr("Show subtitles"));
    vis->setShortcut(QKeySequence("V"));
    vis->setCheckable(true);
    vis->setChecked(m_mpv->getFlag("sub-visibility"));
    connect(vis, &QAction::triggered, this, [this](bool on) { m_mpv->setFlag("sub-visibility", on); });
}

void MainWindow::buildVideoMenu(QMenu *menu)
{
    QAction *pp = menu->addAction(m_mpv->getFlag("pause") ? tr("Play") : tr("Pause"));
    pp->setShortcut(QKeySequence("Space"));
    connect(pp, &QAction::triggered, this, &MainWindow::togglePause);
    QAction *loop = menu->addAction(Theme::icon("repeat"), queueActive() ? tr("Loop playlist") : tr("Loop (repeat this video)"));
    loop->setShortcut(QKeySequence("R"));
    loop->setCheckable(true);
    loop->setChecked(Settings::repeat());
    connect(loop, &QAction::triggered, this, &MainWindow::setRepeat);
    if (isUrl(m_current)) {
        QAction *save = menu->addAction(Theme::icon("plus"), tr("Save to playlist…"));
        connect(save, &QAction::triggered, this, [this] {
            PlaylistItem item;
            item.url = m_current;
            item.title = m_mpv->getString("media-title");
            item.duration = m_mpv->getDouble("duration", 0);
            item.id = PlaylistStore::youtubeId(m_current);
            const QVariantMap ytdl = m_mpv->getNode("user-data/mpv/ytdl/json-subprocess-result").toMap();
            const QJsonObject info = QJsonDocument::fromJson(m_resolvedJson.isEmpty() ? ytdl.value("stdout").toString().toUtf8()
                                                                                       : m_resolvedJson).object();
            if (info.contains("view_count"))
                item.views = qint64(info.value("view_count").toDouble());
            item.channel = info.value("channel").toString(info.value("uploader").toString());
            YouTubePage::saveToPlaylistMenu(this, m_playlists, item, QCursor::pos());
        });
    }
    if (queueActive()) {
        QMenu *queue = menu->addMenu(Theme::icon("list"), tr("Playlist: %1  (%2 / %3)").arg(m_queueName).arg(m_queueIndex + 1).arg(m_queue.size()));
        fillQueueMenu(queue);
    }
    if (isUrl(m_current) && YtDlp::needsResolver(m_current)) {
        QMenu *quality = menu->addMenu(tr("Quality"));
        quality->setIcon(Theme::icon("settings"));
        fillQualityMenu(quality);
    }

    if (isUrl(m_current) && YtDlp::needsResolver(m_current) && !m_controls->property("live").toBool()) {
        QMenu *dl = menu->addMenu(tr("Download"));
        dl->setIcon(Theme::icon("download"));
        connect(dl->addAction(Theme::icon("film"), tr("Video (MP4)")), &QAction::triggered, this,
                [this] { downloadCurrent("video"); });
        connect(dl->addAction(Theme::icon("music"), tr("Audio (MP3)")), &QAction::triggered, this,
                [this] { downloadCurrent("mp3"); });
    }

    fillTrackMenu(menu->addMenu(tr("Audio")), "audio");
    fillSubsMenu(menu->addMenu(tr("Subtitles")));

    QMenu *aspect = menu->addMenu(tr("Aspect ratio"));
    const QList<QPair<QString, QString>> ratios = {{tr("Default"), "no"}, {"16:9", "16:9"}, {"4:3", "4:3"},
                                                   {"2.35:1", "2.35:1"}, {"1:1", "1:1"}};
    for (const auto &r : ratios) {
        const QString value = r.second;
        connect(aspect->addAction(r.first), &QAction::triggered, this,
                [this, value] { m_mpv->setProp("video-aspect-override", value); });
    }

    QMenu *speed = menu->addMenu(tr("Speed"));
    for (double s : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0}) {
        QAction *a = speed->addAction(QString("%1x").arg(s));
        a->setCheckable(true);
        a->setChecked(qFuzzyCompare(m_mpv->getDouble("speed", 1), s));
        connect(a, &QAction::triggered, this, [this, s] { m_mpv->setProp("speed", s); });
    }

    QAction *shot = menu->addAction(tr("Screenshot"));
    shot->setShortcut(QKeySequence("Ctrl+E"));
    connect(shot, &QAction::triggered, this, [this] { m_mpv->command({"screenshot"}); });
}

void MainWindow::buildImageMenu(QMenu *menu)
{
    auto add = [&](QMenu *m, const QString &text, const QString &keys, auto fn) {
        QAction *a = m->addAction(text);
        if (!keys.isEmpty())
            a->setShortcut(QKeySequence(keys));
        connect(a, &QAction::triggered, this, fn);
        return a;
    };

    QMenu *zoom = menu->addMenu(tr("Zoom"));
    add(zoom, tr("Fit to window"), "0", [this] { m_imageView->setFitMode(ImageView::Fit); });
    add(zoom, tr("Actual size (100%)"), "1", [this] { m_imageView->setFitMode(ImageView::Actual); });
    add(zoom, tr("Fill window"), "2", [this] { m_imageView->setFitMode(ImageView::Fill); });
    add(zoom, tr("Fit width"), "W", [this] { m_imageView->setFitMode(ImageView::FitWidth); });
    add(zoom, tr("Zoom in"), "+", [this] { m_imageView->zoomBy(1.25); });
    add(zoom, tr("Zoom out"), "-", [this] { m_imageView->zoomBy(0.8); });

    QMenu *edit = menu->addMenu(tr("Edit"));
    add(edit, tr("Rotate left"), "Ctrl+L", [this] { applyEdit(Edit::rotate(m_imageView->image(), -90), false, -90); });
    add(edit, tr("Rotate right"), "Ctrl+R", [this] { applyEdit(Edit::rotate(m_imageView->image(), 90), false, 90); });
    add(edit, tr("Flip horizontal"), "H", [this] { applyEdit(Edit::flip(m_imageView->image(), Qt::Horizontal), true, 0); });
    add(edit, tr("Flip vertical"), "V", [this] { applyEdit(Edit::flip(m_imageView->image(), Qt::Vertical), true, 0); });
    QMenu *crop = edit->addMenu(tr("Crop"));
    add(crop, tr("Free"), "C", [this] { startCrop(0); });
    add(crop, tr("Original ratio"), "", [this] { startCrop(-1); });
    for (const auto &r : QList<QPair<QString, double>>{{"1:1", 1.0}, {"4:3", 4.0 / 3}, {"3:2", 1.5}, {"16:9", 16.0 / 9},
                                                       {"3:4", 0.75}, {"9:16", 9.0 / 16}}) {
        const double ratio = r.second;
        add(crop, r.first, "", [this, ratio] { startCrop(ratio); });
    }
    add(edit, tr("Resize…"), "Ctrl+E", [this] { resizeImage(); });
    add(edit, tr("Undo"), "Ctrl+Z", [this] { undoEdit(); })->setEnabled(!m_undo.isEmpty());
    edit->addSeparator();
    add(edit, tr("Save"), "Ctrl+S", [this] { saveImage(); })->setEnabled(m_dirty);
    add(edit, tr("Save as…"), "Ctrl+Shift+S", [this] { saveImageAs(); });

    QMenu *view = menu->addMenu(tr("View"));
    auto toggle = [&](const QString &text, const QString &keys, bool on, auto fn) {
        QAction *a = add(view, text, keys, fn);
        a->setCheckable(true);
        a->setChecked(on);
    };
    toggle(tr("Slideshow"), "S", m_slideTimer.isActive(), [this] { toggleSlideshow(); });
    toggle(tr("Thumbnail strip"), "T", Settings::showThumbs(), [this] { setThumbsVisible(!Settings::showThumbs()); });
    toggle(tr("Folder grid"), "G", m_thumbs->isGrid(), [this] { setGrid(!m_thumbs->isGrid()); });
    toggle(tr("Info panel"), "I", Settings::showExif(), [this] { setExifVisible(!Settings::showExif()); });
    toggle(tr("Histogram"), "Ctrl+H", m_imageView->histogramVisible(),
           [this] { m_imageView->setHistogramVisible(!m_imageView->histogramVisible()); });
    toggle(tr("Overlay text"), "O", m_showOverlay, [this] {
        m_showOverlay = !m_showOverlay;
        updateImageOverlay();
    });
    QMenu *sort = view->addMenu(tr("Sort by"));
    const QStringList sorts = {tr("Name"), tr("Date modified (newest)"), tr("Size (largest)")};
    for (int i = 0; i < sorts.size(); ++i) {
        QAction *a = add(sort, sorts.at(i), "", [this, i] { setSort(i); });
        a->setCheckable(true);
        a->setChecked(m_list.sort() == i);
    }

    QMenu *file = menu->addMenu(tr("File"));
    add(file, tr("Copy image"), "Ctrl+C", [this] { copyImage(); });
    add(file, tr("Copy path"), "Ctrl+Shift+C", [this] { copyPath(); });
    add(file, tr("Show in folder"), "Ctrl+Shift+E", [this] { Platform::revealInFileManager(m_current); });
    add(file, tr("Rename…"), "F2", [this] { renameFile(); });
    add(file, tr("Move to Recycle Bin"), "Del", [this] { deleteFile(); });
    if (Platform::canSetWallpaper())
        add(file, tr("Set as desktop wallpaper"), "", [this] { setWallpaper(); });
}

// ======================================================================== video helpers

QString MainWindow::trackLabel(const QString &type) const
{
    const QByteArray prop = QString("current-tracks/%1").arg(type).toUtf8();
    const QVariantMap t = m_mpv->getNode(prop.constData()).toMap();
    if (t.isEmpty())
        return tr("off");
    QStringList parts;
    parts << QString("#%1").arg(t.value("id").toLongLong());
    if (!t.value("lang").toString().isEmpty())
        parts << t.value("lang").toString();
    if (!t.value("title").toString().isEmpty())
        parts << t.value("title").toString();
    return parts.join(' ');
}

void MainWindow::applySubtitleLanguages()
{
    // mpv's slang matches track tags literally: give it every spelling (en, eng) of each language.
    QStringList slang;
    for (const QString &code : Settings::subLanguages()) {
        slang << code;
        const QLocale::Language l = QLocale::codeToLanguage(code.section('-', 0, 0));
        if (l == QLocale::AnyLanguage)
            continue;
        for (QLocale::LanguageCodeType t : {QLocale::ISO639Part2B, QLocale::ISO639Part2T}) {
            const QString c = QLocale::languageToCode(l, t);
            if (!c.isEmpty() && !slang.contains(c))
                slang << c;
        }
    }
    m_mpv->setProp("slang", slang.join(','));
}

void MainWindow::togglePause()
{
    if (m_mode != Mode::Video)
        return;
    // keep-open pauses on the last frame: Play then restarts from the beginning.
    if (m_mpv->getFlag("eof-reached") || m_videoPage->endScreenVisible()) {
        replay();
        return;
    }
    const bool willPause = !m_mpv->getFlag("pause");
    m_mpv->setFlag("pause", willPause);
    m_videoPage->flash(willPause ? "pause" : "play");
}

void MainWindow::onFileLoaded()
{
    if (!hadVideo())
        return;
    m_queueFailures = 0;
    const QString title = m_mpv->getString("media-title");
    if (!title.isEmpty())
        m_controls->setTitle(title);
    if (!m_resolvedSubs.isEmpty()) {
        // Same subtitles mpv's ytdl hook would offer; select the most preferred language.
        QString selected;
        for (const QString &want : Settings::subLanguages())
            for (const auto &s : m_resolvedSubs)
                if (selected.isEmpty() && s.first.section('-', 0, 0).compare(want.section('-', 0, 0), Qt::CaseInsensitive) == 0) {
                    m_mpv->command({"sub-add", s.second, "select", "YouTube", s.first});
                    selected = s.second;
                }
        for (const auto &s : m_resolvedSubs)
            if (s.second != selected && m_mpv->getNode("track-list").toList().size() < 64)
                m_mpv->command({"sub-add", s.second, "auto", "YouTube", s.first});
        m_resolvedSubs.clear();
    }
    if (queueActive() && m_queueIndex + 1 < m_queue.size())
        m_resolver->prefetch(m_queue.at(m_queueIndex + 1).url, currentStreamFormat());
    if (queueActive()) {
        m_controls->setTitle(QString("%1   ·   %2 / %3").arg(title.isEmpty() ? m_queue.at(m_queueIndex).title : title)
                                 .arg(m_queueIndex + 1).arg(m_queue.size()));
        m_playlists->updateTitle(m_current, title);
    }
    if (isUrl(m_current)) {
        if (!title.isEmpty()) {
            setMediaTitle(title);
            QVariantMap titles = Settings::value("ui/recentTitles").toMap();
            titles.insert(m_current, title);
            Settings::setValue("ui/recentTitles", titles);
        }
        // Live streams report no duration.
        m_controls->setLive(m_mpv->getDouble("duration", 0) <= 0);
        updateQualityBadge();
    }
    updateChapters();

    // Audio-only (no video track, not even cover art): show a music card instead of black.
    bool hasVideo = false;
    for (const QVariant &v : m_mpv->getNode("track-list").toList())
        if (v.toMap().value("type").toString() == "video")
            hasVideo = true;
    if (hasVideo)
        m_videoPage->clearMessage();
    else
        m_videoPage->showMessage("music", title.isEmpty() ? QFileInfo(m_current).fileName() : title,
                                 m_mpv->getString("audio-codec-name").toUpper());

    if (QFileInfo::exists(m_current))
        m_subs->onFileLoaded(m_current);
}

void MainWindow::updateChapters()
{
    QVector<Chapter> chapters;
    for (const QVariant &v : m_mpv->getNode("chapter-list").toList()) {
        const QVariantMap m = v.toMap();
        chapters.push_back({m.value("time").toDouble(), m.value("title").toString()});
    }
    m_controls->setChapters(chapters);
}

QStringList MainWindow::recentFiles() const
{
    return Settings::value("ui/recent").toStringList();
}

void MainWindow::addRecent(const QString &path)
{
    if (!QFileInfo::exists(path) && !isUrl(path))
        return;
    QStringList recent = recentFiles();
    // One entry per folder for images, so browsing a photo folder doesn't flood the list.
    if (mediaTypeOf(path) == MediaType::Image) {
        const QString dir = QFileInfo(path).absolutePath();
        recent.erase(std::remove_if(recent.begin(), recent.end(), [&](const QString &p) {
                         return mediaTypeOf(p) == MediaType::Image && QFileInfo(p).absolutePath() == dir;
                     }), recent.end());
    }
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 12)
        recent.removeLast();
    Settings::setValue("ui/recent", recent);
}

void MainWindow::showVolumeOsd()
{
    osd(tr("Volume %1%").arg(qRound(m_mpv->getDouble("volume"))));
}

void MainWindow::onEof()
{
    if (m_mode != Mode::Video || m_current.isEmpty())
        return;
    const bool local = QFileInfo::exists(m_current);
    if (Settings::resume())
        m_mpv->command({"delete-watch-later-config"}); // finished: next time start from the beginning
    m_finishedCurrent = true; // and don't save the end position when moving on
    if (local && Settings::autoplayNext()) {
        const QString next = folderVideo(1);
        if (!next.isEmpty()) {
            openVideo(next, false);
            return;
        }
    }
    QString title = m_mpv->getString("media-title");
    if (title.isEmpty())
        title = QFileInfo(m_current).fileName();
    if (queueActive()) {
        if (m_queueIndex + 1 < m_queue.size()) {
            playQueueIndex(m_queueIndex + 1);
            return;
        }
        if (Settings::repeat()) { // Loop on: the whole playlist repeats
            playQueueIndex(0);
            return;
        }
        m_queueEnded = true;
        m_videoPage->showEndScreen(tr("End of “%1”").arg(m_queueName), true, tr("Play list again"));
        return;
    }
    m_videoPage->showEndScreen(title, m_openedFromYouTube);
}

QList<QPair<int, int>> MainWindow::streamQualities() const
{
    // mpv's ytdl hook keeps yt-dlp's JSON answer; its "formats" list every height on offer.
    QMap<int, int> heights; // height -> best fps
    const QVariantMap result = m_mpv->getNode("user-data/mpv/ytdl/json-subprocess-result").toMap();
    const QJsonObject info = QJsonDocument::fromJson(m_resolvedJson.isEmpty() ? result.value("stdout").toString().toUtf8()
                                                                               : m_resolvedJson).object();
    for (const QJsonValue &v : info.value("formats").toArray()) {
        const QJsonObject f = v.toObject();
        const int h = f.value("height").toInt();
        if (h <= 0 || f.value("vcodec").toString() == "none")
            continue;
        heights[h] = qMax(heights.value(h), qRound(f.value("fps").toDouble()));
    }
    QList<QPair<int, int>> out;
    for (auto it = heights.cbegin(); it != heights.cend(); ++it)
        if (it.key() >= 144)
            out.prepend({it.key(), it.value()}); // highest first
    return out;
}

static QString qualityLabel(int height, int fps)
{
    return QString("%1p%2").arg(height).arg(fps >= 49 ? QString::number(fps) : QString());
}

QString MainWindow::currentStreamFormat() const
{
    return OpenStreamDialog::formatForQuality(Settings::value("stream/quality", "1080").toString());
}

void MainWindow::updateQualityBadge()
{
    if (!hadVideo() || !isUrl(m_current) || !YtDlp::needsResolver(m_current)) {
        m_controls->setQuality({});
        return;
    }
    const int h = int(m_mpv->getDouble("video-params/h", 0));
    if (h <= 0) {
        m_controls->setQuality(Settings::value("stream/quality").toString() == "audio" ? tr("Audio") : QString());
        return;
    }
    const double fps = m_mpv->getDouble("container-fps", 0);
    m_controls->setQuality(qualityLabel(h, qRound(fps)));
}

void MainWindow::fillQualityMenu(QMenu *menu)
{
    const QString current = Settings::value("stream/quality", "1080").toString();
    const int playingHeight = int(m_mpv->getDouble("video-params/h", 0));
    auto add = [&](const QString &text, const QString &value) {
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(value == current);
        connect(a, &QAction::triggered, this, [this, value] { setStreamQuality(value); });
    };
    add(tr("Auto (best available)"), "best");
    menu->addSeparator();
    const QList<QPair<int, int>> qualities = streamQualities();
    for (const auto &q : qualities) {
        QString text = qualityLabel(q.first, q.second);
        if (q.first >= 2160)
            text += "  4K";
        else if (q.first >= 1440)
            text += "  2K";
        else if (q.first >= 720)
            text += "  HD";
        if (q.first == playingHeight)
            text += tr("   · playing");
        add(text, QString::number(q.first));
    }
    if (qualities.isEmpty()) // list unknown (e.g. still loading): offer common caps
        for (const char *h : {"1080", "720", "480"})
            add(QString("%1p").arg(h), h);
    menu->addSeparator();
    add(tr("Audio only"), "audio");
}

void MainWindow::setStreamQuality(const QString &quality)
{
    Settings::setValue("stream/quality", quality);
    if (m_mode != Mode::Video || !isUrl(m_current) || !YtDlp::needsResolver(m_current))
        return;
    // Reload the same video in the new quality, continuing where it was.
    const double pos = m_mpv->getDouble("time-pos", 0);
    const bool fromYouTube = m_openedFromYouTube;
    m_mpv->setProp("ytdl-format", OpenStreamDialog::formatForQuality(quality));
    m_resolvedJson.clear(); // the reload goes through mpv's hook with the new format
    m_videoPage->hideEndScreen();
    m_videoPage->showMessage("settings",
                             quality == "audio" ? tr("Switching to audio only…")
                             : quality == "best" ? tr("Switching to the best quality…")
                                                 : tr("Switching to %1p…").arg(quality),
                             m_mpv->getString("media-title"));
    m_mpv->command({"loadfile", m_current, "replace", "-1",
                    m_controls->property("live").toBool() ? QString() : QString("start=%1").arg(pos, 0, 'f', 1)});
    m_mpv->setFlag("pause", false);
    m_openedFromYouTube = fromYouTube;
}

void MainWindow::playQueue(const QVector<PlaylistItem> &items, int start, const QString &name)
{
    if (items.isEmpty())
        return;
    m_queue = items;
    m_queueName = name;
    playQueueIndex(qBound(0, start, int(items.size()) - 1));
}

void MainWindow::playQueueIndex(int index)
{
    if (index < 0 || index >= m_queue.size())
        return;
    const QString url = m_queue.at(index).url;
    auto open = [this, index, url] {
        if (index >= m_queue.size() || m_queue.at(index).url != url)
            return; // the list changed while yt-dlp was downloading
        m_queueIndex = index;
        m_queueAdvancing = true;
        openPath(url);
        m_queueAdvancing = false;
        m_openedFromYouTube = true;
        if (m_mode == Mode::Video)
            osd(QString("%1 / %2   %3").arg(index + 1).arg(m_queue.size()).arg(m_queue.at(index).title));
    };
    if (!isUrl(url) || !YtDlp::needsResolver(url)) {
        open();
        return;
    }
    YtDlp::ensure(this, [open](bool ok) {
        if (ok)
            open();
    });
}

void MainWindow::fillQueueMenu(QMenu *menu)
{
    for (int i = 0; i < m_queue.size(); ++i) {
        const PlaylistItem &it = m_queue.at(i);
        QString text = QString("%1.  %2").arg(i + 1).arg(it.title.isEmpty() ? it.url : it.title);
        if (text.size() > 70)
            text = text.left(67) + "…";
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(i == m_queueIndex);
        connect(a, &QAction::triggered, this, [this, i] { playQueueIndex(i); });
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Previous video")), &QAction::triggered, this, [this] { navigate(-1); });
    connect(menu->addAction(tr("Next video")), &QAction::triggered, this, [this] { navigate(1); });
    connect(menu->addAction(tr("Stop playing the list")), &QAction::triggered, this, [this] {
        m_queue.clear();
        m_queueIndex = -1;
        m_mpv->setProp("loop-file", Settings::repeat() ? QStringLiteral("inf") : QStringLiteral("no"));
        osd(tr("Playlist stopped; this video stays"));
    });
}

void MainWindow::replay()
{
    m_finishedCurrent = false;
    if (m_queueEnded && !m_queue.isEmpty()) {
        m_queueEnded = false;
        playQueueIndex(0);
        return;
    }
    userSeek({"seek", "0", "absolute"});
    m_videoPage->hideEndScreen();
    m_mpv->setFlag("pause", false);
    m_videoPage->flash("replay");
}

void MainWindow::setRepeat(bool on)
{
    m_controls->setRepeat(on);
    Settings::setValue("video/repeat", on); // remembered for every video and the next start
    if (queueActive()) {
        // In a playlist Loop repeats the whole list; each video still ends so the list moves on.
        m_mpv->setProp("loop-file", QStringLiteral("no"));
        osd(on ? tr("Loop playlist on") : tr("Loop playlist off"));
        if (on && m_queueEnded)
            replay();
        return;
    }
    // loop-file keeps looping without ever reaching the end screen.
    m_mpv->setProp("loop-file", on ? QStringLiteral("inf") : QStringLiteral("no"));
    osd(on ? tr("Repeat on") : tr("Repeat off"));
    if (on && m_videoPage->endScreenVisible())
        replay();
}

void MainWindow::loadSubtitleFile()
{
    const QString start = QFileInfo(m_current).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Load subtitle"), start,
                                                      tr("Subtitles (%1)").arg(mediaNameFilters(MediaType::Subtitle).join(' ')));
    if (!path.isEmpty())
        m_mpv->command({"sub-add", path, "select"});
}

void MainWindow::openSubtitleSearch()
{
    if (m_mode != Mode::Video)
        return;
    auto *dlg = new SubtitleSearchDialog(m_subs, this);
    dlg->show();
}

// ======================================================================== image helpers

void MainWindow::onImageReady(const LoadedImage &img)
{
    if (img.path != m_current || m_mode != Mode::Image)
        return;
    if (!img.error.isEmpty()) {
        m_imageView->clear();
        m_imageView->showToast(tr("Cannot open %1: %2").arg(QFileInfo(img.path).fileName(), img.error), 4000);
        return;
    }
    const bool fade = m_slideTimer.isActive() && Settings::slideshowFade();
    m_imageView->setImage(img.image, img.path, img.animated, fade);
    m_info = img;
    m_info.image = QImage(); // the view owns the pixels
    if (Settings::showExif())
        m_exif->showFile(img.path, img.originalSize, img.format);
    updateImageOverlay();

    m_loader->prefetch({m_list.step(m_current, 1, MediaType::Image, true),
                        m_list.step(m_current, -1, MediaType::Image, true)});
}

void MainWindow::updateImageOverlay()
{
    if (m_mode != Mode::Image || !m_showOverlay) {
        m_imageView->setOverlayText({});
        return;
    }
    const QImage img = m_imageView->image();
    const int idx = m_list.indexOf(m_current, MediaType::Image) + 1;
    const int count = m_list.files(MediaType::Image).size();
    QString text = QString("%1 / %2   %3   %4 × %5   %6%")
                       .arg(idx).arg(count)
                       .arg(QFileInfo(m_current).fileName())
                       .arg(img.width()).arg(img.height())
                       .arg(qRound(m_imageView->zoomPercent()));
    if (m_dirty)
        text += tr("   • modified (Ctrl+S save, Ctrl+Z undo)");
    m_imageView->setOverlayText(text);
}

void MainWindow::applyEdit(const QImage &img, bool lossy, int rotation)
{
    if (m_mode != Mode::Image || m_imageView->image().isNull())
        return;
    if (m_imageView->isAnimated()) {
        osd(tr("Editing animated images is not supported"));
        return;
    }
    if (img.isNull()) {
        osd(tr("Not enough memory for this edit"));
        return;
    }
    m_undo.push_back({m_imageView->image(), m_lossy, m_rotation});
    // Keep at most 20 steps and ~512 MB of undo pixels (a 50 MP photo is 200 MB per step).
    qint64 bytes = 0;
    for (const EditState &s : m_undo)
        bytes += s.image.sizeInBytes();
    while (m_undo.size() > 1 && (m_undo.size() > 20 || bytes > 512LL * 1024 * 1024)) {
        bytes -= m_undo.first().image.sizeInBytes();
        m_undo.removeFirst();
    }
    m_dirty = true;
    m_lossy = m_lossy || lossy;
    m_rotation = ((m_rotation + rotation) % 360 + 360) % 360;
    m_imageView->replaceImage(img);
    updateImageOverlay();
}

void MainWindow::undoEdit()
{
    if (m_undo.isEmpty())
        return;
    const EditState s = m_undo.takeLast();
    m_lossy = s.lossy;
    m_rotation = s.rotation;
    m_dirty = !m_undo.isEmpty();
    m_imageView->replaceImage(s.image);
    updateImageOverlay();
}

void MainWindow::resetEditState()
{
    m_undo.clear();
    m_dirty = false;
    m_lossy = false;
    m_rotation = 0;
    if (m_imageView->cropMode())
        m_imageView->setCropMode(false);
}

bool MainWindow::maybeSaveImage()
{
    if (!m_dirty)
        return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"),
                                              tr("Save changes to %1?").arg(QFileInfo(m_current).fileName()),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveImage();
    if (answer == QMessageBox::Discard) {
        m_loader->invalidate(m_current);
        resetEditState();
        return true;
    }
    return false;
}

static bool writeImage(const QImage &img, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    QImageWriter writer(&file, QFileInfo(path).suffix().toLower().toLatin1());
    writer.setQuality(Settings::jpegQuality());
    const bool opaqueTarget = isJpeg(path) || QFileInfo(path).suffix().compare("bmp", Qt::CaseInsensitive) == 0;
    const QImage out = opaqueTarget && img.hasAlphaChannel() ? img.convertToFormat(QImage::Format_RGB32) : img;
    if (!writer.write(out)) {
        *error = writer.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool MainWindow::saveImage()
{
    if (!m_dirty)
        return true;
    const QString path = m_current;
    if (m_info.pages > 1 || m_info.highDepth) {
        // Writing over it would drop the other pages / reduce it to 8 bits per channel.
        osd(m_info.pages > 1 ? tr("Multi-page image: saving the edit as a new file")
                             : tr("16-bit image: saving the edit as a new file"));
        return saveImageAs();
    }

    if (!m_lossy && isJpeg(path) && Exif::available()) {
        QString err;
        if (m_rotation == 0 || Exif::rotateLossless(path, m_rotation, &err)) {
            afterSave(path);
            osd(tr("Saved (lossless rotation)"));
            return true;
        }
        // mirrored / odd orientation: fall back to re-encoding
    }

    const QByteArray suffix = QFileInfo(path).suffix().toLower().toLatin1();
    if (!QImageWriter::supportedImageFormats().contains(suffix))
        return saveImageAs();

    QByteArray original;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            original = f.readAll();
    }
    QString err;
    if (!writeImage(m_imageView->image(), path, &err)) {
        QMessageBox::warning(this, tr("Save failed"), err);
        return false;
    }
    Exif::copyMetadata(original, path);
    afterSave(path);
    osd(tr("Saved"));
    return true;
}

bool MainWindow::saveImageAs()
{
    stopSlideshow();
    const QString source = m_current;
    const QFileInfo fi(source);
    QString path = QFileDialog::getSaveFileName(this, tr("Save image as"),
                                                fi.absolutePath() + "/" + fi.completeBaseName() + "_edit." + fi.suffix(),
                                                tr("JPEG (*.jpg *.jpeg);;PNG (*.png);;WebP (*.webp);;BMP (*.bmp);;TIFF (*.tif *.tiff)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".jpg";
    // Read the original's metadata first: "Save as" may target the very same file.
    QByteArray original;
    if (isJpeg(path) && isJpeg(source)) {
        QFile f(source);
        if (f.open(QIODevice::ReadOnly))
            original = f.readAll();
    }
    QString err;
    if (!writeImage(m_imageView->image(), path, &err)) {
        QMessageBox::warning(this, tr("Save failed"), err);
        return false;
    }
    if (!original.isEmpty())
        Exif::copyMetadata(original, path);
    m_info.pages = 1;
    m_info.highDepth = false;
    m_current = QFileInfo(path).absoluteFilePath();
    m_list.setCurrent(m_current);
    m_list.rescan();
    m_thumbs->setFiles(m_list.files());
    m_thumbs->setCurrent(m_current);
    afterSave(m_current);
    setWindowTitle(QFileInfo(m_current).fileName() + " — Vora");
    osd(tr("Saved as %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::afterSave(const QString &path)
{
    m_loader->invalidate(path);
    m_thumbs->refresh(path);
    resetEditState();
    updateImageOverlay();
    if (Settings::showExif()) {
        m_exif->clear(); // file changed on disk: re-read metadata
        m_exif->showFile(path, m_imageView->image().size(), m_info.format);
    }
}

void MainWindow::startCrop(double ratio)
{
    if (m_mode != Mode::Image || m_imageView->isAnimated())
        return;
    const QImage img = m_imageView->image();
    if (ratio < 0 && img.height() > 0)
        ratio = double(img.width()) / img.height();
    m_imageView->setCropMode(true, ratio);
}

void MainWindow::applyCrop()
{
    const QRect r = m_imageView->cropRect();
    m_imageView->setCropMode(false);
    if (r.width() < 2 || r.height() < 2)
        return;
    applyEdit(Edit::crop(m_imageView->image(), r), true, 0);
}

void MainWindow::resizeImage()
{
    stopSlideshow();
    const QImage img = m_imageView->image();
    if (img.isNull() || m_imageView->isAnimated())
        return;
    ResizeDialog dlg(img.size(), this);
    if (dlg.exec() != QDialog::Accepted || dlg.result() == img.size())
        return;
    const QSize s = dlg.result();
    if (qint64(s.width()) * s.height() > 200LL * 1000 * 1000) { // 800 MB of pixels
        QMessageBox::warning(this, tr("Resize"), tr("%1 × %2 is too large.").arg(s.width()).arg(s.height()));
        return;
    }
    applyEdit(Edit::resize(img, s), true, 0);
}

void MainWindow::renameFile()
{
    stopSlideshow();
    if (m_current.isEmpty() || m_mode != Mode::Image)
        return;
    const QString path = m_current;
    const QFileInfo fi(path);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, fi.fileName(), &ok).trimmed();
    if (!ok || name.isEmpty() || name == fi.fileName() || name.contains('/') || name.contains('\\'))
        return;
    const QString target = fi.absolutePath() + "/" + name;
    const bool caseOnly = name.compare(fi.fileName(), Qt::CaseInsensitive) == 0; // "img.JPG" -> "img.jpg"
    if (QFileInfo::exists(target) && !caseOnly) {
        QMessageBox::warning(this, tr("Rename"), tr("%1 already exists.").arg(name));
        return;
    }
    m_imageView->releaseFile(); // an animated GIF keeps its file open
    if (!QFile::rename(path, target)) {
        QMessageBox::warning(this, tr("Rename"), tr("Could not rename the file."));
        return;
    }
    m_loader->invalidate(path);
    m_list.rename(path, target);
    m_current = QFileInfo(target).absoluteFilePath();
    m_thumbs->setFiles(m_list.files());
    m_thumbs->setCurrent(m_current);
    setWindowTitle(name + " — Vora");
    updateImageOverlay();
}

void MainWindow::deleteFile()
{
    stopSlideshow();
    if (m_current.isEmpty() || m_mode != Mode::Image)
        return;
    const QString path = m_current;
    const auto answer = QMessageBox::question(this, tr("Delete"),
                                              tr("Move \"%1\" to the Recycle Bin?").arg(QFileInfo(path).fileName()));
    if (answer != QMessageBox::Yes)
        return;
    QString next = m_list.step(path, 1, MediaType::Image, false);
    if (next.isEmpty())
        next = m_list.step(path, -1, MediaType::Image, false);
    m_imageView->releaseFile();
    if (!QFile::moveToTrash(path)) {
        QMessageBox::warning(this, tr("Delete"), tr("Could not move the file to the Recycle Bin."));
        return;
    }
    resetEditState();
    m_loader->invalidate(path);
    m_list.remove(path);
    m_thumbs->setFiles(m_list.files());
    if (next.isEmpty()) {
        m_imageView->clear();
        showWelcome();
    } else {
        openImage(next);
    }
}

void MainWindow::copyImage()
{
    QApplication::clipboard()->setImage(m_imageView->image());
    osd(tr("Image copied"));
}

void MainWindow::copyPath()
{
    QApplication::clipboard()->setText(QDir::toNativeSeparators(m_current));
    osd(tr("Path copied"));
}

void MainWindow::setWallpaper()
{
    if (m_dirty)
        osd(tr("Using the saved file (unsaved edits ignored)"));
    osd(Platform::setWallpaper(m_current) ? tr("Wallpaper set") : tr("Could not set wallpaper"));
}

void MainWindow::toggleSlideshow()
{
    if (m_slideTimer.isActive()) {
        stopSlideshow();
        osd(tr("Slideshow stopped"));
        return;
    }
    if (m_mode != Mode::Image || m_list.files(MediaType::Image).size() < 2)
        return;
    m_slideTimer.start(Settings::slideshowSeconds() * 1000);
    if (!isFullScreen()) {
        m_slideshowFullscreen = true;
        toggleFullscreen();
    }
    osd(tr("Slideshow — Esc to stop"));
}

void MainWindow::stopSlideshow()
{
    if (!m_slideTimer.isActive())
        return;
    m_slideTimer.stop();
    if (m_slideshowFullscreen && isFullScreen())
        toggleFullscreen();
    m_slideshowFullscreen = false;
}

void MainWindow::slideshowStep()
{
    const QStringList images = m_list.files(MediaType::Image);
    if (m_mode != Mode::Image || images.size() < 2) {
        stopSlideshow();
        return;
    }
    if (m_dirty)
        return; // never auto-discard edits
    QString next;
    if (Settings::slideshowShuffle()) {
        do {
            next = images.at(QRandomGenerator::global()->bounded(int(images.size())));
        } while (next == m_current);
    } else {
        next = m_list.step(m_current, 1, MediaType::Image, true);
    }
    openImage(next);
}

void MainWindow::setThumbsVisible(bool on)
{
    Settings::setValue("image/showThumbs", on);
    updateChrome();
}

void MainWindow::setGrid(bool on)
{
    m_thumbs->setGrid(on);
    updateChrome();
}

void MainWindow::setExifVisible(bool on)
{
    Settings::setValue("image/showExif", on);
    updateChrome();
    if (on && m_mode == Mode::Image)
        m_exif->showFile(m_current, m_info.originalSize, m_info.format);
}

void MainWindow::setSort(int sort)
{
    Settings::setValue("image/sortMode", sort);
    m_list.setSort(FolderList::Sort(sort));
    m_thumbs->setFiles(m_list.files());
    m_thumbs->setCurrent(m_current);
    updateImageOverlay();
}

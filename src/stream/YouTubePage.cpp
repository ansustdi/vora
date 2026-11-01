#include "YouTubePage.h"

#include "Platform.h"
#include "Settings.h"
#include "YtDlp.h"
#include "ui/IconButton.h"
#include "ui/Theme.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <memory>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

// ======================================================================== helpers

static QString formatDuration(double sec)
{
    const int s = qMax(0, int(sec));
    if (s >= 3600)
        return QString("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

static QString formatViews(qint64 views)
{
    if (views < 0)
        return {};
    if (views >= 1000000000)
        return QCoreApplication::translate("YouTube", "%1B views").arg(views / 1e9, 0, 'f', 1);
    if (views >= 1000000)
        return QCoreApplication::translate("YouTube", "%1M views").arg(views / 1e6, 0, 'f', 1);
    if (views >= 1000)
        return QCoreApplication::translate("YouTube", "%1K views").arg(views / 1e3, 0, 'f', 0);
    return QCoreApplication::translate("YouTube", "%1 views").arg(views);
}

static void setMuted(QLabel *label, double scale = 1.0)
{
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, Theme::kMuted);
    label->setPalette(p);
    if (scale != 1.0) {
        QFont f = label->font();
        f.setPointSizeF(f.pointSizeF() * scale);
        label->setFont(f);
    }
}

static QPushButton *chip(const QString &icon, const QString &text, const QString &tip, bool primary, QWidget *parent)
{
    auto *b = new QPushButton(Theme::icon(icon, primary ? QColor(Qt::white) : QColor(220, 220, 224)), text, parent);
    b->setObjectName(primary ? "chipPrimary" : "chip");
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setIconSize(QSize(14, 14));
    b->setToolTip(tip);
    return b;
}

static PlaylistItem toItem(const YtVideo &v)
{
    PlaylistItem it;
    it.url = v.url;
    it.title = v.title;
    it.channel = v.channel;
    it.id = PlaylistStore::youtubeId(v.url).isEmpty() ? QString() : v.id; // ids are only unique on YouTube
    it.duration = v.duration;
    it.views = v.views;
    return it;
}

// ======================================================================== two-line title

// Word-wrapped text clamped to two lines with an ellipsis; never grows into the line below.
class ClampedLabel : public QWidget
{
public:
    ClampedLabel(const QString &text, const QFont &font, QWidget *parent, int lines = 2)
        : QWidget(parent), m_text(text.simplified()), m_lines(lines) // simplified() also folds newlines
    {
        setFont(font);
        setToolTip(m_text);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(fontMetrics().lineSpacing() * lines + 2);
        setMinimumWidth(40);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        const QFontMetrics fm(font());
        QTextLayout layout(m_text, font());
        layout.beginLayout();
        QStringList lines;
        for (int i = 0; i < m_lines; ++i) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(width());
            if (i == m_lines - 1) // last visible line takes the rest, elided
                lines << fm.elidedText(m_text.mid(line.textStart()), Qt::ElideRight, width());
            else
                lines << m_text.mid(line.textStart(), line.textLength()).trimmed();
        }
        layout.endLayout();
        for (int i = 0; i < lines.size(); ++i)
            p.drawText(QPointF(0, fm.ascent() + i * fm.lineSpacing()), lines.at(i));
    }

private:
    QString m_text;
    int m_lines;
};

// ======================================================================== thumbnail

// Rounded thumbnail with a duration (or LIVE) badge.
class ThumbView : public QWidget
{
public:
    ThumbView(double duration, bool live, QSize size, QWidget *parent)
        : QWidget(parent), m_duration(duration), m_live(live)
    {
        setFixedSize(size);
    }

    void setPixmap(const QPixmap &pm)
    {
        m_pixmap = pm;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), 8, 8);
        p.setClipPath(clip);
        if (m_pixmap.isNull()) {
            QLinearGradient g(0, 0, width(), height());
            g.setColorAt(0, QColor(40, 42, 50));
            g.setColorAt(1, QColor(26, 27, 32));
            p.fillRect(rect(), g);
            const int s = qMin(30, height() / 3);
            p.drawPixmap(QPointF((width() - s) / 2.0, (height() - s) / 2.0),
                         Theme::pixmap("youtube", s, devicePixelRatioF(), QColor(255, 255, 255, 60)));
        } else {
            // cover-crop to the widget's aspect
            const QSizeF src = QSizeF(m_pixmap.size()).scaled(size(), Qt::KeepAspectRatioByExpanding);
            const QRectF target((width() - src.width()) / 2, (height() - src.height()) / 2, src.width(), src.height());
            p.drawPixmap(target, m_pixmap, QRectF(m_pixmap.rect()));
        }
        p.setClipping(false);

        QString badge;
        QColor badgeColor(0, 0, 0, 200);
        if (m_live) {
            badge = "LIVE";
            badgeColor = QColor(220, 38, 38);
        } else if (m_duration > 0) {
            badge = formatDuration(m_duration);
        }
        if (!badge.isEmpty()) {
            QFont f = font();
            f.setPointSizeF(f.pointSizeF() * (height() < 90 ? 0.75 : 0.85));
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            const QFontMetrics fm(f);
            const QRectF b(width() - fm.horizontalAdvance(badge) - 12 - 5, height() - fm.height() - 4 - 5,
                           fm.horizontalAdvance(badge) + 12, fm.height() + 4);
            p.setPen(Qt::NoPen);
            p.setBrush(badgeColor);
            p.drawRoundedRect(b, 4, 4);
            p.setPen(Qt::white);
            p.drawText(b, Qt::AlignCenter, badge);
        }
    }

private:
    double m_duration;
    bool m_live;
    QPixmap m_pixmap;
};

// ======================================================================== result card

class ResultCard : public QWidget
{
public:
    ResultCard(const YtVideo &v, std::function<void(const QString &, QWidget *)> onAction)
        : QWidget(nullptr), m_video(v)
    {
        m_thumb = new ThumbView(v.duration, v.live, QSize(208, 117), this);

        QFont tf = font();
        tf.setPointSizeF(tf.pointSizeF() * 1.12);
        tf.setWeight(QFont::DemiBold);
        auto *title = new ClampedLabel(v.title, tf, this);

        QStringList metaParts;
        if (!v.channel.isEmpty())
            metaParts << v.channel;
        if (v.live)
            metaParts << QCoreApplication::translate("YouTube", "live now");
        else if (const QString views = formatViews(v.views); !views.isEmpty())
            metaParts << views;
        auto *meta = new QLabel(metaParts.join("  ·  "), this);
        setMuted(meta, 0.95);

        auto *play = chip("play", QCoreApplication::translate("YouTube", " Play"),
                          QCoreApplication::translate("YouTube", "Watch in Vora"), true, this);
        auto *save = chip("plus", QCoreApplication::translate("YouTube", " Save"),
                          QCoreApplication::translate("YouTube", "Add to a playlist"), false, this);
        auto *mp4 = chip("download", QCoreApplication::translate("YouTube", " Video"),
                         QCoreApplication::translate("YouTube", "Download as MP4 video"), false, this);
        auto *mp3 = chip("music", QCoreApplication::translate("YouTube", " Audio"),
                         QCoreApplication::translate("YouTube", "Download as MP3 audio"), false, this);
        if (v.live) {
            mp4->setEnabled(false); // live streams can't be downloaded as a finished file
            mp3->setEnabled(false);
            mp4->setToolTip(QCoreApplication::translate("YouTube", "Live streams can't be downloaded"));
            mp3->setToolTip(mp4->toolTip());
        }
        QObject::connect(play, &QPushButton::clicked, this, [onAction, play] { onAction("play", play); });
        QObject::connect(save, &QPushButton::clicked, this, [onAction, save] { onAction("save", save); });
        QObject::connect(mp4, &QPushButton::clicked, this, [onAction, mp4] { onAction("video", mp4); });
        QObject::connect(mp3, &QPushButton::clicked, this, [onAction, mp3] { onAction("mp3", mp3); });

        auto *actions = new QHBoxLayout;
        actions->setSpacing(8);
        actions->addWidget(play);
        actions->addWidget(save);
        actions->addWidget(mp4);
        actions->addWidget(mp3);
        actions->addStretch();

        auto *text = new QVBoxLayout;
        text->setSpacing(4);
        text->addWidget(title);
        text->addWidget(meta);
        text->addStretch();
        text->addLayout(actions);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(10, 8, 10, 8);
        row->setSpacing(16);
        row->addWidget(m_thumb);
        row->addLayout(text, 1);

        // Resting the mouse on a result starts resolving it, so Play is (nearly) instant.
        m_hover.setSingleShot(true);
        m_hover.setInterval(300);
        QObject::connect(&m_hover, &QTimer::timeout, this, [onAction, this] { onAction("hover", this); });
    }

    const YtVideo &video() const { return m_video; }
    ThumbView *thumb() const { return m_thumb; }

protected:
    void enterEvent(QEnterEvent *) override { m_hover.start(); }
    void leaveEvent(QEvent *) override { m_hover.stop(); }

private:
    YtVideo m_video;
    ThumbView *m_thumb;
    QTimer m_hover;
};

// ======================================================================== playlist row

class PlaylistRow : public QWidget
{
public:
    PlaylistRow(int number, const PlaylistItem &item, std::function<void(const QString &)> onAction)
        : QWidget(nullptr)
    {
        auto *num = new QLabel(QString::number(number), this);
        num->setFixedWidth(26);
        num->setAlignment(Qt::AlignCenter);
        setMuted(num);

        m_thumb = new ThumbView(item.duration, false, QSize(128, 72), this);

        QFont tf = font();
        tf.setWeight(QFont::DemiBold);
        auto *title = new ClampedLabel(item.title.isEmpty() ? item.url : item.title, tf, this);
        QStringList metaParts;
        if (!item.channel.isEmpty())
            metaParts << item.channel;
        if (const QString views = formatViews(item.views); !views.isEmpty())
            metaParts << views;
        auto *meta = new QLabel(metaParts.join("  ·  "), this);
        setMuted(meta, 0.92);

        auto *play = new IconButton("play", 32, 15, this);
        play->setToolTip(QCoreApplication::translate("YouTube", "Play from here"));
        auto *remove = new IconButton("close", 32, 14, this);
        remove->setToolTip(QCoreApplication::translate("YouTube", "Remove from playlist"));
        QObject::connect(play, &IconButton::clicked, this, [onAction] { onAction("play"); });
        QObject::connect(remove, &IconButton::clicked, this, [onAction] { onAction("remove"); });

        auto *text = new QVBoxLayout;
        text->setSpacing(2);
        text->addStretch();
        text->addWidget(title);
        text->addWidget(meta);
        text->addStretch();

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(6, 6, 10, 6);
        row->setSpacing(12);
        row->addWidget(num);
        row->addWidget(m_thumb);
        row->addLayout(text, 1);
        row->addWidget(play);
        row->addWidget(remove);
        setToolTip(QCoreApplication::translate("YouTube", "Drag to reorder · double-click to play from here"));
        m_hover.setSingleShot(true);
        m_hover.setInterval(300);
        QObject::connect(&m_hover, &QTimer::timeout, this, [onAction] { onAction("hover"); });
    }

    ThumbView *thumb() const { return m_thumb; }

protected:
    void enterEvent(QEnterEvent *) override { m_hover.start(); }
    void leaveEvent(QEvent *) override { m_hover.stop(); }

private:
    ThumbView *m_thumb;
    QTimer m_hover;
};

// ======================================================================== download row

class DownloadRow : public QWidget
{
public:
    DownloadRow(int id, DownloadManager *mgr, std::function<void(const QString &, const QString &)> play)
        : QWidget(nullptr), m_id(id), m_mgr(mgr), m_play(std::move(play))
    {
        m_icon = new QLabel(this);
        m_icon->setFixedSize(22, 22);
        m_title = new QLabel(this);
        m_title->setMinimumWidth(1); // let the row shrink to the panel width
        QFont tf = m_title->font();
        tf.setWeight(QFont::DemiBold);
        m_title->setFont(tf);
        m_bar = new QProgressBar(this);
        m_bar->setRange(0, 1000);
        m_bar->setTextVisible(false);
        m_status = new QLabel(this);
        m_status->setMinimumWidth(1);
        setMuted(m_status, 0.88);

        m_cancel = new IconButton("close", 28, 14, this);
        m_cancel->setToolTip(QCoreApplication::translate("YouTube", "Cancel"));
        m_open = new IconButton("play", 28, 14, this);
        m_open->setToolTip(QCoreApplication::translate("YouTube", "Play"));
        m_reveal = new IconButton("folder", 28, 15, this);
        m_reveal->setToolTip(QCoreApplication::translate("YouTube", "Show in folder"));
        QObject::connect(m_cancel, &IconButton::clicked, this, [this] { m_mgr->cancel(m_id); });
        QObject::connect(m_open, &IconButton::clicked, this, [this] {
            if (const DownloadJob *j = m_mgr->job(m_id))
                m_play(j->file, j->title);
        });
        QObject::connect(m_reveal, &IconButton::clicked, this, [this] {
            if (const DownloadJob *j = m_mgr->job(m_id))
                Platform::revealInFileManager(j->file);
        });

        auto *text = new QVBoxLayout;
        text->setSpacing(4);
        text->addWidget(m_title);
        text->addWidget(m_bar);
        text->addWidget(m_status);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(12, 10, 8, 10);
        row->setSpacing(10);
        row->addWidget(m_icon, 0, Qt::AlignTop);
        row->addLayout(text, 1);
        row->addWidget(m_open, 0, Qt::AlignVCenter);
        row->addWidget(m_reveal, 0, Qt::AlignVCenter);
        row->addWidget(m_cancel, 0, Qt::AlignVCenter);
        refresh();
    }

    void refresh()
    {
        const DownloadJob *j = m_mgr->job(m_id);
        if (!j)
            return;
        const bool audio = j->kind != "video";
        const QColor iconColor = j->state == DownloadJob::Failed ? QColor(240, 90, 90)
                               : j->state == DownloadJob::Done   ? QColor(80, 200, 120)
                                                                 : QColor(200, 200, 206);
        m_icon->setPixmap(Theme::pixmap(audio ? "music" : "film", 20, devicePixelRatioF(), iconColor));
        m_title->setText(fontMetrics().elidedText(j->title, Qt::ElideRight, 200));
        m_title->setToolTip(j->title);
        m_bar->setValue(int(j->percent * 10));
        m_bar->setVisible(j->state == DownloadJob::Running || j->state == DownloadJob::Queued);
        m_status->setText(fontMetrics().elidedText(j->status, Qt::ElideRight, 200));
        m_status->setToolTip(j->status);
        const bool active = j->state == DownloadJob::Running || j->state == DownloadJob::Queued;
        m_cancel->setVisible(active);
        m_open->setVisible(j->state == DownloadJob::Done);
        m_reveal->setVisible(j->state == DownloadJob::Done);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 9));
        p.drawRoundedRect(QRectF(rect()).adjusted(4, 3, -4, -3), 9, 9);
    }

private:
    int m_id;
    DownloadManager *m_mgr;
    std::function<void(const QString &, const QString &)> m_play;
    QLabel *m_icon;
    QLabel *m_title;
    QProgressBar *m_bar;
    QLabel *m_status;
    IconButton *m_cancel;
    IconButton *m_open;
    IconButton *m_reveal;
};

// ======================================================================== page

YouTubePage::YouTubePage(DownloadManager *downloads, PlaylistStore *playlists, QWidget *parent)
    : QWidget(parent)
    , m_downloads(downloads)
    , m_store(playlists)
    , m_nam(new QNetworkAccessManager(this))
{
    // --- header
    m_back = new QPushButton(Theme::icon("back"), QString(), this);
    m_back->setObjectName("chip");
    m_back->setToolTip(tr("Back to the player (Esc)"));
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setFocusPolicy(Qt::NoFocus);

    auto *logo = new QLabel(this);
    logo->setPixmap(Theme::pixmap("youtube", 26, devicePixelRatioF(), QColor(255, 60, 60)));
    auto *brand = new QLabel("YouTube", this);
    QFont bf = brand->font();
    bf.setPointSizeF(bf.pointSizeF() * 1.3);
    bf.setWeight(QFont::Bold);
    brand->setFont(bf);

    m_tabSearch = chip("search", tr(" Search"), tr("Search YouTube"), false, this);
    m_tabLists = chip("list", tr(" Playlists"), tr("Your playlists"), false, this);
    for (QPushButton *b : {m_tabSearch, m_tabLists})
        b->setCheckable(true);
    m_tabSearch->setChecked(true);

    m_query = new QLineEdit(this);
    m_query->setObjectName("searchField");
    m_query->setPlaceholderText(tr("Search YouTube, or paste a video link"));
    m_query->setClearButtonEnabled(true);
    m_query->addAction(Theme::icon("search", Theme::kMuted), QLineEdit::LeadingPosition);

    m_searchButton = new QPushButton(tr("Search"), this);
    m_searchButton->setObjectName("pill");
    m_searchButton->setCursor(Qt::PointingHandCursor);
    m_searchButton->setFocusPolicy(Qt::NoFocus);

    m_quality = new QComboBox(this);
    m_quality->setToolTip(tr("Quality for playing and video downloads"));
    m_quality->setFocusPolicy(Qt::NoFocus);
    m_quality->addItem(tr("Best quality"), "best");
    m_quality->addItem(tr("Up to 1080p"), "1080");
    m_quality->addItem(tr("Up to 720p"), "720");
    m_quality->addItem(tr("Up to 480p"), "480");
    m_quality->addItem(tr("Audio only"), "audio");
    m_quality->setCurrentIndex(qMax(0, m_quality->findData(Settings::value("stream/quality", "1080").toString())));
    connect(m_quality, &QComboBox::currentIndexChanged, this,
            [this] { Settings::setValue("stream/quality", m_quality->currentData()); });

    m_downloadsToggle = chip("download", tr(" Downloads"), tr("Show or hide downloads"), false, this);
    m_downloadsToggle->setCheckable(true);

    auto *header = new QHBoxLayout;
    header->setSpacing(10);
    header->addWidget(m_back);
    header->addSpacing(4);
    header->addWidget(logo);
    header->addWidget(brand);
    header->addSpacing(10);
    header->addWidget(m_tabSearch);
    header->addWidget(m_tabLists);
    header->addSpacing(10);
    header->addWidget(m_query, 1);
    header->addWidget(m_searchButton);
    header->addStretch(0); // takes the space when the search box is hidden (Playlists tab)
    header->addSpacing(8);
    header->addWidget(m_quality);
    header->addWidget(m_downloadsToggle);

    m_status = new QLabel(this);
    setMuted(m_status, 0.92);

    // --- body: empty/loading/error state, results, playlists
    m_statePage = new QWidget(this);
    m_stateIcon = new QLabel(m_statePage);
    m_stateIcon->setAlignment(Qt::AlignCenter);
    m_stateTitle = new QLabel(m_statePage);
    m_stateTitle->setAlignment(Qt::AlignCenter);
    QFont sf = m_stateTitle->font();
    sf.setPointSizeF(sf.pointSizeF() * 1.5);
    sf.setWeight(QFont::DemiBold);
    m_stateTitle->setFont(sf);
    m_stateSubtitle = new QLabel(m_statePage);
    m_stateSubtitle->setAlignment(Qt::AlignCenter);
    m_stateSubtitle->setWordWrap(true);
    setMuted(m_stateSubtitle);
    auto *stateLay = new QVBoxLayout(m_statePage);
    stateLay->addStretch(2);
    stateLay->addWidget(m_stateIcon);
    stateLay->addSpacing(8);
    stateLay->addWidget(m_stateTitle);
    stateLay->addWidget(m_stateSubtitle);
    stateLay->addStretch(3);

    m_list = new QListWidget(this);
    m_list->setObjectName("results");
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setSpacing(2);

    m_more = new QPushButton(tr("Load more results"), this);
    m_more->setObjectName("pillGhost");
    m_more->setCursor(Qt::PointingHandCursor);
    m_more->setFocusPolicy(Qt::NoFocus);

    m_resultsPage = new QWidget(this);
    auto *resultsLay = new QVBoxLayout(m_resultsPage);
    resultsLay->setContentsMargins(0, 0, 0, 0);
    resultsLay->addWidget(m_list, 1);
    resultsLay->addWidget(m_more, 0, Qt::AlignHCenter);

    m_playlistsPage = buildPlaylistsPage();

    m_body = new QStackedWidget(this);
    m_body->addWidget(m_statePage);
    m_body->addWidget(m_resultsPage);
    m_body->addWidget(m_playlistsPage);

    // --- downloads panel
    m_panel = new QWidget(this);
    m_panel->setObjectName("downloadsPanel");
    m_panel->setFixedWidth(360);
    m_panel->setAttribute(Qt::WA_StyledBackground);
    auto *panelTitle = new QLabel(tr("Downloads"), m_panel);
    QFont pf = panelTitle->font();
    pf.setPointSizeF(pf.pointSizeF() * 1.15);
    pf.setWeight(QFont::DemiBold);
    panelTitle->setFont(pf);
    m_panelFolder = new QLabel(m_panel);
    setMuted(m_panelFolder, 0.88);
    auto *openFolder = chip("folder", tr(" Open folder"), tr("Open the download folder"), false, m_panel);
    auto *changeFolder = chip("settings", tr(" Change…"), tr("Choose where downloads are saved"), false, m_panel);
    auto *folderButtons = new QHBoxLayout;
    folderButtons->addWidget(openFolder);
    folderButtons->addWidget(changeFolder);
    folderButtons->addStretch();

    auto *rowsHost = new QWidget;
    m_panelRows = new QVBoxLayout(rowsHost);
    m_panelRows->setContentsMargins(0, 0, 0, 0);
    m_panelRows->setSpacing(2);
    m_panelEmpty = new QLabel(tr("Downloads appear here.\nUse Video or Audio on any result."), rowsHost);
    m_panelEmpty->setAlignment(Qt::AlignCenter);
    setMuted(m_panelEmpty);
    m_panelRows->addWidget(m_panelEmpty);
    m_panelRows->addStretch();
    auto *scroll = new QScrollArea(m_panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // rows elide instead of scrolling
    scroll->setWidget(rowsHost);
    scroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");

    auto *panelLay = new QVBoxLayout(m_panel);
    panelLay->setContentsMargins(14, 14, 10, 14);
    panelLay->setSpacing(6);
    panelLay->addWidget(panelTitle);
    panelLay->addWidget(m_panelFolder);
    panelLay->addLayout(folderButtons);
    panelLay->addSpacing(6);
    panelLay->addWidget(scroll, 1);
    m_panel->hide();

    // --- assemble
    auto *main = new QVBoxLayout;
    main->setContentsMargins(20, 16, 20, 12);
    main->setSpacing(8);
    main->addLayout(header);
    main->addWidget(m_status);
    main->addWidget(m_body, 1);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addLayout(main, 1);
    root->addWidget(m_panel);

    showState("youtube", tr("Search YouTube"),
              tr("Watch right here, save videos to playlists, or download them as MP4 / MP3.\nNo account needed."));
    updateDownloadsHeader();

    // --- wiring
    connect(m_back, &QPushButton::clicked, this, &YouTubePage::backRequested);
    connect(m_tabSearch, &QPushButton::clicked, this, [this] { setTab(false); });
    connect(m_tabLists, &QPushButton::clicked, this, [this] { setTab(true); });
    connect(m_query, &QLineEdit::returnPressed, this, [this] { runSearch(); });
    connect(m_searchButton, &QPushButton::clicked, this, [this] { runSearch(); });
    connect(m_more, &QPushButton::clicked, this, [this] { runSearch(true); });
    connect(m_downloadsToggle, &QPushButton::toggled, m_panel, &QWidget::setVisible);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (auto *card = static_cast<ResultCard *>(m_list->itemWidget(item)))
            emit playRequested(card->video().url, card->video().title);
    });
    connect(openFolder, &QPushButton::clicked, this, [] {
        QDir().mkpath(DownloadManager::folder());
        Platform::revealInFileManager(DownloadManager::folder() + "/.");
    });
    connect(changeFolder, &QPushButton::clicked, this, &YouTubePage::chooseFolder);
    connect(m_downloads, &DownloadManager::jobAdded, this, &YouTubePage::onJobAdded);
    connect(m_downloads, &DownloadManager::jobChanged, this, &YouTubePage::onJobChanged);
    connect(m_store, &PlaylistStore::changed, this, [this] { refreshLists(); });
    for (int id : m_downloads->ids())
        onJobAdded(id);
    refreshLists();
}

void YouTubePage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0, QColor(22, 23, 28));
    g.setColorAt(1, QColor(14, 14, 16));
    p.fillRect(rect(), g);
}

void YouTubePage::focusSearch()
{
    {
        const QSignalBlocker block(m_quality);
        m_quality->setCurrentIndex(qMax(0, m_quality->findData(Settings::value("stream/quality", "1080").toString())));
    }
    if (m_tabLists->isChecked())
        return;
    m_query->setFocus();
    m_query->selectAll();
}

void YouTubePage::search(const QString &query)
{
    setTab(false);
    m_query->setText(query);
    runSearch();
}

void YouTubePage::setCanGoBack(bool canGoBack)
{
    m_back->setVisible(canGoBack);
}

void YouTubePage::setTab(bool playlists)
{
    m_tabSearch->setChecked(!playlists);
    m_tabLists->setChecked(playlists);
    m_query->setVisible(!playlists);
    m_searchButton->setVisible(!playlists);
    if (playlists) {
        refreshLists();
        m_body->setCurrentWidget(m_playlistsPage);
        m_status->setText(tr("%n playlist(s)", nullptr, m_store->count()));
    } else {
        m_body->setCurrentWidget(m_list->count() > 0 ? m_resultsPage : m_statePage);
        m_status->setText(m_list->count() > 0 ? tr("%n result(s) for “%1”", nullptr, m_list->count()).arg(m_lastQuery) : QString());
        m_query->setFocus();
    }
}

void YouTubePage::showPlaylists(int list)
{
    setTab(true);
    if (list >= 0 && list < m_listsView->count())
        m_listsView->setCurrentRow(list);
}

void YouTubePage::showState(const QString &icon, const QString &title, const QString &subtitle)
{
    m_stateIcon->setPixmap(Theme::pixmap(icon, 64, devicePixelRatioF(), icon == "youtube" ? QColor(255, 60, 60) : QColor(255, 255, 255, 150)));
    m_stateTitle->setText(title);
    m_stateSubtitle->setText(subtitle);
    m_body->setCurrentWidget(m_statePage);
}

// ------------------------------------------------------------------------ search

void YouTubePage::runSearch(bool more)
{
    const QString q = more ? m_lastQuery : m_query->text().trimmed();
    if (q.isEmpty())
        return;

    // A pasted link plays directly instead of being searched for.
    if (!more && q.contains("://")) {
        emit playRequested(q, QString());
        return;
    }

    YtDlp::ensure(this, [this, q, more](bool ok) {
        if (!ok)
            return;
        m_lastQuery = q;
        m_count = more ? m_count + 20 : 20;
        m_status->setText(tr("Searching for “%1”…").arg(q));
        m_searchButton->setEnabled(false);
        m_more->setEnabled(false);
        if (!more)
            showState("search", tr("Searching…"), q);
        m_search.search(q, m_count, [this, q, more](QVector<YtVideo> results, const QString &error) {
            m_searchButton->setEnabled(true);
            m_more->setEnabled(true);
            if (q != m_lastQuery)
                return;
            const bool onLists = m_tabLists->isChecked();
            if (!error.isEmpty()) {
                if (more)
                    m_count -= 20; // "Load more" failed: keep the count matching what's shown
                m_status->clear();
                showState("alert", tr("Search failed"), error);
                if (onLists)
                    setTab(true);
                return;
            }
            if (results.isEmpty()) {
                m_status->clear();
                showState("search", tr("No results"), tr("Try different words."));
                return;
            }
            showResults(results);
            if (onLists)
                setTab(true); // keep showing what the user switched to; results wait on the Search tab
        });
    });
}

void YouTubePage::showResults(const QVector<YtVideo> &results)
{
    const int scroll = m_list->verticalScrollBar()->value();
    m_list->clear();
    for (const YtVideo &v : results) {
        auto *item = new QListWidgetItem(m_list);
        auto *card = new ResultCard(v, [this, v](const QString &action, QWidget *button) {
            if (action == "play")
                emit playRequested(v.url, v.title);
            else if (action == "hover")
                emit prefetchRequested(v.url);
            else if (action == "save")
                saveToPlaylistMenu(this, m_store, toItem(v), button->mapToGlobal(QPoint(0, button->height())));
            else
                download(v, action);
        });
        item->setSizeHint(QSize(0, 133));
        m_list->setItemWidget(item, card);
        thumbnailInto(v.id, card->thumb());
    }
    m_body->setCurrentWidget(m_resultsPage);
    m_list->verticalScrollBar()->setValue(scroll);
    m_more->setVisible(results.size() >= m_count && m_count < 100);
    if (!results.isEmpty() && !results.first().live)
        emit prefetchRequested(results.first().url);
    m_status->setText(tr("%n result(s) for “%1”", nullptr, int(results.size())).arg(m_lastQuery));
}

void YouTubePage::thumbnailInto(const QString &id, ThumbView *view)
{
    if (id.isEmpty() || !view)
        return;
    if (m_thumbs.contains(id)) {
        view->setPixmap(m_thumbs.value(id));
        return;
    }
    const bool alreadyRequested = m_waiting.contains(id);
    m_waiting[id] << QPointer<ThumbView>(view);
    if (alreadyRequested)
        return;
    QNetworkRequest req{QUrl(QString("https://i.ytimg.com/vi/%1/mqdefault.jpg").arg(id))};
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        reply->deleteLater();
        const QList<QPointer<ThumbView>> views = m_waiting.take(id);
        QPixmap pm;
        if (reply->error() != QNetworkReply::NoError || !pm.loadFromData(reply->readAll()))
            return;
        m_thumbs.insert(id, pm);
        for (const QPointer<ThumbView> &v : views)
            if (v)
                v->setPixmap(pm);
    });
}

void YouTubePage::download(const YtVideo &video, const QString &kind)
{
    YtDlp::ensure(this, [this, video, kind](bool ok) {
        if (!ok)
            return;
        m_downloads->add(video.url, video.title, kind);
        m_status->setText(kind == "video" ? tr("Downloading video: %1").arg(video.title)
                                          : tr("Downloading audio: %1").arg(video.title));
    });
}

// ------------------------------------------------------------------------ playlists

void YouTubePage::saveToPlaylistMenu(QWidget *parent, PlaylistStore *store, const PlaylistItem &item, const QPoint &globalPos)
{
    QMenu menu(parent);
    menu.addSection(QCoreApplication::translate("YouTube", "Save to playlist"));
    for (int i = 0; i < store->count(); ++i) {
        const Playlist &p = store->at(i);
        bool already = false;
        for (const PlaylistItem &it : p.items)
            already = already || it.url == item.url || (!item.id.isEmpty() && it.id == item.id);
        QAction *a = menu.addAction(Theme::icon("list"), QString("%1   (%2)").arg(p.name).arg(p.items.size()));
        a->setCheckable(true);
        a->setChecked(already);
        a->setEnabled(!already);
        QObject::connect(a, &QAction::triggered, parent, [store, i, item] { store->addItem(i, item); });
    }
    menu.addSeparator();
    QAction *create = menu.addAction(Theme::icon("plus"), QCoreApplication::translate("YouTube", "New playlist…"));
    QObject::connect(create, &QAction::triggered, parent, [parent, store, item] {
        bool ok = false;
        const QString name = QInputDialog::getText(parent, QCoreApplication::translate("YouTube", "New playlist"),
                                                   QCoreApplication::translate("YouTube", "Name:"), QLineEdit::Normal,
                                                   QString(), &ok);
        if (ok && !name.trimmed().isEmpty())
            store->addItem(store->create(name), item);
    });
    menu.exec(globalPos);
}

QWidget *YouTubePage::buildPlaylistsPage()
{
    auto *page = new QWidget(this);

    // left: the lists
    auto *listsHeader = new QLabel(tr("YOUR PLAYLISTS"), page);
    QFont hf = listsHeader->font();
    hf.setPointSizeF(hf.pointSizeF() * 0.85);
    hf.setWeight(QFont::DemiBold);
    hf.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    listsHeader->setFont(hf);
    setMuted(listsHeader);

    m_listsView = new QListWidget(page);
    m_listsView->setObjectName("results");
    m_listsView->setFrameShape(QFrame::NoFrame);
    m_listsView->setFixedWidth(250);
    m_listsView->setIconSize(QSize(18, 18));

    auto *newList = chip("plus", tr(" New"), tr("Create a playlist"), false, page);
    auto *renameList = chip("settings", tr(" Rename"), tr("Rename the selected playlist"), false, page);
    auto *deleteList = chip("close", tr(" Delete"), tr("Delete the selected playlist"), false, page);
    auto *listButtons = new QHBoxLayout;
    listButtons->setSpacing(6);
    listButtons->addWidget(newList);
    listButtons->addWidget(renameList);
    listButtons->addWidget(deleteList);
    listButtons->addStretch();

    auto *left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(listsHeader);
    left->addWidget(m_listsView, 1);
    left->addLayout(listButtons);

    // right: the selected list
    m_listTitle = new QLabel(page);
    QFont tf = m_listTitle->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.6);
    tf.setWeight(QFont::Bold);
    m_listTitle->setFont(tf);
    m_listInfo = new QLabel(page);
    setMuted(m_listInfo);

    m_playAll = new QPushButton(Theme::icon("play", Qt::white), tr(" Play all"), page);
    m_playAll->setObjectName("pill");
    m_playAll->setCursor(Qt::PointingHandCursor);
    m_playAll->setFocusPolicy(Qt::NoFocus);
    m_shuffle = new QPushButton(Theme::icon("shuffle"), tr(" Shuffle"), page);
    m_shuffle->setObjectName("pillGhost");
    m_shuffle->setCursor(Qt::PointingHandCursor);
    m_shuffle->setFocusPolicy(Qt::NoFocus);
    m_sort = new QPushButton(Theme::icon("list"), tr(" Sort"), page);
    m_sort->setObjectName("pillGhost");
    m_sort->setCursor(Qt::PointingHandCursor);
    m_sort->setFocusPolicy(Qt::NoFocus);
    m_sort->setToolTip(tr("Reorder this playlist"));
    auto *addLink = chip("link", tr(" Add link or YouTube playlist…"),
                         tr("Paste a video link, or a YouTube playlist link to import all its videos"), false, page);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(10);
    actions->addWidget(m_playAll);
    actions->addWidget(m_shuffle);
    actions->addWidget(m_sort);
    actions->addStretch();
    actions->addWidget(addLink);

    m_itemsView = new QListWidget(page);
    m_itemsView->setObjectName("results");
    m_itemsView->setFrameShape(QFrame::NoFrame);
    m_itemsView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_itemsView->setDragDropMode(QAbstractItemView::InternalMove);
    m_itemsView->setDefaultDropAction(Qt::MoveAction);
    m_itemsView->setSelectionMode(QAbstractItemView::SingleSelection);

    m_itemsEmpty = new QLabel(tr("This playlist is empty.\n\nUse “Save” on any search result,\n"
                                 "right-click ▸ Save to playlist while watching,\nor add a link above."),
                              page);
    m_itemsEmpty->setAlignment(Qt::AlignCenter);
    setMuted(m_itemsEmpty);
    m_itemsStack = new QStackedWidget(page);
    m_itemsStack->addWidget(m_itemsEmpty);
    m_itemsStack->addWidget(m_itemsView);

    auto *right = new QVBoxLayout;
    right->setSpacing(8);
    right->addWidget(m_listTitle);
    right->addWidget(m_listInfo);
    right->addSpacing(4);
    right->addLayout(actions);
    right->addSpacing(4);
    right->addWidget(m_itemsStack, 1);

    auto *lay = new QHBoxLayout(page);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->setSpacing(24);
    lay->addLayout(left);
    lay->addLayout(right, 1);

    connect(newList, &QPushButton::clicked, this, &YouTubePage::newPlaylist);
    connect(renameList, &QPushButton::clicked, this, &YouTubePage::renamePlaylist);
    connect(deleteList, &QPushButton::clicked, this, &YouTubePage::deletePlaylist);
    connect(addLink, &QPushButton::clicked, this, &YouTubePage::addLinkToPlaylist);
    connect(m_playAll, &QPushButton::clicked, this, [this] { playList(false); });
    connect(m_shuffle, &QPushButton::clicked, this, [this] { playList(true); });
    connect(m_sort, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        menu.addSection(tr("Sort playlist by"));
        const QList<QPair<QString, QString>> modes = {
            {tr("Most viewed"), "views_desc"},       {tr("Least viewed"), "views_asc"},
            {tr("Title (A–Z)"), "title"},            {tr("Longest first"), "duration_desc"},
            {tr("Shortest first"), "duration_asc"},  {tr("Newest added"), "added_desc"},
            {tr("Oldest added"), "added_asc"}};
        for (const auto &m : modes) {
            const QString mode = m.second;
            connect(menu.addAction(m.first), &QAction::triggered, this, [this, mode] { sortCurrentList(mode); });
        }
        menu.exec(m_sort->mapToGlobal(QPoint(0, m_sort->height())));
    });
    connect(m_listsView, &QListWidget::currentRowChanged, this, [this] {
        if (!m_refreshing)
            refreshItems();
    });
    connect(m_listsView, &QListWidget::itemDoubleClicked, this, [this] { renamePlaylist(); });
    connect(m_itemsView, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        playList(false, m_itemsView->row(item));
    });
    // Drag-and-drop reorder: item widgets don't survive the move, so update the store and rebuild.
    connect(m_itemsView->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int start, int, const QModelIndex &, int row) {
                if (m_refreshing)
                    return;
                const int to = row > start ? row - 1 : row;
                QMetaObject::invokeMethod(this, [this, start, to] { m_store->moveItem(currentList(), start, to); },
                                          Qt::QueuedConnection);
            });
    return page;
}

int YouTubePage::currentList() const
{
    return m_listsView->currentRow();
}

void YouTubePage::refreshLists()
{
    m_refreshing = true;
    const int keep = qMax(0, m_listsView->currentRow());
    m_listsView->clear();
    for (int i = 0; i < m_store->count(); ++i) {
        const Playlist &p = m_store->at(i);
        auto *item = new QListWidgetItem(Theme::icon("list"), QString("%1\n%2").arg(p.name, tr("%n video(s)", nullptr, int(p.items.size()))));
        item->setSizeHint(QSize(0, 52));
        m_listsView->addItem(item);
    }
    m_listsView->setCurrentRow(qMin(keep, m_listsView->count() - 1));
    m_refreshing = false;
    refreshItems();
}

void YouTubePage::refreshItems()
{
    const int list = currentList();
    m_refreshing = true;
    const int scroll = m_itemsView->verticalScrollBar()->value();
    m_itemsView->clear();
    m_refreshing = false;
    if (list < 0 || list >= m_store->count()) {
        m_listTitle->clear();
        m_listInfo->clear();
        m_itemsStack->setCurrentWidget(m_itemsEmpty);
        return;
    }
    const Playlist &p = m_store->at(list);
    double total = 0;
    for (const PlaylistItem &it : p.items)
        total += it.duration;
    m_listTitle->setText(p.name);
    m_listInfo->setText(total > 0 ? tr("%n video(s)  ·  %1", nullptr, int(p.items.size())).arg(formatDuration(total))
                                  : tr("%n video(s)", nullptr, int(p.items.size())));
    m_playAll->setEnabled(!p.items.isEmpty());
    m_shuffle->setEnabled(p.items.size() > 1);
    m_sort->setEnabled(p.items.size() > 1 && !m_fetchingViews);

    m_refreshing = true;
    for (int i = 0; i < p.items.size(); ++i) {
        const PlaylistItem it = p.items.at(i);
        auto *item = new QListWidgetItem(m_itemsView);
        item->setSizeHint(QSize(0, 86));
        auto *row = new PlaylistRow(i + 1, it, [this, i, list, it](const QString &action) {
            if (action == "play")
                playList(false, i);
            else if (action == "hover")
                emit prefetchRequested(it.url);
            else if (action == "remove")
                QMetaObject::invokeMethod(this, [this, list, i] { m_store->removeItem(list, i); }, Qt::QueuedConnection);
        });
        m_itemsView->setItemWidget(item, row);
        thumbnailInto(it.id, row->thumb());
    }
    m_refreshing = false;
    m_itemsView->verticalScrollBar()->setValue(scroll);
    m_itemsStack->setCurrentWidget(p.items.isEmpty() ? static_cast<QWidget *>(m_itemsEmpty) : m_itemsView);
    if (!p.items.isEmpty() && m_body->currentWidget() == m_playlistsPage)
        emit prefetchRequested(p.items.first().url);
}

void YouTubePage::newPlaylist()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New playlist"), tr("Name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    const int index = m_store->create(name);
    m_listsView->setCurrentRow(index);
}

void YouTubePage::renamePlaylist()
{
    const int list = currentList();
    if (list < 0)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename playlist"), tr("Name:"), QLineEdit::Normal,
                                               m_store->at(list).name, &ok);
    if (ok)
        m_store->rename(list, name);
}

void YouTubePage::deletePlaylist()
{
    const int list = currentList();
    if (list < 0)
        return;
    const Playlist &p = m_store->at(list);
    if (QMessageBox::question(this, tr("Delete playlist"),
                              tr("Delete “%1” (%n video(s))?", nullptr, int(p.items.size())).arg(p.name))
        == QMessageBox::Yes)
        m_store->remove(list);
}

void YouTubePage::addLinkToPlaylist()
{
    const int list = currentList();
    if (list < 0)
        return;
    bool ok = false;
    const QString url = QInputDialog::getText(this, tr("Add to playlist"),
                                              tr("Video link, or a YouTube playlist link to import all its videos:"),
                                              QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || url.isEmpty())
        return;
    if (m_importing) {
        QMessageBox::information(this, tr("Add to playlist"), tr("Another link is still being read. Try again in a moment."));
        return;
    }
    const QString listName = m_store->at(list).name; // the row may move while yt-dlp works
    YtDlp::ensure(this, [this, listName, url](bool ready) {
        if (!ready)
            return;
        m_importing = true;
        m_status->setText(tr("Reading %1…").arg(url));
        m_fetcher.fetch(url, [this, listName, url](QVector<YtVideo> videos, const QString &error) {
            m_importing = false;
            const int target = m_store->indexOf(listName);
            if (target < 0) {
                m_status->setText(tr("Playlist “%1” no longer exists").arg(listName));
                return;
            }
            if (!error.isEmpty() || videos.isEmpty()) {
                m_status->setText(error.isEmpty() ? tr("Nothing found at that link") : error);
                return;
            }
            QVector<PlaylistItem> items;
            for (const YtVideo &v : videos)
                items << toItem(v);
            m_store->addItems(target, items);
            m_status->setText(tr("Added %n video(s) to “%1”", nullptr, int(items.size())).arg(listName));
        });
    });
}

void YouTubePage::sortCurrentList(const QString &mode)
{
    const int list = currentList();
    if (list < 0 || list >= m_store->count() || m_fetchingViews)
        return;
    if (!mode.startsWith("views")) {
        m_store->sortList(list, mode);
        return;
    }
    // Sorting by views needs every count: look up the ones saved without (older saves, links).
    QStringList missing;
    for (const PlaylistItem &it : m_store->at(list).items)
        if (it.views < 0)
            missing << it.url;
    if (missing.isEmpty()) {
        m_store->sortList(list, mode);
        return;
    }
    const QString listName = m_store->at(list).name; // lists may be added/removed meanwhile
    YtDlp::ensure(this, [this, listName, mode, missing](bool ok) {
        if (!ok)
            return;
        m_fetchingViews = true;
        m_sort->setEnabled(false);
        const int total = int(missing.size());
        auto found = std::make_shared<QVector<PlaylistItem>>();
        m_status->setText(tr("Getting view counts… 0 / %1").arg(total));
        m_info.fetch(
            missing,
            [this, found, total](const YtVideo &v) {
                *found << toItem(v); // applied in one go at the end: no per-video rebuild + save
                m_status->setText(tr("Getting view counts… %1 / %2").arg(found->size()).arg(total));
            },
            [this, listName, mode, found](int got, int total) {
                m_fetchingViews = false;
                m_store->updateInfos(*found);
                const int target = m_store->indexOf(listName);
                if (target >= 0)
                    m_store->sortList(target, mode);
                m_status->setText(got == total ? tr("Sorted by views")
                                               : tr("Sorted by views (%1 of %2 counts unavailable)").arg(total - got).arg(total));
                refreshItems();
            });
    });
}

void YouTubePage::playList(bool shuffle, int start)
{
    const int list = currentList();
    if (list < 0 || list >= m_store->count())
        return;
    QVector<PlaylistItem> items = m_store->at(list).items;
    if (items.isEmpty())
        return;
    if (shuffle) {
        std::shuffle(items.begin(), items.end(), *QRandomGenerator::global());
        start = 0;
    }
    emit playQueueRequested(items, qBound(0, start, int(items.size()) - 1), m_store->at(list).name);
}

// ------------------------------------------------------------------------ downloads panel

void YouTubePage::onJobAdded(int id)
{
    if (m_rows.contains(id))
        return;
    auto *row = new DownloadRow(id, m_downloads, [this](const QString &file, const QString &title) {
        emit playRequested(file, title);
    });
    m_rows.insert(id, row);
    m_panelEmpty->hide();
    m_panelRows->insertWidget(0, row); // newest on top
    m_downloadsToggle->setChecked(true); // reveal the panel on the first download
    updateDownloadsHeader();
}

void YouTubePage::onJobChanged(int id)
{
    if (DownloadRow *row = m_rows.value(id))
        row->refresh();
    updateDownloadsHeader();
}

void YouTubePage::updateDownloadsHeader()
{
    int active = 0;
    for (int id : m_downloads->ids())
        if (const DownloadJob *j = m_downloads->job(id))
            active += j->state == DownloadJob::Running || j->state == DownloadJob::Queued;
    m_downloadsToggle->setText(active > 0 ? tr(" Downloads (%1)").arg(active) : tr(" Downloads"));
    m_panelFolder->setText(fontMetrics().elidedText(QDir::toNativeSeparators(DownloadManager::folder()), Qt::ElideMiddle, 320));
    m_panelFolder->setToolTip(QDir::toNativeSeparators(DownloadManager::folder()));
}

void YouTubePage::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Save downloads to"), DownloadManager::folder());
    if (!dir.isEmpty()) {
        DownloadManager::setFolder(dir);
        updateDownloadsHeader();
    }
}

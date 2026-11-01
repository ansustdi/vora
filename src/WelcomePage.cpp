#include "WelcomePage.h"

#include "FolderList.h"
#include "Settings.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

WelcomePage::WelcomePage(QWidget *parent)
    : QWidget(parent)
{
    auto *logo = new QLabel(this);
    logo->setPixmap(QIcon(":/icon.svg").pixmap(88, 88));
    logo->setAlignment(Qt::AlignCenter);

    auto *title = new QLabel("Vora", this);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 2.4);
    tf.setWeight(QFont::Bold);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);

    auto *subtitle = new QLabel(tr("Drop a video or image anywhere · right-click for the menu"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    QPalette sp = subtitle->palette();
    sp.setColor(QPalette::WindowText, Theme::kMuted);
    subtitle->setPalette(sp);

    auto *openFile = new QPushButton(Theme::icon("file", Qt::white), tr(" Open file"), this);
    openFile->setObjectName("pill");
    auto *openFolder = new QPushButton(Theme::icon("folder"), tr(" Open folder"), this);
    openFolder->setObjectName("pillGhost");
    auto *youtube = new QPushButton(Theme::icon("youtube", QColor(255, 80, 80)), tr(" YouTube"), this);
    youtube->setObjectName("pillGhost");
    youtube->setToolTip("Ctrl+Y");
    youtube->setCursor(Qt::PointingHandCursor);
    youtube->setFocusPolicy(Qt::NoFocus);
    youtube->setIconSize(QSize(18, 18));
    for (QPushButton *b : {openFile, openFolder}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setIconSize(QSize(18, 18));
    }
    openFile->setToolTip("Ctrl+O");
    openFolder->setToolTip("Ctrl+Shift+O");

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    buttons->addStretch();
    buttons->addWidget(openFile);
    buttons->addWidget(openFolder);
    buttons->addWidget(youtube);
    buttons->addStretch();

    m_recentHeader = new QLabel(tr("RECENT"), this);
    QFont hf = m_recentHeader->font();
    hf.setPointSizeF(hf.pointSizeF() * 0.85);
    hf.setWeight(QFont::DemiBold);
    hf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    m_recentHeader->setFont(hf);
    m_recentHeader->setPalette(sp);
    m_recentHeader->setContentsMargins(12, 0, 0, 4);

    auto *recentBox = new QWidget(this);
    recentBox->setFixedWidth(520);
    m_recentList = new QVBoxLayout(recentBox);
    m_recentList->setContentsMargins(0, 0, 0, 0);
    m_recentList->setSpacing(0);

    auto *column = new QVBoxLayout(this);
    column->addStretch(3);
    column->addWidget(logo);
    column->addSpacing(10);
    column->addWidget(title);
    column->addSpacing(4);
    column->addWidget(subtitle);
    column->addSpacing(26);
    column->addLayout(buttons);
    column->addSpacing(34);
    column->addWidget(m_recentHeader, 0, Qt::AlignHCenter);
    column->addWidget(recentBox, 0, Qt::AlignHCenter);
    column->addStretch(4);
    m_recentHeader->setFixedWidth(520);

    connect(openFile, &QPushButton::clicked, this, &WelcomePage::openFileClicked);
    connect(openFolder, &QPushButton::clicked, this, &WelcomePage::openFolderClicked);
    connect(youtube, &QPushButton::clicked, this, &WelcomePage::youtubeClicked);
}

void WelcomePage::setRecent(const QStringList &paths)
{
    while (QLayoutItem *item = m_recentList->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int shown = 0;
    const QVariantMap titles = Settings::value("ui/recentTitles").toMap();
    for (const QString &path : paths) {
        if (shown >= 6)
            break;
        if (path.contains("://") && !QFileInfo::exists(path)) {
            const QUrl u(path);
            const QString title = titles.value(path, u.host() + u.path()).toString();
            auto *b = new QPushButton(Theme::icon("link", Theme::kMuted), QString(), this);
            b->setObjectName("recentItem");
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            b->setIconSize(QSize(18, 18));
            b->setText(QString("  %1    %2").arg(fontMetrics().elidedText(title, Qt::ElideRight, 260), u.host()));
            b->setToolTip(path);
            connect(b, &QPushButton::clicked, this, [this, path] { emit recentClicked(path); });
            m_recentList->addWidget(b);
            ++shown;
            continue;
        }
        const QFileInfo fi(path);
        if (!fi.exists())
            continue;
        const bool image = mediaTypeOf(path) == MediaType::Image;
        auto *b = new QPushButton(Theme::icon(image ? "image" : isAudioFile(path) ? "music" : "film", Theme::kMuted), QString(), this);
        b->setObjectName("recentItem");
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setIconSize(QSize(18, 18));
        const QString dir = QDir::toNativeSeparators(fi.absolutePath());
        b->setText(QString("  %1    %2").arg(fi.fileName(), fontMetrics().elidedText(dir, Qt::ElideMiddle, 220)));
        b->setToolTip(QDir::toNativeSeparators(path));
        connect(b, &QPushButton::clicked, this, [this, path] { emit recentClicked(path); });
        m_recentList->addWidget(b);
        ++shown;
    }
    m_recentHeader->setVisible(shown > 0);
}

void WelcomePage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QRadialGradient bg(QPointF(width() / 2.0, height() * 0.35), qMax(width(), height()) * 0.75);
    bg.setColorAt(0, QColor(30, 34, 46));
    bg.setColorAt(1, QColor(13, 13, 15));
    p.fillRect(rect(), bg);
}

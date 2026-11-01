#include "OpenStreamDialog.h"

#include "Settings.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

OpenStreamDialog::OpenStreamDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Open stream"));
    setMinimumWidth(560);

    auto *title = new QLabel(tr("Watch a stream or online video"), this);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.25);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);

    auto *hint = new QLabel(tr("YouTube, Twitch, Kick and 1000+ other sites, plus direct links, .m3u8 playlists "
                               "and rtsp:// cameras."),
                            this);
    hint->setWordWrap(true);
    QPalette hp = hint->palette();
    hp.setColor(QPalette::WindowText, Theme::kMuted);
    hint->setPalette(hp);

    m_url = new QLineEdit(this);
    m_url->setPlaceholderText("https://www.youtube.com/watch?v=…   ·   https://www.twitch.tv/…   ·   https://kick.com/…");
    m_url->setClearButtonEnabled(true);
    const QString clip = QApplication::clipboard()->text().trimmed();
    if (clip.contains("://") && !clip.contains('\n') && QUrl(clip).isValid())
        m_url->setText(clip);
    m_url->selectAll();

    m_quality = new QComboBox(this);
    m_quality->addItem(tr("Best available"), "best");
    m_quality->addItem(tr("Up to 1080p"), "1080");
    m_quality->addItem(tr("Up to 720p"), "720");
    m_quality->addItem(tr("Up to 480p (saves data)"), "480");
    m_quality->addItem(tr("Audio only"), "audio");
    m_quality->setCurrentIndex(qMax(0, m_quality->findData(Settings::value("stream/quality", "1080").toString())));

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 8, 0, 8);
    form->addRow(tr("Link"), m_url);
    form->addRow(tr("Quality"), m_quality);

    auto *open = new QPushButton(Theme::icon("play", Qt::white), tr(" Play"), this);
    open->setObjectName("pill");
    open->setDefault(true);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName("pillGhost");
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(open);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 18, 20, 16);
    lay->addWidget(title);
    lay->addWidget(hint);
    lay->addLayout(form);
    lay->addLayout(buttons);

    connect(open, &QPushButton::clicked, this, &OpenStreamDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

QString OpenStreamDialog::url() const
{
    QString u = m_url->text().trimmed();
    if (!u.contains("://") && !u.isEmpty())
        u.prepend("https://"); // "youtube.com/watch?v=…" typed without scheme
    return u;
}

void OpenStreamDialog::accept()
{
    if (m_url->text().trimmed().isEmpty())
        return;
    Settings::setValue("stream/quality", m_quality->currentData());
    QDialog::accept();
}

QString OpenStreamDialog::formatForQuality(const QString &quality)
{
    // Plain HTTPS streams start faster than HLS playlists (fewer round-trips before the first
    // frame), so prefer them; HLS stays as the fallback (live Twitch / Kick / YouTube are HLS-only).
    const QString direct = "[protocol!*=m3u8]";
    if (quality == "audio")
        return "bestaudio" + direct + "/bestaudio/best";
    if (quality == "best")
        return "bestvideo" + direct + "+bestaudio" + direct + "/bestvideo+bestaudio/best";
    // "?" keeps formats without a height field (e.g. some live HLS variants) eligible.
    const QString h = QString("[height<=?%1]").arg(quality);
    return "bestvideo" + h + direct + "+bestaudio" + direct + "/bestvideo" + h + "+bestaudio/best" + h + "/best";
}

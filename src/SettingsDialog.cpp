#include "SettingsDialog.h"

#include "Platform.h"
#include "Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setMinimumWidth(480);

    // --- Subtitles
    auto *subs = new QWidget;
    m_langs = new QLineEdit(Settings::subLanguages().join(','));
    m_langs->setPlaceholderText("en,mn,ru");
    m_langs->setToolTip(tr("Preferred subtitle languages, most wanted first"));
    m_subsMode = new QComboBox;
    m_subsMode->addItem(tr("Show the list of subtitles found online"), "ask");
    m_subsMode->addItem(tr("Download the best match automatically"), "auto");
    m_subsMode->addItem(tr("Do nothing (search with Ctrl+J)"), "off");
    m_subsMode->setCurrentIndex(qMax(0, m_subsMode->findData(Settings::subsMode())));
    m_subsNext = new QCheckBox(tr("Save downloaded subtitles next to the video"));
    m_subsNext->setChecked(Settings::subsNextToVideo());
    auto *hint = new QLabel(tr("Subtitles in the video's folder (and its Subs folder) are used first. "
                               "Otherwise Vora searches online by file fingerprint — no account needed."));
    hint->setWordWrap(true);
    QPalette hp = hint->palette();
    hp.setColor(QPalette::WindowText, QColor(150, 152, 160));
    hint->setPalette(hp);
    auto *sf = new QFormLayout(subs);
    sf->addRow(tr("Languages"), m_langs);
    sf->addRow(tr("No subtitle?"), m_subsMode);
    sf->addRow(m_subsNext);
    sf->addRow(hint);

    // --- Playback
    auto *video = new QWidget;
    m_hwdec = new QCheckBox(tr("Hardware decoding"));
    m_hwdec->setChecked(Settings::hwdec());
    m_resume = new QCheckBox(tr("Resume from last position"));
    m_resume->setChecked(Settings::resume());
    m_autoplay = new QCheckBox(tr("Play next video in folder when finished"));
    m_autoplay->setChecked(Settings::autoplayNext());
    auto *vf = new QFormLayout(video);
    vf->addRow(m_hwdec);
    vf->addRow(m_resume);
    vf->addRow(m_autoplay);

    // --- Images
    auto *images = new QWidget;
    m_slideSec = new QSpinBox;
    m_slideSec->setRange(1, 120);
    m_slideSec->setSuffix(tr(" s"));
    m_slideSec->setValue(Settings::slideshowSeconds());
    m_shuffle = new QCheckBox(tr("Shuffle"));
    m_shuffle->setChecked(Settings::slideshowShuffle());
    m_fade = new QCheckBox(tr("Fade transition"));
    m_fade->setChecked(Settings::slideshowFade());
    m_quality = new QSpinBox;
    m_quality->setRange(50, 100);
    m_quality->setValue(Settings::jpegQuality());
    auto *imf = new QFormLayout(images);
    imf->addRow(tr("Slideshow interval"), m_slideSec);
    imf->addRow(m_shuffle);
    imf->addRow(m_fade);
    imf->addRow(tr("JPEG / WebP save quality"), m_quality);

    // --- System
    auto *system = new QWidget;
    auto *reg = new QPushButton(tr("Add Vora to Explorer's \"Open with\" menu"));
    auto *syl = new QVBoxLayout(system);
    syl->addWidget(reg);
    syl->addStretch();
    connect(reg, &QPushButton::clicked, this, [this] {
        QString err;
        if (Platform::registerOpenWith(&err))
            QMessageBox::information(this, tr("Done"), tr("Vora now appears under \"Open with\" for media files."));
        else
            QMessageBox::warning(this, tr("Not registered"), err);
    });

    auto *tabs = new QTabWidget;
    tabs->addTab(subs, tr("Subtitles"));
    tabs->addTab(video, tr("Playback"));
    tabs->addTab(images, tr("Images"));
    tabs->addTab(system, tr("System"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(tabs);
    lay->addWidget(buttons);
}

void SettingsDialog::save()
{
    Settings::setValue("subs/languages", m_langs->text().trimmed());
    Settings::setValue("subs/mode", m_subsMode->currentData());
    Settings::setValue("subs/nextToVideo", m_subsNext->isChecked());
    Settings::setValue("video/hwdec", m_hwdec->isChecked());
    Settings::setValue("video/resume", m_resume->isChecked());
    Settings::setValue("video/autoplayNext", m_autoplay->isChecked());
    Settings::setValue("image/slideshowSeconds", m_slideSec->value());
    Settings::setValue("image/slideshowShuffle", m_shuffle->isChecked());
    Settings::setValue("image/slideshowFade", m_fade->isChecked());
    Settings::setValue("image/jpegQuality", m_quality->value());
}

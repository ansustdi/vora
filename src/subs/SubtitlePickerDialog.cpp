#include "SubtitlePickerDialog.h"

#include "Settings.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

static QString languageName(const QString &code)
{
    const QLocale::Language l = QLocale::codeToLanguage(code.section('-', 0, 0));
    return l == QLocale::AnyLanguage ? code : QLocale::languageToString(l);
}

SubtitlePickerDialog::SubtitlePickerDialog(const QVector<SubResult> &results, const QString &videoName, QWidget *parent)
    : QDialog(parent)
    , m_results(results)
{
    setWindowTitle(tr("Subtitles found"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(640, 380);

    auto *title = new QLabel(tr("%n subtitle(s) found online", nullptr, int(results.size())), this);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.25);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);
    auto *sub = new QLabel(videoName, this);
    QPalette sp = sub->palette();
    sp.setColor(QPalette::WindowText, QColor(150, 152, 160));
    sub->setPalette(sp);
    sub->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Language"), tr("Subtitle"), tr("Match")});
    m_list->setRootIsDecorated(false);
    m_list->setAlternatingRowColors(true);
    m_list->setUniformRowHeights(true);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    for (const SubResult &r : results) {
        QStringList match;
        if (r.hashMatch)
            match << tr("exact file");
        if (r.rating > 0)
            match << QString("★ %1").arg(r.rating);
        else if (r.downloads > 0)
            match << tr("%1 downloads").arg(r.downloads);
        auto *item = new QTreeWidgetItem(m_list, {languageName(r.language), r.release.isEmpty() ? r.fileName : r.release,
                                                  match.join("  ·  ")});
        item->setToolTip(1, r.fileName);
    }
    if (m_list->topLevelItemCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0)); // already ranked best-first

    m_always = new QCheckBox(tr("Always load the best match without asking"), this);

    auto *load = new QPushButton(tr("Load subtitle"), this);
    load->setObjectName("pill");
    load->setDefault(true);
    auto *skip = new QPushButton(tr("No thanks"), this);
    skip->setObjectName("pillGhost");

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_always);
    buttons->addStretch();
    buttons->addWidget(skip);
    buttons->addWidget(load);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 16, 18, 16);
    lay->setSpacing(8);
    lay->addWidget(title);
    lay->addWidget(sub);
    lay->addSpacing(4);
    lay->addWidget(m_list, 1);
    lay->addSpacing(4);
    lay->addLayout(buttons);

    connect(load, &QPushButton::clicked, this, &SubtitlePickerDialog::accept);
    connect(skip, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &SubtitlePickerDialog::accept);
}

void SubtitlePickerDialog::accept()
{
    const int row = m_list->indexOfTopLevelItem(m_list->currentItem());
    if (row >= 0 && row < m_results.size()) {
        if (m_always->isChecked())
            Settings::setValue("subs/mode", "auto");
        emit chosen(m_results.at(row));
    }
    QDialog::accept();
}

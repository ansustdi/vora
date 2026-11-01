#include "SubtitleSearchDialog.h"

#include "Settings.h"
#include "SubtitleManager.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <tuple>

SubtitleSearchDialog::SubtitleSearchDialog(SubtitleManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle(tr("Search subtitles"));
    resize(820, 460);
    setAttribute(Qt::WA_DeleteOnClose);

    const SubQuery q = manager->buildQuery();
    m_hash = q.hash;

    m_query = new QLineEdit(q.query, this);
    m_langs = new QLineEdit(q.languages.join(','), this);
    m_langs->setToolTip(tr("Comma separated codes, e.g. en,mn,ru,pt-br"));
    m_season = new QLineEdit(q.season >= 0 ? QString::number(q.season) : QString(), this);
    m_episode = new QLineEdit(q.episode >= 0 ? QString::number(q.episode) : QString(), this);
    m_season->setValidator(new QIntValidator(0, 999, this));
    m_episode->setValidator(new QIntValidator(0, 9999, this));
    m_season->setMaximumWidth(60);
    m_episode->setMaximumWidth(60);

    m_useHash = new QPushButton(tr("Match by file hash"), this);
    m_useHash->setCheckable(true);
    m_useHash->setChecked(!m_hash.isEmpty());
    m_useHash->setEnabled(!m_hash.isEmpty());

    auto *searchBtn = new QPushButton(tr("Search"), this);
    searchBtn->setDefault(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Title"), m_query);
    auto *row = new QHBoxLayout;
    row->addWidget(m_langs, 1);
    row->addWidget(new QLabel(tr("Season"), this));
    row->addWidget(m_season);
    row->addWidget(new QLabel(tr("Episode"), this));
    row->addWidget(m_episode);
    row->addWidget(m_useHash);
    row->addWidget(searchBtn);
    form->addRow(tr("Languages"), row);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({tr("Source"), tr("Lang"), tr("Release"), tr("Popularity"), tr("Hash match")});
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_status = new QLabel(this);
    m_download = new QPushButton(tr("Download && load"), this);
    m_download->setEnabled(false);
    auto *close = new QPushButton(tr("Close"), this);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_download);
    bottom->addWidget(close);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_table, 1);
    lay->addLayout(bottom);

    connect(searchBtn, &QPushButton::clicked, this, &SubtitleSearchDialog::runSearch);
    connect(m_query, &QLineEdit::returnPressed, this, &SubtitleSearchDialog::runSearch);
    connect(m_download, &QPushButton::clicked, this, &SubtitleSearchDialog::downloadSelected);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &SubtitleSearchDialog::downloadSelected);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this] { m_download->setEnabled(!m_table->selectedItems().isEmpty()); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_manager, &SubtitleManager::status, this, [this](const QString &s) { m_status->setText(s); });
    // The hash and save location belong to the video it was opened for.
    connect(m_manager, &SubtitleManager::videoChanged, this, &QDialog::close);

    m_status->setText(manager->hasOpenSubtitles() ? tr("Searches by file fingerprint and by title.")
                                                  : tr("Searches by file fingerprint."));
}

void SubtitleSearchDialog::runSearch()
{
    SubQuery q;
    q.query = m_query->text().trimmed();
    if (m_useHash->isChecked())
        q.hash = m_hash;
    for (const QString &l : m_langs->text().split(',', Qt::SkipEmptyParts))
        q.languages << l.trimmed().toLower();
    q.season = m_season->text().isEmpty() ? -1 : m_season->text().toInt();
    q.episode = m_episode->text().isEmpty() ? -1 : m_episode->text().toInt();

    m_manager->reloadSettings();
    m_status->setText(tr("Searching…"));
    m_table->setRowCount(0);
    QPointer<SubtitleSearchDialog> self(this);
    m_manager->searchAll(q, [self](QVector<SubResult> results, const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty()) {
            self->m_status->setText(error);
            return;
        }
        std::stable_sort(results.begin(), results.end(), [](const SubResult &a, const SubResult &b) {
            return std::make_tuple(a.hashMatch, a.size >= 2000, a.downloads + int(a.rating), a.size)
                 > std::make_tuple(b.hashMatch, b.size >= 2000, b.downloads + int(b.rating), b.size);
        });
        self->m_results = results;
        self->m_table->setRowCount(results.size());
        for (int i = 0; i < results.size(); ++i) {
            const SubResult &r = results.at(i);
            auto *release = new QTableWidgetItem(r.release.isEmpty() ? r.fileName : r.release);
            release->setToolTip(r.title + "\n" + r.fileName);
            self->m_table->setItem(i, 0, new QTableWidgetItem(r.provider == "bsplayer" ? "BSPlayer" : "OpenSubtitles"));
            self->m_table->setItem(i, 1, new QTableWidgetItem(r.language));
            self->m_table->setItem(i, 2, release);
            auto *pop = new QTableWidgetItem;
            if (r.provider == "bsplayer")
                pop->setText(tr("★ %1").arg(r.rating));
            else
                pop->setData(Qt::DisplayRole, r.downloads);
            self->m_table->setItem(i, 3, pop);
            self->m_table->setItem(i, 4, new QTableWidgetItem(r.hashMatch ? "✔" : ""));
        }
        self->m_status->setText(tr("%n result(s)", nullptr, int(results.size())));
    });
}

void SubtitleSearchDialog::downloadSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_results.size())
        return;
    m_manager->downloadAndLoad(m_results.at(row));
}

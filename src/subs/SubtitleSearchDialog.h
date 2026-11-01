#pragma once

#include "OpenSubtitlesClient.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class SubtitleManager;

// Manual search: edit the query, browse results, pick one to download.
class SubtitleSearchDialog : public QDialog
{
    Q_OBJECT
public:
    SubtitleSearchDialog(SubtitleManager *manager, QWidget *parent = nullptr);

private:
    void runSearch();
    void downloadSelected();

    SubtitleManager *m_manager;
    QLineEdit *m_query;
    QLineEdit *m_langs;
    QLineEdit *m_season;
    QLineEdit *m_episode;
    QPushButton *m_useHash;
    QTableWidget *m_table;
    QLabel *m_status;
    QPushButton *m_download;
    QVector<SubResult> m_results;
    QString m_hash;
};

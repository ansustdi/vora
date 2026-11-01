#pragma once

#include "OpenSubtitlesClient.h"

#include <QDialog>

class QCheckBox;
class QTreeWidget;

// BSPlayer-style "subtitles found" chooser shown when a video starts without subtitles.
class SubtitlePickerDialog : public QDialog
{
    Q_OBJECT
public:
    SubtitlePickerDialog(const QVector<SubResult> &results, const QString &videoName, QWidget *parent = nullptr);

signals:
    void chosen(const SubResult &result);

private:
    void accept() override;

    QVector<SubResult> m_results;
    QTreeWidget *m_list;
    QCheckBox *m_always;
};

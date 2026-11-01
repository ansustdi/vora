#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;

// "Open stream": URL (YouTube / Twitch / Kick / direct links / m3u8 / rtsp…) + quality.
class OpenStreamDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OpenStreamDialog(QWidget *parent = nullptr);

    QString url() const;

    // mpv --ytdl-format value for the saved quality choice.
    static QString formatForQuality(const QString &quality);

private:
    void accept() override;

    QLineEdit *m_url;
    QComboBox *m_quality;
};

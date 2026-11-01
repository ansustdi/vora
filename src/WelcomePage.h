#pragma once

#include <QWidget>

class QVBoxLayout;
class QLabel;

// Start screen: logo, open buttons and recently opened files.
class WelcomePage : public QWidget
{
    Q_OBJECT
public:
    explicit WelcomePage(QWidget *parent = nullptr);
    void setRecent(const QStringList &paths);

signals:
    void openFileClicked();
    void openFolderClicked();
    void youtubeClicked();
    void recentClicked(const QString &path);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVBoxLayout *m_recentList;
    QLabel *m_recentHeader;
};

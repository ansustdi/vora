#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void save();

    QLineEdit *m_langs;
    QComboBox *m_subsMode;
    QCheckBox *m_subsNext;

    QCheckBox *m_hwdec;
    QCheckBox *m_resume;
    QCheckBox *m_autoplay;

    QSpinBox *m_slideSec;
    QCheckBox *m_shuffle;
    QCheckBox *m_fade;
    QSpinBox *m_quality;
};

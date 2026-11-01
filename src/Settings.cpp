#include "Settings.h"

#include <QSettings>

namespace Settings {

QVariant value(const QString &key, const QVariant &def)
{
    return QSettings().value(key, def);
}

void setValue(const QString &key, const QVariant &value)
{
    QSettings().setValue(key, value);
}

QStringList subLanguages()
{
    QStringList langs;
    const QStringList raw = value("subs/languages", "en").toString().split(',', Qt::SkipEmptyParts);
    for (const QString &l : raw) {
        const QString t = l.trimmed().toLower();
        if (!t.isEmpty() && !langs.contains(t))
            langs << t;
    }
    if (langs.isEmpty())
        langs << "en";
    return langs;
}

} // namespace Settings

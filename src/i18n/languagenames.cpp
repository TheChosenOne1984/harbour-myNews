#include "languagenames.h"

#include <QLocale>

namespace i18n {

QString languageDisplayName(const QString &code)
{
    if (code.isEmpty())
        return QString();

    const QLocale locale(code);
    QString name = locale.nativeLanguageName();
    if (name.isEmpty())
        return code;

    const bool callerGaveRegion =
        code.contains(QLatin1Char('-')) || code.contains(QLatin1Char('_'));
    if (!callerGaveRegion && name.contains(QLatin1Char(' '))) {
        // Qt filled in a region we were not asked about. Fall back to the plain language
        // name, which for the languages this matters for is also the endonym.
        const QString plain = QLocale::languageToString(locale.language());
        if (!plain.isEmpty())
            name = plain;
    }

    name[0] = name.at(0).toUpper();
    return name;
}

} // namespace i18n

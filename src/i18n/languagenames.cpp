#include "languagenames.h"

#include <QLocale>

namespace i18n {

QString normalizeLanguageTag(const QString &code)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty())
        return QString();

    int end = trimmed.size();
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch.isSpace()) {
            end = i;
            break;
        }
    }

    const QString primary = trimmed.left(end).toLower();
    // Anything that is not a plain language subtag is not something we can partition on.
    for (const QChar ch : primary) {
        if (ch < QLatin1Char('a') || ch > QLatin1Char('z'))
            return QString();
    }
    return (primary.size() >= 2 && primary.size() <= 3) ? primary : QString();
}

void setDefaultLocale(const QString &code)
{
    QLocale::setDefault(code.isEmpty() ? QLocale::system() : QLocale(code));
}

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
    // Language tags are normalised before they are stored, so in practice this is always
    // a bare subtag and the branch below is what runs.
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

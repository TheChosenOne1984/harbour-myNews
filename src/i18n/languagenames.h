#ifndef I18N_LANGUAGENAMES_H
#define I18N_LANGUAGENAMES_H

#include <QString>

namespace i18n {

/*!
 * \brief The endonym for a language code — "Deutsch" for de, "English" for en.
 *
 * QLocale::nativeLanguageName() is the right source, but a bare code resolves to a
 * default region: QLocale("en") is en_US and answers "American English", which is wrong
 * for a picker offering the language rather than a regional variant. When the caller gave
 * no region, the region-qualified name is replaced by the plain language name.
 *
 * Shared by the UI language picker and the feed language picker so the two never disagree.
 */
QString languageDisplayName(const QString &code);

} // namespace i18n

#endif // I18N_LANGUAGENAMES_H

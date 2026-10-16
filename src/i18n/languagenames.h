#ifndef I18N_LANGUAGENAMES_H
#define I18N_LANGUAGENAMES_H

#include <QString>

namespace i18n {

/*!
 * \brief Reduces a language tag to its primary subtag: "en-GB" and "en_US" both become "en".
 *
 * The language is a partition predicate for clustering (§6): two articles only ever
 * compare if their languages match exactly. Keeping regional subtags would therefore put
 * a British and an American report of the same story in different partitions, where they
 * can never find each other — a silent loss with nothing gained, since regional variants
 * of a language are the same language as far as character n-grams are concerned.
 *
 * Script variants (zh-Hans/zh-Hant, sr-Latn/sr-Cyrl) are pooled too. That costs a few
 * wasted comparisons and nothing else: the texts cannot resemble each other, so no false
 * grouping can result.
 */
QString normalizeLanguageTag(const QString &code);

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

/*!
 * \brief The language the source strings are written in.
 *
 * It has no .qm file -- there is nothing to translate English into English -- so a picker
 * that lists installed translations alone leaves it unreachable, and a reader on a German
 * device cannot choose English at all. It has to be offered explicitly.
 */
inline QString sourceLanguage() { return QStringLiteral("en"); }

/*!
 * \brief Makes QLocale's default follow the chosen UI language.
 *
 * Everything formatted through a default-constructed QLocale -- the dates the article
 * list shows for anything older than a week -- otherwise stays in the device locale while
 * the rest of the interface is in another language. Empty means "System".
 */
void setDefaultLocale(const QString &code);

} // namespace i18n

#endif // I18N_LANGUAGENAMES_H

#ifndef I18N_RELATIVETIME_H
#define I18N_RELATIVETIME_H

#include <QString>
#include <QtGlobal>

namespace i18n {

/*!
 * \brief A short, translated "how long ago", for the article list.
 *
 * Implemented here rather than with Silica's Formatter.DurationElapsed, which renders in
 * the *device* locale. With a freely choosable UI language that would put German time
 * strings in an English interface — see task0-findings.md section 6.
 *
 * Deliberately terse: it shares a line with the sources in a fixed-height delegate.
 *
 * \param published unix seconds
 * \param now       unix seconds, injectable so the result is testable
 */
QString relativeTime(qint64 published, qint64 now);

} // namespace i18n

#endif // I18N_RELATIVETIME_H

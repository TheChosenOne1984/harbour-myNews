#include "relativetime.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>

namespace i18n {
namespace {

constexpr qint64 Minute = 60;
constexpr qint64 Hour = 60 * Minute;
constexpr qint64 Day = 24 * Hour;

} // namespace

QString relativeTime(qint64 published, qint64 now)
{
    if (published <= 0)
        return QString();

    const qint64 age = now - published;

    // A publisher's clock running ahead, or a feed dating articles into the future, must
    // not produce "in -3 minutes".
    //
    // The context is written out in every call rather than kept in a constant: lupdate
    // only extracts a translate() whose context is a literal, and with a constant none of
    // these four strings ever reached a .ts file -- a German interface said "now" and "3 d".
    if (age < Minute)
        return QCoreApplication::translate("RelativeTime", "now");

    if (age < Hour) {
        return QCoreApplication::translate("RelativeTime", "%n min", "minutes ago, abbreviated",
                                           int(age / Minute));
    }
    if (age < Day) {
        return QCoreApplication::translate("RelativeTime", "%n h", "hours ago, abbreviated",
                                           int(age / Hour));
    }
    if (age < 7 * Day) {
        return QCoreApplication::translate("RelativeTime", "%n d", "days ago, abbreviated",
                                           int(age / Day));
    }

    // Older than a week, a date says more than a count of days. Follows the UI language
    // through QLocale's default, which LanguageManager sets via i18n::setDefaultLocale().
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(published * 1000).date(),
                              QLocale::ShortFormat);
}

} // namespace i18n

#include "normalize.h"

#include <QChar>
#include <QVector>

namespace cluster {

QString normalize(const QString &text)
{
    // NFKC first: folds ligatures, full-width forms and compatibility characters onto
    // their plain equivalents, so "ﬁ" and "fi" produce the same n-grams.
    const QString decomposed = text.normalized(QString::NormalizationForm_KC).toCaseFolded();

    QString out;
    out.reserve(decomposed.size());

    bool pendingSpace = false;
    for (const QChar ch : decomposed) {
        if (ch.isLetterOrNumber()) {
            if (pendingSpace && !out.isEmpty())
                out.append(QLatin1Char(' '));
            pendingSpace = false;
            out.append(ch);
        } else {
            // Punctuation, symbols and whitespace all collapse to a single separator.
            // Quotation marks differ between publishers for the same wire copy, so
            // dropping them outright is what makes those copies match.
            pendingSpace = true;
        }
    }

    return out;
}

QString stripKicker(const QString &title)
{
    // Longest kicker we accept, in characters before the separator. Long enough for
    // "Nach der Landtagswahl in Sachsen-Anhalt", short enough not to eat a headline.
    constexpr int MaxKickerLength = 45;
    // What has to survive, so we never reduce a title to a fragment.
    constexpr int MinRemainder = 20;

    // Publishers do not agree on the separator: ":" is the most common, but dashes and
    // an exclamation mark are used for exactly the same purpose. Handling only the
    // colon is worse than handling none, because it strips one publisher's headline and
    // not another's -- and that asymmetry breaks matches that used to work.
    static const QVector<QString> separators{
        QStringLiteral(":"),
        QStringLiteral(" \u2013 "),   // en dash
        QStringLiteral(" \u2014 "),   // em dash
        QStringLiteral(" - "),
        QStringLiteral("!"),
        QStringLiteral(" | "),
    };

    int bestEnd = -1;
    for (const QString &separator : separators) {
        const int at = title.indexOf(separator);
        if (at <= 0 || at > MaxKickerLength)
            continue;
        const int end = at + separator.size();
        // Earliest separator wins: the kicker is a prefix, so anything later belongs to
        // the headline itself.
        if (bestEnd < 0 || end < bestEnd)
            bestEnd = end;
    }
    if (bestEnd < 0)
        return title;

    int start = bestEnd;
    while (start < title.size() && title.at(start).isSpace())
        ++start;

    const QString remainder = title.mid(start);
    if (remainder.size() < MinRemainder)
        return title;

    return remainder;
}

QString normalizeArticle(const QString &title, const QString &teaser)
{
    if (teaser.isEmpty())
        return normalize(title);
    return normalize(title + QLatin1Char(' ') + teaser);
}

QString clusterSubject(const QString &title)
{
    return normalize(stripKicker(title));
}

} // namespace cluster

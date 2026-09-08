#include "ogimage.h"

#include "feed/feedfetcher.h"

#include <QRegularExpression>

namespace image {
namespace {

/*!
 * Matches one <meta> tag and captures its property/name and content, in either order --
 * publishers write both, and some put other attributes between them.
 */
const QRegularExpression &metaTagPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("<meta\\s+[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

QString attributeOf(const QString &tag, const QString &name)
{
    const QRegularExpression pattern(
        QStringLiteral("\\b%1\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(name),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(tag);
    if (!match.hasMatch())
        return QString();
    for (int i = 2; i <= 4; ++i) {
        const QString captured = match.captured(i);
        if (!captured.isNull())
            return captured.trimmed();
    }
    return QString();
}

/*! Resolves entities that turn up in a URL attribute. Nothing more is needed here. */
QString unescape(QString value)
{
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    value.replace(QStringLiteral("&#38;"), QStringLiteral("&"));
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    return value;
}

} // namespace

QString extractPreviewImage(const QByteArray &html, const QUrl &baseUrl)
{
    // Only the head is worth reading, and only a bounded amount of it.
    const QString text = QString::fromUtf8(html.left(MaxHtmlBytes));

    QString openGraph;
    QString twitter;

    QRegularExpressionMatchIterator it = metaTagPattern().globalMatch(text);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        QString key = attributeOf(tag, QStringLiteral("property"));
        if (key.isEmpty())
            key = attributeOf(tag, QStringLiteral("name"));
        if (key.isEmpty())
            continue;

        key = key.toLower();
        const QString content = attributeOf(tag, QStringLiteral("content"));
        if (content.isEmpty())
            continue;

        // og:image:secure_url and og:image:url are the same picture; the plain property
        // wins when both are present.
        if (key == QLatin1String("og:image")) {
            openGraph = content;
        } else if (openGraph.isEmpty()
                   && (key == QLatin1String("og:image:secure_url")
                       || key == QLatin1String("og:image:url"))) {
            openGraph = content;
        } else if (twitter.isEmpty() && key == QLatin1String("twitter:image")) {
            twitter = content;
        }
    }

    QString found = !openGraph.isEmpty() ? openGraph : twitter;

    if (found.isEmpty()) {
        static const QRegularExpression linkPattern(
            QStringLiteral("<link\\s+[^>]*rel\\s*=\\s*[\"']?image_src[\"']?[^>]*>"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = linkPattern.match(text);
        if (match.hasMatch())
            found = attributeOf(match.captured(0), QStringLiteral("href"));
    }

    if (found.isEmpty())
        return QString();

    found = unescape(found).trimmed();

    // Publishers write protocol-relative and root-relative paths freely; both need the
    // article's own address to become fetchable.
    const QUrl resolved = baseUrl.isValid() ? baseUrl.resolved(QUrl(found)) : QUrl(found);
    // Only http(s), and only with a host: a feed must not talk us into fetching a file://
    // URL. The same rule now guards every other URL the app dereferences.
    if (!feed::isFetchableUrl(resolved))
        return QString();
    return resolved.toString();
}

} // namespace image

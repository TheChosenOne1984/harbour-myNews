#include "feedparser.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QXmlStreamReader>

namespace feed {
namespace {

const QString NsAtom = QStringLiteral("http://www.w3.org/2005/Atom");
const QString NsRdf = QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#");
const QString NsRss1 = QStringLiteral("http://purl.org/rss/1.0/");
const QString NsDc = QStringLiteral("http://purl.org/dc/elements/1.1/");
const QString NsContent = QStringLiteral("http://purl.org/rss/1.0/modules/content/");
const QString NsMedia = QStringLiteral("http://search.yahoo.com/mrss/");

/*!
 * A single element with its attributes, text and children.
 *
 * Only ever built for one <item>/<entry> at a time, so peak memory stays proportional to
 * the largest entry rather than to the document.
 */
struct Node
{
    QString name;
    QString ns;
    QVector<QPair<QString, QString>> attributes;
    QString text;
    QVector<Node> children;

    QString attribute(const QString &key) const
    {
        for (const auto &pair : attributes) {
            if (pair.first == key)
                return pair.second;
        }
        return QString();
    }

    const Node *child(const QString &childName, const QString &childNs = QString()) const
    {
        for (const Node &node : children) {
            if (node.name != childName)
                continue;
            if (!childNs.isNull() && node.ns != childNs)
                continue;
            return &node;
        }
        return nullptr;
    }

    QString childText(const QString &childName, const QString &childNs = QString()) const
    {
        const Node *node = child(childName, childNs);
        return node ? node->text : QString();
    }
};

Node readNode(QXmlStreamReader &reader)
{
    Node node;
    node.name = reader.name().toString();
    node.ns = reader.namespaceUri().toString();
    for (const QXmlStreamAttribute &attribute : reader.attributes()) {
        node.attributes.append(qMakePair(attribute.name().toString(),
                                         attribute.value().toString()));
    }

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement)
            node.children.append(readNode(reader));
        else if (token == QXmlStreamReader::Characters)
            node.text += reader.text();
        else if (token == QXmlStreamReader::EndElement)
            break;
        else if (token == QXmlStreamReader::Invalid)
            break;
    }
    node.text = node.text.trimmed();
    return node;
}

/*! First non-empty text among the named children, tried in order. */
QString firstOf(const Node &node, const QVector<QPair<QString, QString>> &candidates)
{
    for (const auto &candidate : candidates) {
        const QString text = node.childText(candidate.first, candidate.second);
        if (!text.isEmpty())
            return text;
    }
    return QString();
}

QString atomLink(const Node &entry)
{
    QString fallback;
    for (const Node &node : entry.children) {
        if (node.name != QLatin1String("link"))
            continue;
        const QString rel = node.attribute(QStringLiteral("rel"));
        const QString href = node.attribute(QStringLiteral("href"));
        if (href.isEmpty())
            continue;
        if (rel.isEmpty() || rel == QLatin1String("alternate"))
            return href;
        if (fallback.isEmpty())
            fallback = href;
    }
    return fallback;
}

/*! Image, in the priority order section 5 gives. */
QString extractImage(const Node &entry, const QString &bodyHtml)
{
    for (const Node &node : entry.children) {
        if (node.ns != NsMedia)
            continue;
        if (node.name != QLatin1String("content") && node.name != QLatin1String("thumbnail"))
            continue;
        const QString url = node.attribute(QStringLiteral("url"));
        if (!url.isEmpty())
            return url;
    }
    for (const Node &node : entry.children) {
        if (node.name != QLatin1String("enclosure"))
            continue;
        if (!node.attribute(QStringLiteral("type")).startsWith(QLatin1String("image/")))
            continue;
        const QString url = node.attribute(QStringLiteral("url"));
        if (!url.isEmpty())
            return url;
    }
    return firstImageInHtml(bodyHtml);
}

QString truncateTeaser(const QString &text)
{
    if (text.size() <= MaxTeaserLength)
        return text;
    // Cut on a word boundary when one is close, so the teaser does not end mid-word.
    int cut = text.lastIndexOf(QLatin1Char(' '), MaxTeaserLength);
    if (cut < MaxTeaserLength - 40)
        cut = MaxTeaserLength;
    return text.left(cut).trimmed();
}

ParsedArticle articleFromRss2(const Node &item)
{
    ParsedArticle article;
    article.title = stripHtml(item.childText(QStringLiteral("title")));
    article.link = item.childText(QStringLiteral("link"));
    article.guid = item.childText(QStringLiteral("guid"));
    const QString body = firstOf(item, {
        {QStringLiteral("encoded"), NsContent},
        {QStringLiteral("description"), QString()},
    });
    article.teaser = truncateTeaser(stripHtml(body));
    article.published = parseTimestamp(firstOf(item, {
        {QStringLiteral("pubDate"), QString()},
        {QStringLiteral("date"), NsDc},
    }));
    article.imageUrl = extractImage(item, body);
    return article;
}

ParsedArticle articleFromAtom(const Node &entry)
{
    ParsedArticle article;
    article.title = stripHtml(entry.childText(QStringLiteral("title")));
    article.link = atomLink(entry);
    article.guid = entry.childText(QStringLiteral("id"));
    const QString body = firstOf(entry, {
        {QStringLiteral("content"), QString()},
        {QStringLiteral("summary"), QString()},
    });
    article.teaser = truncateTeaser(stripHtml(body));
    article.published = parseTimestamp(firstOf(entry, {
        {QStringLiteral("published"), QString()},
        {QStringLiteral("updated"), QString()},
    }));
    article.imageUrl = extractImage(entry, body);
    return article;
}

ParsedArticle articleFromRdf(const Node &item)
{
    ParsedArticle article;
    article.title = stripHtml(item.childText(QStringLiteral("title")));
    article.link = item.childText(QStringLiteral("link"));
    article.guid = item.attribute(QStringLiteral("about"));
    const QString body = firstOf(item, {
        {QStringLiteral("encoded"), NsContent},
        {QStringLiteral("description"), QString()},
    });
    article.teaser = truncateTeaser(stripHtml(body));
    article.published = parseTimestamp(firstOf(item, {
        {QStringLiteral("date"), NsDc},
        {QStringLiteral("pubDate"), QString()},
    }));
    article.imageUrl = extractImage(item, body);
    return article;
}

} // namespace

// --- Building blocks -----------------------------------------------------------------

namespace {

/*! The handful of named entities that actually turn up in feeds. */
QString resolveEntity(const QString &name)
{
    static const QHash<QString, QString> known{
        {QStringLiteral("amp"), QStringLiteral("&")},
        {QStringLiteral("lt"), QStringLiteral("<")},
        {QStringLiteral("gt"), QStringLiteral(">")},
        {QStringLiteral("quot"), QStringLiteral("\"")},
        {QStringLiteral("apos"), QStringLiteral("'")},
        {QStringLiteral("nbsp"), QStringLiteral(" ")},
        {QStringLiteral("shy"), QString()},
        {QStringLiteral("ndash"), QStringLiteral("\u2013")},
        {QStringLiteral("mdash"), QStringLiteral("\u2014")},
        {QStringLiteral("hellip"), QStringLiteral("\u2026")},
        {QStringLiteral("laquo"), QStringLiteral("\u00ab")},
        {QStringLiteral("raquo"), QStringLiteral("\u00bb")},
        {QStringLiteral("bdquo"), QStringLiteral("\u201e")},
        {QStringLiteral("ldquo"), QStringLiteral("\u201c")},
        {QStringLiteral("rdquo"), QStringLiteral("\u201d")},
        {QStringLiteral("sbquo"), QStringLiteral("\u201a")},
        {QStringLiteral("lsquo"), QStringLiteral("\u2018")},
        {QStringLiteral("rsquo"), QStringLiteral("\u2019")},
        {QStringLiteral("euro"), QStringLiteral("\u20ac")},
        {QStringLiteral("auml"), QStringLiteral("\u00e4")},
        {QStringLiteral("ouml"), QStringLiteral("\u00f6")},
        {QStringLiteral("uuml"), QStringLiteral("\u00fc")},
        {QStringLiteral("Auml"), QStringLiteral("\u00c4")},
        {QStringLiteral("Ouml"), QStringLiteral("\u00d6")},
        {QStringLiteral("Uuml"), QStringLiteral("\u00dc")},
        {QStringLiteral("szlig"), QStringLiteral("\u00df")},
    };
    return known.value(name, QString());
}

} // namespace

QString stripHtml(const QString &html)
{
    QString out;
    out.reserve(html.size());

    bool inTag = false;
    for (int i = 0; i < html.size(); ++i) {
        const QChar ch = html.at(i);

        if (inTag) {
            if (ch == QLatin1Char('>'))
                inTag = false;
            continue;
        }
        if (ch == QLatin1Char('<')) {
            inTag = true;
            // A tag boundary is a word boundary: "<p>a</p><p>b</p>" must not become "ab".
            out.append(QLatin1Char(' '));
            continue;
        }
        if (ch == QLatin1Char('&')) {
            const int semicolon = html.indexOf(QLatin1Char(';'), i + 1);
            // Anything longer than this is not an entity, it is a stray ampersand.
            if (semicolon > i && semicolon - i <= 10) {
                const QString body = html.mid(i + 1, semicolon - i - 1);
                QString replacement;
                bool resolved = false;
                if (body.startsWith(QLatin1Char('#'))) {
                    bool ok = false;
                    const bool hex = body.size() > 1
                        && (body.at(1) == QLatin1Char('x') || body.at(1) == QLatin1Char('X'));
                    const uint code = hex ? body.mid(2).toUInt(&ok, 16)
                                          : body.mid(1).toUInt(&ok, 10);
                    if (ok && code > 0 && code <= 0x10ffff) {
                        replacement = QString::fromUcs4(&code, 1);
                        resolved = true;
                    }
                } else {
                    replacement = resolveEntity(body);
                    resolved = !replacement.isNull();
                }
                if (resolved) {
                    out.append(replacement);
                    i = semicolon;
                    continue;
                }
            }
        }
        out.append(ch);
    }

    return out.simplified();
}

qint64 parseTimestamp(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return 0;

    // RSS uses RFC 822, Atom uses ISO 8601, and plenty of feeds mix them regardless of
    // the format they claim -- so both are always tried.
    QDateTime dt = QDateTime::fromString(trimmed, Qt::RFC2822Date);
    if (!dt.isValid())
        dt = QDateTime::fromString(trimmed, Qt::ISODate);
    if (!dt.isValid()) {
        // Seen in the wild: ISO 8601 with a fractional second, which Qt 5.6 rejects.
        static const QRegularExpression fractional(QStringLiteral("\\.\\d+"));
        QString cleaned = trimmed;
        cleaned.remove(fractional);
        dt = QDateTime::fromString(cleaned, Qt::ISODate);
    }
    if (!dt.isValid())
        return 0;

    if (dt.timeSpec() == Qt::LocalTime)
        dt.setTimeSpec(Qt::UTC);   // no zone given: assume UTC rather than the device's
    return dt.toUTC().toMSecsSinceEpoch() / 1000;
}

QString firstImageInHtml(const QString &html)
{
    if (html.isEmpty())
        return QString();
    static const QRegularExpression imgTag(
        QStringLiteral("<img[^>]+src\\s*=\\s*[\"']([^\"']+)[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = imgTag.match(html);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString ParsedFeed::formatName(Format format)
{
    switch (format) {
    case Rss2: return QStringLiteral("RSS 2.0");
    case Atom: return QStringLiteral("Atom 1.0");
    case Rdf:  return QStringLiteral("RSS 1.0 / RDF");
    case Unknown: break;
    }
    return QStringLiteral("unknown");
}

ParsedFeed parseFeed(const QByteArray &data)
{
    ParsedFeed feed;
    QXmlStreamReader reader(data);

    // Find the root element and decide the format from it -- not from the URL and not
    // from Content-Type, both of which publishers get wrong.
    while (!reader.atEnd() && reader.readNext() != QXmlStreamReader::StartElement) {
        if (reader.hasError())
            break;
    }
    if (reader.hasError() || reader.atEnd()) {
        feed.error = reader.hasError() ? reader.errorString()
                                       : QStringLiteral("document has no root element");
        return feed;
    }

    const QString rootName = reader.name().toString();
    const QString rootNs = reader.namespaceUri().toString();
    if (rootName == QLatin1String("rss")) {
        feed.format = ParsedFeed::Rss2;
    } else if (rootName == QLatin1String("feed") && rootNs == NsAtom) {
        feed.format = ParsedFeed::Atom;
        feed.lang = reader.attributes().value(QStringLiteral("xml:lang")).toString();
        if (feed.lang.isEmpty()) {
            feed.lang = reader.attributes()
                            .value(QStringLiteral("http://www.w3.org/XML/1998/namespace"),
                                   QStringLiteral("lang"))
                            .toString();
        }
    } else if (rootName == QLatin1String("RDF") && rootNs == NsRdf) {
        feed.format = ParsedFeed::Rdf;
    } else {
        feed.error = QStringLiteral("unrecognised root element <%1>").arg(rootName);
        return feed;
    }

    // Stream the body, materialising only one item at a time.
    int depth = 1;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::Invalid) {
            feed.error = reader.errorString();
            break;
        }
        if (token == QXmlStreamReader::EndElement) {
            --depth;
            continue;
        }
        if (token != QXmlStreamReader::StartElement)
            continue;

        const QString name = reader.name().toString();
        const QString ns = reader.namespaceUri().toString();

        const bool isItem =
            (feed.format == ParsedFeed::Rss2 && name == QLatin1String("item"))
            || (feed.format == ParsedFeed::Atom && name == QLatin1String("entry") && ns == NsAtom)
            || (feed.format == ParsedFeed::Rdf && name == QLatin1String("item") && ns == NsRss1);

        if (isItem) {
            const Node node = readNode(reader);
            ParsedArticle article;
            switch (feed.format) {
            case ParsedFeed::Rss2: article = articleFromRss2(node); break;
            case ParsedFeed::Atom: article = articleFromAtom(node); break;
            case ParsedFeed::Rdf:  article = articleFromRdf(node); break;
            case ParsedFeed::Unknown: break;
            }
            // Section 5: the guid falls back to the link when the feed omits it.
            if (article.guid.isEmpty())
                article.guid = article.link;
            if (!article.title.isEmpty() && !article.guid.isEmpty())
                feed.articles.append(article);
            continue;
        }

        // Channel-level metadata. In RSS 1.0 <channel> is a sibling of <item>, in RSS 2.0
        // it is their parent -- reading these as they stream past handles both.
        ++depth;
        if (name == QLatin1String("title") && feed.title.isEmpty() && depth <= 3) {
            feed.title = stripHtml(reader.readElementText(QXmlStreamReader::IncludeChildElements));
            --depth;
        } else if (name == QLatin1String("language") && feed.lang.isEmpty()) {
            feed.lang = reader.readElementText().trimmed();
            --depth;
        } else if (name == QLatin1String("link") && feed.siteUrl.isEmpty() && depth <= 3) {
            if (feed.format == ParsedFeed::Atom) {
                const QString rel = reader.attributes().value(QStringLiteral("rel")).toString();
                if (rel.isEmpty() || rel == QLatin1String("alternate"))
                    feed.siteUrl = reader.attributes().value(QStringLiteral("href")).toString();
            } else {
                feed.siteUrl = reader.readElementText().trimmed();
                --depth;
            }
        }
    }

    // atEnd() also becomes true on PrematureEndOfDocumentError, so leaving the loop is
    // not by itself proof that the document was complete. A response cut off mid
    // transfer has to be reported, or a truncated feed looks like a successful fetch
    // and the missing articles are never retried.
    if (feed.error.isEmpty() && reader.hasError())
        feed.error = reader.errorString();

    if (feed.articles.isEmpty() && feed.error.isEmpty())
        feed.error = QStringLiteral("no items found");
    return feed;
}

} // namespace feed

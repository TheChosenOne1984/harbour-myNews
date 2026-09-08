#ifndef FEED_FEEDPARSER_H
#define FEED_FEEDPARSER_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace feed {

/*! One entry as it came out of a feed document. Nothing is stored yet. */
struct ParsedArticle
{
    QString guid;        //!< <guid>, <id>, rdf:about -- falls back to link
    QString link;
    QString title;       //!< tags stripped, entities resolved
    QString teaser;      //!< plain text, truncated (see MaxTeaserLength)
    qint64 published = 0;//!< unix seconds; 0 means the feed gave nothing usable
    QString imageUrl;
};

/*!
 * \brief A parsed feed document.
 *
 * The three formats in section 5 of the plan are all real: of a 966-article sample from
 * German publishers, 166 articles arrived as RSS 1.0 / RDF. Detection is by root element
 * and namespace, never by URL or Content-Type, both of which lie routinely.
 */
struct ParsedFeed
{
    enum Format
    {
        Unknown,
        Rss2,   //!< <rss version="2.0">
        Atom,   //!< <feed xmlns="http://www.w3.org/2005/Atom">
        Rdf     //!< <rdf:RDF> with the RSS 1.0 namespace
    };

    Format format = Unknown;
    QString title;
    QString siteUrl;
    QString lang;        //!< as declared; often absent and sometimes wrong
    QVector<ParsedArticle> articles;
    QString error;       //!< empty when parsing succeeded

    bool isValid() const { return format != Unknown && error.isEmpty(); }
};

/*! Teasers are truncated on the way in (section 5). */
constexpr int MaxTeaserLength = 300;

/*!
 * \brief Parses a feed document.
 *
 * Never throws and never returns partial nonsense: on a malformed document the result
 * carries an \c error and whatever articles were read before the failure, so one broken
 * item does not discard a whole feed.
 */
ParsedFeed parseFeed(const QByteArray &data);

// --- Building blocks, exposed because they are worth testing on their own -------------

/*! Strips tags and resolves entities, collapsing whitespace. */
QString stripHtml(const QString &html);

/*! RFC 822/2822 and ISO 8601. Returns unix seconds, or 0 if neither parses. */
qint64 parseTimestamp(const QString &value);

/*! The src of the first <img> in a fragment of HTML, or an empty string. */
QString firstImageInHtml(const QString &html);

} // namespace feed

#endif // FEED_FEEDPARSER_H

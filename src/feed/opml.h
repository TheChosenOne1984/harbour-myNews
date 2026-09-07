#ifndef FEED_OPML_H
#define FEED_OPML_H

#include <QByteArray>
#include <QString>
#include <QVector>

#include "feed/feedstore.h"

namespace feed {

/*! One subscription read out of an OPML document. */
struct OpmlEntry
{
    QString url;
    QString title;
    QString siteUrl;
    QString lang;      //!< empty unless the file carried one; OPML has no standard field
    QString category;  //!< the enclosing outline's text, when the file groups feeds
};

struct OpmlDocument
{
    QString title;
    QVector<OpmlEntry> entries;
    QString error;

    bool isValid() const { return error.isEmpty() && !entries.isEmpty(); }
};

/*!
 * \brief Reads an OPML subscription list.
 *
 * OPML is the interchange format with other readers, not our catalogue — the catalogue is
 * JSON, because it has to carry a language, region and category per feed (§10).
 *
 * Outlines nest, so a file exported by another reader usually groups feeds under category
 * outlines. Those are flattened, keeping the enclosing outline's text as the category.
 *
 * OPML has no standard language attribute. Where one is present it is used; where it is
 * not, the caller supplies a default and the affected subscriptions are flagged for
 * review rather than the import stopping to ask (§7).
 */
OpmlDocument parseOpml(const QByteArray &xml);

/*!
 * \brief Writes subscriptions as OPML.
 *
 * Includes the language as an attribute so a round-trip through this app preserves it.
 * Other readers ignore attributes they do not know.
 */
QByteArray writeOpml(const QVector<FeedRecord> &feeds,
                     const QString &title = QStringLiteral("myNews subscriptions"));

} // namespace feed

#endif // FEED_OPML_H

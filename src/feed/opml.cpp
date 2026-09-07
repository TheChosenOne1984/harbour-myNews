#include "opml.h"

#include <QDateTime>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace feed {
namespace {

/*! Attribute lookup that tolerates the spellings different exporters use. */
QString attribute(const QXmlStreamAttributes &attributes, const QVector<QString> &names)
{
    for (const QString &name : names) {
        const QString value = attributes.value(name).toString().trimmed();
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

} // namespace

OpmlDocument parseOpml(const QByteArray &xml)
{
    OpmlDocument document;
    QXmlStreamReader reader(xml);

    // Outline text of each open ancestor, so a nested feed can inherit its group name.
    QVector<QString> groupStack;
    bool sawOpml = false;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();

        if (token == QXmlStreamReader::Invalid) {
            document.error = reader.errorString();
            break;
        }
        if (token == QXmlStreamReader::EndElement) {
            if (reader.name() == QLatin1String("outline") && !groupStack.isEmpty())
                groupStack.removeLast();
            continue;
        }
        if (token != QXmlStreamReader::StartElement)
            continue;

        const QStringRef name = reader.name();
        if (name == QLatin1String("opml")) {
            sawOpml = true;
            continue;
        }
        if (name == QLatin1String("title") && document.title.isEmpty()) {
            document.title = reader.readElementText().trimmed();
            continue;
        }
        if (name != QLatin1String("outline"))
            continue;

        const QXmlStreamAttributes attributes = reader.attributes();
        const QString text = attribute(attributes, {QStringLiteral("text"),
                                                    QStringLiteral("title")});
        const QString xmlUrl = attribute(attributes, {QStringLiteral("xmlUrl"),
                                                      QStringLiteral("xmlurl")});

        if (xmlUrl.isEmpty()) {
            // A grouping outline. Its text becomes the category of everything inside it.
            groupStack.append(text);
            continue;
        }

        OpmlEntry entry;
        entry.url = xmlUrl;
        entry.title = text.isEmpty() ? xmlUrl : text;
        entry.siteUrl = attribute(attributes, {QStringLiteral("htmlUrl"),
                                               QStringLiteral("htmlurl")});
        entry.lang = attribute(attributes, {QStringLiteral("language"),
                                            QStringLiteral("lang")});
        entry.category = groupStack.isEmpty() ? QString() : groupStack.last();
        document.entries.append(entry);

        // A feed outline can still be self-closing or contain children; push a level so
        // the matching EndElement does not pop somebody else's group.
        groupStack.append(text);
    }

    if (document.error.isEmpty() && !sawOpml)
        document.error = QStringLiteral("not an OPML document");
    else if (document.error.isEmpty() && document.entries.isEmpty())
        document.error = QStringLiteral("no subscriptions found");
    return document;
}

QByteArray writeOpml(const QVector<FeedRecord> &feeds, const QString &title)
{
    QByteArray out;
    QXmlStreamWriter writer(&out);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);

    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("opml"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("2.0"));

    writer.writeStartElement(QStringLiteral("head"));
    writer.writeTextElement(QStringLiteral("title"), title);
    writer.writeTextElement(QStringLiteral("dateCreated"),
                            QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date));
    writer.writeEndElement();

    writer.writeStartElement(QStringLiteral("body"));
    for (const FeedRecord &feed : feeds) {
        writer.writeEmptyElement(QStringLiteral("outline"));
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("rss"));
        writer.writeAttribute(QStringLiteral("text"),
                              feed.title.isEmpty() ? feed.url : feed.title);
        writer.writeAttribute(QStringLiteral("title"),
                              feed.title.isEmpty() ? feed.url : feed.title);
        writer.writeAttribute(QStringLiteral("xmlUrl"), feed.url);
        if (!feed.siteUrl.isEmpty())
            writer.writeAttribute(QStringLiteral("htmlUrl"), feed.siteUrl);
        // Our extension. Other readers ignore it; we read it back on import, which is
        // what makes a round-trip through this app keep the language (§7).
        if (!feed.lang.isEmpty())
            writer.writeAttribute(QStringLiteral("language"), feed.lang);
    }
    writer.writeEndElement();

    writer.writeEndElement();
    writer.writeEndDocument();
    return out;
}

} // namespace feed

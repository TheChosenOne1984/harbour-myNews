#include <QtTest>

#include "feed/feedparser.h"
#include "testrunner.h"

using namespace feed;

/*!
 * Fixtures here are synthetic. They reproduce variations observed in real German feeds
 * -- RDF from NDR, kicker separators, missing teasers, mixed date formats -- without
 * copying publisher content into the repository (section 11).
 */
class TestFeedParser : public QObject
{
    Q_OBJECT

private slots:
    void detectsRss2();
    void detectsAtom();
    void detectsRdf();
    void rejectsUnknownRoot();
    void guidFallsBackToLink();
    void skipsItemsWithoutTitle();
    void truncatesTeaser();
    void dropsPlaceholderTeasers_data();
    void dropsPlaceholderTeasers();
    void stripsHtmlFromTitleAndTeaser();
    void resolvesEntities();
    void extractsImages_data();
    void extractsImages();
    void parsesTimestamps_data();
    void parsesTimestamps();
    void keepsArticlesFromTruncatedDocument();
    void survivesDeeplyNestedItem();
    void keepsShallowNestingIntact();
    void reportsEmptyDocument();
};

void TestFeedParser::detectsRss2()
{
    const QByteArray xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<rss version=\"2.0\" xmlns:content=\"http://purl.org/rss/1.0/modules/content/\">"
        " <channel>"
        "  <title>Example News</title>"
        "  <link>https://example.invalid/</link>"
        "  <language>de</language>"
        "  <item>"
        "   <title>Erste Meldung</title>"
        "   <link>https://example.invalid/1</link>"
        "   <guid isPermaLink=\"false\">urn:example:1</guid>"
        "   <description>Ein Teaser.</description>"
        "   <pubDate>Mon, 07 Sep 2026 08:30:00 +0200</pubDate>"
        "  </item>"
        " </channel>"
        "</rss>";

    const ParsedFeed feed = parseFeed(xml);
    QVERIFY2(feed.isValid(), qPrintable(feed.error));
    QCOMPARE(feed.format, ParsedFeed::Rss2);
    QCOMPARE(feed.title, QStringLiteral("Example News"));
    QCOMPARE(feed.siteUrl, QStringLiteral("https://example.invalid/"));
    QCOMPARE(feed.lang, QStringLiteral("de"));
    QCOMPARE(feed.articles.size(), 1);

    const ParsedArticle &article = feed.articles.first();
    QCOMPARE(article.title, QStringLiteral("Erste Meldung"));
    QCOMPARE(article.link, QStringLiteral("https://example.invalid/1"));
    QCOMPARE(article.guid, QStringLiteral("urn:example:1"));
    QCOMPARE(article.teaser, QStringLiteral("Ein Teaser."));
    QCOMPARE(article.published, Q_INT64_C(1788762600));   // 06:30:00 UTC
}

void TestFeedParser::detectsAtom()
{
    const QByteArray xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<feed xmlns=\"http://www.w3.org/2005/Atom\" xml:lang=\"en\">"
        " <title>Example Atom</title>"
        " <link rel=\"self\" href=\"https://example.invalid/feed\"/>"
        " <link rel=\"alternate\" href=\"https://example.invalid/\"/>"
        " <entry>"
        "  <title>Second story</title>"
        "  <id>tag:example.invalid,2026:2</id>"
        "  <link rel=\"related\" href=\"https://example.invalid/other\"/>"
        "  <link rel=\"alternate\" href=\"https://example.invalid/2\"/>"
        "  <summary>A summary.</summary>"
        "  <updated>2026-09-07T08:30:00Z</updated>"
        " </entry>"
        "</feed>";

    const ParsedFeed feed = parseFeed(xml);
    QVERIFY2(feed.isValid(), qPrintable(feed.error));
    QCOMPARE(feed.format, ParsedFeed::Atom);
    QCOMPARE(feed.lang, QStringLiteral("en"));
    QCOMPARE(feed.articles.size(), 1);

    const ParsedArticle &article = feed.articles.first();
    QCOMPARE(article.guid, QStringLiteral("tag:example.invalid,2026:2"));
    // rel="alternate" wins over the other links, whatever order they appear in.
    QCOMPARE(article.link, QStringLiteral("https://example.invalid/2"));
    QCOMPARE(article.published, Q_INT64_C(1788769800));
}

void TestFeedParser::detectsRdf()
{
    // In RSS 1.0 the <item> elements are siblings of <channel>, not children -- the
    // mistake that makes a naive RSS 2.0 parser return zero articles for NDR.
    const QByteArray xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\""
        "         xmlns=\"http://purl.org/rss/1.0/\""
        "         xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        " <channel rdf:about=\"https://example.invalid/feed\">"
        "  <title>Example RDF</title>"
        "  <link>https://example.invalid/</link>"
        " </channel>"
        " <item rdf:about=\"https://example.invalid/3\">"
        "  <title>Dritte Meldung</title>"
        "  <link>https://example.invalid/3</link>"
        "  <description>Beschreibung.</description>"
        "  <dc:date>2026-09-07T08:30:00+02:00</dc:date>"
        " </item>"
        "</rdf:RDF>";

    const ParsedFeed feed = parseFeed(xml);
    QVERIFY2(feed.isValid(), qPrintable(feed.error));
    QCOMPARE(feed.format, ParsedFeed::Rdf);
    QCOMPARE(feed.title, QStringLiteral("Example RDF"));
    QCOMPARE(feed.articles.size(), 1);
    QCOMPARE(feed.articles.first().guid, QStringLiteral("https://example.invalid/3"));
    QCOMPARE(feed.articles.first().published, Q_INT64_C(1788762600));
}

void TestFeedParser::rejectsUnknownRoot()
{
    const ParsedFeed feed = parseFeed("<html><body>not a feed</body></html>");
    QVERIFY(!feed.isValid());
    QCOMPARE(feed.format, ParsedFeed::Unknown);
    QVERIFY(feed.error.contains(QStringLiteral("html")));
}

void TestFeedParser::guidFallsBackToLink()
{
    const QByteArray xml =
        "<rss version=\"2.0\"><channel><title>T</title>"
        "<item><title>No guid here</title><link>https://example.invalid/4</link></item>"
        "</channel></rss>";
    const ParsedFeed feed = parseFeed(xml);
    QCOMPARE(feed.articles.size(), 1);
    QCOMPARE(feed.articles.first().guid, QStringLiteral("https://example.invalid/4"));
}

void TestFeedParser::skipsItemsWithoutTitle()
{
    const QByteArray xml =
        "<rss version=\"2.0\"><channel><title>T</title>"
        "<item><link>https://example.invalid/5</link></item>"
        "<item><title>Has a title</title><link>https://example.invalid/6</link></item>"
        "</channel></rss>";
    const ParsedFeed feed = parseFeed(xml);
    QCOMPARE(feed.articles.size(), 1);
    QCOMPARE(feed.articles.first().title, QStringLiteral("Has a title"));
}

void TestFeedParser::truncatesTeaser()
{
    QString body;
    while (body.size() < 800)
        body += QStringLiteral("Sehr langer Teasertext ohne Ende. ");
    const QByteArray xml =
        "<rss version=\"2.0\"><channel><title>T</title><item>"
        "<title>Lang</title><link>https://example.invalid/7</link>"
        "<description>" + body.toUtf8() + "</description>"
        "</item></channel></rss>";

    const ParsedFeed feed = parseFeed(xml);
    QCOMPARE(feed.articles.size(), 1);
    const QString teaser = feed.articles.first().teaser;
    QVERIFY2(teaser.size() <= MaxTeaserLength,
             qPrintable(QStringLiteral("teaser is %1 chars").arg(teaser.size())));
    QVERIFY(teaser.size() > MaxTeaserLength - 40);
    QVERIFY2(!teaser.endsWith(QLatin1Char(' ')), "truncation should not leave a trailing space");
}

void TestFeedParser::dropsPlaceholderTeasers_data()
{
    QTest::addColumn<QByteArray>("description");
    QTest::addColumn<bool>("expectEmpty");

    // Seen in a real feed: the word "None" rendered under a headline as if it were the
    // summary.
    QTest::newRow("None") << QByteArray("None") << true;
    QTest::newRow("null") << QByteArray("null") << true;
    QTest::newRow("dash") << QByteArray("-") << true;
    QTest::newRow("n/a") << QByteArray("N/A") << true;
    QTest::newRow("real teaser") << QByteArray("Die Feuerwehr war im Einsatz.") << false;
    // A headline that legitimately begins with the word must survive.
    QTest::newRow("none in a sentence")
        << QByteArray("None of the parties reached a majority.") << false;
}

void TestFeedParser::dropsPlaceholderTeasers()
{
    QFETCH(QByteArray, description);
    QFETCH(bool, expectEmpty);

    const QByteArray xml =
        "<rss version=\"2.0\"><channel><title>T</title><item>"
        "<title>Eine Schlagzeile</title><link>https://example.invalid/1</link>"
        "<description>" + description + "</description>"
        "</item></channel></rss>";

    const ParsedFeed feed = parseFeed(xml);
    QCOMPARE(feed.articles.size(), 1);
    QCOMPARE(feed.articles.first().teaser.isEmpty(), expectEmpty);
}

void TestFeedParser::stripsHtmlFromTitleAndTeaser()
{
    const QByteArray xml =
        "<rss version=\"2.0\"><channel><title>T</title><item>"
        "<title>Titel mit &lt;b&gt;Markup&lt;/b&gt;</title>"
        "<link>https://example.invalid/8</link>"
        "<description><![CDATA[<p>Erster Absatz.</p><p>Zweiter Absatz.</p>]]></description>"
        "</item></channel></rss>";

    const ParsedFeed feed = parseFeed(xml);
    QCOMPARE(feed.articles.size(), 1);
    QCOMPARE(feed.articles.first().title, QStringLiteral("Titel mit Markup"));
    // Tag boundaries must become spaces, otherwise paragraphs run together.
    QCOMPARE(feed.articles.first().teaser, QStringLiteral("Erster Absatz. Zweiter Absatz."));
}

void TestFeedParser::resolvesEntities()
{
    QCOMPARE(stripHtml(QStringLiteral("Gr&uuml;&szlig;e &amp; Tsch&#252;ss")),
             QStringLiteral("Grüße & Tschüss"));
    QCOMPARE(stripHtml(QStringLiteral("Preis &#x20AC;5")), QStringLiteral("Preis €5"));
    // A stray ampersand must survive rather than eat the rest of the sentence.
    QCOMPARE(stripHtml(QStringLiteral("Fisch & Chips")), QStringLiteral("Fisch & Chips"));
    QCOMPARE(stripHtml(QStringLiteral("A &notanentity; B")),
             QStringLiteral("A &notanentity; B"));
}

void TestFeedParser::extractsImages_data()
{
    QTest::addColumn<QByteArray>("itemBody");
    QTest::addColumn<QString>("expected");

    QTest::newRow("media:content")
        << QByteArray("<media:content url=\"https://cdn.invalid/a.jpg\" medium=\"image\"/>"
                      "<description>&lt;img src=\"https://cdn.invalid/inline.jpg\"&gt;</description>")
        << QStringLiteral("https://cdn.invalid/a.jpg");
    QTest::newRow("media:thumbnail")
        << QByteArray("<media:thumbnail url=\"https://cdn.invalid/b.jpg\"/>")
        << QStringLiteral("https://cdn.invalid/b.jpg");
    QTest::newRow("enclosure")
        << QByteArray("<enclosure url=\"https://cdn.invalid/c.jpg\" type=\"image/jpeg\"/>")
        << QStringLiteral("https://cdn.invalid/c.jpg");
    QTest::newRow("enclosure that is not an image")
        << QByteArray("<enclosure url=\"https://cdn.invalid/d.mp3\" type=\"audio/mpeg\"/>")
        << QString();
    QTest::newRow("inline img")
        << QByteArray("<description>&lt;p&gt;&lt;img src='https://cdn.invalid/e.jpg'&gt;"
                      "Text&lt;/p&gt;</description>")
        << QStringLiteral("https://cdn.invalid/e.jpg");
    QTest::newRow("none")
        << QByteArray("<description>Kein Bild.</description>")
        << QString();
}

void TestFeedParser::extractsImages()
{
    QFETCH(QByteArray, itemBody);
    QFETCH(QString, expected);

    const QByteArray xml =
        "<rss version=\"2.0\" xmlns:media=\"http://search.yahoo.com/mrss/\">"
        "<channel><title>T</title><item>"
        "<title>Bildtest</title><link>https://example.invalid/9</link>"
        + itemBody +
        "</item></channel></rss>";

    const ParsedFeed feed = parseFeed(xml);
    QCOMPARE(feed.articles.size(), 1);
    QCOMPARE(feed.articles.first().imageUrl, expected);
}

void TestFeedParser::parsesTimestamps_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<qint64>("expected");

    QTest::newRow("RFC 822 with offset")
        << QStringLiteral("Mon, 07 Sep 2026 08:30:00 +0200") << Q_INT64_C(1788762600);
    QTest::newRow("RFC 822 GMT")
        << QStringLiteral("Mon, 07 Sep 2026 06:30:00 GMT") << Q_INT64_C(1788762600);
    QTest::newRow("ISO 8601 Zulu")
        << QStringLiteral("2026-09-07T06:30:00Z") << Q_INT64_C(1788762600);
    QTest::newRow("ISO 8601 with offset")
        << QStringLiteral("2026-09-07T08:30:00+02:00") << Q_INT64_C(1788762600);
    QTest::newRow("ISO 8601 with fractional seconds")
        << QStringLiteral("2026-09-07T06:30:00.123Z") << Q_INT64_C(1788762600);
    QTest::newRow("no zone means UTC, not local")
        << QStringLiteral("2026-09-07T06:30:00") << Q_INT64_C(1788762600);
    QTest::newRow("empty") << QString() << Q_INT64_C(0);
    QTest::newRow("garbage") << QStringLiteral("gestern Abend") << Q_INT64_C(0);
}

void TestFeedParser::parsesTimestamps()
{
    QFETCH(QString, input);
    QFETCH(qint64, expected);
    QCOMPARE(parseTimestamp(input), expected);
}

void TestFeedParser::keepsArticlesFromTruncatedDocument()
{
    // A response cut off mid-transfer must still yield what was read, not nothing.
    const QByteArray xml =
        "<rss version=\"2.0\"><channel><title>T</title>"
        "<item><title>Erhalten</title><link>https://example.invalid/10</link></item>"
        "<item><title>Abgeschnit";

    const ParsedFeed feed = parseFeed(xml);
    QVERIFY(!feed.error.isEmpty());
    QCOMPARE(feed.articles.size(), 1);
    QCOMPARE(feed.articles.first().title, QStringLiteral("Erhalten"));
}

namespace {

QByteArray nestedItem(int depth)
{
    QByteArray xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<rss version=\"2.0\"><channel><item>"
        "<title>Senat beschliesst Haushalt</title><link>https://example.invalid/a</link>"
        "<description>";
    for (int i = 0; i < depth; ++i)
        xml += "<b>";
    xml += "Kern";
    for (int i = 0; i < depth; ++i)
        xml += "</b>";
    xml += "</description></item></channel></rss>";
    return xml;
}

} // namespace

void TestFeedParser::survivesDeeplyNestedItem()
{
    // readNode() used to recurse once per level with no limit. Measured on aarch64, about
    // 35 000 levels exhausted the stack -- a document of roughly 245 KB, so the 5 MB
    // response cap was no protection at all. Any subscribed feed could crash the app on
    // every refresh.
    const ParsedFeed feed = parseFeed(nestedItem(50000));

    // The point of the test is that we get here at all.
    QCOMPARE(feed.articles.size(), 1);
    QCOMPARE(feed.articles.first().title, QStringLiteral("Senat beschliesst Haushalt"));
    QCOMPARE(feed.articles.first().link, QStringLiteral("https://example.invalid/a"));
    // Shallow fields survive; the document is still reported as not fully read.
    QVERIFY2(!feed.error.isEmpty(), "a skipped subtree must be reported");
}

void TestFeedParser::keepsShallowNestingIntact()
{
    // The nesting an Atom <content type="xhtml"> actually carries must pass untouched, or
    // the limit would be a bug of its own: no skipping, and therefore nothing to report.
    //
    // Not asserted here: that the text *inside* those elements reaches the teaser. It does
    // not, and never did -- a Node keeps only the character data of its own level, so
    // markup nested as real elements loses its text. Escaped HTML and CDATA, which is what
    // publishers actually send, arrive as characters on the description itself and are
    // unaffected. Out of scope for the depth limit; noted rather than silently widened.
    const ParsedFeed feed = parseFeed(nestedItem(20));
    QCOMPARE(feed.articles.size(), 1);
    QVERIFY2(feed.error.isEmpty(), qPrintable(feed.error));
    QCOMPARE(feed.articles.first().title, QStringLiteral("Senat beschliesst Haushalt"));
}

void TestFeedParser::reportsEmptyDocument()
{
    QVERIFY(!parseFeed(QByteArray()).isValid());
    QVERIFY(!parseFeed("   ").isValid());
}

int runFeedParserTests(int argc, char **argv)
{
    TestFeedParser test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_feedparser.moc"

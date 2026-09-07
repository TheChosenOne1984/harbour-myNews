#include <QtTest>

#include "feed/opml.h"
#include "i18n/languagenames.h"
#include "testrunner.h"

using namespace feed;

class TestOpml : public QObject
{
    Q_OBJECT

private slots:
    void readsAFlatList();
    void flattensNestedGroups();
    void readsLanguageWhenPresent();
    void rejectsNonOpml();
    void writesRoundTrippableDocument();
    void exportCarriesLanguage();

    void languageNamesAreEndonyms();
};

void TestOpml::readsAFlatList()
{
    const QByteArray xml = R"(<?xml version="1.0"?>
<opml version="2.0">
  <head><title>Subscriptions</title></head>
  <body>
    <outline type="rss" text="Alpha" xmlUrl="https://example.invalid/a"
             htmlUrl="https://example.invalid/"/>
    <outline type="rss" title="Beta" xmlUrl="https://example.invalid/b"/>
  </body>
</opml>)";

    const OpmlDocument document = parseOpml(xml);
    QVERIFY2(document.isValid(), qPrintable(document.error));
    QCOMPARE(document.title, QStringLiteral("Subscriptions"));
    QCOMPARE(document.entries.size(), 2);
    QCOMPARE(document.entries.at(0).title, QStringLiteral("Alpha"));
    QCOMPARE(document.entries.at(0).siteUrl, QStringLiteral("https://example.invalid/"));
    // Exporters disagree on text vs title; both have to work.
    QCOMPARE(document.entries.at(1).title, QStringLiteral("Beta"));
}

void TestOpml::flattensNestedGroups()
{
    // What a file exported by another reader normally looks like.
    const QByteArray xml = R"(<?xml version="1.0"?>
<opml version="2.0">
  <body>
    <outline text="News">
      <outline type="rss" text="Alpha" xmlUrl="https://example.invalid/a"/>
      <outline text="Regional">
        <outline type="rss" text="Beta" xmlUrl="https://example.invalid/b"/>
      </outline>
    </outline>
    <outline type="rss" text="Gamma" xmlUrl="https://example.invalid/c"/>
  </body>
</opml>)";

    const OpmlDocument document = parseOpml(xml);
    QVERIFY2(document.isValid(), qPrintable(document.error));
    QCOMPARE(document.entries.size(), 3);
    QCOMPARE(document.entries.at(0).category, QStringLiteral("News"));
    QCOMPARE(document.entries.at(1).category, QStringLiteral("Regional"));
    // Back at the top level, the group must not leak onto the next feed.
    QCOMPARE(document.entries.at(2).category, QString());
}

void TestOpml::readsLanguageWhenPresent()
{
    // OPML has no standard language attribute; ours is read back on import, which is
    // what makes a round-trip through this app keep the language (section 7).
    const QByteArray xml = R"(<opml version="2.0"><body>
      <outline type="rss" text="A" xmlUrl="https://example.invalid/a" language="de"/>
      <outline type="rss" text="B" xmlUrl="https://example.invalid/b"/>
    </body></opml>)";

    const OpmlDocument document = parseOpml(xml);
    QCOMPARE(document.entries.size(), 2);
    QCOMPARE(document.entries.at(0).lang, QStringLiteral("de"));
    QVERIFY(document.entries.at(1).lang.isEmpty());   // caller supplies the fallback
}

void TestOpml::rejectsNonOpml()
{
    QVERIFY(!parseOpml("<html><body/></html>").isValid());
    QVERIFY(!parseOpml(QByteArray()).isValid());
    QVERIFY(!parseOpml("<opml version=\"2.0\"><body/></opml>").isValid());
}

void TestOpml::writesRoundTrippableDocument()
{
    QVector<FeedRecord> feeds;
    FeedRecord a;
    a.url = QStringLiteral("https://example.invalid/a");
    a.title = QStringLiteral("Alpha & Söhne");   // entities and non-ASCII must survive
    a.siteUrl = QStringLiteral("https://example.invalid/");
    a.lang = QStringLiteral("de");
    feeds.append(a);

    const QByteArray xml = writeOpml(feeds);
    const OpmlDocument document = parseOpml(xml);

    QVERIFY2(document.isValid(), qPrintable(document.error));
    QCOMPARE(document.entries.size(), 1);
    QCOMPARE(document.entries.first().url, a.url);
    QCOMPARE(document.entries.first().title, a.title);
    QCOMPARE(document.entries.first().siteUrl, a.siteUrl);
    QCOMPARE(document.entries.first().lang, a.lang);
}

void TestOpml::exportCarriesLanguage()
{
    QVector<FeedRecord> feeds;
    FeedRecord a;
    a.url = QStringLiteral("https://example.invalid/a");
    a.title = QStringLiteral("Alpha");
    a.lang = QStringLiteral("nds");
    feeds.append(a);

    const QByteArray xml = writeOpml(feeds);
    QVERIFY2(xml.contains("language=\"nds\""), xml.constData());
    // A feed without a title must still export something usable as one.
    feeds[0].title.clear();
    QVERIFY(writeOpml(feeds).contains("https://example.invalid/a"));
}

void TestOpml::languageNamesAreEndonyms()
{
    QCOMPARE(i18n::languageDisplayName(QStringLiteral("de")), QStringLiteral("Deutsch"));
    // QLocale("en") is en_US and answers "American English", which is wrong for a picker
    // offering the language rather than a regional variant.
    const QString english = i18n::languageDisplayName(QStringLiteral("en"));
    QVERIFY2(!english.contains(QStringLiteral("American")), qPrintable(english));
    QCOMPARE(english, QStringLiteral("English"));
    // An explicit region is what the caller asked for, so it stays.
    QVERIFY(i18n::languageDisplayName(QStringLiteral("en_US")).contains(QStringLiteral("English")));
    QVERIFY(i18n::languageDisplayName(QString()).isEmpty());
}

int runOpmlTests(int argc, char **argv)
{
    TestOpml test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_opml.moc"

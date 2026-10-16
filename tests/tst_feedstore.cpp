#include <QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "feed/feedstore.h"
#include "testrunner.h"

using namespace feed;

/*!
 * Exercises the store against a real SQLite file rather than :memory:, so that WAL,
 * foreign keys and the FTS5 triggers are all genuinely in play.
 */
class TestFeedStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createsSchemaAndReportsVersion();
    void migratesRegionalLanguageTags();
    void hasFullTextSearch();
    void addFeedIsIdempotent();
    void removingFeedCascadesToArticles();
    void removingFeedRepairsSharedCluster();
    void manualLanguageOverrideSurvivesRefresh();
    void curatedTitleSurvivesRefresh();
    void catalogueLanguageSurvivesRefresh();
    void declaredLanguageIsUpdatedByRefresh();
    void languageOverrideReachesArticles();
    void storesLanguagesWithoutRegion();
    void ingestDeduplicatesByGuid();
    void ingestUpdatesChangedTitleAndRehashes();
    void ingestFallsBackToFirstSeen();
    void clustersAcrossFeeds();
    void clusterMembersCarryTheirSourceName();
    void ingestBlanksUnusableLinks();
    void keepsLanguagesInSeparateClusters();
    void leadArticlePrefersOneWithAnImage();
    void clusterListIsNewestFirst();
    void backoffGrowsAndClearsOnSuccess();
    void purgeRemovesArticlesAndEmptyClusters();
    void purgeRepairsSurvivingCluster();
    void searchFindsSubstring();
    void searchResultsNameTheirSource();

private:
    ParsedArticle article(const QString &guid, const QString &title,
                          qint64 published, const QString &image = QString()) const;

    QTemporaryDir *m_dir = nullptr;
    FeedStore *m_store = nullptr;
    qint64 m_now = 1788762600;
};

ParsedArticle TestFeedStore::article(const QString &guid, const QString &title,
                                     qint64 published, const QString &image) const
{
    ParsedArticle a;
    a.guid = guid;
    a.link = QStringLiteral("https://example.invalid/") + guid;
    a.title = title;
    a.teaser = QStringLiteral("Teaser für ") + title;
    a.published = published;
    a.imageUrl = image;
    return a;
}

void TestFeedStore::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_store = new FeedStore(m_dir->path() + QStringLiteral("/mynews.db"));
    QVERIFY2(m_store->open(), qPrintable(m_store->lastError()));
}

void TestFeedStore::cleanup()
{
    delete m_store;
    m_store = nullptr;
    delete m_dir;
    m_dir = nullptr;
}

void TestFeedStore::createsSchemaAndReportsVersion()
{
    QVERIFY(m_store->isOpen());
    QCOMPARE(m_store->schemaVersion(), 3);
    QVERIFY(m_store->feeds().isEmpty());
}

void TestFeedStore::migratesRegionalLanguageTags()
{
    // Build a version 1 database by hand, with the regional tags that version allowed.
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/old.db");
    {
        QSqlDatabase old = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                     QStringLiteral("v1"));
        old.setDatabaseName(path);
        QVERIFY(old.open());
        QSqlQuery q(old);
        QVERIFY(q.exec("CREATE TABLE feed (id INTEGER PRIMARY KEY, url TEXT NOT NULL UNIQUE,"
                       " title TEXT, site_url TEXT, lang TEXT, lang_source TEXT, etag TEXT,"
                       " last_modified TEXT, last_fetch INTEGER DEFAULT 0,"
                       " last_status INTEGER DEFAULT 0, enabled INTEGER NOT NULL DEFAULT 1,"
                       " failure_count INTEGER NOT NULL DEFAULT 0,"
                       " next_attempt INTEGER NOT NULL DEFAULT 0)"));
        QVERIFY(q.exec("CREATE TABLE cluster (id INTEGER PRIMARY KEY, lead_article_id INTEGER,"
                       " lang TEXT, published INTEGER, member_count INTEGER NOT NULL DEFAULT 0)"));
        QVERIFY(q.exec("CREATE TABLE article (id INTEGER PRIMARY KEY, feed_id INTEGER NOT NULL"
                       " REFERENCES feed(id) ON DELETE CASCADE, guid TEXT NOT NULL,"
                       " link TEXT NOT NULL, title TEXT NOT NULL, teaser TEXT,"
                       " published INTEGER, first_seen INTEGER NOT NULL, image_url TEXT,"
                       " image_state TEXT NOT NULL DEFAULT 'none', lang TEXT, simhash INTEGER,"
                       " cluster_id INTEGER REFERENCES cluster(id) ON DELETE SET NULL,"
                       " read INTEGER NOT NULL DEFAULT 0, UNIQUE(feed_id, guid))"));
        QVERIFY(q.exec("INSERT INTO feed (url, lang, lang_source) VALUES"
                       " ('https://example.invalid/gb', 'en-GB', 'declared'),"
                       " ('https://example.invalid/us', 'en_US', 'declared'),"
                       " ('https://example.invalid/de', 'de', 'manual')"));
        QVERIFY(q.exec("INSERT INTO article (feed_id, guid, link, title, first_seen, lang)"
                       " VALUES (1, 'a', 'https://example.invalid/a', 'A', 1, 'en-GB')"));
        QVERIFY(q.exec("PRAGMA user_version = 1"));
        old.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("v1"));

    feed::FeedStore store(path);
    QVERIFY2(store.open(), qPrintable(store.lastError()));
    QCOMPARE(store.schemaVersion(), 3);

    // Both English feeds must now share a clustering partition instead of being unable to
    // ever match each other.
    const QVector<feed::FeedRecord> feeds = store.feeds();
    QCOMPARE(feeds.size(), 3);
    QCOMPARE(feeds.at(0).lang, QStringLiteral("en"));
    QCOMPARE(feeds.at(1).lang, QStringLiteral("en"));
    QCOMPARE(feeds.at(2).lang, QStringLiteral("de"));
    QCOMPARE(store.articlesInCluster(-1).size(), 0);

    feed::ArticleRecord article;
    QVERIFY(store.articleById(1, &article));
    QCOMPARE(article.lang, QStringLiteral("en"));

    // 2 -> 3 added the category column; rows that predate it simply have none.
    QVERIFY(feeds.at(0).category.isEmpty());
}

void TestFeedStore::hasFullTextSearch()
{
    // Verified available on the 5.1.0.11 target; if this ever fails, search must degrade
    // rather than the app breaking.
    QVERIFY2(m_store->hasFullTextSearch(), "FTS5 unavailable -- see task0-findings.md");
}

void TestFeedStore::addFeedIsIdempotent()
{
    const QString url = QStringLiteral("https://example.invalid/feed");
    const qint64 first = m_store->addFeed(url, QStringLiteral("Example"), QStringLiteral("de"));
    QVERIFY(first > 0);
    QCOMPARE(m_store->addFeed(url), first);
    QCOMPARE(m_store->feeds().size(), 1);
}

void TestFeedStore::removingFeedCascadesToArticles()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/a"),
                                           QString(), QStringLiteral("de"));
    m_store->ingest(feedId, {article(QStringLiteral("1"),
                                     QStringLiteral("Eine ausreichend lange Meldung"), m_now)},
                    m_now);
    QCOMPARE(m_store->unreadCount(), 1);

    QVERIFY(m_store->removeFeed(feedId));
    QCOMPARE(m_store->unreadCount(), 0);
    // The cluster must go with it, or the feed page shows an entry with nothing behind it.
    QVERIFY(m_store->articlesInCluster(1).isEmpty());
}

void TestFeedStore::removingFeedRepairsSharedCluster()
{
    const qint64 a = m_store->addFeed(QStringLiteral("https://example.invalid/r1"),
                                      QString(), QStringLiteral("de"));
    const qint64 b = m_store->addFeed(QStringLiteral("https://example.invalid/r2"),
                                      QString(), QStringLiteral("de"));
    const QString title = QStringLiteral("Landgericht eröffnet Verfahren gegen Xavier Naidoo");
    m_store->ingest(a, {article(QStringLiteral("1"), title, m_now)}, m_now);
    m_store->ingest(b, {article(QStringLiteral("1"), title, m_now + 60)}, m_now + 60);

    // Feed a supplies the lead: lowest feed id, earliest published.
    QVector<ClusterSummary> before = m_store->clusterSummaries();
    QCOMPARE(before.size(), 1);
    QCOMPARE(before.first().memberCount, 2);
    const qint64 leadBefore = before.first().leadArticleId;

    QVERIFY(m_store->removeFeed(a));

    // The story is still carried by feed b, so it must stay in the list -- with b's
    // article as its lead, not a dangling pointer at the one that was deleted.
    const QVector<ClusterSummary> after = m_store->clusterSummaries();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().memberCount, 1);
    QVERIFY(after.first().leadArticleId != leadBefore);
    ArticleRecord lead;
    QVERIFY(m_store->articleById(after.first().leadArticleId, &lead));
    QCOMPARE(lead.feedId, b);
}

void TestFeedStore::manualLanguageOverrideSurvivesRefresh()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/b"));
    QVERIFY(m_store->setFeedLanguage(feedId, QStringLiteral("nds"), QStringLiteral("manual")));

    ParsedFeed parsed;
    parsed.format = ParsedFeed::Rss2;
    parsed.title = QStringLiteral("Neuer Titel");
    parsed.lang = QStringLiteral("de");     // the feed declares something else
    QVERIFY(m_store->updateFeedMetadata(feedId, parsed));

    FeedRecord record;
    QVERIFY(m_store->feedById(feedId, &record));
    QCOMPARE(record.title, QStringLiteral("Neuer Titel"));
    QCOMPARE(record.lang, QStringLiteral("nds"));
    QCOMPARE(record.langSource, QStringLiteral("manual"));
}

void TestFeedStore::curatedTitleSurvivesRefresh()
{
    // A feed's own <title> is often "Home", "News" or a bare domain, and it ends up in
    // the source pills under a headline. A name chosen by a person wins.
    const qint64 curated = m_store->addFeed(QStringLiteral("https://example.invalid/b4"),
                                            QStringLiteral("Der Tagesspiegel"),
                                            QStringLiteral("de"),
                                            QStringLiteral("catalogue"));
    ParsedFeed parsed;
    parsed.format = ParsedFeed::Rss2;
    parsed.title = QStringLiteral("tagesspiegel.de: News");
    QVERIFY(m_store->updateFeedMetadata(curated, parsed));

    FeedRecord record;
    QVERIFY(m_store->feedById(curated, &record));
    QCOMPARE(record.title, QStringLiteral("Der Tagesspiegel"));

    // A feed added with no title at all still gets one from the document.
    const qint64 bare = m_store->addFeed(QStringLiteral("https://example.invalid/b5"));
    QVERIFY(m_store->updateFeedMetadata(bare, parsed));
    QVERIFY(m_store->feedById(bare, &record));
    QCOMPARE(record.title, QStringLiteral("tagesspiegel.de: News"));
}

void TestFeedStore::catalogueLanguageSurvivesRefresh()
{
    // A catalogue entry was checked by hand; the document's own claim was not, and
    // section 7 says the checked value wins.
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/b2"),
                                           QString(), QStringLiteral("nds"),
                                           QStringLiteral("catalogue"));
    ParsedFeed parsed;
    parsed.format = ParsedFeed::Rss2;
    parsed.lang = QStringLiteral("de");
    QVERIFY(m_store->updateFeedMetadata(feedId, parsed));

    FeedRecord record;
    QVERIFY(m_store->feedById(feedId, &record));
    QCOMPARE(record.lang, QStringLiteral("nds"));
    QCOMPARE(record.langSource, QStringLiteral("catalogue"));
}

void TestFeedStore::declaredLanguageIsUpdatedByRefresh()
{
    // A value we only took from the document may be replaced by the document.
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/b3"),
                                           QString(), QStringLiteral("en"),
                                           QStringLiteral("declared"));
    ParsedFeed parsed;
    parsed.format = ParsedFeed::Rss2;
    parsed.lang = QStringLiteral("de");
    QVERIFY(m_store->updateFeedMetadata(feedId, parsed));

    FeedRecord record;
    QVERIFY(m_store->feedById(feedId, &record));
    QCOMPARE(record.lang, QStringLiteral("de"));
    QCOMPARE(record.langSource, QStringLiteral("declared"));
}

void TestFeedStore::languageOverrideReachesArticles()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/c"),
                                           QString(), QStringLiteral("de"));
    m_store->ingest(feedId, {article(QStringLiteral("1"),
                                     QStringLiteral("Eine ausreichend lange Meldung"), m_now)},
                    m_now);
    QVERIFY(m_store->setFeedLanguage(feedId, QStringLiteral("en"), QStringLiteral("manual")));

    // Articles inherit the feed language, so an override that stopped at the feed row
    // would leave the clusterer comparing against a stale value.
    const QVector<ArticleRecord> members = m_store->articlesInCluster(1);
    QCOMPARE(members.size(), 1);
    QCOMPARE(members.first().lang, QStringLiteral("en"));
}

void TestFeedStore::clusterMembersCarryTheirSourceName()
{
    // The cluster view exists to show who else ran the story, so the member rows have to
    // know their publisher. Nothing else joins feed into an article query, which is why
    // this is easy to drop again without noticing.
    const qint64 a = m_store->addFeed(QStringLiteral("https://example.invalid/na"),
                                      QStringLiteral("Nordsee Anzeiger"),
                                      QStringLiteral("de"));
    const qint64 b = m_store->addFeed(QStringLiteral("https://example.invalid/tb"),
                                      QStringLiteral("Tagesblatt"), QStringLiteral("de"));
    const QString headline = QStringLiteral("Senat beschliesst neuen Haushalt fuer Hamburg");
    m_store->ingest(a, {article(QStringLiteral("a1"), headline, m_now)}, m_now);
    m_store->ingest(b, {article(QStringLiteral("b1"), headline, m_now)}, m_now);

    const QVector<ArticleRecord> members = m_store->articlesInCluster(1);
    QCOMPARE(members.size(), 2);
    QStringList names;
    for (const ArticleRecord &member : members)
        names << member.sourceTitle;
    names.sort();
    QCOMPARE(names, QStringList({QStringLiteral("Nordsee Anzeiger"),
                                 QStringLiteral("Tagesblatt")}));
}

void TestFeedStore::ingestBlanksUnusableLinks()
{
    // A link the app will not dereference must not be stored: it would otherwise reach
    // the in-process browser engine and Qt.openUrlExternally untouched. The article stays,
    // because dropping it would silently remove a cluster member.
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/hostile"),
                                           QStringLiteral("Hostile"), QStringLiteral("de"));
    ParsedArticle hostile = article(QStringLiteral("h1"),
                                    QStringLiteral("Senat beschliesst neuen Haushalt"), m_now);
    hostile.link = QStringLiteral("file:///home/defaultuser/.local/share/secrets");
    ParsedArticle sound = article(QStringLiteral("h2"),
                                  QStringLiteral("Hafen meldet steigenden Umschlag"), m_now);
    sound.link = QStringLiteral("https://example.invalid/hafen");

    QVERIFY2(feedId > 0, qPrintable(m_store->lastError()));
    // article.link is NOT NULL, so "no link" has to be an empty string rather than a
    // default-constructed QString -- which is what this assertion is really guarding.
    const IngestResult ingested = m_store->ingest(feedId, {hostile, sound}, m_now);
    QVERIFY2(ingested.inserted == 2, qPrintable(m_store->lastError()));

    QHash<QString, QString> linkOf;
    for (const qint64 id : m_store->allArticleIds()) {
        ArticleRecord record;
        QVERIFY(m_store->articleById(id, &record));
        linkOf.insert(record.title, record.link);
    }

    QCOMPARE(linkOf.size(), 2);
    QCOMPARE(linkOf.value(QStringLiteral("Senat beschliesst neuen Haushalt")), QString());
    QCOMPARE(linkOf.value(QStringLiteral("Hafen meldet steigenden Umschlag")),
             QStringLiteral("https://example.invalid/hafen"));
}

void TestFeedStore::storesLanguagesWithoutRegion()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/c2"),
                                           QString(), QStringLiteral("en-GB"),
                                           QStringLiteral("catalogue"));
    feed::FeedRecord record;
    QVERIFY(m_store->feedById(feedId, &record));
    QCOMPARE(record.lang, QStringLiteral("en"));

    QVERIFY(m_store->setFeedLanguage(feedId, QStringLiteral("pt_BR"),
                                     QStringLiteral("manual")));
    QVERIFY(m_store->feedById(feedId, &record));
    QCOMPARE(record.lang, QStringLiteral("pt"));
}

void TestFeedStore::ingestDeduplicatesByGuid()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/d"),
                                           QString(), QStringLiteral("de"));
    const QVector<ParsedArticle> batch{
        article(QStringLiteral("1"), QStringLiteral("Eine ausreichend lange Meldung"), m_now)};

    IngestResult first = m_store->ingest(feedId, batch, m_now);
    QCOMPARE(first.inserted, 1);

    IngestResult second = m_store->ingest(feedId, batch, m_now + 60);
    QCOMPARE(second.inserted, 0);
    QCOMPARE(second.unchanged, 1);
    QCOMPARE(second.clustersCreated, 0);
}

void TestFeedStore::ingestUpdatesChangedTitleAndRehashes()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/e"),
                                           QString(), QStringLiteral("de"));
    m_store->ingest(feedId, {article(QStringLiteral("1"),
                                     QStringLiteral("Erste Fassung der Schlagzeile"), m_now)},
                    m_now);
    const quint64 before = m_store->articlesInCluster(1).first().simhash;

    const IngestResult result = m_store->ingest(
        feedId, {article(QStringLiteral("1"),
                         QStringLiteral("Korrigierte Fassung der Schlagzeile"), m_now)},
        m_now + 60);
    QCOMPARE(result.updated, 1);
    QCOMPARE(result.inserted, 0);

    const ArticleRecord after = m_store->articlesInCluster(1).first();
    QCOMPARE(after.title, QStringLiteral("Korrigierte Fassung der Schlagzeile"));
    QVERIFY2(after.simhash != before, "a changed title must produce a new simhash");
}

void TestFeedStore::ingestFallsBackToFirstSeen()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/f"),
                                           QString(), QStringLiteral("de"));
    ParsedArticle undated = article(QStringLiteral("1"),
                                    QStringLiteral("Meldung ohne Datumsangabe"), 0);
    undated.published = 0;
    m_store->ingest(feedId, {undated}, m_now);

    const ArticleRecord stored = m_store->articlesInCluster(1).first();
    QCOMPARE(stored.published, m_now);
    QCOMPARE(stored.firstSeen, m_now);
}

void TestFeedStore::clustersAcrossFeeds()
{
    const qint64 a = m_store->addFeed(QStringLiteral("https://example.invalid/g1"),
                                      QString(), QStringLiteral("de"));
    const qint64 b = m_store->addFeed(QStringLiteral("https://example.invalid/g2"),
                                      QString(), QStringLiteral("de"));

    // Same story, different kickers -- the case the whole feature exists for.
    m_store->ingest(a, {article(QStringLiteral("1"),
        QStringLiteral("Wegen Volksverhetzung: Landgericht Mannheim eröffnet Verfahren gegen Xavier Naidoo"),
        m_now)}, m_now);
    const IngestResult second = m_store->ingest(b, {article(QStringLiteral("1"),
        QStringLiteral("Anklage wegen Volksverhetzung: Landgericht Mannheim eröffnet Verfahren gegen Xavier Naidoo"),
        m_now + 300)}, m_now + 300);

    QCOMPARE(second.clustersJoined, 1);
    QCOMPARE(second.clustersCreated, 0);
    QCOMPARE(m_store->articlesInCluster(1).size(), 2);
}

void TestFeedStore::keepsLanguagesInSeparateClusters()
{
    const qint64 de = m_store->addFeed(QStringLiteral("https://example.invalid/h1"),
                                       QString(), QStringLiteral("de"));
    const qint64 en = m_store->addFeed(QStringLiteral("https://example.invalid/h2"),
                                       QString(), QStringLiteral("en"));
    const QString title = QStringLiteral("Landgericht eröffnet Verfahren gegen Xavier Naidoo");

    m_store->ingest(de, {article(QStringLiteral("1"), title, m_now)}, m_now);
    const IngestResult other = m_store->ingest(en, {article(QStringLiteral("1"), title, m_now)},
                                               m_now);
    QCOMPARE(other.clustersCreated, 1);
    QCOMPARE(other.clustersJoined, 0);
}

void TestFeedStore::leadArticlePrefersOneWithAnImage()
{
    const qint64 a = m_store->addFeed(QStringLiteral("https://example.invalid/i1"),
                                      QString(), QStringLiteral("de"));
    const qint64 b = m_store->addFeed(QStringLiteral("https://example.invalid/i2"),
                                      QString(), QStringLiteral("de"));
    const QString title = QStringLiteral("Landgericht eröffnet Verfahren gegen Xavier Naidoo");

    m_store->ingest(a, {article(QStringLiteral("1"), title, m_now)}, m_now);
    m_store->ingest(b, {article(QStringLiteral("1"), title, m_now + 60,
                                QStringLiteral("https://cdn.invalid/x.jpg"))}, m_now + 60);

    ClusterRecord record;
    QVERIFY(m_store->clusterById(1, &record));
    QCOMPARE(record.memberCount, 2);
    // Newest member decides the ordering timestamp.
    QCOMPARE(record.published, m_now + 60);

    ArticleRecord lead;
    QVERIFY(m_store->articleById(record.leadArticleId, &lead));
    // The article with an image leads, even though it arrived second and from a feed
    // that was subscribed later (section 6.6).
    QCOMPARE(lead.feedId, b);
    QVERIFY(!lead.imageUrl.isEmpty());
}

void TestFeedStore::clusterListIsNewestFirst()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/n"),
                                           QString(), QStringLiteral("de"));
    m_store->ingest(feedId, {
        article(QStringLiteral("1"), QStringLiteral("Die ältere unabhängige Meldung"), m_now - 7200),
        article(QStringLiteral("2"), QStringLiteral("Ganz frische andere Schlagzeile"), m_now),
    }, m_now);

    const QVector<ClusterRecord> list = m_store->clusters();
    QCOMPARE(list.size(), 2);
    QVERIFY(list.at(0).published > list.at(1).published);
}

void TestFeedStore::backoffGrowsAndClearsOnSuccess()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/j"));

    QVERIFY(m_store->recordFetchFailure(feedId, 503, m_now));
    FeedRecord after1;
    m_store->feedById(feedId, &after1);
    QCOMPARE(after1.failureCount, 1);
    QVERIFY(after1.nextAttempt > m_now);
    // A feed in backoff must not be handed out for fetching again straight away.
    QVERIFY(m_store->feedsDueForFetch(m_now).isEmpty());

    QVERIFY(m_store->recordFetchFailure(feedId, 503, m_now));
    FeedRecord after2;
    m_store->feedById(feedId, &after2);
    QCOMPARE(after2.failureCount, 2);
    QVERIFY2(after2.nextAttempt > after1.nextAttempt, "backoff must grow");

    QVERIFY(m_store->recordFetchSuccess(feedId, 200, QStringLiteral("\"abc\""),
                                        QStringLiteral("Mon, 07 Sep 2026 06:30:00 GMT"), m_now));
    FeedRecord recovered;
    m_store->feedById(feedId, &recovered);
    QCOMPARE(recovered.failureCount, 0);
    QCOMPARE(recovered.nextAttempt, Q_INT64_C(0));
    QCOMPARE(recovered.etag, QStringLiteral("\"abc\""));
    QCOMPARE(m_store->feedsDueForFetch(m_now).size(), 1);
}

void TestFeedStore::purgeRemovesArticlesAndEmptyClusters()
{
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/k"),
                                           QString(), QStringLiteral("de"));
    const qint64 old = m_now - 30 * 24 * 3600;
    m_store->ingest(feedId, {article(QStringLiteral("1"),
                                     QStringLiteral("Sehr alte Meldung aus dem Archiv"), old)},
                    old);
    m_store->ingest(feedId, {article(QStringLiteral("2"),
                                     QStringLiteral("Ganz frische andere Meldung heute"), m_now)},
                    m_now);
    QCOMPARE(m_store->unreadCount(), 2);

    const int deleted = m_store->purgeArticlesOlderThan(m_now - 14 * 24 * 3600);
    QCOMPARE(deleted, 1);
    QCOMPARE(m_store->unreadCount(), 1);
    QVERIFY2(m_store->articlesInCluster(1).isEmpty(),
             "the purged article's cluster should be gone, not left empty");
}

void TestFeedStore::purgeRepairsSurvivingCluster()
{
    const qint64 a = m_store->addFeed(QStringLiteral("https://example.invalid/l1"),
                                      QString(), QStringLiteral("de"));
    const qint64 b = m_store->addFeed(QStringLiteral("https://example.invalid/l2"),
                                      QString(), QStringLiteral("de"));
    const QString title = QStringLiteral("Landgericht eröffnet Verfahren gegen Xavier Naidoo");
    const qint64 old = m_now - 30 * 24 * 3600;

    // Both in one cluster, but only one of them is old enough to be purged.
    m_store->ingest(a, {article(QStringLiteral("1"), title, old)}, old);
    m_store->ingest(b, {article(QStringLiteral("1"), title, old + 3600)}, old + 3600);
    QCOMPARE(m_store->articlesInCluster(1).size(), 2);

    m_store->purgeArticlesOlderThan(old + 1800);
    const QVector<ArticleRecord> remaining = m_store->articlesInCluster(1);
    QCOMPARE(remaining.size(), 1);
    QCOMPARE(remaining.first().feedId, b);
}

void TestFeedStore::searchFindsSubstring()
{
    if (!m_store->hasFullTextSearch())
        QSKIP("FTS5 unavailable");

    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/m"),
                                           QString(), QStringLiteral("de"));
    m_store->ingest(feedId, {article(QStringLiteral("1"),
        QStringLiteral("Großbrand in der Hamburger Speicherstadt"), m_now)}, m_now);

    // The trigram tokenizer matches inside words, which is what makes search work in a
    // language that compounds them.
    QCOMPARE(m_store->search(QStringLiteral("speicher")).size(), 1);
    QCOMPARE(m_store->search(QStringLiteral("Speicherstadt")).size(), 1);
    QCOMPARE(m_store->search(QStringLiteral("Kiel")).size(), 0);
}

void TestFeedStore::searchResultsNameTheirSource()
{
    if (!m_store->hasFullTextSearch())
        QSKIP("FTS5 unavailable");

    // A hit without a publisher is not a usable search result, and only this query and
    // articlesInCluster join feed -- so it is easy to lose again without noticing.
    const qint64 feedId = m_store->addFeed(QStringLiteral("https://example.invalid/s"),
                                           QStringLiteral("Nordsee Anzeiger"),
                                           QStringLiteral("de"));
    m_store->ingest(feedId, {article(QStringLiteral("1"),
        QStringLiteral("Deichbau vor Cuxhaven beginnt im Herbst"), m_now)}, m_now);

    const QVector<ArticleRecord> hits = m_store->search(QStringLiteral("Deichbau"));
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().sourceTitle, QStringLiteral("Nordsee Anzeiger"));
}

int runFeedStoreTests(int argc, char **argv)
{
    TestFeedStore test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_feedstore.moc"

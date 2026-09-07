#include <QtTest>

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
    void hasFullTextSearch();
    void addFeedIsIdempotent();
    void removingFeedCascadesToArticles();
    void manualLanguageOverrideSurvivesRefresh();
    void catalogueLanguageSurvivesRefresh();
    void declaredLanguageIsUpdatedByRefresh();
    void languageOverrideReachesArticles();
    void ingestDeduplicatesByGuid();
    void ingestUpdatesChangedTitleAndRehashes();
    void ingestFallsBackToFirstSeen();
    void clustersAcrossFeeds();
    void keepsLanguagesInSeparateClusters();
    void leadArticlePrefersOneWithAnImage();
    void clusterListIsNewestFirst();
    void backoffGrowsAndClearsOnSuccess();
    void purgeRemovesArticlesAndEmptyClusters();
    void purgeRepairsSurvivingCluster();
    void searchFindsSubstring();

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
    QCOMPARE(m_store->schemaVersion(), 1);
    QVERIFY(m_store->feeds().isEmpty());
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

int runFeedStoreTests(int argc, char **argv)
{
    TestFeedStore test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_feedstore.moc"

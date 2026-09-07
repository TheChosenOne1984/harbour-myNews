#include <QtTest>

#include <QTemporaryDir>

#include "feed/feedstore.h"
#include "i18n/relativetime.h"
#include "model/clustermodel.h"
#include "testrunner.h"

class TestClusterView : public QObject
{
    Q_OBJECT

private slots:
    void relativeTime_data();
    void relativeTime();

    void summariesJoinLeadAndSources();
    void summariesPutTheLeadSourceFirst();
    void summariesReportUnreadWhileAnyMemberIsUnread();
    void summariesAreNewestFirst();

    void modelExposesRoles();
    void modelTrimsTheSourceRow_data();
    void modelTrimsTheSourceRow();
    void modelMarksRowsReadWithoutResetting();

    void filtersByUnread();
    void filtersByRegion();
    void filtersBySource();
    void filtersByAge();
    void combinesFilters();
    void offersOnlySubscribedRegions();
};

void TestClusterView::relativeTime_data()
{
    QTest::addColumn<qint64>("age");
    QTest::addColumn<QString>("expected");

    QTest::newRow("just now") << Q_INT64_C(5) << QStringLiteral("now");
    QTest::newRow("one minute") << Q_INT64_C(60) << QStringLiteral("1 min");
    QTest::newRow("minutes") << Q_INT64_C(25 * 60) << QStringLiteral("25 min");
    QTest::newRow("one hour") << Q_INT64_C(3600) << QStringLiteral("1 h");
    QTest::newRow("hours") << Q_INT64_C(5 * 3600) << QStringLiteral("5 h");
    QTest::newRow("one day") << Q_INT64_C(24 * 3600) << QStringLiteral("1 d");
    QTest::newRow("days") << Q_INT64_C(3 * 24 * 3600) << QStringLiteral("3 d");
    // A publisher's clock running ahead must not produce "in -3 minutes".
    QTest::newRow("published in the future") << Q_INT64_C(-90) << QStringLiteral("now");
}

void TestClusterView::relativeTime()
{
    QFETCH(qint64, age);
    QFETCH(QString, expected);

    const qint64 now = 1788762600;
    QCOMPARE(i18n::relativeTime(now - age, now), expected);

    // Older than a week becomes a date rather than a growing count of days.
    const QString old = i18n::relativeTime(now - 30 * 24 * 3600, now);
    QVERIFY2(!old.contains(QStringLiteral(" d")), qPrintable(old));
    QVERIFY(!old.isEmpty());
    // Nothing to say about an article with no date at all.
    QVERIFY(i18n::relativeTime(0, now).isEmpty());
}

namespace {

/*! Two feeds carrying the same story, plus one story of its own. */
struct Fixture
{
    QTemporaryDir dir;
    feed::FeedStore *store = nullptr;
    qint64 feedA = -1;
    qint64 feedB = -1;
    qint64 now = 1788762600;

    bool build()
    {
        store = new feed::FeedStore(dir.path() + QStringLiteral("/f.db"));
        if (!store->open())
            return false;
        feedA = store->addFeed(QStringLiteral("https://a.invalid/feed"),
                               QStringLiteral("Alpha"), QStringLiteral("de"));
        feedB = store->addFeed(QStringLiteral("https://b.invalid/feed"),
                               QStringLiteral("Beta"), QStringLiteral("de"));
        return feedA > 0 && feedB > 0;
    }

    qint64 addFeed(const QString &url, const QString &title, const QString &category)
    {
        return store->addFeed(url, title, QStringLiteral("de"),
                              QStringLiteral("catalogue"), category);
    }

    feed::ParsedArticle article(const QString &guid, const QString &title, qint64 published,
                                const QString &image = QString()) const
    {
        feed::ParsedArticle a;
        a.guid = guid;
        a.link = QStringLiteral("https://example.invalid/") + guid;
        a.title = title;
        a.teaser = QStringLiteral("Teaser");
        a.published = published;
        a.imageUrl = image;
        return a;
    }

    ~Fixture() { delete store; }
};

const QString SharedStory =
    QStringLiteral("Landgericht eröffnet Verfahren gegen Xavier Naidoo");
const QString OtherStory =
    QStringLiteral("Finnland errichtet Zaun an der Grenze zu Russland");

} // namespace

void TestClusterView::summariesJoinLeadAndSources()
{
    Fixture f;
    QVERIFY(f.build());

    // Same story from two publishers, plus an unrelated one.
    f.store->ingest(f.feedA, {f.article(QStringLiteral("1"), SharedStory, f.now)}, f.now);
    f.store->ingest(f.feedB, {f.article(QStringLiteral("1"), SharedStory, f.now + 60)},
                    f.now + 60);
    f.store->ingest(f.feedA, {f.article(QStringLiteral("2"), OtherStory, f.now + 120)},
                    f.now + 120);

    const QVector<feed::ClusterSummary> summaries = f.store->clusterSummaries();
    QCOMPARE(summaries.size(), 2);

    // The clustered story carries both sources on one row -- the whole point of the app.
    const feed::ClusterSummary &shared =
        summaries.at(0).memberCount == 2 ? summaries.at(0) : summaries.at(1);
    QCOMPARE(shared.memberCount, 2);
    QCOMPARE(shared.sources.size(), 2);
    QVERIFY(shared.sources.contains(QStringLiteral("Alpha")));
    QVERIFY(shared.sources.contains(QStringLiteral("Beta")));
    QCOMPARE(shared.title, SharedStory);
    QVERIFY(!shared.link.isEmpty());
}

void TestClusterView::summariesPutTheLeadSourceFirst()
{
    Fixture f;
    QVERIFY(f.build());

    // Beta arrives second but has an image, so it becomes the lead (section 6.6) -- and
    // the pill row has to name the source whose headline is on display first.
    f.store->ingest(f.feedA, {f.article(QStringLiteral("1"), SharedStory, f.now)}, f.now);
    f.store->ingest(f.feedB, {f.article(QStringLiteral("1"), SharedStory, f.now + 60,
                                        QStringLiteral("https://cdn.invalid/x.jpg"))},
                    f.now + 60);

    const QVector<feed::ClusterSummary> summaries = f.store->clusterSummaries();
    QCOMPARE(summaries.size(), 1);
    QCOMPARE(summaries.first().sources.first(), QStringLiteral("Beta"));
    QCOMPARE(summaries.first().imageUrl, QStringLiteral("https://cdn.invalid/x.jpg"));
}

void TestClusterView::summariesReportUnreadWhileAnyMemberIsUnread()
{
    Fixture f;
    QVERIFY(f.build());
    f.store->ingest(f.feedA, {f.article(QStringLiteral("1"), SharedStory, f.now)}, f.now);
    f.store->ingest(f.feedB, {f.article(QStringLiteral("1"), SharedStory, f.now + 60)},
                    f.now + 60);

    QVERIFY(f.store->clusterSummaries().first().unread);

    const QVector<feed::ArticleRecord> members = f.store->articlesInCluster(1);
    QCOMPARE(members.size(), 2);
    f.store->markRead(members.first().id, true);
    // One of two read is still an unread story.
    QVERIFY(f.store->clusterSummaries().first().unread);

    f.store->markRead(members.last().id, true);
    QVERIFY(!f.store->clusterSummaries().first().unread);
}

void TestClusterView::summariesAreNewestFirst()
{
    Fixture f;
    QVERIFY(f.build());
    f.store->ingest(f.feedA, {f.article(QStringLiteral("1"), OtherStory, f.now - 7200)},
                    f.now - 7200);
    f.store->ingest(f.feedA, {f.article(QStringLiteral("2"), SharedStory, f.now)}, f.now);

    const QVector<feed::ClusterSummary> summaries = f.store->clusterSummaries();
    QCOMPARE(summaries.size(), 2);
    QVERIFY(summaries.at(0).published > summaries.at(1).published);
    QCOMPARE(summaries.at(0).title, SharedStory);
}

namespace {

feed::ClusterSummary summaryWith(const QStringList &sources, bool unread = true)
{
    feed::ClusterSummary summary;
    summary.clusterId = 1;
    summary.leadArticleId = 1;
    summary.title = SharedStory;
    summary.teaser = QStringLiteral("Teaser");
    summary.published = 1788762600;
    summary.memberCount = sources.size();
    summary.unread = unread;
    summary.sources = sources;
    return summary;
}

} // namespace

void TestClusterView::modelExposesRoles()
{
    model::ClusterModel model;
    model.setClusters({summaryWith({QStringLiteral("Alpha"), QStringLiteral("Beta")})});

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0);
    QCOMPARE(model.data(index, model::ClusterModel::TitleRole).toString(), SharedStory);
    QCOMPARE(model.data(index, model::ClusterModel::MemberCountRole).toInt(), 2);
    QVERIFY(model.data(index, model::ClusterModel::UnreadRole).toBool());

    // QML addresses roles by name.
    const QList<QByteArray> names = model.roleNames().values();
    for (const char *role : {"visibleSources", "hiddenSourceCount", "unread", "sources"})
        QVERIFY2(names.contains(QByteArray(role)), role);
}

void TestClusterView::modelTrimsTheSourceRow_data()
{
    QTest::addColumn<int>("sourceCount");
    QTest::addColumn<int>("expectedVisible");
    QTest::addColumn<int>("expectedHidden");

    QTest::newRow("one") << 1 << 1 << 0;
    QTest::newRow("three fit") << 3 << 3 << 0;
    // Section 9: beyond three, show the first two and a count -- so the row cannot grow
    // past its fixed height however many publishers ran the story.
    QTest::newRow("four") << 4 << 2 << 2;
    QTest::newRow("ten") << 10 << 2 << 8;
}

void TestClusterView::modelTrimsTheSourceRow()
{
    QFETCH(int, sourceCount);
    QFETCH(int, expectedVisible);
    QFETCH(int, expectedHidden);

    QStringList sources;
    for (int i = 0; i < sourceCount; ++i)
        sources << QStringLiteral("Source %1").arg(i);

    model::ClusterModel model;
    model.setClusters({summaryWith(sources)});

    const QModelIndex index = model.index(0);
    QCOMPARE(model.data(index, model::ClusterModel::VisibleSourcesRole).toStringList().size(),
             expectedVisible);
    QCOMPARE(model.data(index, model::ClusterModel::HiddenSourceCountRole).toInt(),
             expectedHidden);
    // Visible plus hidden must account for every source, or the count lies.
    QCOMPARE(expectedVisible + (expectedHidden > 0 ? expectedHidden : 0), sourceCount);
}

void TestClusterView::modelMarksRowsReadWithoutResetting()
{
    model::ClusterModel model;
    model.setClusters({summaryWith({QStringLiteral("Alpha")})});

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.setRowRead(1, true);
    QVERIFY(!model.data(model.index(0), model::ClusterModel::UnreadRole).toBool());
    // Resetting would drop the reader back to the top of the list on every tap.
    QCOMPARE(reset.size(), 0);
    QCOMPARE(changed.size(), 1);

    // Marking an already-read row again must not emit anything.
    model.setRowRead(1, true);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(model.indexOfCluster(99), -1);
}

namespace {

/*! Two Hamburg feeds and one national one, each with a story of its own. */
struct FilterFixture
{
    Fixture f;
    qint64 hamburgA = -1;
    qint64 hamburgB = -1;
    qint64 national = -1;

    bool build()
    {
        if (!f.build())
            return false;
        hamburgA = f.addFeed(QStringLiteral("https://hh1.invalid/f"),
                             QStringLiteral("NDR Hamburg"), QStringLiteral("hamburg"));
        hamburgB = f.addFeed(QStringLiteral("https://hh2.invalid/f"),
                             QStringLiteral("MOPO"), QStringLiteral("hamburg"));
        national = f.addFeed(QStringLiteral("https://de.invalid/f"),
                             QStringLiteral("tagesschau"), QStringLiteral("national"));

        // One story both Hamburg feeds carried, one only the national feed, one old.
        f.store->ingest(hamburgA, {f.article(QStringLiteral("1"), SharedStory, f.now)}, f.now);
        f.store->ingest(hamburgB, {f.article(QStringLiteral("1"), SharedStory, f.now + 60)},
                        f.now + 60);
        f.store->ingest(national, {f.article(QStringLiteral("2"), OtherStory, f.now + 120)},
                        f.now + 120);
        f.store->ingest(national, {f.article(QStringLiteral("3"),
            QStringLiteral("Eine deutlich ältere unabhängige Meldung"), f.now - 5 * 24 * 3600)},
            f.now - 5 * 24 * 3600);
        return true;
    }

    int count(const feed::ClusterFilter &filter) const
    {
        return f.store->clusterSummaries(filter, f.now + 200).size();
    }
};

} // namespace

void TestClusterView::filtersByUnread()
{
    FilterFixture x;
    QVERIFY(x.build());

    feed::ClusterFilter filter;
    QCOMPARE(x.count(filter), 3);

    filter.unreadOnly = true;
    QCOMPARE(x.count(filter), 3);

    // Reading one member is not enough: a cluster is unread while any member is, so the
    // row must keep matching until the whole story has been read.
    const QVector<feed::ArticleRecord> members = x.f.store->articlesInCluster(1);
    QCOMPARE(members.size(), 2);
    x.f.store->markRead(members.first().id, true);
    QCOMPARE(x.count(filter), 3);

    x.f.store->markRead(members.last().id, true);
    QCOMPARE(x.count(filter), 2);
}

void TestClusterView::filtersByRegion()
{
    FilterFixture x;
    QVERIFY(x.build());

    feed::ClusterFilter filter;
    filter.category = QStringLiteral("hamburg");
    QCOMPARE(x.count(filter), 1);

    filter.category = QStringLiteral("national");
    QCOMPARE(x.count(filter), 2);

    filter.category = QStringLiteral("nowhere");
    QCOMPARE(x.count(filter), 0);
}

void TestClusterView::filtersBySource()
{
    FilterFixture x;
    QVERIFY(x.build());

    feed::ClusterFilter filter;
    filter.feedId = x.hamburgB;
    // MOPO contributed to one story -- the shared one. A story counts as MOPO's because
    // MOPO carried it, not because MOPO's article happens to be the one on display.
    QCOMPARE(x.count(filter), 1);

    filter.feedId = x.national;
    QCOMPARE(x.count(filter), 2);
}

void TestClusterView::filtersByAge()
{
    FilterFixture x;
    QVERIFY(x.build());

    feed::ClusterFilter filter;
    filter.maxAgeSeconds = 24 * 3600;
    QCOMPARE(x.count(filter), 2);   // the five-day-old story drops out

    filter.maxAgeSeconds = 7 * 24 * 3600;
    QCOMPARE(x.count(filter), 3);
}

void TestClusterView::combinesFilters()
{
    FilterFixture x;
    QVERIFY(x.build());

    feed::ClusterFilter filter;
    filter.category = QStringLiteral("national");
    filter.maxAgeSeconds = 24 * 3600;
    // National *and* recent: the old national story is excluded by the age.
    QCOMPARE(x.count(filter), 1);

    QVERIFY(!feed::ClusterFilter().isActive());
    QVERIFY(filter.isActive());
}

void TestClusterView::offersOnlySubscribedRegions()
{
    FilterFixture x;
    QVERIFY(x.build());

    // A filter must not offer a region that would select nothing.
    const QStringList categories = x.f.store->subscribedCategories();
    QCOMPARE(categories.size(), 2);
    QVERIFY(categories.contains(QStringLiteral("hamburg")));
    QVERIFY(categories.contains(QStringLiteral("national")));

    QVERIFY(x.f.store->setFeedEnabled(x.hamburgA, false));
    QVERIFY(x.f.store->setFeedEnabled(x.hamburgB, false));
    QCOMPARE(x.f.store->subscribedCategories(), QStringList{QStringLiteral("national")});
}

int runClusterViewTests(int argc, char **argv)
{
    TestClusterView test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_clusterview.moc"

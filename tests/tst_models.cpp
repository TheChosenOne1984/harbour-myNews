#include <QtTest>

#include <QSignalSpy>

#include "catalog/catalog.h"
#include "model/catalogmodel.h"
#include "model/subscriptionmodel.h"
#include "testrunner.h"

using namespace model;

class TestModels : public QObject
{
    Q_OBJECT

private slots:
    void subscriptionRolesAreExposed();
    void subscriptionFallsBackToUrlAsTitle();
    void subscriptionHealth_data();
    void subscriptionHealth();
    void subscriptionMarksUnconfirmedLanguages();
    void subscriptionFindsRowsAndUrls();

    void catalogExposesEntries();
    void catalogFiltersByCategory();
    void catalogLocalisesCategoryNames();
    void catalogTracksSubscriptionsWithoutResetting();
    void catalogEntryAtIsUsableForSubscribing();

private:
    static feed::FeedRecord makeFeed(qint64 id, const QString &url);
    static catalog::Catalog makeCatalog();
};

feed::FeedRecord TestModels::makeFeed(qint64 id, const QString &url)
{
    feed::FeedRecord feed;
    feed.id = id;
    feed.url = url;
    feed.lang = QStringLiteral("de");
    feed.langSource = QStringLiteral("catalogue");
    feed.enabled = true;
    feed.lastFetch = 1788762600;
    return feed;
}

catalog::Catalog TestModels::makeCatalog()
{
    catalog::Catalog result;
    result.version = 1;

    catalog::Category hamburg;
    hamburg.id = QStringLiteral("hamburg");
    hamburg.name = QStringLiteral("Hamburg");
    catalog::Category national;
    national.id = QStringLiteral("national");
    national.name = QStringLiteral("Germany");
    national.localisedNames.insert(QStringLiteral("de"), QStringLiteral("Deutschland"));
    result.categories << hamburg << national;

    catalog::Entry a;
    a.url = QStringLiteral("https://example.invalid/hh");
    a.title = QStringLiteral("Hamburg Feed");
    a.lang = QStringLiteral("de");
    a.category = QStringLiteral("hamburg");
    catalog::Entry b;
    b.url = QStringLiteral("https://example.invalid/de");
    b.title = QStringLiteral("National Feed");
    b.lang = QStringLiteral("de");
    b.category = QStringLiteral("national");
    result.entries << a << b;
    return result;
}

void TestModels::subscriptionRolesAreExposed()
{
    SubscriptionModel model;
    QSignalSpy spy(&model, &SubscriptionModel::countChanged);

    feed::FeedRecord feed = makeFeed(1, QStringLiteral("https://example.invalid/a"));
    feed.title = QStringLiteral("Example");
    model.setFeeds({feed});

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(spy.size(), 1);

    const QModelIndex index = model.index(0);
    QCOMPARE(model.data(index, SubscriptionModel::TitleRole).toString(),
             QStringLiteral("Example"));
    QCOMPARE(model.data(index, SubscriptionModel::FeedIdRole).toLongLong(), Q_INT64_C(1));
    QCOMPARE(model.data(index, SubscriptionModel::LangRole).toString(), QStringLiteral("de"));

    // QML addresses roles by name, so the names have to be there.
    const QHash<int, QByteArray> names = model.roleNames();
    QVERIFY(names.values().contains(QByteArray("health")));
    QVERIFY(names.values().contains(QByteArray("languageIsConfirmed")));
}

void TestModels::subscriptionFallsBackToUrlAsTitle()
{
    // A feed that has never been fetched has no title; an empty row would look broken.
    SubscriptionModel model;
    model.setFeeds({makeFeed(1, QStringLiteral("https://example.invalid/a"))});
    QCOMPARE(model.data(model.index(0), SubscriptionModel::TitleRole).toString(),
             QStringLiteral("https://example.invalid/a"));
}

void TestModels::subscriptionHealth_data()
{
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<qint64>("lastFetch");
    QTest::addColumn<int>("failures");
    QTest::addColumn<int>("expected");

    QTest::newRow("never fetched")
        << true << Q_INT64_C(0) << 0 << int(SubscriptionModel::Unknown);
    QTest::newRow("fine")
        << true << Q_INT64_C(1788762600) << 0 << int(SubscriptionModel::Ok);
    QTest::newRow("failing once")
        << true << Q_INT64_C(1788762600) << 1 << int(SubscriptionModel::Failing);
    QTest::newRow("failing persistently")
        << true << Q_INT64_C(1788762600) << 5 << int(SubscriptionModel::Broken);
    QTest::newRow("switched off")
        << false << Q_INT64_C(1788762600) << 9 << int(SubscriptionModel::Disabled);
}

void TestModels::subscriptionHealth()
{
    QFETCH(bool, enabled);
    QFETCH(qint64, lastFetch);
    QFETCH(int, failures);
    QFETCH(int, expected);

    feed::FeedRecord feed = makeFeed(1, QStringLiteral("https://example.invalid/a"));
    feed.enabled = enabled;
    feed.lastFetch = lastFetch;
    feed.failureCount = failures;

    // Section 5 wants persistent failure surfaced, so "broke once" and "broken for days"
    // must not look the same.
    QCOMPARE(int(SubscriptionModel::healthOf(feed)), expected);
}

void TestModels::subscriptionMarksUnconfirmedLanguages()
{
    SubscriptionModel model;

    feed::FeedRecord fromCatalog = makeFeed(1, QStringLiteral("https://example.invalid/a"));
    feed::FeedRecord fromImport = makeFeed(2, QStringLiteral("https://example.invalid/b"));
    fromImport.langSource = QStringLiteral("declared");
    feed::FeedRecord corrected = makeFeed(3, QStringLiteral("https://example.invalid/c"));
    corrected.langSource = QStringLiteral("manual");
    model.setFeeds({fromCatalog, fromImport, corrected});

    // OPML import leaves "declared" values that were really the device language; those
    // are the rows the subscriptions page should flag for review (section 7).
    QVERIFY(model.data(model.index(0), SubscriptionModel::LanguageIsConfirmedRole).toBool());
    QVERIFY(!model.data(model.index(1), SubscriptionModel::LanguageIsConfirmedRole).toBool());
    QVERIFY(model.data(model.index(2), SubscriptionModel::LanguageIsConfirmedRole).toBool());
}

void TestModels::subscriptionFindsRowsAndUrls()
{
    SubscriptionModel model;
    model.setFeeds({makeFeed(7, QStringLiteral("https://example.invalid/a")),
                    makeFeed(9, QStringLiteral("https://example.invalid/b"))});

    QCOMPARE(model.indexOfFeed(9), 1);
    QCOMPARE(model.indexOfFeed(123), -1);
    QCOMPARE(model.subscribedUrls().size(), 2);
    QVERIFY(model.subscribedUrls().contains(QStringLiteral("https://example.invalid/b")));
}

void TestModels::catalogExposesEntries()
{
    CatalogModel model;
    model.setCatalog(makeCatalog());

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.totalCount(), 2);
    QCOMPARE(model.data(model.index(0), CatalogModel::TitleRole).toString(),
             QStringLiteral("Hamburg Feed"));
    QVERIFY(!model.data(model.index(0), CatalogModel::SubscribedRole).toBool());
    QCOMPARE(model.categoryIds(), QStringList()
                                      << QStringLiteral("hamburg") << QStringLiteral("national"));
}

void TestModels::catalogFiltersByCategory()
{
    CatalogModel model;
    model.setCatalog(makeCatalog());

    model.setCategoryFilter(QStringLiteral("national"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), CatalogModel::TitleRole).toString(),
             QStringLiteral("National Feed"));
    // totalCount keeps reporting the whole catalogue, so a filter that hides everything
    // can be told apart from an empty catalogue.
    QCOMPARE(model.totalCount(), 2);

    model.setCategoryFilter(QString());
    QCOMPARE(model.rowCount(), 2);
}

void TestModels::catalogLocalisesCategoryNames()
{
    CatalogModel model;
    model.setCatalog(makeCatalog());

    QCOMPARE(model.data(model.index(1), CatalogModel::CategoryNameRole).toString(),
             QStringLiteral("Germany"));

    model.setUiLanguage(QStringLiteral("de"));
    QCOMPARE(model.data(model.index(1), CatalogModel::CategoryNameRole).toString(),
             QStringLiteral("Deutschland"));
    // Hamburg has no translation, so it stays as it is rather than becoming empty.
    QCOMPARE(model.data(model.index(0), CatalogModel::CategoryNameRole).toString(),
             QStringLiteral("Hamburg"));
}

void TestModels::catalogTracksSubscriptionsWithoutResetting()
{
    CatalogModel model;
    model.setCatalog(makeCatalog());

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.setSubscribedUrls({QStringLiteral("https://example.invalid/hh")});

    QVERIFY(model.data(model.index(0), CatalogModel::SubscribedRole).toBool());
    QVERIFY(!model.data(model.index(1), CatalogModel::SubscribedRole).toBool());

    // Subscribing must not reset the model: that would scroll the catalogue page back to
    // the top on every tap.
    QCOMPARE(reset.size(), 0);
    QCOMPARE(changed.size(), 1);
}

void TestModels::catalogEntryAtIsUsableForSubscribing()
{
    CatalogModel model;
    model.setCatalog(makeCatalog());

    const QVariantMap entry = model.entryAt(0);
    QCOMPARE(entry.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.invalid/hh"));
    // The language has to come along: section 7 assigns it rather than detecting it later.
    QCOMPARE(entry.value(QStringLiteral("lang")).toString(), QStringLiteral("de"));
    QVERIFY(model.entryAt(99).isEmpty());
}

int runModelTests(int argc, char **argv)
{
    TestModels test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_models.moc"

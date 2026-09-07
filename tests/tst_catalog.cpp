#include <QtTest>

#include <QTemporaryDir>

#include "app/settings.h"
#include "catalog/catalog.h"
#include "catalog/catalogservice.h"
#include "testrunner.h"

class TestCatalog : public QObject
{
    Q_OBJECT

private slots:
    void parsesAWellFormedCatalogue();
    void localisedCategoryNamesFallBack();
    void skipsEntriesMissingRequiredFields();
    void rejectsNewerFormatVersion();
    void rejectsGarbage();
    void shippedCatalogueFileIsValid();

    void settingsDefaults();
    void settingsCatalogueIsNotContactedBeforeConsent();
    void settingsResetKeepsConsentDecision();
    void settingsClampOutOfRangeValues();
    void settingsCatalogUrlFallsBackToDefault();
    void settingsClusterParamsFollowSettings();
    void settingsResetRestoresDefaults();

    void servicePrefersCacheUntilStale();
};

void TestCatalog::parsesAWellFormedCatalogue()
{
    const QByteArray json = R"({
      "version": 1,
      "updated": "2026-09-07",
      "categories": [
        { "id": "hamburg", "name": "Hamburg", "name_de": "Hamburg" },
        { "id": "national", "name": "Germany", "name_de": "Deutschland" }
      ],
      "feeds": [
        { "url": "https://example.invalid/a", "title": "A", "lang": "de",
          "category": "hamburg", "site_url": "https://example.invalid/" },
        { "url": "https://example.invalid/b", "title": "B", "lang": "en-GB",
          "category": "national" }
      ]
    })";

    const catalog::Catalog parsed = catalog::parseCatalog(json);
    QVERIFY2(parsed.isValid(), qPrintable(parsed.error));
    QCOMPARE(parsed.version, 1);
    QCOMPARE(parsed.entries.size(), 2);
    QCOMPARE(parsed.categories.size(), 2);
    QCOMPARE(parsed.entries.first().lang, QStringLiteral("de"));
    // A hand-written catalogue may well say "en-GB"; the app partitions on "en".
    QCOMPARE(parsed.entries.at(1).lang, QStringLiteral("en"));
    QVERIFY(parsed.category(QStringLiteral("hamburg")) != nullptr);
    QVERIFY(parsed.category(QStringLiteral("nowhere")) == nullptr);
}

void TestCatalog::localisedCategoryNamesFallBack()
{
    catalog::Category category;
    category.name = QStringLiteral("Germany");
    category.localisedNames.insert(QStringLiteral("de"), QStringLiteral("Deutschland"));

    QCOMPARE(category.displayName(QStringLiteral("de")), QStringLiteral("Deutschland"));
    // A regional variant falls back to the base language before falling back to English.
    QCOMPARE(category.displayName(QStringLiteral("de-AT")), QStringLiteral("Deutschland"));
    QCOMPARE(category.displayName(QStringLiteral("fr")), QStringLiteral("Germany"));
    QCOMPARE(category.displayName(QString()), QStringLiteral("Germany"));
}

void TestCatalog::skipsEntriesMissingRequiredFields()
{
    // One malformed row in a remotely hosted file must not cost every user their list.
    const QByteArray json = R"({
      "version": 1,
      "feeds": [
        { "url": "https://example.invalid/a", "title": "A", "lang": "de" },
        { "url": "https://example.invalid/b", "title": "No language" },
        { "title": "No url", "lang": "de" },
        { "url": "https://example.invalid/d", "lang": "de" }
      ]
    })";

    const catalog::Catalog parsed = catalog::parseCatalog(json);
    QVERIFY2(parsed.isValid(), qPrintable(parsed.error));
    QCOMPARE(parsed.entries.size(), 1);
    QCOMPARE(parsed.entries.first().title, QStringLiteral("A"));
}

void TestCatalog::rejectsNewerFormatVersion()
{
    const QByteArray json = R"({"version": 99, "feeds":
      [{"url":"https://example.invalid/a","title":"A","lang":"de"}]})";
    const catalog::Catalog parsed = catalog::parseCatalog(json);
    QVERIFY(!parsed.isValid());
    QVERIFY2(parsed.error.contains(QStringLiteral("newer")), qPrintable(parsed.error));
}

void TestCatalog::rejectsGarbage()
{
    QVERIFY(!catalog::parseCatalog("not json").isValid());
    QVERIFY(!catalog::parseCatalog("[]").isValid());
    QVERIFY(!catalog::parseCatalog(QByteArray()).isValid());
    // A document without a version is not a catalogue we can reason about.
    QVERIFY(!catalog::parseCatalog(R"({"feeds":[]})").isValid());
}

void TestCatalog::shippedCatalogueFileIsValid()
{
    // The file that will be hosted is checked in, so a typo in it fails here rather than
    // on every user's first start.
    QFile file(QStringLiteral(MYNEWS_SOURCE_DIR "/catalog/catalog.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));

    const catalog::Catalog parsed = catalog::parseCatalog(file.readAll());
    QVERIFY2(parsed.isValid(), qPrintable(parsed.error));
    QVERIFY(parsed.entries.size() >= 10);

    for (const catalog::Entry &entry : parsed.entries) {
        QVERIFY2(entry.url.startsWith(QStringLiteral("https://")),
                 qPrintable(entry.title + QStringLiteral(": ") + entry.url));
        QVERIFY2(!entry.lang.isEmpty(), qPrintable(entry.title));
        // Every feed must point at a category that exists, or it is unreachable in the UI.
        QVERIFY2(parsed.category(entry.category) != nullptr,
                 qPrintable(entry.title + QStringLiteral(" -> ") + entry.category));
    }
}

void TestCatalog::settingsDefaults()
{
    QTemporaryDir dir;
    app::Settings settings(dir.path() + QStringLiteral("/s.ini"), nullptr);

    QVERIFY(settings.uiLanguage().isEmpty());          // follow the device
    QVERIFY(!settings.catalogEnabled());   // nothing is fetched before consent
    QCOMPARE(settings.catalogUrl(), app::Settings::defaultCatalogUrl());
    QCOMPARE(settings.retentionDays(), 14);
    // The measured clustering defaults, see CLUSTERING.md.
    QCOMPARE(settings.clusterThreshold(), 14);
    QCOMPARE(settings.clusterWindowHours(), 48);
}

void TestCatalog::settingsCatalogueIsNotContactedBeforeConsent()
{
    QTemporaryDir dir;
    app::Settings settings(dir.path() + QStringLiteral("/s.ini"), nullptr);

    // An app that contacts a third party before the user agreed has already done the
    // thing consent was meant to gate (section 10).
    QCOMPARE(settings.catalogConsent(), app::Settings::Unasked);
    QVERIFY(!settings.catalogEnabled());

    settings.setCatalogConsent(app::Settings::Denied);
    QVERIFY(!settings.catalogEnabled());

    settings.setCatalogConsent(app::Settings::Granted);
    QVERIFY(settings.catalogEnabled());

    // The prompt has to name the host it is about.
    settings.setCatalogUrl(QStringLiteral("https://catalog.example.invalid/list.json"));
    QCOMPARE(settings.catalogHost(), QStringLiteral("catalog.example.invalid"));
}

void TestCatalog::settingsResetKeepsConsentDecision()
{
    QTemporaryDir dir;
    app::Settings settings(dir.path() + QStringLiteral("/s.ini"), nullptr);

    settings.setCatalogConsent(app::Settings::Denied);
    settings.setRetentionDays(90);
    settings.resetToDefaults();

    QCOMPARE(settings.retentionDays(), 14);
    // Silently re-enabling a third-party connection the user refused is not something
    // "restore defaults" may do.
    QCOMPARE(settings.catalogConsent(), app::Settings::Denied);
}

void TestCatalog::settingsClampOutOfRangeValues()
{
    QTemporaryDir dir;
    app::Settings settings(dir.path() + QStringLiteral("/s.ini"), nullptr);

    settings.setClusterThreshold(999);
    QCOMPARE(settings.clusterThreshold(), 64);
    settings.setClusterThreshold(-5);
    QCOMPARE(settings.clusterThreshold(), 0);
    settings.setRetentionDays(0);
    QCOMPARE(settings.retentionDays(), 1);
    settings.setClusterWindowHours(0);
    QCOMPARE(settings.clusterWindowHours(), 1);
}

void TestCatalog::settingsCatalogUrlFallsBackToDefault()
{
    QTemporaryDir dir;
    app::Settings settings(dir.path() + QStringLiteral("/s.ini"), nullptr);

    settings.setCatalogUrl(QStringLiteral("https://example.invalid/catalog.json"));
    QCOMPARE(settings.catalogUrl(), QStringLiteral("https://example.invalid/catalog.json"));

    // Clearing the field in the UI must restore the default rather than break the
    // catalogue entirely.
    settings.setCatalogUrl(QString());
    QCOMPARE(settings.catalogUrl(), app::Settings::defaultCatalogUrl());
}

void TestCatalog::settingsClusterParamsFollowSettings()
{
    QTemporaryDir dir;
    app::Settings settings(dir.path() + QStringLiteral("/s.ini"), nullptr);

    settings.setClusterThreshold(10);
    settings.setClusterWindowHours(12);

    const cluster::Params params = settings.clusterParams();
    QCOMPARE(params.hammingThreshold, 10);
    QCOMPARE(params.windowSeconds, Q_INT64_C(12 * 3600));
}

void TestCatalog::settingsResetRestoresDefaults()
{
    QTemporaryDir dir;
    app::Settings settings(dir.path() + QStringLiteral("/s.ini"), nullptr);

    settings.setClusterThreshold(3);
    settings.setRetentionDays(90);

    QSignalSpy spy(&settings, &app::Settings::clusterThresholdChanged);
    settings.resetToDefaults();

    QCOMPARE(spy.size(), 1);
    QCOMPARE(settings.clusterThreshold(), 14);
    QCOMPARE(settings.retentionDays(), 14);
}

void TestCatalog::servicePrefersCacheUntilStale()
{
    QTemporaryDir dir;
    const QString cachePath = dir.path() + QStringLiteral("/catalog.json");

    const QByteArray json = R"({"version":1,"feeds":
      [{"url":"https://example.invalid/a","title":"A","lang":"de","category":"x"}]})";
    QFile file(cachePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(json);
    file.close();

    feed::FeedFetcher fetcher;
    catalog::CatalogService service(&fetcher, cachePath);

    QVERIFY(service.loadFromCache());
    QVERIFY(service.hasCatalog());
    QCOMPARE(service.catalog().entries.size(), 1);
    QVERIFY(service.lastChecked().isValid());

    // Just written, so no network call is due...
    QVERIFY(!service.needsRefresh());
    // ...but it would be a week later.
    QVERIFY(service.needsRefresh(QDateTime::currentDateTime().addDays(8)));

    // A refresh that is not due resolves immediately without touching the network.
    QSignalSpy spy(&service, &catalog::CatalogService::refreshed);
    service.refresh(app::Settings::defaultCatalogUrl());
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    QVERIFY(spy.at(0).at(1).toString().isEmpty());

    // An unusable address is reported rather than attempted.
    service.refresh(QStringLiteral("not a url"), true);
    QCOMPARE(spy.size(), 2);
    QVERIFY(!spy.at(1).at(1).toString().isEmpty());
}

int runCatalogTests(int argc, char **argv)
{
    TestCatalog test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_catalog.moc"

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
    void localisedDescriptionsFallBack();
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
    void settingsChangeNotifiesOnlyItsOwnProperty();

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

void TestCatalog::localisedDescriptionsFallBack()
{
    const QByteArray json = R"({
      "version": 1,
      "feeds": [
        { "url": "https://example.invalid/a", "title": "A", "lang": "de",
          "description": "A newspaper.", "description_de": "Eine Zeitung." }
      ]
    })";

    const catalog::Catalog parsed = catalog::parseCatalog(json);
    QVERIFY2(parsed.isValid(), qPrintable(parsed.error));

    const catalog::Entry &entry = parsed.entries.first();
    QCOMPARE(entry.displayDescription(QStringLiteral("de")), QStringLiteral("Eine Zeitung."));
    QCOMPARE(entry.displayDescription(QStringLiteral("de-AT")), QStringLiteral("Eine Zeitung."));
    QCOMPARE(entry.displayDescription(QStringLiteral("fr")), QStringLiteral("A newspaper."));
    QCOMPARE(entry.displayDescription(QString()), QStringLiteral("A newspaper."));
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
    QVERIFY(parsed.entries.size() >= 15);

    QSet<QString> urls;
    QSet<QString> usedCategories;
    for (const catalog::Entry &entry : parsed.entries) {
        const QByteArray who = entry.title.toUtf8();
        // Publishers' feeds, over TLS, and no duplicates -- a duplicate would show as two
        // identical rows the user could subscribe to twice.
        QVERIFY2(entry.url.startsWith(QStringLiteral("https://")), who.constData());
        QVERIFY2(!urls.contains(entry.url), who.constData());
        urls.insert(entry.url);

        // A tag with a region would put this feed in a partition of its own (section 7).
        QVERIFY2(!entry.lang.isEmpty(), who.constData());
        QVERIFY2(entry.lang == entry.lang.toLower() && !entry.lang.contains(QLatin1Char('-')),
                 who.constData());

        // Every feed must point at a category that exists, or it is unreachable in the UI.
        QVERIFY2(parsed.category(entry.category) != nullptr, who.constData());
        usedCategories.insert(entry.category);

        // The description is what a user picks by, so an entry without one is not ready
        // to be offered.
        QVERIFY2(!entry.description.isEmpty(), who.constData());
        QVERIFY2(!entry.displayDescription(QStringLiteral("de")).isEmpty(), who.constData());
        QVERIFY2(entry.displayDescription(QStringLiteral("de")) != entry.description,
                 qPrintable(entry.title + QStringLiteral(" has no German description")));

        // Descriptions categorise, they do not evaluate. What a source *is* -- a daily,
        // a tabloid, a city magazine, a public or private broadcaster -- is a factual
        // label. Where it stands politically, or how good it is, is not: this is a list
        // the user picks from, not one we rank, and characterising one publisher's
        // politics obliges us to characterise every publisher's. Kept as a test rather
        // than a note so the next addition cannot quietly reintroduce it.
        static const QStringList judgements{
            // political placement
            QStringLiteral("linke"),     QStringLiteral("links"),
            QStringLiteral("rechte"),    QStringLiteral("rechts"),
            QStringLiteral("konservativ"), QStringLiteral("liberal"),
            QStringLiteral("populis"),
            QStringLiteral("left-leaning"), QStringLiteral("left-wing"),
            QStringLiteral("right-leaning"), QStringLiteral("right-wing"),
            QStringLiteral("conservative"), QStringLiteral("populist"),
            // assessments of quality or standing
            QStringLiteral("seriös"),    QStringLiteral("renommiert"),
            QStringLiteral("führend"),   QStringLiteral("angesehen"),
            QStringLiteral("respected"), QStringLiteral("leading"),
            QStringLiteral("reputable"), QStringLiteral("trusted"),
            QStringLiteral("high-quality"),
        };
        for (const QString &word : judgements) {
            for (const QString &text : {entry.description,
                                        entry.displayDescription(QStringLiteral("de"))}) {
                QVERIFY2(!text.contains(word, Qt::CaseInsensitive),
                         qPrintable(entry.title + QStringLiteral(": \"") + text
                                    + QStringLiteral("\" evaluates the source (\"")
                                    + word + QStringLiteral("\")")));
            }
        }
    }

    // A category nobody uses would appear in the filter and always show an empty list.
    for (const catalog::Category &category : parsed.categories)
        QVERIFY2(usedCategories.contains(category.id), qPrintable(category.id));
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
    QCOMPARE(settings.clusterThreshold(), cluster::Params().hammingThreshold);
    QCOMPARE(settings.clusterWindowHours(), 48);
    QVERIFY(settings.clusterThreshold() >= cluster::Params::MinThreshold);
    QVERIFY(settings.clusterThreshold() <= cluster::Params::MaxThreshold);
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

    // The consent prompt shows this host, so the default has to yield a real one.
    settings.setCatalogUrl(QString());
    QVERIFY2(!settings.catalogHost().isEmpty(),
             qPrintable(app::Settings::defaultCatalogUrl()));
    QVERIFY(app::Settings::defaultCatalogUrl().startsWith(QStringLiteral("https://")));
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

    // Clamped to the range measured to behave, not to what the algorithm accepts: a
    // hand-edited configuration file must not be able to break clustering either.
    settings.setClusterThreshold(999);
    QCOMPARE(settings.clusterThreshold(), cluster::Params::MaxThreshold);
    settings.setClusterThreshold(-5);
    QCOMPARE(settings.clusterThreshold(), cluster::Params::MinThreshold);
    QVERIFY(cluster::Params::MaxThreshold < 16);   // 16 reached 18% false groupings
    settings.setRetentionDays(0);
    QCOMPARE(settings.retentionDays(), 1);
    settings.setClusterWindowHours(0);
    QCOMPARE(settings.clusterWindowHours(), cluster::Params::MinWindowHours);
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

    settings.setClusterThreshold(cluster::Params::MinThreshold);
    settings.setRetentionDays(90);

    QSignalSpy spy(&settings, &app::Settings::clusterThresholdChanged);
    settings.resetToDefaults();

    QCOMPARE(spy.size(), 1);
    QCOMPARE(settings.clusterThreshold(), cluster::Params().hammingThreshold);
    QCOMPARE(settings.retentionDays(), 14);
}

void TestCatalog::settingsChangeNotifiesOnlyItsOwnProperty()
{
    QTemporaryDir dir;
    app::Settings settings(dir.path() + QStringLiteral("/s.ini"), nullptr);

    QSignalSpy cache(&settings, &app::Settings::thumbnailCacheMegabytesChanged);
    QSignalSpy browser(&settings, &app::Settings::openArticlesExternallyChanged);
    settings.setThumbnailCacheMegabytes(200);

    QCOMPARE(cache.size(), 1);
    // Re-evaluating the external-browser binding for an unrelated change is harmless
    // today and misleading always.
    QCOMPARE(browser.size(), 0);
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

    // Nor is a well-formed address in a scheme we will not fetch. The field is free text,
    // so this is the only thing standing between it and QNetworkAccessManager.
    service.refresh(QStringLiteral("file:///etc/passwd"), true);
    QCOMPARE(spy.size(), 3);
    QVERIFY(!spy.at(2).at(1).toString().isEmpty());

    service.refresh(QStringLiteral("ftp://example.invalid/catalog.json"), true);
    QCOMPARE(spy.size(), 4);
    QVERIFY(!spy.at(3).at(1).toString().isEmpty());
}

int runCatalogTests(int argc, char **argv)
{
    TestCatalog test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_catalog.moc"

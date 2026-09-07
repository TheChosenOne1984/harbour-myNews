#include "backend.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>

#include "catalog/catalogservice.h"
#include "feed/feedfetcher.h"
#include "feed/feedparser.h"
#include "feed/feedrefresher.h"
#include "feed/opml.h"

namespace app {
namespace {

qint64 nowSeconds()
{
    // QDateTime::currentSecsSinceEpoch() only exists from Qt 5.8; Sailfish is on 5.6.
    return QDateTime::currentMSecsSinceEpoch() / 1000;
}

} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<feed::FeedRecord>>("QVector<feed::FeedRecord>");
    qRegisterMetaType<catalog::Catalog>("catalog::Catalog");
}

Backend::~Backend()
{
    delete m_store;
}

void Backend::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void Backend::initialise(const QString &databasePath, const QString &catalogCachePath)
{
    m_store = new feed::FeedStore(databasePath);
    if (!m_store->open()) {
        emit initialised(false, m_store->lastError());
        return;
    }

    m_fetcher = new feed::FeedFetcher(this);
    m_refresher = new feed::FeedRefresher(m_store, m_fetcher, this);
    m_catalog = new catalog::CatalogService(m_fetcher, catalogCachePath, this);

    connect(m_refresher, &feed::FeedRefresher::finished, this,
            [this](const feed::FeedRefresher::Summary &summary) {
        setBusy(false);
        emitFeeds();
        emit refreshFinished(summary.feedsSucceeded, summary.feedsFailed,
                             summary.articlesInserted, QString());
    });

    connect(m_catalog, &catalog::CatalogService::refreshed, this,
            [this](bool changed, const QString &error) {
        setBusy(false);
        if (changed)
            emit catalogChanged(m_catalog->catalog());
        emit catalogRefreshFinished(changed, error);
    });

    connect(m_fetcher, &feed::FeedFetcher::finished, this,
            [this](const feed::FeedFetcher::Result &result) {
        if (result.id != ProbeRequestId)
            return;
        setBusy(false);
        if (!result.isSuccess()) {
            emit feedProbed(m_probeUrl, QString(), QString(), result.error);
            return;
        }
        const feed::ParsedFeed parsed = feed::parseFeed(result.body);
        if (!parsed.isValid() && parsed.articles.isEmpty()) {
            emit feedProbed(m_probeUrl, QString(), QString(), parsed.error);
            return;
        }
        emit feedProbed(m_probeUrl, parsed.title, parsed.lang, QString());
    });

    // Whatever was cached is available immediately, before any network call.
    if (m_catalog->loadFromCache())
        emit catalogChanged(m_catalog->catalog());

    emit initialised(true, QString());
    emitFeeds();
}

void Backend::emitFeeds()
{
    if (m_store)
        emit feedsChanged(m_store->feeds());
}

void Backend::reloadFeeds()
{
    emitFeeds();
}

void Backend::refreshFeeds(int hammingThreshold, int windowHours)
{
    if (!m_refresher || m_busy)
        return;
    if (m_store->feeds(true).isEmpty()) {
        emit refreshFinished(0, 0, 0, QString());
        return;
    }

    cluster::Params params;
    params.hammingThreshold = hammingThreshold;
    params.windowSeconds = static_cast<qint64>(windowHours) * 3600;
    m_refresher->setClusterParams(params);

    setBusy(true);
    m_refresher->refreshDue(nowSeconds());
}

void Backend::refreshCatalog(const QString &url, bool force)
{
    if (!m_catalog || m_busy)
        return;
    setBusy(true);
    m_catalog->refresh(url, force);
}

void Backend::subscribe(const QString &url, const QString &title, const QString &lang,
                        const QString &langSource)
{
    if (!m_store)
        return;
    if (m_store->addFeed(url, title, lang, langSource) < 0) {
        emit message(m_store->lastError());
        return;
    }
    emitFeeds();
}

void Backend::unsubscribe(qint64 feedId)
{
    if (m_store && m_store->removeFeed(feedId))
        emitFeeds();
}

void Backend::setFeedEnabled(qint64 feedId, bool enabled)
{
    if (m_store && m_store->setFeedEnabled(feedId, enabled))
        emitFeeds();
}

void Backend::setFeedLanguage(qint64 feedId, const QString &lang)
{
    // Anything the user picks is manual, and manual wins over every later refresh (§7).
    if (m_store && m_store->setFeedLanguage(feedId, lang, QStringLiteral("manual")))
        emitFeeds();
}

void Backend::probeFeed(const QString &url)
{
    if (!m_fetcher || m_busy)
        return;
    m_probeUrl = url;

    feed::FeedFetcher::Request request;
    request.id = ProbeRequestId;
    request.url = QUrl(url);
    setBusy(true);
    m_fetcher->enqueue(request);
}

void Backend::importOpml(const QByteArray &xml, const QString &fallbackLang)
{
    if (!m_store)
        return;

    const feed::OpmlDocument document = feed::parseOpml(xml);
    if (!document.isValid()) {
        emit opmlImported(0, 0, document.error);
        return;
    }

    int added = 0;
    int skipped = 0;
    for (const feed::OpmlEntry &entry : document.entries) {
        // OPML carries no language of its own unless we wrote the file. Import never
        // stops to ask: the entries left with a guessed language are marked
        // "declared" and the subscriptions page flags them for review (§7).
        const QString lang = entry.lang.isEmpty() ? fallbackLang : entry.lang;
        const qint64 id = m_store->addFeed(entry.url, entry.title, lang,
                                           QStringLiteral("declared"));
        if (id < 0)
            ++skipped;
        else
            ++added;
    }
    emitFeeds();
    emit opmlImported(added, skipped, QString());
}

void Backend::exportOpml(const QString &filePath)
{
    if (!m_store)
        return;

    const QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        emit opmlExported(filePath, tr("Cannot create %1").arg(dir.absolutePath()));
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit opmlExported(filePath, file.errorString());
        return;
    }
    file.write(feed::writeOpml(m_store->feeds()));
    file.close();
    emit opmlExported(filePath, QString());
}

void Backend::purge(int retentionDays)
{
    if (!m_store)
        return;
    const qint64 cutoff = nowSeconds() - static_cast<qint64>(retentionDays) * 24 * 3600;
    emit purged(m_store->purgeArticlesOlderThan(cutoff));
}

} // namespace app

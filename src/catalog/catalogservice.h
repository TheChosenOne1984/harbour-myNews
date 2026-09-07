#ifndef CATALOG_CATALOGSERVICE_H
#define CATALOG_CATALOGSERVICE_H

#include <QDateTime>
#include <QObject>
#include <QString>

#include "catalog/catalog.h"
#include "feed/feedfetcher.h"

namespace catalog {

/*!
 * \brief Fetches the catalogue and keeps a local copy.
 *
 * Nothing is bundled (section 10): the catalogue is fetched on first start, cached on
 * disk, and refreshed on a slow cadence with If-Modified-Since. The cache is what keeps
 * an unreachable host from being fatal beyond the very first run.
 *
 * Reuses FeedFetcher rather than opening its own connection, so the catalogue request
 * carries the same honest User-Agent, the same timeouts and the same size cap as every
 * request to a publisher.
 */
class CatalogService : public QObject
{
    Q_OBJECT

public:
    /*! How long a cached catalogue is considered fresh. */
    static constexpr qint64 RefreshIntervalSeconds = 7 * 24 * 3600;

    /*! Request id handed to the fetcher; distinct from any feed id. */
    static constexpr qint64 RequestId = -1000;

    CatalogService(feed::FeedFetcher *fetcher, const QString &cacheFilePath,
                   QObject *parent = nullptr);

    /*! The last successfully parsed catalogue, from cache or network. */
    Catalog catalog() const { return m_catalog; }
    bool hasCatalog() const { return m_catalog.isValid(); }

    /*! Reads the cached copy. Call once at startup, before refresh(). */
    bool loadFromCache();

    /*!
     * When the catalogue was last confirmed current -- written, or answered 304.
     *
     * Recorded alongside the cache rather than taken from the file's modification time,
     * which a backup or a file sync can change without the content being any newer.
     */
    QDateTime lastChecked() const;

    /*! True when there is no cache or it is older than RefreshIntervalSeconds. */
    bool needsRefresh(const QDateTime &now = QDateTime::currentDateTime()) const;

    /*!
     * Fetches the catalogue from \a url.
     *
     * \a force ignores the refresh interval, for an explicit user action. Does nothing
     * and reports success when a fresh cache is already loaded and \a force is false.
     */
    void refresh(const QString &url, bool force = false);

signals:
    /*! \a error is empty on success, including when a fresh cache made the fetch a no-op. */
    void refreshed(bool changed, const QString &error);

private slots:
    void onFetchFinished(const feed::FeedFetcher::Result &result);

private:
    bool writeCache(const QByteArray &json);
    void recordChecked();

    feed::FeedFetcher *m_fetcher;
    QString m_cachePath;
    Catalog m_catalog;
    QString m_etag;
    QString m_lastModified;
    bool m_requestInFlight = false;
};

} // namespace catalog

#endif // CATALOG_CATALOGSERVICE_H

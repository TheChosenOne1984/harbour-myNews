#ifndef APP_BACKEND_H
#define APP_BACKEND_H

#include <QObject>
#include <QString>
#include <QVector>

#include "catalog/catalog.h"
#include "cluster/clusterer.h"
#include "feed/feedstore.h"

namespace feed {
class FeedFetcher;
class FeedRefresher;
}
namespace catalog { class CatalogService; }
namespace image {
class ThumbnailCache;
class ThumbnailService;
}

namespace app {

/*!
 * \brief Everything that must not run on the GUI thread.
 *
 * Section 3: all fetching, parsing and clustering happens off the GUI thread. This object
 * is moved to a worker thread by AppController and only ever reached through queued
 * signals — no method here may be called directly from the GUI thread.
 *
 * It owns the writing FeedStore. SQLite connections cannot cross threads, which is why
 * the models on the GUI side are handed their rows instead of querying.
 */
class Backend : public QObject
{
    Q_OBJECT

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

public slots:
    /*! Opens the database and reads the cached catalogue. Must run on the worker thread. */
    void initialise(const QString &databasePath, const QString &catalogCachePath,
                    const QString &thumbnailDirectory);

    void reloadFeeds();

    /*! Re-reads the article list under the current filter, from the first page. */
    void reloadClusters(int limit);

    /*!
     * Reads the page after the ones already shown.
     *
     * Paging rather than one configurable ceiling: a row costs about three kilobytes, so
     * a fortnight of twenty feeds would be tens of megabytes held for articles nobody has
     * scrolled to -- and a number a reader can set wrong is a way to make the app behave
     * badly. This way memory follows what was actually read.
     */
    void loadMoreClusters();

    /*! Replaces the filter and re-reads. */
    void setClusterFilter(bool unreadOnly, const QString &category, qint64 feedId,
                          qint64 maxAgeSeconds);

    void markClusterRead(qint64 clusterId, bool read);
    void markAllRead();

    void refreshFeeds(int hammingThreshold, int windowHours);
    void refreshCatalog(const QString &url, bool force);

    void subscribe(const QString &url, const QString &title, const QString &lang,
                   const QString &langSource, const QString &category);
    void unsubscribe(qint64 feedId);
    void setFeedEnabled(qint64 feedId, bool enabled);
    void setFeedLanguage(qint64 feedId, const QString &lang);

    /*!
     * Fetches a URL once and reports what it is, so the add-feed page can pre-fill the
     * language picker instead of asking a question the user may not be able to answer.
     */
    void probeFeed(const QString &url);

    void importOpml(const QByteArray &xml, const QString &fallbackLang);
    void exportOpml(const QString &filePath);

    /*! Retention (§4). Cutoff is computed by the caller from the setting. */
    void purge(int retentionDays);

    /*! Loads the members of one cluster so the detail view can offer the other sources. */
    void loadClusterMembers(qint64 clusterId);

    /*! Asked for when a row becomes visible -- never for a whole batch (section 8). */
    void requestThumbnail(qint64 articleId);

    void setThumbnailCacheLimit(qint64 bytes);

signals:
    void initialised(bool ok, const QString &error);
    void feedsChanged(const QVector<feed::FeedRecord> &feeds);
    void clustersChanged(const QVector<feed::ClusterSummary> &clusters);
    void clustersAppended(const QVector<feed::ClusterSummary> &clusters);
    void hasMoreClustersChanged(bool hasMore);
    void subscribedCategoriesChanged(const QStringList &categories);
    void unreadCountChanged(int count);

    /*! The members of one cluster, for the "other sources" list. */
    void clusterMembersLoaded(qint64 clusterId, const QVector<feed::ArticleRecord> &members);
    void catalogChanged(const catalog::Catalog &catalog);

    void busyChanged(bool busy);
    void refreshFinished(int feedsOk, int feedsFailed, int articlesAdded,
                         const QString &error);
    void catalogRefreshFinished(bool changed, const QString &error);

    /*! \a lang is what the document declares, empty when it declares nothing. */
    void feedProbed(const QString &url, const QString &title, const QString &lang,
                    const QString &error);

    void opmlImported(int added, int skipped, const QString &error);
    void opmlExported(const QString &filePath, const QString &error);
    void purged(int articlesDeleted);
    void thumbnailReady(qint64 articleId, const QString &path);

    void message(const QString &text);

private:
    void setBusy(bool busy);
    void emitFeeds();
    void emitClusters();

    /*! Rows per page. Roughly a dozen screenfuls, so reaching the end is uncommon. */
    static constexpr int PageSize = 200;

    int m_clusterLimit = PageSize;
    int m_loadedClusters = 0;
    bool m_hasMoreClusters = false;
    feed::ClusterFilter m_filter;

    feed::FeedStore *m_store = nullptr;
    feed::FeedFetcher *m_fetcher = nullptr;
    feed::FeedRefresher *m_refresher = nullptr;
    catalog::CatalogService *m_catalog = nullptr;
    image::ThumbnailCache *m_thumbnails = nullptr;
    image::ThumbnailService *m_thumbnailService = nullptr;

    /*! Request id used for one-off probe fetches; distinct from feed ids. */
    static constexpr qint64 ProbeRequestId = -2000;
    QString m_probeUrl;
    bool m_busy = false;
};

} // namespace app

Q_DECLARE_METATYPE(QVector<feed::FeedRecord>)
Q_DECLARE_METATYPE(QVector<feed::ClusterSummary>)
Q_DECLARE_METATYPE(QVector<feed::ArticleRecord>)
Q_DECLARE_METATYPE(catalog::Catalog)

#endif // APP_BACKEND_H

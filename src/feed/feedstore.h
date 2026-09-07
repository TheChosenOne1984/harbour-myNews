#ifndef FEED_FEEDSTORE_H
#define FEED_FEEDSTORE_H

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include "cluster/clusterer.h"
#include "feed/feedparser.h"

namespace feed {

/*! A subscription, as stored. */
struct FeedRecord
{
    qint64 id = -1;
    QString url;
    QString title;
    QString siteUrl;
    QString lang;
    /*!
     * Where feed.lang came from: "catalogue" | "declared" | "manual".
     *
     * There is no "detected": section 7 assigns the language rather than guessing it.
     * "manual" wins over every later refresh.
     */
    QString langSource;
    QString etag;            //!< for If-None-Match
    QString lastModified;    //!< for If-Modified-Since
    qint64 lastFetch = 0;
    int lastStatus = 0;
    bool enabled = true;

    //! Not in the plan's schema, but section 5 requires per-feed exponential backoff,
    //! and backoff state has to survive a restart to mean anything.
    int failureCount = 0;
    qint64 nextAttempt = 0;
};

/*! An article joined with what the UI needs to show it. */
struct ArticleRecord
{
    qint64 id = -1;
    qint64 feedId = -1;
    QString guid;
    QString link;
    QString title;
    QString teaser;
    qint64 published = 0;
    qint64 firstSeen = 0;
    QString imageUrl;
    QString imageState;      //!< "none" | "pending" | "cached" | "failed"
    QString lang;
    quint64 simhash = 0;
    qint64 clusterId = -1;
    bool read = false;
};

/*! A cluster as the feed list needs it. */
struct ClusterRecord
{
    qint64 id = -1;
    qint64 leadArticleId = -1;
    QString lang;
    qint64 published = 0;    //!< newest member, which is what the list sorts on
    int memberCount = 0;
};

/*!
 * \brief A cluster with everything the article list shows, in one row.
 *
 * Assembled by the store rather than by the model walking members per row: the feed page
 * shows a page of clusters at a time, and a query per row would be a query per scroll.
 */
struct ClusterSummary
{
    qint64 clusterId = -1;
    qint64 leadArticleId = -1;
    QString title;
    QString teaser;
    QString link;
    QString imageUrl;
    QString imageState;
    qint64 published = 0;      //!< newest member, which is what the list sorts on
    int memberCount = 0;
    bool unread = false;       //!< true while any member is unread
    QStringList sources;       //!< feed titles, the lead article's first
};

/*! What one ingest run changed, for logging and for the UI's refresh feedback. */
struct IngestResult
{
    int inserted = 0;
    int updated = 0;     //!< already known, title changed
    int unchanged = 0;
    int clustersCreated = 0;
    int clustersJoined = 0;

    int seen() const { return inserted + updated + unchanged; }
};

/*!
 * \brief SQLite persistence for feeds, articles and clusters.
 *
 * Schema follows section 4 of the plan. Owns one QSqlDatabase connection and is **not**
 * thread-safe: SQLite connections cannot be shared across threads, so each worker
 * thread must construct its own FeedStore against the same file.
 *
 * All queries are parameter-bound. Nothing here touches the GUI.
 */
class FeedStore
{
public:
    explicit FeedStore(const QString &databasePath,
                       const QString &connectionName = QString());
    ~FeedStore();

    FeedStore(const FeedStore &) = delete;
    FeedStore &operator=(const FeedStore &) = delete;

    /*! Opens the file, creates or migrates the schema. */
    bool open();
    void close();
    bool isOpen() const;

    QString lastError() const { return m_lastError; }

    /*! Current schema version, from PRAGMA user_version. */
    int schemaVersion() const;

    /*! True when the SQLite build supports FTS5, checked by trying it once. */
    bool hasFullTextSearch() const { return m_hasFts; }

    // --- Feeds -------------------------------------------------------------------

    /*!
     * Inserts, or returns the existing id when the URL is already subscribed.
     *
     * \a lang should always be given (section 7): it is a partition predicate for
     * clustering, and a feed without one only ever clusters with other feeds that have
     * none. The catalogue supplies it, and the add-feed flow pre-fills it.
     */
    qint64 addFeed(const QString &url, const QString &title = QString(),
                   const QString &lang = QString(),
                   const QString &langSource = QStringLiteral("declared"));
    bool removeFeed(qint64 feedId);
    QVector<FeedRecord> feeds(bool enabledOnly = false) const;
    bool feedById(qint64 feedId, FeedRecord *out) const;

    /*! Records the metadata a successful fetch produced. */
    bool updateFeedMetadata(qint64 feedId, const ParsedFeed &parsed);

    /*! Stores conditional-GET validators and clears the backoff. */
    bool recordFetchSuccess(qint64 feedId, int httpStatus, const QString &etag,
                            const QString &lastModified, qint64 now);

    /*!
     * Records a failure and schedules the next attempt.
     *
     * Exponential with a cap, so a feed that has moved away is retried daily rather than
     * every refresh, and a publisher that returns 429 is not hammered.
     */
    bool recordFetchFailure(qint64 feedId, int httpStatus, qint64 now);

    /*! Feeds that are enabled and whose backoff has expired. */
    QVector<FeedRecord> feedsDueForFetch(qint64 now) const;

    bool setFeedLanguage(qint64 feedId, const QString &lang, const QString &source);
    bool setFeedEnabled(qint64 feedId, bool enabled);

    // --- Articles ----------------------------------------------------------------

    /*!
     * \brief Stores what a parse produced, deduplicating and clustering.
     *
     * Section 5: an article already stored is updated rather than re-inserted, and is
     * only re-clustered when its title changed. Runs in a single transaction.
     */
    IngestResult ingest(qint64 feedId, const QVector<ParsedArticle> &articles, qint64 now,
                        const cluster::Params &params = cluster::Params());

    /*! Clusters newest first -- the feed page's model. */
    QVector<ClusterRecord> clusters(int limit = 100, int offset = 0) const;

    /*! The same, with the lead article and contributing sources joined in. */
    QVector<ClusterSummary> clusterSummaries(int limit = 100, int offset = 0) const;
    bool clusterById(qint64 clusterId, ClusterRecord *out) const;

    QVector<ArticleRecord> articlesInCluster(qint64 clusterId) const;

    /*! Every article id, so the thumbnail cache can drop files for purged articles. */
    QVector<qint64> allArticleIds() const;
    bool articleById(qint64 articleId, ArticleRecord *out) const;
    bool markRead(qint64 articleId, bool read = true);
    int markAllRead();
    int unreadCount() const;

    bool setImageState(qint64 articleId, const QString &state);

    /*! Records an image URL discovered from the article page, so a later run can fetch
     *  the picture directly instead of reading the page again. */
    bool setImageUrl(qint64 articleId, const QString &url);

    // --- Retention ---------------------------------------------------------------

    /*!
     * Deletes articles older than the cutoff, then the clusters left without members,
     * and repairs the lead article of the clusters that lost theirs.
     *
     * \return number of articles deleted.
     */
    int purgeArticlesOlderThan(qint64 cutoff);

    // --- Search ------------------------------------------------------------------

    /*! Trigram FTS5 search over title and teaser. Empty when FTS5 is unavailable. */
    QVector<ArticleRecord> search(const QString &text, int limit = 100) const;

private:
    bool createSchema();
    bool migrate(int fromVersion);
    bool detectFullTextSearch();
    bool exec(const QString &sql);
    void setError(const QString &context, const QString &detail) const;

    /*! Recomputes lead article, published and member_count for one cluster. */
    bool refreshCluster(qint64 clusterId);

    QString m_path;
    QString m_connectionName;
    QSqlDatabase m_db;
    mutable QString m_lastError;
    bool m_hasFts = false;
};

} // namespace feed

#endif // FEED_FEEDSTORE_H

#ifndef FEED_FEEDREFRESHER_H
#define FEED_FEEDREFRESHER_H

#include <QObject>
#include <QSet>
#include <QtGlobal>

#include "cluster/clusterer.h"
#include "feed/feedfetcher.h"
#include "feed/feedstore.h"

namespace feed {

/*!
 * \brief Drives one refresh: fetch, parse, store.
 *
 * The three pieces are deliberately separate and individually testable; this is the only
 * place that knows the order they go in. It exists so the CLI tool and the app's refresh
 * action share one implementation rather than each growing their own.
 *
 * Asynchronous throughout, and it never touches the GUI. The store it is given must
 * belong to the same thread.
 */
class FeedRefresher : public QObject
{
    Q_OBJECT

public:
    struct Summary
    {
        int feedsAttempted = 0;
        int feedsSucceeded = 0;
        int feedsUnchanged = 0;   //!< answered 304
        int feedsFailed = 0;
        int articlesInserted = 0;
        int articlesUpdated = 0;
        int clustersCreated = 0;
        int clustersJoined = 0;
    };

    FeedRefresher(FeedStore *store, FeedFetcher *fetcher, QObject *parent = nullptr);

    void setClusterParams(const cluster::Params &params) { m_params = params; }
    cluster::Params clusterParams() const { return m_params; }

    /*! Refreshes every enabled feed whose backoff has expired. */
    void refreshDue(qint64 now);

    /*! Refreshes one feed regardless of its backoff -- for an explicit user action. */

    Summary summary() const { return m_summary; }

signals:
    /*! One feed done. \a error is empty on success, including on 304. */
    void feedFinished(qint64 feedId, const feed::IngestResult &result, const QString &error);

    void finished(const feed::FeedRefresher::Summary &summary);

private slots:
    void onFetchFinished(const feed::FeedFetcher::Result &result);

private:
    void startRequests(const QVector<FeedRecord> &feeds, qint64 now);

    FeedStore *m_store;
    FeedFetcher *m_fetcher;
    cluster::Params m_params;
    QSet<qint64> m_outstanding;
    Summary m_summary;
    qint64 m_now = 0;
};

} // namespace feed

Q_DECLARE_METATYPE(feed::FeedRefresher::Summary)
Q_DECLARE_METATYPE(feed::IngestResult)

#endif // FEED_FEEDREFRESHER_H

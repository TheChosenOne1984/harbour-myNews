#include "feedrefresher.h"

#include "feed/feedparser.h"

namespace feed {

FeedRefresher::FeedRefresher(FeedStore *store, FeedFetcher *fetcher, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_fetcher(fetcher)
{
    qRegisterMetaType<feed::IngestResult>("feed::IngestResult");
    qRegisterMetaType<feed::FeedRefresher::Summary>("feed::FeedRefresher::Summary");
    connect(m_fetcher, &FeedFetcher::finished, this, &FeedRefresher::onFetchFinished);
}

void FeedRefresher::refreshDue(qint64 now)
{
    startRequests(m_store->feedsDueForFetch(now), now);
}


void FeedRefresher::startRequests(const QVector<FeedRecord> &feeds, qint64 now)
{
    m_now = now;
    m_summary = Summary();
    m_summary.feedsAttempted = feeds.size();

    if (feeds.isEmpty()) {
        emit finished(m_summary);
        return;
    }

    QVector<FeedFetcher::Request> requests;
    requests.reserve(feeds.size());
    for (const FeedRecord &feed : feeds) {
        m_outstanding.insert(feed.id);
        FeedFetcher::Request request;
        request.id = feed.id;
        request.url = QUrl(feed.url);
        // Conditional GET: send back whatever the publisher gave us last time.
        request.etag = feed.etag;
        request.lastModified = feed.lastModified;
        requests.append(request);
    }
    m_fetcher->enqueue(requests);
}

void FeedRefresher::onFetchFinished(const FeedFetcher::Result &result)
{
    if (!m_outstanding.remove(result.id))
        return;   // not ours; another refresher shares the fetcher

    IngestResult ingested;
    QString error;

    if (result.notModified) {
        // Nothing changed. Still a success, so the backoff is cleared.
        ++m_summary.feedsUnchanged;
        ++m_summary.feedsSucceeded;
        m_store->recordFetchSuccess(result.id, result.httpStatus, result.etag,
                                    result.lastModified, m_now);
    } else if (!result.isSuccess()) {
        error = result.error;
        ++m_summary.feedsFailed;
        m_store->recordFetchFailure(result.id, result.httpStatus, m_now);
    } else {
        const ParsedFeed parsed = parseFeed(result.body);
        if (parsed.articles.isEmpty() && !parsed.isValid()) {
            // A document we cannot read is a failure of this feed, not of the refresh:
            // it backs off like any other error rather than being retried every time.
            error = parsed.error;
            ++m_summary.feedsFailed;
            m_store->recordFetchFailure(result.id, result.httpStatus, m_now);
        } else {
            m_store->updateFeedMetadata(result.id, parsed);
            ingested = m_store->ingest(result.id, parsed.articles, m_now, m_params,
                                       m_maxArticleAge);

            m_summary.articlesInserted += ingested.inserted;
            m_summary.articlesUpdated += ingested.updated;
            m_summary.clustersCreated += ingested.clustersCreated;
            m_summary.clustersJoined += ingested.clustersJoined;
            ++m_summary.feedsSucceeded;

            m_store->recordFetchSuccess(result.id, result.httpStatus, result.etag,
                                        result.lastModified, m_now);
            // A partially readable document still counts as a success, but the reason it
            // was partial is worth surfacing.
            if (!parsed.error.isEmpty())
                error = parsed.error;
        }
    }

    emit feedFinished(result.id, ingested, error);

    if (m_outstanding.isEmpty())
        emit finished(m_summary);
}

} // namespace feed

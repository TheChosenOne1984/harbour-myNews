#ifndef IMAGE_THUMBNAILSERVICE_H
#define IMAGE_THUMBNAILSERVICE_H

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QUrl>

#include "feed/feedfetcher.h"
#include "image/thumbnailcache.h"

namespace feed { class FeedStore; }

namespace image {

/*!
 * \brief Gets a thumbnail for an article, and only when one is actually wanted.
 *
 * Section 8 is explicit that this is lazy: a request arrives when a row becomes visible,
 * never for a whole batch. Fetching the article page of 200 freshly ingested articles to
 * look for an og:image would be a burst of requests at every publisher for pictures
 * nobody has scrolled to.
 *
 * Each article is attempted at most once. The outcome, success or failure, is written to
 * article.image_state so a restart does not try again.
 *
 * Two shapes of work:
 *  - the feed gave an image URL: fetch it and store it
 *  - it did not: fetch the article page, read its og:image, then fetch that
 */
class ThumbnailService : public QObject
{
    Q_OBJECT

public:
    ThumbnailService(feed::FeedStore *store, feed::FeedFetcher *fetcher,
                     ThumbnailCache *cache, QObject *parent = nullptr);

    /*! Request ids handed to the fetcher live in their own range. */
    static constexpr qint64 ImageRequestBase = -1000000;
    static constexpr qint64 PageRequestBase = -2000000;

    /*! How many articles may be in flight at once. */
    static constexpr int MaxInFlight = 3;

public slots:
    /*! Called when a row becomes visible. Cheap and idempotent. */
    void request(qint64 articleId);

    /*! Forgets in-flight work, e.g. when the list is replaced. */
    void cancelPending();

signals:
    void thumbnailReady(qint64 articleId, const QString &path);
    void thumbnailFailed(qint64 articleId);

private slots:
    void onFetchFinished(const feed::FeedFetcher::Result &result);

private:
    struct Job
    {
        qint64 articleId = -1;
        QUrl imageUrl;
        QUrl articleUrl;
        bool triedPage = false;   //!< the article page has already been read for og:image
    };

    void pump();
    void startImageFetch(const Job &job);
    void startPageFetch(const Job &job);
    void finish(qint64 articleId, const QString &path);
    void fail(qint64 articleId);

    feed::FeedStore *m_store;
    feed::FeedFetcher *m_fetcher;
    ThumbnailCache *m_cache;

    QQueue<Job> m_queue;
    QHash<qint64, Job> m_inFlight;     //!< keyed by article id
    QSet<qint64> m_attempted;          //!< within this session, so a retry loop cannot form
};

} // namespace image

#endif // IMAGE_THUMBNAILSERVICE_H

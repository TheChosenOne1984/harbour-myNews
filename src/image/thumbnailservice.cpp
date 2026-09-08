#include "thumbnailservice.h"

#include "feed/feedstore.h"
#include "image/ogimage.h"

namespace image {

ThumbnailService::ThumbnailService(feed::FeedStore *store, feed::FeedFetcher *fetcher,
                                   ThumbnailCache *cache, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_fetcher(fetcher)
    , m_cache(cache)
{
    connect(m_fetcher, &feed::FeedFetcher::finished,
            this, &ThumbnailService::onFetchFinished);
}

void ThumbnailService::request(qint64 articleId)
{
    if (articleId < 0 || m_attempted.contains(articleId) || m_inFlight.contains(articleId))
        return;

    // Already on disk: answer immediately and mark it recently used so eviction takes it
    // last.
    if (m_cache->contains(articleId)) {
        m_cache->touch(articleId);
        emit thumbnailReady(articleId, m_cache->pathFor(articleId));
        return;
    }

    feed::ArticleRecord article;
    if (!m_store->articleById(articleId, &article))
        return;

    // Section 8: the result of a previous attempt is persisted, so a picture that is not
    // there is looked for once and not on every scroll past the row.
    if (article.imageState == QLatin1String("failed"))
        return;

    m_attempted.insert(articleId);

    Job job;
    job.articleId = articleId;
    job.imageUrl = QUrl(article.imageUrl);
    job.articleUrl = QUrl(article.link);
    if (!job.imageUrl.isValid() || job.imageUrl.host().isEmpty())
        job.imageUrl = QUrl();

    m_queue.enqueue(job);
    pump();
}

void ThumbnailService::cancelPending()
{
    m_queue.clear();
}

void ThumbnailService::pump()
{
    while (!m_queue.isEmpty() && m_inFlight.size() < MaxInFlight) {
        const Job job = m_queue.dequeue();
        if (job.imageUrl.isEmpty())
            startPageFetch(job);
        else
            startImageFetch(job);
    }
}

void ThumbnailService::startImageFetch(const Job &job)
{
    m_inFlight.insert(job.articleId, job);
    feed::FeedFetcher::Request request;
    request.id = ImageRequestBase - job.articleId;
    request.url = job.imageUrl;
    // A page of thumbnails comes from one CDN; serialising them with a gap would make a
    // screenful take the better part of a minute.
    request.policy = feed::FeedFetcher::Policy::Parallel;
    m_fetcher->enqueue(request);
}

void ThumbnailService::startPageFetch(const Job &job)
{
    if (!job.articleUrl.isValid() || job.articleUrl.host().isEmpty()) {
        fail(job.articleId);
        return;
    }
    Job pageJob = job;
    pageJob.triedPage = true;
    m_inFlight.insert(job.articleId, pageJob);

    feed::FeedFetcher::Request request;
    request.id = PageRequestBase - job.articleId;
    request.url = job.articleUrl;
    // The article page is a request to the publisher's own site, not to a CDN, so it
    // keeps section 5's manners.
    m_fetcher->enqueue(request);
}

void ThumbnailService::onFetchFinished(const feed::FeedFetcher::Result &result)
{
    const bool isImage = result.id <= ImageRequestBase && result.id > PageRequestBase;
    const bool isPage = result.id <= PageRequestBase;
    if (!isImage && !isPage)
        return;   // a feed or the catalogue; the fetcher is shared

    const qint64 articleId =
        (isImage ? ImageRequestBase : PageRequestBase) - result.id;

    const auto it = m_inFlight.find(articleId);
    if (it == m_inFlight.end())
        return;
    const Job job = it.value();
    m_inFlight.erase(it);

    if (!result.isSuccess() || result.body.isEmpty()) {
        fail(articleId);
        pump();
        return;
    }

    if (isImage) {
        const QString path = m_cache->store(articleId, result.body);
        if (path.isEmpty())
            fail(articleId);
        else
            finish(articleId, path);
        pump();
        return;
    }

    // The article page came back; look for the picture the publisher declared for it.
    const QString found = extractPreviewImage(result.body, job.articleUrl);
    if (found.isEmpty()) {
        fail(articleId);
        pump();
        return;
    }

    // Remember it, so a later run fetches the image directly instead of the page again.
    m_store->setImageUrl(articleId, found);

    Job imageJob = job;
    imageJob.imageUrl = QUrl(found);
    startImageFetch(imageJob);
    pump();
}

void ThumbnailService::finish(qint64 articleId, const QString &path)
{
    m_store->setImageState(articleId, QStringLiteral("cached"));
    emit thumbnailReady(articleId, path);
}

void ThumbnailService::fail(qint64 articleId)
{
    // Persisted, so the attempt is not repeated after a restart (section 8).
    m_store->setImageState(articleId, QStringLiteral("failed"));
    emit thumbnailFailed(articleId);
}

} // namespace image

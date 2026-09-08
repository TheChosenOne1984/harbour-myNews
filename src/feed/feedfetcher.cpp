#include "feedfetcher.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariant>

namespace feed {
namespace {

const char *const AppVersion = "0.1.0";
const char *const ProjectUrl = "https://github.com/kaiwiechers/myNews";

QString hostKey(const QUrl &url)
{
    return url.host().toLower();
}

} // namespace

bool isFetchableUrl(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty())
        return false;
    const QString scheme = url.scheme();
    return scheme == QLatin1String("https") || scheme == QLatin1String("http");
}

bool isFetchableUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    return !trimmed.isEmpty() && isFetchableUrl(QUrl(trimmed));
}

FeedFetcher::FeedFetcher(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_userAgent(defaultUserAgent())
{
    qRegisterMetaType<feed::FeedFetcher::Result>("feed::FeedFetcher::Result");
}

FeedFetcher::~FeedFetcher()
{
    abortAll();
}

QString FeedFetcher::defaultUserAgent()
{
    return QStringLiteral("myNews/%1 (+%2)")
        .arg(QLatin1String(AppVersion), QLatin1String(ProjectUrl));
}

void FeedFetcher::setUserAgent(const QString &userAgent)
{
    if (!userAgent.isEmpty())
        m_userAgent = userAgent;
}

void FeedFetcher::enqueue(const Request &request)
{
    if (!isFetchableUrl(request.url)) {
        Result result;
        result.id = request.id;
        result.url = request.url;
        result.error = QStringLiteral("refusing to fetch %1").arg(
            request.url.scheme().isEmpty() ? QStringLiteral("an invalid URL")
                                           : QStringLiteral("a %1 URL").arg(request.url.scheme()));
        emit finished(result);
        return;
    }
    if (request.policy == Policy::Parallel)
        m_parallelQueue.enqueue(request);
    else
        m_queues[hostKey(request.url)].enqueue(request);
    // Queue draining is deferred so that enqueueing a batch does not start the first
    // request before the caller has finished connecting to the signals.
    QTimer::singleShot(0, this, [this]() { pumpQueues(); });
}

void FeedFetcher::enqueue(const QVector<Request> &requests)
{
    for (const Request &request : requests)
        enqueue(request);
}

int FeedFetcher::pending() const
{
    int count = m_inFlight.size() + m_parallelQueue.size();
    for (auto it = m_queues.constBegin(); it != m_queues.constEnd(); ++it)
        count += it.value().size();
    return count;
}

bool FeedFetcher::isIdle() const
{
    return pending() == 0;
}

void FeedFetcher::pumpQueues()
{
    // Parallel work first: it is what a reader is waiting for on screen, and it does not
    // hold a host slot that a feed refresh might want.
    while (!m_parallelQueue.isEmpty() && m_parallelInFlight < MaxParallel) {
        ++m_parallelInFlight;
        start(m_parallelQueue.dequeue());
    }

    for (auto it = m_queues.begin(); it != m_queues.end(); ++it) {
        if (m_inFlight.size() >= MaxConcurrent)
            return;
        const QString host = it.key();
        // One at a time per host, and only once the cooldown from the previous request
        // to that host has elapsed.
        if (it.value().isEmpty() || m_busyHosts.contains(host))
            continue;
        m_busyHosts.insert(host);
        start(it.value().dequeue());
    }

    if (isIdle())
        emit allFinished();
}

void FeedFetcher::start(const Request &request)
{
    QNetworkRequest networkRequest(request.url);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    networkRequest.setRawHeader("Accept",
                                "application/rss+xml, application/atom+xml, "
                                "application/xml;q=0.9, text/xml;q=0.9, */*;q=0.8");
    // Accept-Encoding is deliberately NOT set here. Qt already sends it and decompresses
    // the response transparently -- but only while it owns the header. Setting it by hand
    // makes QNetworkAccessManager hand back the raw gzip bytes instead, and every feed
    // then fails to parse with "Encountered incorrectly encoded content". Found by
    // running the ingestion against real feeds: 14 of 15 publishers gzip their output.

    // Conditional GET on every refresh (section 5). A feed that has not changed then
    // costs one 304 instead of a full document.
    if (!request.etag.isEmpty())
        networkRequest.setRawHeader("If-None-Match", request.etag.toUtf8());
    if (!request.lastModified.isEmpty())
        networkRequest.setRawHeader("If-Modified-Since", request.lastModified.toUtf8());

    networkRequest.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    networkRequest.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, false);

    QNetworkReply *reply = m_network->get(networkRequest);

    InFlight entry;
    entry.request = request;

    // Qt 5.6 has no total-request timeout: QNetworkRequest only grew one in 5.15, and
    // its transfer timeout only covers stalls. A plain timer is the portable way to keep
    // a slow server from holding a slot open forever.
    entry.timeout = new QTimer(reply);
    entry.timeout->setSingleShot(true);
    entry.timeout->setInterval(TotalTimeoutMs);
    connect(entry.timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    entry.timeout->start();

    m_inFlight.insert(reply, entry);

    connect(reply, &QNetworkReply::finished, this, &FeedFetcher::onReplyFinished);
    connect(reply, &QNetworkReply::downloadProgress, this, &FeedFetcher::onDownloadProgress);
}

void FeedFetcher::onDownloadProgress(qint64 received, qint64 total)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    // Hard cap (section 5). Checked against both the declared length and what has
    // actually arrived, because a hostile or broken server may declare neither.
    if (received > MaxResponseBytes || total > MaxResponseBytes) {
        reply->setProperty("mynews_oversize", true);
        reply->abort();
    }
}

void FeedFetcher::onReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    const InFlight entry = m_inFlight.value(reply);

    Result result;
    result.id = entry.request.id;
    result.url = entry.request.url;
    result.httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.etag = QString::fromUtf8(reply->rawHeader("ETag"));
    result.lastModified = QString::fromUtf8(reply->rawHeader("Last-Modified"));

    if (reply->property("mynews_oversize").toBool()) {
        result.error = QStringLiteral("response exceeds %1 bytes").arg(MaxResponseBytes);
    } else if (result.httpStatus == 304) {
        // Not an error: the feed is unchanged and there is nothing to parse.
        result.notModified = true;
    } else if (reply->error() != QNetworkReply::NoError) {
        result.error = reply->errorString();
    } else if (result.httpStatus >= 400) {
        result.error = QStringLiteral("HTTP %1").arg(result.httpStatus);
    } else {
        result.body = reply->readAll();
        if (result.body.size() > MaxResponseBytes) {
            result.body.clear();
            result.error = QStringLiteral("response exceeds %1 bytes").arg(MaxResponseBytes);
        }
    }

    complete(reply, result);
}

void FeedFetcher::complete(QNetworkReply *reply, Result result)
{
    const InFlight entry = m_inFlight.take(reply);
    if (entry.timeout)
        entry.timeout->stop();

    const bool parallel = entry.request.policy == Policy::Parallel;
    const QString host = hostKey(entry.request.url);
    reply->deleteLater();

    emit finished(result);

    if (parallel) {
        // No cooldown: the point of this policy is that the next picture starts at once.
        if (m_parallelInFlight > 0)
            --m_parallelInFlight;
        pumpQueues();
    } else {
        releaseHost(host);
    }
}

void FeedFetcher::releaseHost(const QString &host)
{
    // Wait out the per-host gap before the next request to the same publisher, then
    // carry on. Without this a subscription list with ten feeds from one site would
    // arrive as ten simultaneous requests.
    QTimer *cooldown = m_hostCooldowns.value(host);
    if (!cooldown) {
        cooldown = new QTimer(this);
        cooldown->setSingleShot(true);
        cooldown->setInterval(PerHostDelayMs);
        connect(cooldown, &QTimer::timeout, this, [this, host]() {
            m_busyHosts.remove(host);
            pumpQueues();
        });
        m_hostCooldowns.insert(host, cooldown);
    }
    cooldown->start();
}

void FeedFetcher::abortAll()
{
    const QList<QNetworkReply *> replies = m_inFlight.keys();
    for (QNetworkReply *reply : replies) {
        m_inFlight.remove(reply);
        reply->abort();
        reply->deleteLater();
    }
    m_queues.clear();
    m_parallelQueue.clear();
    m_parallelInFlight = 0;
    m_busyHosts.clear();
}

} // namespace feed

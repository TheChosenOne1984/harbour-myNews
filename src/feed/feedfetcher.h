#ifndef FEED_FEEDFETCHER_H
#define FEED_FEEDFETCHER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace feed {

/*!
 * \brief Fetches feed documents over HTTP, politely.
 *
 * Section 5 of the plan makes the manners mandatory, not optional: conditional GET on
 * every refresh, an honest User-Agent, gzip, one request per host at a time with a gap
 * between them, timeouts and a hard response-size cap. Those rules are what keeps the
 * app off publishers' block lists, so none of them are configurable away.
 *
 * Asynchronous and signal-based; it never blocks. Backoff itself lives in FeedStore,
 * because it has to survive a restart -- this class only reports what happened.
 */
class FeedFetcher : public QObject
{
    Q_OBJECT

public:
    /*!
     * How hard the fetcher tries not to bother a host.
     *
     * Section 5's manners are about *feed documents*: one request per host at a time with
     * a gap between them, so a subscription list does not arrive as a burst. Applied to
     * images that rule is wrong -- a page's worth of thumbnails all come from one CDN, and
     * serialising them with a gap makes fifty pictures take the better part of a minute.
     * A browser opens several connections to a CDN without anyone considering it rude.
     */
    enum class Policy
    {
        Serialised,   //!< feeds, article pages, the catalogue: one per host, with a gap
        Parallel      //!< images: several at once, still under a global cap
    };

    struct Request
    {
        //! Opaque caller-chosen handle, echoed back in the Result. Not a feed id: this
        //! class is also what fetches the catalogue.
        qint64 id = -1;
        Policy policy = Policy::Serialised;
        QUrl url;
        QString etag;          //!< sent as If-None-Match when present
        QString lastModified;  //!< sent as If-Modified-Since when present
    };

    struct Result
    {
        qint64 id = -1;
        QUrl url;
        int httpStatus = 0;
        bool notModified = false;   //!< 304: success, nothing to parse
        QByteArray body;
        QString etag;
        QString lastModified;
        QString error;              //!< empty on success, including on 304

        bool isSuccess() const { return error.isEmpty(); }
    };

    /*! Timeouts and the size cap, from section 5. */
    static constexpr int ConnectTimeoutMs = 10 * 1000;
    static constexpr int TotalTimeoutMs = 30 * 1000;
    static constexpr qint64 MaxResponseBytes = 5 * 1024 * 1024;

    /*! Gap between two requests to the same host. */
    static constexpr int PerHostDelayMs = 800;

    /*! Ceiling on serialised requests in flight across all hosts. */
    static constexpr int MaxConcurrent = 4;

    /*! Ceiling on parallel requests. Enough to keep a scrolling list fed, not a flood. */
    static constexpr int MaxParallel = 6;

    explicit FeedFetcher(QObject *parent = nullptr);
    ~FeedFetcher() override;

    /*!
     * The User-Agent sent with every request.
     *
     * Section 11: honest and identifiable, naming the app, its version and the project
     * URL. Never a browser string -- masquerading to obtain different content is out.
     */
    static QString defaultUserAgent();
    void setUserAgent(const QString &userAgent);
    QString userAgent() const { return m_userAgent; }

    void enqueue(const Request &request);
    void enqueue(const QVector<Request> &requests);

    int pending() const;
    bool isIdle() const;

    void abortAll();

signals:
    /*! Emitted once per request, success or failure. */
    void finished(const feed::FeedFetcher::Result &result);

    /*! Emitted when the queue drains. */
    void allFinished();

private slots:
    void onReplyFinished();
    void onDownloadProgress(qint64 received, qint64 total);

private:
    struct InFlight
    {
        Request request;
        QTimer *timeout = nullptr;
    };

    void pumpQueues();
    void start(const Request &request);
    void complete(QNetworkReply *reply, Result result);
    void releaseHost(const QString &host);

    QNetworkAccessManager *m_network;
    QString m_userAgent;

    //! One queue per host, so a publisher with many feeds is still served one at a time.
    QHash<QString, QQueue<Request>> m_queues;
    QQueue<Request> m_parallelQueue;
    int m_parallelInFlight = 0;
    QSet<QString> m_busyHosts;
    QHash<QString, QTimer *> m_hostCooldowns;
    QHash<QNetworkReply *, InFlight> m_inFlight;
};

} // namespace feed

Q_DECLARE_METATYPE(feed::FeedFetcher::Result)

#endif // FEED_FEEDFETCHER_H

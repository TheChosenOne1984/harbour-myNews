#include <QtTest>

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "feed/feedfetcher.h"
#include "testrunner.h"

using namespace feed;

namespace {

/*!
 * A deliberately minimal HTTP/1.0 server.
 *
 * Testing section 5's mandatory manners -- conditional GET, honest User-Agent, the size
 * cap -- needs something that records what was actually sent on the wire. Asserting on
 * the code that builds the headers would only restate the implementation.
 */
class StubServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit StubServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, &StubServer::serve);
    }

    QUrl url(const QString &path = QStringLiteral("/feed")) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
    }

    // What to answer with.
    int status = 200;
    QByteArray body = "<rss version=\"2.0\"><channel><title>T</title></channel></rss>";
    QByteArray etag = "\"v1\"";
    QByteArray lastModified = "Mon, 07 Sep 2026 06:30:00 GMT";
    int delayMs = 0;
    bool announceHugeLength = false;
    QByteArray contentEncoding;   //!< set to "gzip" to serve the body compressed

    // What was received.
    QVector<QHash<QByteArray, QByteArray>> requests;
    QVector<qint64> requestTimes;

    QByteArray header(int index, const QByteArray &name) const
    {
        return requests.value(index).value(name.toLower());
    }

private slots:
    void serve()
    {
        QTcpSocket *socket = nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray head = socket->property("head").toByteArray() + socket->readAll();
            socket->setProperty("head", head);
            if (!head.contains("\r\n\r\n"))
                return;

            QHash<QByteArray, QByteArray> parsed;
            const QList<QByteArray> lines = head.split('\n');
            for (int i = 1; i < lines.size(); ++i) {
                const int colon = lines.at(i).indexOf(':');
                if (colon <= 0)
                    continue;
                parsed.insert(lines.at(i).left(colon).trimmed().toLower(),
                              lines.at(i).mid(colon + 1).trimmed());
            }
            requests.append(parsed);
            requestTimes.append(QDateTime::currentMSecsSinceEpoch());

            if (delayMs > 0)
                QThread::msleep(static_cast<unsigned long>(delayMs));

            const qint64 declared = announceHugeLength
                ? FeedFetcher::MaxResponseBytes * 4 : body.size();
            QByteArray response = "HTTP/1.0 " + QByteArray::number(status) + " X\r\n";
            response += "Content-Type: application/xml\r\n";
            response += "Content-Length: " + QByteArray::number(declared) + "\r\n";
            if (!contentEncoding.isEmpty())
                response += "Content-Encoding: " + contentEncoding + "\r\n";
            if (!etag.isEmpty())
                response += "ETag: " + etag + "\r\n";
            if (!lastModified.isEmpty())
                response += "Last-Modified: " + lastModified + "\r\n";
            response += "Connection: close\r\n\r\n";
            if (status != 304)
                response += body;

            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
};

} // namespace

class TestFeedFetcher : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void userAgentIsHonestAndIdentifiable();
    void sendsConditionalGetHeaders();
    void omitsConditionalHeadersWhenUnknown();
    void treatsNotModifiedAsSuccess();
    void deliversBodyAndValidators();
    void reportsHttpErrors();
    void rejectsOversizedResponse();
    void rejectsInvalidUrl();
    void serialisesRequestsToOneHost();
    void decompressesGzippedResponse();

private:
    FeedFetcher::Result fetch(const FeedFetcher::Request &request);

    StubServer *m_server = nullptr;
    FeedFetcher *m_fetcher = nullptr;
};

void TestFeedFetcher::init()
{
    m_server = new StubServer;
    QVERIFY2(m_server->listen(QHostAddress::LocalHost), qPrintable(m_server->errorString()));
    m_fetcher = new FeedFetcher;
}

void TestFeedFetcher::cleanup()
{
    delete m_fetcher;
    m_fetcher = nullptr;
    delete m_server;
    m_server = nullptr;
}

FeedFetcher::Result TestFeedFetcher::fetch(const FeedFetcher::Request &request)
{
    QSignalSpy spy(m_fetcher, &FeedFetcher::finished);
    m_fetcher->enqueue(request);
    if (spy.isEmpty() && !spy.wait(15000))
        return FeedFetcher::Result();
    return spy.at(0).at(0).value<FeedFetcher::Result>();
}

void TestFeedFetcher::userAgentIsHonestAndIdentifiable()
{
    const QString agent = FeedFetcher::defaultUserAgent();
    QVERIFY2(agent.contains(QStringLiteral("myNews")), qPrintable(agent));
    QVERIFY2(agent.contains(QStringLiteral("http")), "must carry the project URL");
    // Section 11: no masquerading as a desktop browser to obtain different content.
    for (const char *forbidden : {"Mozilla", "Chrome", "Safari", "Gecko", "AppleWebKit"})
        QVERIFY2(!agent.contains(QLatin1String(forbidden)), forbidden);

    FeedFetcher::Request request;
    request.feedId = 1;
    request.url = m_server->url();
    fetch(request);
    QCOMPARE(m_server->requests.size(), 1);
    QCOMPARE(m_server->header(0, "user-agent"), agent.toUtf8());
    QVERIFY(m_server->header(0, "accept-encoding").contains("gzip"));
}

void TestFeedFetcher::sendsConditionalGetHeaders()
{
    FeedFetcher::Request request;
    request.feedId = 1;
    request.url = m_server->url();
    request.etag = QStringLiteral("\"v1\"");
    request.lastModified = QStringLiteral("Mon, 07 Sep 2026 06:30:00 GMT");
    fetch(request);

    QCOMPARE(m_server->requests.size(), 1);
    QCOMPARE(m_server->header(0, "if-none-match"), QByteArray("\"v1\""));
    QCOMPARE(m_server->header(0, "if-modified-since"),
             QByteArray("Mon, 07 Sep 2026 06:30:00 GMT"));
}

void TestFeedFetcher::omitsConditionalHeadersWhenUnknown()
{
    FeedFetcher::Request request;
    request.feedId = 1;
    request.url = m_server->url();
    fetch(request);

    QCOMPARE(m_server->requests.size(), 1);
    QVERIFY(m_server->header(0, "if-none-match").isEmpty());
    QVERIFY(m_server->header(0, "if-modified-since").isEmpty());
}

void TestFeedFetcher::treatsNotModifiedAsSuccess()
{
    m_server->status = 304;

    FeedFetcher::Request request;
    request.feedId = 7;
    request.url = m_server->url();
    request.etag = QStringLiteral("\"v1\"");
    const FeedFetcher::Result result = fetch(request);

    // 304 is success-with-no-changes, not a failure -- treating it as one would trip the
    // backoff on exactly the feeds that are behaving best.
    QCOMPARE(result.feedId, Q_INT64_C(7));
    QCOMPARE(result.httpStatus, 304);
    QVERIFY(result.notModified);
    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QVERIFY(result.body.isEmpty());
}

void TestFeedFetcher::deliversBodyAndValidators()
{
    FeedFetcher::Request request;
    request.feedId = 3;
    request.url = m_server->url();
    const FeedFetcher::Result result = fetch(request);

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QCOMPARE(result.httpStatus, 200);
    QVERIFY(!result.notModified);
    QCOMPARE(result.body, m_server->body);
    QCOMPARE(result.etag, QStringLiteral("\"v1\""));
    QCOMPARE(result.lastModified, QStringLiteral("Mon, 07 Sep 2026 06:30:00 GMT"));
}

void TestFeedFetcher::reportsHttpErrors()
{
    m_server->status = 404;
    m_server->body = "not found";

    FeedFetcher::Request request;
    request.feedId = 4;
    request.url = m_server->url();
    const FeedFetcher::Result result = fetch(request);

    QVERIFY(!result.isSuccess());
    QCOMPARE(result.httpStatus, 404);
}

void TestFeedFetcher::rejectsOversizedResponse()
{
    // Declared length alone must be enough to refuse: the point of the cap is not to
    // download five megabytes before deciding.
    m_server->announceHugeLength = true;

    FeedFetcher::Request request;
    request.feedId = 5;
    request.url = m_server->url();
    const FeedFetcher::Result result = fetch(request);

    QVERIFY(!result.isSuccess());
    QVERIFY2(result.error.contains(QStringLiteral("exceeds")), qPrintable(result.error));
}

void TestFeedFetcher::rejectsInvalidUrl()
{
    FeedFetcher::Request request;
    request.feedId = 6;
    request.url = QUrl(QStringLiteral("not a url"));
    const FeedFetcher::Result result = fetch(request);

    QVERIFY(!result.isSuccess());
    QCOMPARE(result.feedId, Q_INT64_C(6));
    QVERIFY(m_server->requests.isEmpty());
}

void TestFeedFetcher::serialisesRequestsToOneHost()
{
    QSignalSpy spy(m_fetcher, &FeedFetcher::finished);

    QVector<FeedFetcher::Request> batch;
    for (int i = 0; i < 3; ++i) {
        FeedFetcher::Request request;
        request.feedId = i;
        request.url = m_server->url(QStringLiteral("/feed%1").arg(i));
        batch.append(request);
    }
    m_fetcher->enqueue(batch);

    while (spy.size() < 3) {
        if (!spy.wait(20000))
            break;
    }
    QCOMPARE(spy.size(), 3);
    QCOMPARE(m_server->requests.size(), 3);

    // Section 5: do not fan out; leave a gap between requests to the same host.
    for (int i = 1; i < m_server->requestTimes.size(); ++i) {
        const qint64 gap = m_server->requestTimes.at(i) - m_server->requestTimes.at(i - 1);
        QVERIFY2(gap >= FeedFetcher::PerHostDelayMs - 100,
                 qPrintable(QStringLiteral("only %1 ms between requests %2 and %3")
                                .arg(gap).arg(i - 1).arg(i)));
    }
}

void TestFeedFetcher::decompressesGzippedResponse()
{
    // Regression guard. Setting Accept-Encoding by hand makes Qt stop decompressing and
    // hand back the raw gzip bytes; every feed then fails to parse. Found the hard way:
    // 14 of 15 real publishers gzip their feeds, so the whole app was broken by one
    // well-meant header. FeedFetcher must leave Accept-Encoding to Qt.
    static const QByteArray gzipped = QByteArray::fromRawData(
        "\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\x45\xce\xc1\x0a\xc2\x30"
        "\x0c\x06\xe0\x57\x19\xbd\xbb\xa8\x27\x91\xac\xbb\xe9\x41\xbc\xe9"
        "\x03\x94\x2d\x6c\xc1\x34\x2b\x6d\x1d\x63\x4f\xef\x70\x38\x6f\x81"
        "\xff\xe3\xff\x83\xf5\xe4\xa5\x18\x29\x26\x1e\xb4\x32\x87\x72\x6f"
        "\x0a\xd2\x66\x68\x59\xbb\xca\x3c\x1f\x97\xdd\xc9\xd4\x16\x63\x4a"
        "\x7f\x74\x5c\x90\xc5\xa6\x77\xaa\x24\x16\x33\x67\x21\x7b\x9d\x39"
        "\x20\xac\x37\x72\x26\xff\x0b\x6e\x83\x0f\x91\x3d\x53\xcc\x54\xdc"
        "\x49\xda\xb7\x76\x1b\x14\xd6\x97\xed\x73\x0e\xe9\x0c\x40\x93\xf3"
        "\x41\xa8\x64\x1d\x9d\x70\x0b\xdd\x8c\xf0\x05\x08\x6b\x21\x6c\x9b"
        "\xb0\x3c\x64\x3f\x46\x18\x48\x74\xbc\x00\x00\x00",
        156);
    static const QByteArray plain =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><rss version=\"2.0\"><channel>"
        "<title>Gzip</title><item><title>Komprimierte Meldung</title>"
        "<link>https://example.invalid/gz</link></item></channel></rss>";

    m_server->body = gzipped;
    m_server->contentEncoding = "gzip";

    FeedFetcher::Request request;
    request.feedId = 8;
    request.url = m_server->url();
    const FeedFetcher::Result result = fetch(request);

    QVERIFY2(result.isSuccess(), qPrintable(result.error));
    QCOMPARE(result.body, plain);

    // Qt has to be the one asking, or it will not decompress.
    QVERIFY(m_server->header(0, "accept-encoding").contains("gzip"));
}

int runFeedFetcherTests(int argc, char **argv)
{
    TestFeedFetcher test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_feedfetcher.moc"

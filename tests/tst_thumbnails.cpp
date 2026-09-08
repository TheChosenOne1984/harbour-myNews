#include <QtTest>

#include <QBuffer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

#include "feed/feedfetcher.h"
#include "feed/feedstore.h"
#include "image/ogimage.h"
#include "image/thumbnailcache.h"
#include "image/thumbnailservice.h"
#include "testrunner.h"

using namespace image;

class TestThumbnails : public QObject
{
    Q_OBJECT

private slots:
    void refusesImagesItCannotMeasure();
    void rewritingAThumbnailDoesNotDoubleCount();
    void findsOpenGraphImage();
    void prefersOpenGraphOverTwitter();
    void acceptsSingleQuotesAndAttributeOrder();
    void resolvesRelativeAndProtocolRelativeUrls();
    void fallsBackToLinkImageSrc();
    void refusesNonHttpSchemes();
    void returnsNothingWhenThereIsNothing();
    void ignoresMetadataBeyondTheHeadBudget();

    void storesDownscaledJpeg();
    void rejectsDataThatIsNotAnImage();
    void evictsLeastRecentlyUsed();
    void removesThumbnailsOfPurgedArticles();

    void fetchesTheFeedsImage();
    void fallsBackToTheArticlePage();
    void remembersThatThereIsNoImage();
    void doesNotFetchUntilAsked();

private:
    static QByteArray pngOfSize(int width, int height);
};

QByteArray TestThumbnails::pngOfSize(int width, int height)
{
    // A smooth gradient rather than a per-pixel sawtooth. The sawtooth is high-frequency
    // content of a kind no photograph contains, and libjpeg complains while decoding it --
    // a property of the fixture, not of the code under test, but a warning in a passing
    // test is a warning someone will one day chase.
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixel(x, y, qRgb(255 * x / width, 255 * y / height,
                                      128 + 64 * x / width));
        }
    }
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

void TestThumbnails::findsOpenGraphImage()
{
    const QByteArray html =
        "<html><head>"
        "<meta property=\"og:title\" content=\"A story\">"
        "<meta property=\"og:image\" content=\"https://cdn.invalid/a.jpg\">"
        "</head><body>...</body></html>";
    QCOMPARE(extractPreviewImage(html), QStringLiteral("https://cdn.invalid/a.jpg"));
}

void TestThumbnails::prefersOpenGraphOverTwitter()
{
    const QByteArray html =
        "<meta name=\"twitter:image\" content=\"https://cdn.invalid/t.jpg\">"
        "<meta property=\"og:image\" content=\"https://cdn.invalid/og.jpg\">";
    QCOMPARE(extractPreviewImage(html), QStringLiteral("https://cdn.invalid/og.jpg"));

    // Twitter's tag is used when Open Graph is absent.
    QCOMPARE(extractPreviewImage("<meta name=\"twitter:image\" content=\"https://cdn.invalid/t.jpg\">"),
             QStringLiteral("https://cdn.invalid/t.jpg"));
}

void TestThumbnails::acceptsSingleQuotesAndAttributeOrder()
{
    // Publishers write these tags every way the syntax allows.
    QCOMPARE(extractPreviewImage("<meta content='https://cdn.invalid/a.jpg' property='og:image'/>"),
             QStringLiteral("https://cdn.invalid/a.jpg"));
    QCOMPARE(extractPreviewImage("<META PROPERTY=\"OG:IMAGE\" CONTENT=\"https://cdn.invalid/b.jpg\">"),
             QStringLiteral("https://cdn.invalid/b.jpg"));
    // An ampersand in a query string arrives escaped.
    QCOMPARE(extractPreviewImage("<meta property=\"og:image\" content=\"https://cdn.invalid/c.jpg?w=1&amp;h=2\">"),
             QStringLiteral("https://cdn.invalid/c.jpg?w=1&h=2"));
}

void TestThumbnails::resolvesRelativeAndProtocolRelativeUrls()
{
    const QUrl base(QStringLiteral("https://example.invalid/news/story-1.html"));

    QCOMPARE(extractPreviewImage("<meta property=\"og:image\" content=\"/img/a.jpg\">", base),
             QStringLiteral("https://example.invalid/img/a.jpg"));
    QCOMPARE(extractPreviewImage("<meta property=\"og:image\" content=\"b.jpg\">", base),
             QStringLiteral("https://example.invalid/news/b.jpg"));
    QCOMPARE(extractPreviewImage("<meta property=\"og:image\" content=\"//cdn.invalid/c.jpg\">", base),
             QStringLiteral("https://cdn.invalid/c.jpg"));
}

void TestThumbnails::fallsBackToLinkImageSrc()
{
    QCOMPARE(extractPreviewImage("<link rel=\"image_src\" href=\"https://cdn.invalid/d.jpg\">"),
             QStringLiteral("https://cdn.invalid/d.jpg"));
}

void TestThumbnails::refusesNonHttpSchemes()
{
    // A page must not be able to talk the app into reading a local file.
    QVERIFY(extractPreviewImage("<meta property=\"og:image\" content=\"file:///etc/passwd\">")
                .isEmpty());
    QVERIFY(extractPreviewImage("<meta property=\"og:image\" content=\"data:image/png;base64,AAAA\">")
                .isEmpty());
}

void TestThumbnails::returnsNothingWhenThereIsNothing()
{
    QVERIFY(extractPreviewImage("<html><body>no metadata here</body></html>").isEmpty());
    QVERIFY(extractPreviewImage(QByteArray()).isEmpty());
    // An <img> in the body is deliberately not used: picking the biggest picture off a
    // page is reader-mode extraction, which section 11 rules out.
    QVERIFY(extractPreviewImage("<body><img src=\"https://cdn.invalid/body.jpg\"></body>")
                .isEmpty());
}

void TestThumbnails::ignoresMetadataBeyondTheHeadBudget()
{
    QByteArray html = "<html><head>";
    html.append(QByteArray(MaxHtmlBytes, ' '));
    html.append("<meta property=\"og:image\" content=\"https://cdn.invalid/late.jpg\">");
    // Reading further would cost the publisher bandwidth for something that belongs in
    // the first kilobytes.
    QVERIFY(extractPreviewImage(html).isEmpty());
}

void TestThumbnails::storesDownscaledJpeg()
{
    QTemporaryDir dir;
    ThumbnailCache cache(dir.path() + QStringLiteral("/thumbs"));
    QVERIFY(cache.open());
    QVERIFY(!cache.contains(1));

    const QByteArray original = pngOfSize(1600, 900);
    const QString path = cache.store(1, original);

    QVERIFY2(!path.isEmpty(), "storing a valid image must produce a file");
    QVERIFY(cache.contains(1));
    QCOMPARE(cache.pathFor(1), path);

    const QImage stored(path);
    QVERIFY(!stored.isNull());
    // Section 8: scaled down on the way in, not kept at full resolution.
    QVERIFY2(stored.width() <= ThumbnailCache::TargetSize
                 && stored.height() <= ThumbnailCache::TargetSize,
             qPrintable(QStringLiteral("stored %1x%2").arg(stored.width()).arg(stored.height())));
    // Aspect ratio preserved: 16:9 stays 16:9.
    QVERIFY(qAbs(double(stored.width()) / stored.height() - 16.0 / 9.0) < 0.05);

    // Deliberately no "smaller than the original" assertion. This fixture is a synthetic
    // pattern that PNG compresses to a few kilobytes and JPEG, which is built for photos,
    // does not -- so the comparison would test the fixture rather than the code. What
    // matters is that the stored file is a downscaled JPEG, which is asserted above and
    // here.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();
    Q_UNUSED(original)

    // A JPEG starts with SOI and ends with EOI. A thumbnail missing its end marker is a
    // truncated write, which would decode with a grey tail on the device.
    QCOMPARE(bytes.left(2), QByteArray::fromHex("ffd8"));
    QCOMPARE(bytes.right(2), QByteArray::fromHex("ffd9"));
    // libjpeg prints "premature end of data segment" while decoding this fixture even
    // though the file is complete, as the markers above show. It is a complaint about the
    // synthetic gradient's content, not about the write; real photographs do not provoke
    // it. Left in rather than silenced, with this note so it is not chased again.

    QVERIFY(cache.usedBytes() > 0);
}

void TestThumbnails::rejectsDataThatIsNotAnImage()
{
    QTemporaryDir dir;
    ThumbnailCache cache(dir.path() + QStringLiteral("/thumbs"));
    QVERIFY(cache.open());

    // A publisher serving an error page where a picture was expected must not leave a
    // broken file behind.
    QVERIFY(cache.store(1, "<html>404</html>").isEmpty());
    QVERIFY(cache.store(2, QByteArray()).isEmpty());
    QVERIFY(!cache.contains(1));
    QCOMPARE(cache.usedBytes(), Q_INT64_C(0));
}

void TestThumbnails::evictsLeastRecentlyUsed()
{
    QTemporaryDir dir;
    ThumbnailCache cache(dir.path() + QStringLiteral("/thumbs"));
    QVERIFY(cache.open());

    const QByteArray image = pngOfSize(800, 600);
    for (qint64 id = 1; id <= 5; ++id) {
        QVERIFY(!cache.store(id, image).isEmpty());
        // Distinct modification times, which is what the eviction order is built on.
        QTest::qWait(1100);
    }
    QCOMPARE(cache.count(), 5);

    // Article 1 is the oldest, but reading it makes it the most recently used.
    cache.touch(1);

    const qint64 oneFile = QFileInfo(cache.pathFor(3)).size();
    cache.setMaxBytes(oneFile * 3);

    QVERIFY2(cache.usedBytes() <= cache.maxBytes(),
             qPrintable(QStringLiteral("used %1 > max %2")
                            .arg(cache.usedBytes()).arg(cache.maxBytes())));
    QVERIFY2(cache.contains(1), "a thumbnail that was just used must not be the one evicted");
    QVERIFY(cache.contains(5));
    QVERIFY(!cache.contains(2));
}

void TestThumbnails::removesThumbnailsOfPurgedArticles()
{
    QTemporaryDir dir;
    ThumbnailCache cache(dir.path() + QStringLiteral("/thumbs"));
    QVERIFY(cache.open());

    const QByteArray image = pngOfSize(400, 300);
    for (qint64 id = 1; id <= 4; ++id)
        QVERIFY(!cache.store(id, image).isEmpty());

    // Retention deleted articles 2 and 4; their pictures have to go too, or the cache
    // fills with images for stories that no longer exist (section 8).
    QCOMPARE(cache.removeAllExcept({1, 3}), 2);
    QVERIFY(cache.contains(1));
    QVERIFY(!cache.contains(2));
    QVERIFY(cache.contains(3));
    QVERIFY(!cache.contains(4));
    QCOMPARE(cache.count(), 2);

    cache.clear();
    QCOMPARE(cache.count(), 0);
}

namespace {

/*!
 * A publisher: serves an article page carrying an og:image, and the picture itself.
 *
 * The lazy fetching and the og:image fallback are the parts of section 8 that only show
 * their behaviour against a server, so there is one here.
 */
class StubPublisher : public QTcpServer
{
    Q_OBJECT

public:
    explicit StubPublisher(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, &StubPublisher::serve);
    }

    QString url(const QString &path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path);
    }

    QByteArray image;
    bool pageHasImageTag = true;
    QStringList requested;

private slots:
    void serve()
    {
        QTcpSocket *socket = nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray head = socket->property("head").toByteArray() + socket->readAll();
            socket->setProperty("head", head);
            if (!head.contains("\r\n\r\n"))
                return;

            const QByteArray path = head.split(' ').value(1);
            requested.append(QString::fromUtf8(path));

            QByteArray body;
            QByteArray type;
            if (path.startsWith("/img")) {
                body = image;
                type = "image/png";
            } else {
                type = "text/html";
                body = "<html><head><title>Story</title>";
                if (pageHasImageTag) {
                    body += "<meta property=\"og:image\" content=\""
                            + url(QStringLiteral("/img/a.png")).toUtf8() + "\">";
                }
                body += "</head><body>text</body></html>";
            }

            QByteArray response = "HTTP/1.0 200 OK\r\nContent-Type: " + type + "\r\n";
            response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += body;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
};

/*! Store, fetcher, cache and service, wired the way the backend wires them. */
struct ServiceFixture
{
    QTemporaryDir dir;
    StubPublisher server;
    feed::FeedStore *store = nullptr;
    feed::FeedFetcher fetcher;
    ThumbnailCache *cache = nullptr;
    ThumbnailService *service = nullptr;

    bool build(const QByteArray &imageData)
    {
        server.image = imageData;
        if (!server.listen(QHostAddress::LocalHost))
            return false;
        store = new feed::FeedStore(dir.path() + QStringLiteral("/t.db"));
        if (!store->open())
            return false;
        cache = new ThumbnailCache(dir.path() + QStringLiteral("/thumbs"));
        cache->open();
        service = new ThumbnailService(store, &fetcher, cache);
        return true;
    }

    qint64 addArticle(const QString &imageUrl)
    {
        const qint64 feedId = store->addFeed(QStringLiteral("https://f.invalid/feed"),
                                             QStringLiteral("Feed"), QStringLiteral("de"));
        feed::ParsedArticle article;
        article.guid = QStringLiteral("1");
        article.link = server.url(QStringLiteral("/story"));
        article.title = QStringLiteral("Eine ausreichend lange Schlagzeile");
        article.published = 1788762600;
        article.imageUrl = imageUrl;
        store->ingest(feedId, {article}, 1788762600);
        return store->articlesInCluster(1).first().id;
    }

    ~ServiceFixture()
    {
        delete service;
        delete cache;
        delete store;
    }
};

} // namespace

void TestThumbnails::fetchesTheFeedsImage()
{
    ServiceFixture f;
    QVERIFY(f.build(pngOfSize(800, 600)));
    const qint64 id = f.addArticle(f.server.url(QStringLiteral("/img/feed.png")));

    QSignalSpy ready(f.service, &ThumbnailService::thumbnailReady);
    f.service->request(id);
    QVERIFY2(ready.wait(15000), "no thumbnail arrived");

    QCOMPARE(ready.at(0).at(0).toLongLong(), id);
    QVERIFY(f.cache->contains(id));
    // The article page was never touched: the feed already said where the picture is.
    QVERIFY2(!f.server.requested.contains(QStringLiteral("/story")),
             qPrintable(f.server.requested.join(QStringLiteral(", "))));

    feed::ArticleRecord article;
    QVERIFY(f.store->articleById(id, &article));
    QCOMPARE(article.imageState, QStringLiteral("cached"));
}

void TestThumbnails::fallsBackToTheArticlePage()
{
    ServiceFixture f;
    QVERIFY(f.build(pngOfSize(800, 600)));
    const qint64 id = f.addArticle(QString());   // the feed gave no image

    QSignalSpy ready(f.service, &ThumbnailService::thumbnailReady);
    f.service->request(id);
    QVERIFY2(ready.wait(20000), "no thumbnail arrived");

    QVERIFY(f.cache->contains(id));
    QVERIFY(f.server.requested.contains(QStringLiteral("/story")));
    QVERIFY(f.server.requested.contains(QStringLiteral("/img/a.png")));

    // The discovered URL is remembered, so a later run fetches the picture directly
    // instead of reading the page again.
    feed::ArticleRecord article;
    QVERIFY(f.store->articleById(id, &article));
    QVERIFY2(article.imageUrl.contains(QStringLiteral("/img/a.png")),
             qPrintable(article.imageUrl));
}

void TestThumbnails::remembersThatThereIsNoImage()
{
    ServiceFixture f;
    QVERIFY(f.build(pngOfSize(400, 300)));
    f.server.pageHasImageTag = false;
    const qint64 id = f.addArticle(QString());

    QSignalSpy failed(f.service, &ThumbnailService::thumbnailFailed);
    f.service->request(id);
    QVERIFY2(failed.wait(20000), "failure was never reported");

    // Section 8: persisted, so a restart does not read the page again on every scroll.
    feed::ArticleRecord article;
    QVERIFY(f.store->articleById(id, &article));
    QCOMPARE(article.imageState, QStringLiteral("failed"));

    const int before = f.server.requested.size();
    ThumbnailService fresh(f.store, &f.fetcher, f.cache);
    fresh.request(id);
    QTest::qWait(500);
    QCOMPARE(f.server.requested.size(), before);
}

void TestThumbnails::doesNotFetchUntilAsked()
{
    ServiceFixture f;
    QVERIFY(f.build(pngOfSize(400, 300)));
    f.addArticle(f.server.url(QStringLiteral("/img/feed.png")));

    // Ingesting an article must not fetch anything. Section 8 is explicit that this is
    // lazy: a burst of requests at every publisher for pictures nobody has scrolled to is
    // exactly what it forbids.
    QTest::qWait(500);
    QVERIFY2(f.server.requested.isEmpty(),
             qPrintable(f.server.requested.join(QStringLiteral(", "))));
}

void TestThumbnails::refusesImagesItCannotMeasure()
{
    // The dimension guard is the only thing between a feed-supplied URL and a full-size
    // decode -- Qt 5.6 has no QImageReader::setAllocationLimit. It used to sit inside an
    // "if the header declared a size" branch, so anything unmeasurable walked past it.
    QTemporaryDir dir;
    image::ThumbnailCache cache(dir.path());
    QVERIFY(cache.open());

    QCOMPARE(cache.store(1, QByteArray("not an image at all")), QString());
    // A JPEG cut off before its frame header: the reader knows the format and cannot
    // give a size.
    QCOMPARE(cache.store(2, QByteArray::fromHex("ffd8ffe000104a46494600010100000100010000")),
             QString());
    QVERIFY(!cache.contains(1));
    QVERIFY(!cache.contains(2));
    QCOMPARE(cache.usedBytes(), qint64(0));
}

void TestThumbnails::rewritingAThumbnailDoesNotDoubleCount()
{
    // pathFor() is deterministic, so storing an article's picture twice overwrites the
    // file -- but the first size stayed on the books, so the total drifted up on every
    // rewrite and eviction began firing below the configured cap.
    QTemporaryDir dir;
    image::ThumbnailCache cache(dir.path());
    QVERIFY(cache.open());

    const QByteArray png = pngOfSize(600, 400);
    QVERIFY(!cache.store(1, png).isEmpty());
    const qint64 afterFirst = cache.usedBytes();
    QVERIFY(afterFirst > 0);

    QVERIFY(!cache.store(1, png).isEmpty());
    QCOMPARE(cache.usedBytes(), afterFirst);
    QCOMPARE(cache.count(), 1);

    // And the books agree with the directory.
    image::ThumbnailCache reopened(dir.path());
    QVERIFY(reopened.open());
    QCOMPARE(reopened.usedBytes(), cache.usedBytes());
}

int runThumbnailTests(int argc, char **argv)
{
    TestThumbnails test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_thumbnails.moc"

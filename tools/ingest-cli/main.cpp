/*
 * End-to-end exercise of the ingestion core: fetch, parse, store, cluster.
 *
 * Runs the real code against real feeds without the UI, which is what
 * "headless, testable" in milestone 3 is for. Also the fastest way to see what the C++
 * parser makes of a publisher's actual document, as opposed to a test fixture.
 *
 *   ingest-cli --db /tmp/mynews.db --feeds tools/corpus-feeds.json
 *   ingest-cli --db /tmp/mynews.db --list
 *   ingest-cli --db /tmp/mynews.db --search speicher
 *   ingest-cli --db /tmp/mynews.db --purge-days 14
 */

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#include "feed/feedfetcher.h"
#include "feed/feedrefresher.h"
#include "feed/feedstore.h"

static QTextStream out(stdout);
static QTextStream err(stderr);

namespace {

int addFeedsFromFile(feed::FeedStore &store, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "cannot open " << path << ": " << file.errorString() << "\n";
        return -1;
    }
    const QJsonArray feeds =
        QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("feeds")).toArray();

    int added = 0;
    for (const QJsonValue &value : feeds) {
        const QJsonObject obj = value.toObject();
        const qint64 before = store.feeds().size();
        // The catalogue calls it "title"; the older corpus file called it "source".
        QString title = obj.value(QStringLiteral("title")).toString();
        if (title.isEmpty())
            title = obj.value(QStringLiteral("source")).toString();
        store.addFeed(obj.value(QStringLiteral("url")).toString(), title,
                      obj.value(QStringLiteral("lang")).toString(),
                      QStringLiteral("catalogue"));
        if (store.feeds().size() > before)
            ++added;
    }
    return added;
}

void printClusters(feed::FeedStore &store, int limit, int minSize)
{
    const QVector<feed::ClusterRecord> clusters = store.clusters(limit * 4);
    int shown = 0;
    for (const feed::ClusterRecord &record : clusters) {
        if (record.memberCount < minSize)
            continue;
        if (shown++ >= limit)
            break;

        const QVector<feed::ArticleRecord> members = store.articlesInCluster(record.id);
        QStringList sources;
        for (const feed::ArticleRecord &article : members) {
            feed::FeedRecord feedRecord;
            store.feedById(article.feedId, &feedRecord);
            const QString name = feedRecord.title.isEmpty() ? feedRecord.url : feedRecord.title;
            if (!sources.contains(name))
                sources.append(name);
        }

        out << "\n== " << record.memberCount << " articles, "
            << sources.size() << " source(s): " << sources.join(QStringLiteral(", ")) << "\n";
        for (const feed::ArticleRecord &article : members) {
            feed::FeedRecord feedRecord;
            store.feedById(article.feedId, &feedRecord);
            out << QString("   %1 [%2] %3\n")
                       .arg(article.id == record.leadArticleId ? QStringLiteral("*")
                                                               : QStringLiteral(" "))
                       .arg(feedRecord.title.left(14), -14)
                       .arg(article.title);
        }
    }
    if (shown == 0)
        out << "no clusters with at least " << minSize << " members\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ingest-cli"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Exercises the myNews ingestion core without the UI.");
    parser.addHelpOption();

    QCommandLineOption dbOpt(QStringLiteral("db"), QStringLiteral("SQLite file"), "PATH",
                             QStringLiteral("mynews.db"));
    QCommandLineOption feedsOpt(QStringLiteral("feeds"),
                                QStringLiteral("Subscribe to the feeds in this JSON file"),
                                "PATH");
    QCommandLineOption refreshOpt(QStringLiteral("refresh"),
                                  QStringLiteral("Fetch every due feed (implied by --feeds)"));
    QCommandLineOption listOpt(QStringLiteral("list"),
                               QStringLiteral("Print clusters and exit"));
    QCommandLineOption searchOpt(QStringLiteral("search"),
                                 QStringLiteral("Full-text search and exit"), "TEXT");
    QCommandLineOption purgeOpt(QStringLiteral("purge-days"),
                                QStringLiteral("Delete articles older than N days"), "N");
    QCommandLineOption limitOpt(QStringLiteral("limit"),
                                QStringLiteral("Clusters to print (default 15)"), "N", "15");
    QCommandLineOption minSizeOpt(QStringLiteral("min-size"),
                                  QStringLiteral("Only clusters with N members (default 2)"),
                                  "N", "2");
    for (const QCommandLineOption &option : {dbOpt, feedsOpt, refreshOpt, listOpt, searchOpt,
                                             purgeOpt, limitOpt, minSizeOpt}) {
        parser.addOption(option);
    }
    parser.process(app);

    feed::FeedStore store(parser.value(dbOpt));
    if (!store.open()) {
        err << "cannot open store: " << store.lastError() << "\n";
        return 1;
    }
    out << "store: " << parser.value(dbOpt) << "  schema v" << store.schemaVersion()
        << "  fts5=" << (store.hasFullTextSearch() ? "yes" : "no") << "\n";

    // QDateTime::currentSecsSinceEpoch() only exists from Qt 5.8; Sailfish is on 5.6.
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;

    if (parser.isSet(purgeOpt)) {
        const qint64 cutoff = now - parser.value(purgeOpt).toLongLong() * 24 * 3600;
        out << "purged " << store.purgeArticlesOlderThan(cutoff) << " articles\n";
        return 0;
    }
    if (parser.isSet(searchOpt)) {
        const QVector<feed::ArticleRecord> hits = store.search(parser.value(searchOpt));
        out << hits.size() << " hit(s) for \"" << parser.value(searchOpt) << "\"\n";
        for (const feed::ArticleRecord &article : hits)
            out << "   " << article.title << "\n";
        return 0;
    }
    if (parser.isSet(listOpt)) {
        printClusters(store, parser.value(limitOpt).toInt(), parser.value(minSizeOpt).toInt());
        return 0;
    }

    if (parser.isSet(feedsOpt)) {
        const int added = addFeedsFromFile(store, parser.value(feedsOpt));
        if (added < 0)
            return 1;
        out << "subscribed: " << added << " new, " << store.feeds().size() << " total\n";
    }
    if (store.feeds().isEmpty()) {
        err << "no feeds subscribed -- pass --feeds\n";
        return 1;
    }

    feed::FeedFetcher fetcher;
    feed::FeedRefresher refresher(&store, &fetcher);

    QObject::connect(&refresher, &feed::FeedRefresher::feedFinished,
                     [&store](qint64 feedId, const feed::IngestResult &result,
                              const QString &error) {
        feed::FeedRecord record;
        store.feedById(feedId, &record);
        const QString name = record.title.isEmpty() ? record.url : record.title;
        if (!error.isEmpty()) {
            out << QString("  %1 FAILED: %2\n").arg(name.left(16), -16).arg(error);
        } else {
            out << QString("  %1 %2 new, %3 updated, %4 unchanged"
                           "   (clusters: %5 new, %6 joined)\n")
                       .arg(name.left(16), -16)
                       .arg(result.inserted, 3).arg(result.updated, 3)
                       .arg(result.unchanged, 3)
                       .arg(result.clustersCreated, 3).arg(result.clustersJoined, 3);
        }
        out.flush();
    });

    QObject::connect(&refresher, &feed::FeedRefresher::finished,
                     [&](const feed::FeedRefresher::Summary &summary) {
        out << "\n" << summary.feedsSucceeded << "/" << summary.feedsAttempted
            << " feeds ok (" << summary.feedsUnchanged << " unchanged, "
            << summary.feedsFailed << " failed)\n"
            << summary.articlesInserted << " articles inserted, "
            << summary.articlesUpdated << " updated\n"
            << summary.clustersCreated << " clusters created, "
            << summary.clustersJoined << " joins\n"
            << "unread: " << store.unreadCount() << "\n";
        printClusters(store, parser.value(limitOpt).toInt(), parser.value(minSizeOpt).toInt());
        out.flush();
        app.quit();
    });

    out << "refreshing " << store.feedsDueForFetch(now).size() << " feed(s)...\n";
    out.flush();
    refresher.refreshDue(now);

    // A stuck network must not hang the tool forever.
    QTimer::singleShot(10 * 60 * 1000, &app, &QCoreApplication::quit);
    return app.exec();
}

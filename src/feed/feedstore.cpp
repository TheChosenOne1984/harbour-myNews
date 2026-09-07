#include "feedstore.h"

#include "cluster/normalize.h"
#include "i18n/languagenames.h"
#include "cluster/simhash.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>

namespace feed {
namespace {

/*! Bump this and add a migrate() branch whenever the schema changes. */
constexpr int SchemaVersion = 3;

/*! Backoff: 15 min, then doubling, capped at a day (section 5). */
qint64 backoffSeconds(int failureCount)
{
    constexpr qint64 base = 15 * 60;
    constexpr qint64 cap = 24 * 3600;
    qint64 delay = base;
    for (int i = 1; i < failureCount && delay < cap; ++i)
        delay *= 2;
    return qMin(delay, cap);
}

FeedRecord feedFromRecord(const QSqlQuery &query)
{
    FeedRecord feed;
    feed.id = query.value(QStringLiteral("id")).toLongLong();
    feed.url = query.value(QStringLiteral("url")).toString();
    feed.title = query.value(QStringLiteral("title")).toString();
    feed.siteUrl = query.value(QStringLiteral("site_url")).toString();
    feed.lang = query.value(QStringLiteral("lang")).toString();
    feed.category = query.value(QStringLiteral("category")).toString();
    feed.langSource = query.value(QStringLiteral("lang_source")).toString();
    feed.etag = query.value(QStringLiteral("etag")).toString();
    feed.lastModified = query.value(QStringLiteral("last_modified")).toString();
    feed.lastFetch = query.value(QStringLiteral("last_fetch")).toLongLong();
    feed.lastStatus = query.value(QStringLiteral("last_status")).toInt();
    feed.enabled = query.value(QStringLiteral("enabled")).toInt() != 0;
    feed.failureCount = query.value(QStringLiteral("failure_count")).toInt();
    feed.nextAttempt = query.value(QStringLiteral("next_attempt")).toLongLong();
    return feed;
}

ArticleRecord articleFromRecord(const QSqlQuery &query)
{
    ArticleRecord article;
    article.id = query.value(QStringLiteral("id")).toLongLong();
    article.feedId = query.value(QStringLiteral("feed_id")).toLongLong();
    article.guid = query.value(QStringLiteral("guid")).toString();
    article.link = query.value(QStringLiteral("link")).toString();
    article.title = query.value(QStringLiteral("title")).toString();
    article.teaser = query.value(QStringLiteral("teaser")).toString();
    article.published = query.value(QStringLiteral("published")).toLongLong();
    article.firstSeen = query.value(QStringLiteral("first_seen")).toLongLong();
    article.imageUrl = query.value(QStringLiteral("image_url")).toString();
    article.imageState = query.value(QStringLiteral("image_state")).toString();
    article.lang = query.value(QStringLiteral("lang")).toString();
    article.simhash = static_cast<quint64>(
        query.value(QStringLiteral("simhash")).toLongLong());
    const QVariant clusterId = query.value(QStringLiteral("cluster_id"));
    article.clusterId = clusterId.isNull() ? -1 : clusterId.toLongLong();
    article.read = query.value(QStringLiteral("read")).toInt() != 0;
    return article;
}

/*! SQLite stores INTEGER as signed; round-trip the 64-bit simhash through that. */
qint64 toSigned(quint64 value) { return static_cast<qint64>(value); }

} // namespace

FeedStore::FeedStore(const QString &databasePath, const QString &connectionName)
    : m_path(databasePath)
    // QUuid::WithoutBraces only exists from Qt 5.11 and Sailfish is on 5.6, so the
    // braces are removed by hand.
    , m_connectionName(connectionName.isEmpty()
                           ? QStringLiteral("mynews-%1").arg(
                                 QUuid::createUuid().toString().remove(QLatin1Char('{'))
                                     .remove(QLatin1Char('}')))
                           : connectionName)
{
}

FeedStore::~FeedStore()
{
    close();
}

void FeedStore::close()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

bool FeedStore::isOpen() const
{
    return m_db.isValid() && m_db.isOpen();
}

void FeedStore::setError(const QString &context, const QString &detail) const
{
    m_lastError = detail.isEmpty() ? context : context + QStringLiteral(": ") + detail;
}

bool FeedStore::exec(const QString &sql)
{
    QSqlQuery query(m_db);
    if (query.exec(sql))
        return true;
    setError(QStringLiteral("statement failed"),
             query.lastError().text() + QStringLiteral(" [") + sql.left(120) + QLatin1Char(']'));
    return false;
}

bool FeedStore::open()
{
    if (m_path != QStringLiteral(":memory:")) {
        const QDir dir = QFileInfo(m_path).absoluteDir();
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            setError(QStringLiteral("cannot create database directory"), dir.absolutePath());
            return false;
        }
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        setError(QStringLiteral("cannot open database"), m_db.lastError().text());
        return false;
    }

    // Foreign keys are off by default in SQLite and have to be enabled per connection,
    // otherwise ON DELETE CASCADE silently does nothing.
    if (!exec(QStringLiteral("PRAGMA foreign_keys = ON")))
        return false;
    exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

    const int version = schemaVersion();
    if (version == 0) {
        if (!createSchema())
            return false;
    } else if (version < SchemaVersion) {
        if (!migrate(version))
            return false;
    } else if (version > SchemaVersion) {
        setError(QStringLiteral("database is newer than this build"),
                 QStringLiteral("schema version %1").arg(version));
        return false;
    }

    detectFullTextSearch();
    return true;
}

int FeedStore::schemaVersion() const
{
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
        return query.value(0).toInt();
    return -1;
}

bool FeedStore::createSchema()
{
    static const QVector<QString> statements{
        QStringLiteral(
            "CREATE TABLE feed ("
            "  id INTEGER PRIMARY KEY,"
            "  url TEXT NOT NULL UNIQUE,"
            "  title TEXT,"
            "  site_url TEXT,"
            "  lang TEXT,"
            "  category TEXT,"
            "  lang_source TEXT,"
            "  etag TEXT,"
            "  last_modified TEXT,"
            "  last_fetch INTEGER DEFAULT 0,"
            "  last_status INTEGER DEFAULT 0,"
            "  enabled INTEGER NOT NULL DEFAULT 1,"
            "  failure_count INTEGER NOT NULL DEFAULT 0,"
            "  next_attempt INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral(
            "CREATE TABLE cluster ("
            "  id INTEGER PRIMARY KEY,"
            "  lead_article_id INTEGER,"
            "  lang TEXT,"
            "  published INTEGER,"
            "  member_count INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral(
            "CREATE TABLE article ("
            "  id INTEGER PRIMARY KEY,"
            "  feed_id INTEGER NOT NULL REFERENCES feed(id) ON DELETE CASCADE,"
            "  guid TEXT NOT NULL,"
            "  link TEXT NOT NULL,"
            "  title TEXT NOT NULL,"
            "  teaser TEXT,"
            "  published INTEGER,"
            "  first_seen INTEGER NOT NULL,"
            "  image_url TEXT,"
            "  image_state TEXT NOT NULL DEFAULT 'none',"
            "  lang TEXT,"
            "  simhash INTEGER,"
            "  cluster_id INTEGER REFERENCES cluster(id) ON DELETE SET NULL,"
            "  read INTEGER NOT NULL DEFAULT 0,"
            "  UNIQUE(feed_id, guid))"),
        QStringLiteral("CREATE INDEX idx_article_published ON article(published DESC)"),
        QStringLiteral("CREATE INDEX idx_article_cluster ON article(cluster_id)"),
        QStringLiteral("CREATE INDEX idx_article_lang_pub ON article(lang, published DESC)"),
        QStringLiteral("CREATE INDEX idx_cluster_published ON cluster(published DESC)"),
    };

    if (!m_db.transaction()) {
        setError(QStringLiteral("cannot start schema transaction"), m_db.lastError().text());
        return false;
    }
    for (const QString &statement : statements) {
        if (!exec(statement)) {
            m_db.rollback();
            return false;
        }
    }
    if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(SchemaVersion))) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        setError(QStringLiteral("cannot commit schema"), m_db.lastError().text());
        return false;
    }
    return true;
}

bool FeedStore::migrate(int fromVersion)
{
    if (!m_db.transaction()) {
        setError(QStringLiteral("cannot start migration"), m_db.lastError().text());
        return false;
    }

    int version = fromVersion;

    // 1 -> 2: language tags lost their regional subtag, so that a feed stored as "en-GB"
    // shares a clustering partition with one stored as "en-US" instead of being unable to
    // ever match it.
    if (version == 1) {
        for (const QString &table : {QStringLiteral("feed"), QStringLiteral("article")}) {
            QSqlQuery read(m_db);
            if (!read.exec(QStringLiteral(
                    "SELECT DISTINCT lang FROM %1 WHERE lang IS NOT NULL AND lang <> ''")
                               .arg(table))) {
                setError(QStringLiteral("migration 1->2 failed"), read.lastError().text());
                m_db.rollback();
                return false;
            }
            QStringList originals;
            while (read.next())
                originals.append(read.value(0).toString());

            for (const QString &original : originals) {
                const QString normalised = i18n::normalizeLanguageTag(original);
                if (normalised == original)
                    continue;
                QSqlQuery update(m_db);
                update.prepare(QStringLiteral("UPDATE %1 SET lang = ? WHERE lang = ?")
                                   .arg(table));
                update.addBindValue(normalised.isEmpty() ? QVariant() : QVariant(normalised));
                update.addBindValue(original);
                if (!update.exec()) {
                    setError(QStringLiteral("migration 1->2 failed"),
                             update.lastError().text());
                    m_db.rollback();
                    return false;
                }
            }
        }
        version = 2;
    }

    // 2 -> 3: feeds gained the catalogue category they came from, so the article list can
    // be filtered by region. Existing rows get none, which reads as "no region".
    if (version == 2) {
        if (!exec(QStringLiteral("ALTER TABLE feed ADD COLUMN category TEXT"))) {
            m_db.rollback();
            return false;
        }
        version = 3;
    }

    if (version != SchemaVersion) {
        setError(QStringLiteral("no migration path"),
                 QStringLiteral("from schema version %1").arg(fromVersion));
        m_db.rollback();
        return false;
    }

    if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(SchemaVersion))) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        setError(QStringLiteral("cannot commit migration"), m_db.lastError().text());
        return false;
    }
    return true;
}

bool FeedStore::detectFullTextSearch()
{
    // Verified present on the 5.1.0.11 target (SQLite 3.50.4), but a probe costs nothing
    // and keeps search optional rather than fatal.
    QSqlQuery query(m_db);
    m_hasFts = query.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS article_fts USING fts5("
        "  title, teaser, content='article', content_rowid='id', tokenize='trigram')"));
    if (!m_hasFts) {
        m_lastError.clear();   // absence is a supported configuration, not an error
        return false;
    }

    // An external-content FTS5 table holds no data of its own -- without these triggers
    // it stays permanently empty and every search returns nothing.
    static const QVector<QString> triggers{
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS article_fts_ai AFTER INSERT ON article BEGIN"
            "  INSERT INTO article_fts(rowid, title, teaser)"
            "    VALUES (new.id, new.title, new.teaser);"
            " END"),
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS article_fts_ad AFTER DELETE ON article BEGIN"
            "  INSERT INTO article_fts(article_fts, rowid, title, teaser)"
            "    VALUES ('delete', old.id, old.title, old.teaser);"
            " END"),
        QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS article_fts_au AFTER UPDATE ON article BEGIN"
            "  INSERT INTO article_fts(article_fts, rowid, title, teaser)"
            "    VALUES ('delete', old.id, old.title, old.teaser);"
            "  INSERT INTO article_fts(rowid, title, teaser)"
            "    VALUES (new.id, new.title, new.teaser);"
            " END"),
    };
    for (const QString &trigger : triggers) {
        if (!exec(trigger)) {
            m_hasFts = false;
            m_lastError.clear();
            return false;
        }
    }

    // Catches up an index created against an already-populated database, which is what
    // happens on the first run after FTS5 becomes available.
    QSqlQuery count(m_db);
    if (count.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM article),"
                                  "       (SELECT COUNT(*) FROM article_fts)"))
        && count.next()
        && count.value(0).toInt() != count.value(1).toInt()) {
        exec(QStringLiteral("INSERT INTO article_fts(article_fts) VALUES('rebuild')"));
    }
    return true;
}

// --- Feeds ---------------------------------------------------------------------------

qint64 FeedStore::addFeed(const QString &url, const QString &title, const QString &lang,
                          const QString &langSource, const QString &category)
{
    QSqlQuery existing(m_db);
    existing.prepare(QStringLiteral("SELECT id FROM feed WHERE url = ?"));
    existing.addBindValue(url);
    if (existing.exec() && existing.next())
        return existing.value(0).toLongLong();

    // Regional subtags are dropped here rather than at every call site: the language is
    // a partition predicate, and "en-GB" must not be a different partition from "en-US".
    const QString normalised = i18n::normalizeLanguageTag(lang);

    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO feed (url, title, lang, lang_source, category) VALUES (?, ?, ?, ?, ?)"));
    insert.addBindValue(url);
    insert.addBindValue(title);
    insert.addBindValue(normalised.isEmpty() ? QVariant() : QVariant(normalised));
    insert.addBindValue(normalised.isEmpty() ? QVariant() : QVariant(langSource));
    insert.addBindValue(category.isEmpty() ? QVariant() : QVariant(category));
    if (!insert.exec()) {
        setError(QStringLiteral("cannot add feed"), insert.lastError().text());
        return -1;
    }
    return insert.lastInsertId().toLongLong();
}

bool FeedStore::removeFeed(qint64 feedId)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM feed WHERE id = ?"));
    query.addBindValue(feedId);
    if (!query.exec()) {
        setError(QStringLiteral("cannot remove feed"), query.lastError().text());
        return false;
    }
    // ON DELETE CASCADE took the articles; the clusters they were in may now be empty.
    exec(QStringLiteral("DELETE FROM cluster WHERE id NOT IN "
                        "(SELECT DISTINCT cluster_id FROM article WHERE cluster_id IS NOT NULL)"));
    return true;
}

QVector<FeedRecord> FeedStore::feeds(bool enabledOnly) const
{
    QVector<FeedRecord> result;
    QSqlQuery query(m_db);
    const QString sql = enabledOnly
        ? QStringLiteral("SELECT * FROM feed WHERE enabled = 1 ORDER BY id")
        : QStringLiteral("SELECT * FROM feed ORDER BY id");
    if (!query.exec(sql)) {
        setError(QStringLiteral("cannot list feeds"), query.lastError().text());
        return result;
    }
    while (query.next())
        result.append(feedFromRecord(query));
    return result;
}

bool FeedStore::feedById(qint64 feedId, FeedRecord *out) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM feed WHERE id = ?"));
    query.addBindValue(feedId);
    if (!query.exec() || !query.next())
        return false;
    if (out)
        *out = feedFromRecord(query);
    return true;
}

bool FeedStore::updateFeedMetadata(qint64 feedId, const ParsedFeed &parsed)
{
    FeedRecord current;
    if (!feedById(feedId, &current))
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE feed SET title = ?, site_url = ?, lang = ?, lang_source = ? WHERE id = ?"));
    // The feed's own title is only used when we have none. A catalogue entry or a name
    // the user typed was chosen by a person; a feed's <title> is frequently "Home",
    // "News" or the bare domain, and those become the source pills under a headline.
    query.addBindValue(current.title.isEmpty() ? parsed.title : current.title);
    query.addBindValue(parsed.siteUrl.isEmpty() ? current.siteUrl : parsed.siteUrl);

    // A manual override always wins: the user corrected us, so a later declared value
    // must not silently undo that. A catalogue value is likewise not overwritten -- it
    // was checked by hand, unlike the document's own claim (section 7).
    if (current.langSource == QLatin1String("manual")
        || current.langSource == QLatin1String("catalogue")
        || parsed.lang.isEmpty()) {
        query.addBindValue(current.lang.isEmpty() ? QVariant() : QVariant(current.lang));
        query.addBindValue(current.langSource.isEmpty() ? QVariant()
                                                        : QVariant(current.langSource));
    } else {
        query.addBindValue(i18n::normalizeLanguageTag(parsed.lang));
        query.addBindValue(QStringLiteral("declared"));
    }
    query.addBindValue(feedId);

    if (!query.exec()) {
        setError(QStringLiteral("cannot update feed metadata"), query.lastError().text());
        return false;
    }
    return true;
}

bool FeedStore::recordFetchSuccess(qint64 feedId, int httpStatus, const QString &etag,
                                   const QString &lastModified, qint64 now)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE feed SET etag = ?, last_modified = ?, last_fetch = ?, last_status = ?,"
        "  failure_count = 0, next_attempt = 0 WHERE id = ?"));
    query.addBindValue(etag.isEmpty() ? QVariant() : QVariant(etag));
    query.addBindValue(lastModified.isEmpty() ? QVariant() : QVariant(lastModified));
    query.addBindValue(now);
    query.addBindValue(httpStatus);
    query.addBindValue(feedId);
    if (!query.exec()) {
        setError(QStringLiteral("cannot record fetch success"), query.lastError().text());
        return false;
    }
    return true;
}

bool FeedStore::recordFetchFailure(qint64 feedId, int httpStatus, qint64 now)
{
    FeedRecord current;
    if (!feedById(feedId, &current))
        return false;

    const int failures = current.failureCount + 1;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE feed SET last_fetch = ?, last_status = ?, failure_count = ?,"
        "  next_attempt = ? WHERE id = ?"));
    query.addBindValue(now);
    query.addBindValue(httpStatus);
    query.addBindValue(failures);
    query.addBindValue(now + backoffSeconds(failures));
    query.addBindValue(feedId);
    if (!query.exec()) {
        setError(QStringLiteral("cannot record fetch failure"), query.lastError().text());
        return false;
    }
    return true;
}

QVector<FeedRecord> FeedStore::feedsDueForFetch(qint64 now) const
{
    QVector<FeedRecord> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT * FROM feed WHERE enabled = 1 AND next_attempt <= ? ORDER BY id"));
    query.addBindValue(now);
    if (!query.exec()) {
        setError(QStringLiteral("cannot list due feeds"), query.lastError().text());
        return result;
    }
    while (query.next())
        result.append(feedFromRecord(query));
    return result;
}

bool FeedStore::setFeedLanguage(qint64 feedId, const QString &lang, const QString &source)
{
    const QString normalised = i18n::normalizeLanguageTag(lang);

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE feed SET lang = ?, lang_source = ? WHERE id = ?"));
    query.addBindValue(normalised);
    query.addBindValue(source);
    query.addBindValue(feedId);
    if (!query.exec()) {
        setError(QStringLiteral("cannot set feed language"), query.lastError().text());
        return false;
    }
    // Articles inherit the feed's language (section 7), so an override has to reach them
    // or clustering keeps using the old value.
    QSqlQuery articles(m_db);
    articles.prepare(QStringLiteral("UPDATE article SET lang = ? WHERE feed_id = ?"));
    articles.addBindValue(normalised);
    articles.addBindValue(feedId);
    articles.exec();
    return true;
}

bool FeedStore::setFeedEnabled(qint64 feedId, bool enabled)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE feed SET enabled = ? WHERE id = ?"));
    query.addBindValue(enabled ? 1 : 0);
    query.addBindValue(feedId);
    if (!query.exec()) {
        setError(QStringLiteral("cannot change feed state"), query.lastError().text());
        return false;
    }
    return true;
}

// --- Articles ------------------------------------------------------------------------

namespace {

/*! One stored article as the clusterer needs to see it. */
struct Candidate
{
    qint64 id;
    qint64 clusterId;
    quint64 simhash;
};

} // namespace

IngestResult FeedStore::ingest(qint64 feedId, const QVector<ParsedArticle> &articles,
                               qint64 now, const cluster::Params &params)
{
    IngestResult result;

    FeedRecord feed;
    if (!feedById(feedId, &feed)) {
        setError(QStringLiteral("cannot ingest"), QStringLiteral("unknown feed"));
        return result;
    }

    if (!m_db.transaction()) {
        setError(QStringLiteral("cannot start ingest transaction"), m_db.lastError().text());
        return result;
    }

    for (const ParsedArticle &incoming : articles) {
        // Section 5: fall back to first-seen when the feed gave no usable date, so an
        // article without a timestamp still sorts and still clusters.
        const qint64 published = incoming.published > 0 ? incoming.published : now;

        QSqlQuery existing(m_db);
        existing.prepare(QStringLiteral(
            "SELECT id, title, cluster_id FROM article WHERE feed_id = ? AND guid = ?"));
        existing.addBindValue(feedId);
        existing.addBindValue(incoming.guid);
        if (!existing.exec()) {
            setError(QStringLiteral("cannot look up article"), existing.lastError().text());
            m_db.rollback();
            return IngestResult();
        }

        if (existing.next()) {
            const qint64 id = existing.value(0).toLongLong();
            const QString storedTitle = existing.value(1).toString();

            if (storedTitle == incoming.title) {
                ++result.unchanged;
                continue;
            }

            // Section 5: an existing article is updated, not re-inserted, and is only
            // re-clustered when the title actually changed -- which it just did.
            QSqlQuery update(m_db);
            update.prepare(QStringLiteral(
                "UPDATE article SET title = ?, teaser = ?, link = ?, image_url = ?,"
                "  published = ?, simhash = ? WHERE id = ?"));
            update.addBindValue(incoming.title);
            update.addBindValue(incoming.teaser);
            update.addBindValue(incoming.link);
            update.addBindValue(incoming.imageUrl.isEmpty() ? QVariant()
                                                            : QVariant(incoming.imageUrl));
            update.addBindValue(published);
            update.addBindValue(toSigned(cluster::simhash(
                cluster::clusterSubject(incoming.title))));
            update.addBindValue(id);
            if (!update.exec()) {
                setError(QStringLiteral("cannot update article"), update.lastError().text());
                m_db.rollback();
                return IngestResult();
            }
            ++result.updated;
            continue;
        }

        const quint64 simhash = cluster::simhash(cluster::clusterSubject(incoming.title));
        const QString lang = feed.lang;

        // Candidate selection (section 6.4): same language, inside the time window. The
        // Hamming comparison happens here rather than in SQL because SQLite has no
        // popcount -- and at a 48h window this is a few hundred rows, so a linear scan
        // is the right shape. Do not add LSH.
        QSqlQuery candidates(m_db);
        candidates.prepare(QStringLiteral(
            "SELECT id, cluster_id, simhash FROM article"
            " WHERE cluster_id IS NOT NULL"
            "   AND published BETWEEN ? AND ?"
            "   AND ((lang IS NULL AND ? IS NULL) OR lang = ?)"));
        candidates.addBindValue(published - params.windowSeconds);
        candidates.addBindValue(published + params.windowSeconds);
        candidates.addBindValue(lang.isEmpty() ? QVariant() : QVariant(lang));
        candidates.addBindValue(lang.isEmpty() ? QVariant() : QVariant(lang));

        qint64 bestCluster = -1;
        int bestDistance = params.hammingThreshold + 1;
        if (candidates.exec()) {
            while (candidates.next()) {
                const quint64 other =
                    static_cast<quint64>(candidates.value(2).toLongLong());
                const int distance = cluster::hammingDistance(simhash, other);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestCluster = candidates.value(1).toLongLong();
                }
            }
        }

        qint64 clusterId = -1;
        if (bestCluster >= 0 && bestDistance <= params.hammingThreshold) {
            clusterId = bestCluster;
            ++result.clustersJoined;
        } else {
            QSqlQuery createCluster(m_db);
            createCluster.prepare(QStringLiteral(
                "INSERT INTO cluster (lang, published, member_count) VALUES (?, ?, 0)"));
            createCluster.addBindValue(lang.isEmpty() ? QVariant() : QVariant(lang));
            createCluster.addBindValue(published);
            if (!createCluster.exec()) {
                setError(QStringLiteral("cannot create cluster"),
                         createCluster.lastError().text());
                m_db.rollback();
                return IngestResult();
            }
            clusterId = createCluster.lastInsertId().toLongLong();
            ++result.clustersCreated;
        }

        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral(
            "INSERT INTO article (feed_id, guid, link, title, teaser, published,"
            "  first_seen, image_url, image_state, lang, simhash, cluster_id, read)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)"));
        insert.addBindValue(feedId);
        insert.addBindValue(incoming.guid);
        insert.addBindValue(incoming.link);
        insert.addBindValue(incoming.title);
        insert.addBindValue(incoming.teaser);
        insert.addBindValue(published);
        insert.addBindValue(now);
        insert.addBindValue(incoming.imageUrl.isEmpty() ? QVariant()
                                                        : QVariant(incoming.imageUrl));
        insert.addBindValue(incoming.imageUrl.isEmpty() ? QStringLiteral("none")
                                                        : QStringLiteral("pending"));
        insert.addBindValue(lang.isEmpty() ? QVariant() : QVariant(lang));
        insert.addBindValue(toSigned(simhash));
        insert.addBindValue(clusterId);
        if (!insert.exec()) {
            setError(QStringLiteral("cannot insert article"), insert.lastError().text());
            m_db.rollback();
            return IngestResult();
        }
        ++result.inserted;
        refreshCluster(clusterId);
    }

    if (!m_db.commit()) {
        setError(QStringLiteral("cannot commit ingest"), m_db.lastError().text());
        m_db.rollback();
        return IngestResult();
    }
    return result;
}

bool FeedStore::refreshCluster(qint64 clusterId)
{
    // Lead article, per section 6.6: an article with an image first, then the earliest
    // published. Source priority is subscription order, which is the feed id.
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id FROM article WHERE cluster_id = ?"
        " ORDER BY (image_state = 'cached') DESC,"
        "          (image_url IS NOT NULL) DESC,"
        "          feed_id ASC,"
        "          published ASC"
        " LIMIT 1"));
    query.addBindValue(clusterId);
    if (!query.exec())
        return false;

    QVariant lead;
    if (query.next())
        lead = query.value(0);

    QSqlQuery update(m_db);
    update.prepare(QStringLiteral(
        "UPDATE cluster SET lead_article_id = ?,"
        "  member_count = (SELECT COUNT(*) FROM article WHERE cluster_id = ?),"
        "  published = (SELECT MAX(published) FROM article WHERE cluster_id = ?)"
        " WHERE id = ?"));
    update.addBindValue(lead);
    update.addBindValue(clusterId);
    update.addBindValue(clusterId);
    update.addBindValue(clusterId);
    return update.exec();
}

namespace {

ClusterRecord clusterFromRecord(const QSqlQuery &query)
{
    ClusterRecord record;
    record.id = query.value(QStringLiteral("id")).toLongLong();
    const QVariant lead = query.value(QStringLiteral("lead_article_id"));
    record.leadArticleId = lead.isNull() ? -1 : lead.toLongLong();
    record.lang = query.value(QStringLiteral("lang")).toString();
    record.published = query.value(QStringLiteral("published")).toLongLong();
    record.memberCount = query.value(QStringLiteral("member_count")).toInt();
    return record;
}

} // namespace

QVector<ClusterRecord> FeedStore::clusters(int limit, int offset) const
{
    QVector<ClusterRecord> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT * FROM cluster WHERE member_count > 0"
        " ORDER BY published DESC LIMIT ? OFFSET ?"));
    query.addBindValue(limit);
    query.addBindValue(offset);
    if (!query.exec()) {
        setError(QStringLiteral("cannot list clusters"), query.lastError().text());
        return result;
    }
    while (query.next())
        result.append(clusterFromRecord(query));
    return result;
}

QVector<ClusterSummary> FeedStore::clusterSummaries(int limit, int offset) const
{
    return clusterSummaries(ClusterFilter(), 0, limit, offset);
}

QVector<ClusterSummary> FeedStore::clusterSummaries(const ClusterFilter &filter, qint64 now,
                                                    int limit, int offset) const
{
    QVector<ClusterSummary> result;

    // The filter restricts on properties of a cluster's *members* -- a story counts as
    // Hamburg news if any Hamburg feed carried it -- so it is an EXISTS over the member
    // rows rather than a condition on the lead article. Filtering on the lead alone would
    // drop a story from the region filter merely because a national paper happened to
    // supply the picture.
    QStringList conditions;
    QVariantList bindings;

    if (filter.unreadOnly)
        conditions << QStringLiteral("m.read = 0");
    if (!filter.category.isEmpty()) {
        conditions << QStringLiteral("mf.category = ?");
        bindings << filter.category;
    }
    if (filter.feedId >= 0) {
        conditions << QStringLiteral("m.feed_id = ?");
        bindings << filter.feedId;
    }

    QString where = QStringLiteral("c.member_count > 0");
    if (!conditions.isEmpty()) {
        where += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM article m JOIN feed mf ON mf.id = m.feed_id"
            "              WHERE m.cluster_id = c.id AND %1)").arg(conditions.join(
            QStringLiteral(" AND ")));
    }
    if (filter.maxAgeSeconds > 0)
        where += QStringLiteral(" AND c.published >= ?");

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT c.id, c.member_count, c.published, a.id, a.title, a.teaser, a.link,"
        "       a.image_url, a.image_state"
        "  FROM cluster c JOIN article a ON a.id = c.lead_article_id"
        " WHERE %1"
        " ORDER BY c.published DESC LIMIT ? OFFSET ?").arg(where));
    for (const QVariant &value : bindings)
        query.addBindValue(value);
    if (filter.maxAgeSeconds > 0)
        query.addBindValue(now - filter.maxAgeSeconds);
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec()) {
        setError(QStringLiteral("cannot list clusters"), query.lastError().text());
        return result;
    }

    QHash<qint64, int> rowOf;
    QStringList ids;
    while (query.next()) {
        ClusterSummary summary;
        summary.clusterId = query.value(0).toLongLong();
        summary.memberCount = query.value(1).toInt();
        summary.published = query.value(2).toLongLong();
        summary.leadArticleId = query.value(3).toLongLong();
        summary.title = query.value(4).toString();
        summary.teaser = query.value(5).toString();
        summary.link = query.value(6).toString();
        summary.imageUrl = query.value(7).toString();
        summary.imageState = query.value(8).toString();

        rowOf.insert(summary.clusterId, result.size());
        ids.append(QString::number(summary.clusterId));
        result.append(summary);
    }
    if (result.isEmpty())
        return result;

    // One further query for every member of every cluster on this page, rather than one
    // per row. The id list is built from integers we just read out of the database, so
    // there is nothing to bind and nothing to inject.
    QSqlQuery members(m_db);
    if (!members.exec(QStringLiteral(
            "SELECT ar.cluster_id, ar.id, ar.read, f.title, f.url"
            "  FROM article ar JOIN feed f ON f.id = ar.feed_id"
            " WHERE ar.cluster_id IN (%1)"
            " ORDER BY ar.cluster_id, ar.published ASC").arg(ids.join(QLatin1Char(','))))) {
        setError(QStringLiteral("cannot list cluster members"), members.lastError().text());
        return result;
    }

    while (members.next()) {
        const qint64 clusterId = members.value(0).toLongLong();
        const auto it = rowOf.constFind(clusterId);
        if (it == rowOf.constEnd())
            continue;

        ClusterSummary &summary = result[it.value()];
        const qint64 articleId = members.value(1).toLongLong();
        if (members.value(2).toInt() == 0)
            summary.unread = true;

        QString source = members.value(3).toString();
        if (source.isEmpty())
            source = members.value(4).toString();
        if (summary.sources.contains(source))
            continue;
        // The lead article's source leads the pill row, whatever order the members came
        // back in -- it is the one whose headline is being shown.
        if (articleId == summary.leadArticleId)
            summary.sources.prepend(source);
        else
            summary.sources.append(source);
    }
    return result;
}

QStringList FeedStore::subscribedCategories() const
{
    QStringList categories;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
            "SELECT DISTINCT category FROM feed"
            " WHERE enabled = 1 AND category IS NOT NULL AND category <> ''"
            " ORDER BY category"))) {
        return categories;
    }
    while (query.next())
        categories << query.value(0).toString();
    return categories;
}

bool FeedStore::clusterById(qint64 clusterId, ClusterRecord *out) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM cluster WHERE id = ?"));
    query.addBindValue(clusterId);
    if (!query.exec() || !query.next())
        return false;
    if (out)
        *out = clusterFromRecord(query);
    return true;
}

bool FeedStore::articleById(qint64 articleId, ArticleRecord *out) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT * FROM article WHERE id = ?"));
    query.addBindValue(articleId);
    if (!query.exec() || !query.next())
        return false;
    if (out)
        *out = articleFromRecord(query);
    return true;
}

QVector<ArticleRecord> FeedStore::articlesInCluster(qint64 clusterId) const
{
    QVector<ArticleRecord> result;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT * FROM article WHERE cluster_id = ? ORDER BY published ASC"));
    query.addBindValue(clusterId);
    if (!query.exec()) {
        setError(QStringLiteral("cannot list cluster members"), query.lastError().text());
        return result;
    }
    while (query.next())
        result.append(articleFromRecord(query));
    return result;
}

QVector<qint64> FeedStore::allArticleIds() const
{
    QVector<qint64> ids;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id FROM article")))
        return ids;
    while (query.next())
        ids.append(query.value(0).toLongLong());
    return ids;
}

bool FeedStore::markRead(qint64 articleId, bool read)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE article SET read = ? WHERE id = ?"));
    query.addBindValue(read ? 1 : 0);
    query.addBindValue(articleId);
    return query.exec();
}

int FeedStore::markAllRead()
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("UPDATE article SET read = 1 WHERE read = 0")))
        return 0;
    return query.numRowsAffected();
}

int FeedStore::unreadCount() const
{
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM article WHERE read = 0"))
        && query.next())
        return query.value(0).toInt();
    return 0;
}

bool FeedStore::setImageState(qint64 articleId, const QString &state)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE article SET image_state = ? WHERE id = ?"));
    query.addBindValue(state);
    query.addBindValue(articleId);
    return query.exec();
}

// --- Retention -----------------------------------------------------------------------

bool FeedStore::setImageUrl(qint64 articleId, const QString &url)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE article SET image_url = ? WHERE id = ?"));
    query.addBindValue(url.isEmpty() ? QVariant() : QVariant(url));
    query.addBindValue(articleId);
    return query.exec();
}

int FeedStore::purgeArticlesOlderThan(qint64 cutoff)
{
    if (!m_db.transaction()) {
        setError(QStringLiteral("cannot start purge transaction"), m_db.lastError().text());
        return 0;
    }

    // Remember which clusters are affected before the rows disappear.
    QVector<qint64> touched;
    QSqlQuery affected(m_db);
    affected.prepare(QStringLiteral(
        "SELECT DISTINCT cluster_id FROM article WHERE published < ? AND cluster_id IS NOT NULL"));
    affected.addBindValue(cutoff);
    if (affected.exec()) {
        while (affected.next())
            touched.append(affected.value(0).toLongLong());
    }

    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM article WHERE published < ?"));
    del.addBindValue(cutoff);
    if (!del.exec()) {
        setError(QStringLiteral("cannot purge articles"), del.lastError().text());
        m_db.rollback();
        return 0;
    }
    const int deleted = del.numRowsAffected();

    // A cluster whose members are gone would otherwise stay in the list as an entry with
    // nothing behind it, and a surviving cluster may have lost its lead article.
    for (const qint64 clusterId : touched)
        refreshCluster(clusterId);
    exec(QStringLiteral("DELETE FROM cluster WHERE member_count = 0"));

    if (!m_db.commit()) {
        setError(QStringLiteral("cannot commit purge"), m_db.lastError().text());
        m_db.rollback();
        return 0;
    }
    return deleted;
}

// --- Search --------------------------------------------------------------------------

QVector<ArticleRecord> FeedStore::search(const QString &text, int limit) const
{
    QVector<ArticleRecord> result;
    if (!m_hasFts || text.trimmed().isEmpty())
        return result;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT a.* FROM article_fts f JOIN article a ON a.id = f.rowid"
        " WHERE article_fts MATCH ? ORDER BY a.published DESC LIMIT ?"));
    // The trigram tokenizer matches substrings, so the raw text is the query. Quoting it
    // keeps FTS5 operators in user input from being interpreted as syntax.
    query.addBindValue(QLatin1Char('"') + QString(text).replace(QLatin1Char('"'),
                                                                QStringLiteral("\"\""))
                       + QLatin1Char('"'));
    query.addBindValue(limit);
    if (!query.exec()) {
        setError(QStringLiteral("search failed"), query.lastError().text());
        return result;
    }
    while (query.next())
        result.append(articleFromRecord(query));
    return result;
}

} // namespace feed

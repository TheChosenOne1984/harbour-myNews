#include "catalogservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

namespace catalog {
namespace {

/*! Validators sit beside the cache file so a restart can still send a conditional GET. */
QString validatorPath(const QString &cachePath)
{
    return cachePath + QStringLiteral(".meta");
}

} // namespace

CatalogService::CatalogService(feed::FeedFetcher *fetcher, const QString &cacheFilePath,
                               QObject *parent)
    : QObject(parent)
    , m_fetcher(fetcher)
    , m_cachePath(cacheFilePath)
{
    connect(m_fetcher, &feed::FeedFetcher::finished, this, &CatalogService::onFetchFinished);
}

bool CatalogService::loadFromCache()
{
    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const Catalog parsed = parseCatalog(file.readAll());
    if (!parsed.isValid())
        return false;
    m_catalog = parsed;

    QSettings meta(validatorPath(m_cachePath), QSettings::IniFormat);
    m_etag = meta.value(QStringLiteral("etag")).toString();
    m_lastModified = meta.value(QStringLiteral("lastModified")).toString();
    return true;
}

QDateTime CatalogService::lastChecked() const
{
    if (!QFileInfo::exists(m_cachePath))
        return QDateTime();

    QSettings meta(validatorPath(m_cachePath), QSettings::IniFormat);
    const QString stored = meta.value(QStringLiteral("lastChecked")).toString();
    const QDateTime parsed = QDateTime::fromString(stored, Qt::ISODate);
    if (parsed.isValid())
        return parsed;
    // A cache written by an older build has no marker; fall back to the file time.
    return QFileInfo(m_cachePath).lastModified();
}

void CatalogService::recordChecked()
{
    QSettings meta(validatorPath(m_cachePath), QSettings::IniFormat);
    meta.setValue(QStringLiteral("lastChecked"),
                  QDateTime::currentDateTime().toString(Qt::ISODate));
    meta.sync();
}

bool CatalogService::needsRefresh(const QDateTime &now) const
{
    const QDateTime checked = lastChecked();
    if (!checked.isValid())
        return true;
    return checked.secsTo(now) >= RefreshIntervalSeconds;
}

void CatalogService::refresh(const QString &url, bool force)
{
    if (m_requestInFlight)
        return;

    if (!force && hasCatalog() && !needsRefresh()) {
        emit refreshed(false, QString());
        return;
    }

    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty()) {
        emit refreshed(false, tr("The catalogue address is not a valid URL."));
        return;
    }

    feed::FeedFetcher::Request request;
    request.id = RequestId;
    request.url = parsed;
    // Conditional GET here too: the catalogue changes rarely, so most refreshes should
    // cost a 304.
    request.etag = m_etag;
    request.lastModified = m_lastModified;

    m_requestInFlight = true;
    m_fetcher->enqueue(request);
}

void CatalogService::onFetchFinished(const feed::FeedFetcher::Result &result)
{
    if (result.id != RequestId)
        return;   // a feed, not us -- the fetcher is shared
    m_requestInFlight = false;

    if (result.notModified) {
        // Still current, so the refresh interval restarts even though nothing was
        // downloaded. QFileDevice::setFileTime only exists from Qt 5.10, and the file's
        // own timestamp would be the wrong thing to trust anyway.
        recordChecked();
        emit refreshed(false, QString());
        return;
    }

    if (!result.isSuccess()) {
        emit refreshed(false, result.error);
        return;
    }

    const Catalog parsed = parseCatalog(result.body);
    if (!parsed.isValid()) {
        // Keep whatever we had: a broken document upstream must not empty a working
        // catalogue on every device that refreshes.
        emit refreshed(false, parsed.error);
        return;
    }

    m_catalog = parsed;
    m_etag = result.etag;
    m_lastModified = result.lastModified;
    writeCache(result.body);
    emit refreshed(true, QString());
}

bool CatalogService::writeCache(const QByteArray &json)
{
    const QDir dir = QFileInfo(m_cachePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    // Write to a temporary file and rename, so an interrupted write cannot leave a
    // truncated catalogue that then fails to parse on every start.
    const QString tempPath = m_cachePath + QStringLiteral(".tmp");
    QFile file(tempPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (file.write(json) != json.size()) {
        file.close();
        file.remove();
        return false;
    }
    file.close();

    QFile::remove(m_cachePath);
    if (!QFile::rename(tempPath, m_cachePath)) {
        QFile::remove(tempPath);
        return false;
    }

    QSettings meta(validatorPath(m_cachePath), QSettings::IniFormat);
    meta.setValue(QStringLiteral("etag"), m_etag);
    meta.setValue(QStringLiteral("lastModified"), m_lastModified);
    meta.setValue(QStringLiteral("lastChecked"),
                  QDateTime::currentDateTime().toString(Qt::ISODate));
    meta.sync();
    return true;
}

} // namespace catalog

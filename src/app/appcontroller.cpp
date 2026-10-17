#include "appcontroller.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QDir>

#include "app/backend.h"
#include "feed/feedfetcher.h"
#include "i18n/languagenames.h"
#include "i18n/relativetime.h"

#include <QCryptographicHash>
#include <QDateTime>

namespace app {

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_backend(new Backend)
{
    m_backend->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_backend, &QObject::deleteLater);

    connect(m_backend, &Backend::initialised, this, [this](bool ok, const QString &error) {
        m_ready = ok;
        emit readyChanged();
        if (!ok) {
            setStatusMessage(error);
            return;
        }
        if (!m_settings)
            return;
        applyThumbnailCacheLimit();
        // Section 4: retention runs on start, in the background. It also takes the
        // thumbnails of the articles it deletes, which is what keeps the cache from
        // filling with pictures for stories that no longer exist.
        QMetaObject::invokeMethod(m_backend, "purge", Qt::QueuedConnection,
                                  Q_ARG(int, m_settings->retentionDays()));
        // Section 12: refresh on app start, on the cover action and on pull-down. Queued
        // behind the purge, so it never ingests into rows about to be deleted. Feeds still
        // in backoff are skipped by the refresher, and with no subscriptions this is a
        // no-op -- a first start contacts nobody.
        refresh();
    });

    connect(m_backend, &Backend::feedsChanged, this,
            [this](const QVector<feed::FeedRecord> &feeds) {
        const int before = m_subscriptions.rowCount();
        m_subscriptions.setFeeds(feeds);
        // The catalogue page shows which entries are already subscribed, so the two
        // stay in step without either knowing about the other.
        m_catalog.setSubscribedUrls(m_subscriptions.subscribedUrls());
        if (m_subscriptions.rowCount() != before)
            emit subscriptionCountChanged();
    });

    connect(m_backend, &Backend::catalogChanged, this,
            [this](const catalog::Catalog &catalog) {
        m_catalog.setCatalog(catalog);
        m_catalog.setSubscribedUrls(m_subscriptions.subscribedUrls());
        setCatalogError(QString());
        emit hasCatalogChanged();
    });

    connect(m_backend, &Backend::clustersChanged, this,
            [this](const QVector<feed::ClusterSummary> &clusters) {
        m_clusters.setClusters(clusters);
        const QString headline = clusters.isEmpty() ? QString() : clusters.first().title;
        if (headline != m_leadHeadline) {
            m_leadHeadline = headline;
            emit leadHeadlineChanged();
        }
    });

    connect(m_backend, &Backend::clustersAppended, this,
            [this](const QVector<feed::ClusterSummary> &clusters) {
        m_clusters.appendClusters(clusters);
    });

    connect(m_backend, &Backend::hasMoreClustersChanged, this, [this](bool hasMore) {
        if (hasMore == m_hasMoreArticles)
            return;
        m_hasMoreArticles = hasMore;
        emit hasMoreArticlesChanged();
    });

    connect(m_backend, &Backend::subscribedCategoriesChanged, this,
            [this](const QStringList &categories) {
        if (categories == m_subscribedCategories)
            return;
        m_subscribedCategories = categories;
        // A category the user is no longer subscribed to would filter to an empty list
        // with no way back except guessing, so the filter drops it.
        if (!m_filterCategory.isEmpty() && !categories.contains(m_filterCategory)) {
            m_filterCategory.clear();
            pushFilter();
        }
        emit subscribedCategoriesChanged();
    });

    connect(m_backend, &Backend::unreadCountChanged, this, [this](int count) {
        if (count == m_unreadCount)
            return;
        m_unreadCount = count;
        emit unreadCountChanged();
    });

    connect(m_backend, &Backend::clusterMembersLoaded, this,
            [this](qint64 clusterId, const QVector<feed::ArticleRecord> &members) {
        QVariantList list;
        for (const feed::ArticleRecord &article : members) {
            QVariantMap map;
            map.insert(QStringLiteral("articleId"), article.id);
            map.insert(QStringLiteral("title"), article.title);
            map.insert(QStringLiteral("link"), article.link);
            map.insert(QStringLiteral("published"), article.published);
            map.insert(QStringLiteral("read"), article.read);
            map.insert(QStringLiteral("source"), article.sourceTitle);
            list.append(map);
        }
        emit clusterMembersLoaded(clusterId, list);
    });

    connect(m_backend, &Backend::searchFinished, this,
            [this](const QString &text, const QVector<feed::ArticleRecord> &hits) {
        QVariantList list;
        for (const feed::ArticleRecord &article : hits) {
            QVariantMap map;
            map.insert(QStringLiteral("articleId"), article.id);
            map.insert(QStringLiteral("clusterId"), article.clusterId);
            map.insert(QStringLiteral("title"), article.title);
            map.insert(QStringLiteral("teaser"), article.teaser);
            map.insert(QStringLiteral("link"), article.link);
            map.insert(QStringLiteral("published"), article.published);
            map.insert(QStringLiteral("read"), article.read);
            map.insert(QStringLiteral("source"), article.sourceTitle);
            list.append(map);
        }
        emit searchFinished(text, list);
    });

    connect(m_backend, &Backend::thumbnailReady, this,
            [this](qint64 articleId, const QString &path) {
        m_clusters.setRowThumbnail(articleId, path);
    });

    connect(m_backend, &Backend::busyChanged, this, [this](bool busy) { setBusy(busy); });

    connect(m_backend, &Backend::refreshFinished, this,
            [this](int ok, int failed, int added, const QString &error) {
        if (!error.isEmpty())
            setStatusMessage(error);
        emit refreshFinished(ok, failed, added);
    });

    connect(m_backend, &Backend::catalogRefreshFinished, this,
            [this](bool changed, const QString &error) {
        setCatalogError(error);

        // A button that does something has to say what it did. Without this a manual
        // reload looked identical whether it succeeded, was already current, or failed.
        QString status;
        if (!error.isEmpty()) {
            status = hasCatalog()
                ? tr("Could not reload: %1. The stored catalogue is still in use.").arg(error)
                : tr("Could not load the catalogue: %1").arg(error);
        } else if (changed) {
            status = tr("Loaded %n source(s).", "", m_catalog.totalCount());
        } else {
            status = tr("Already up to date: %n source(s).", "", m_catalog.totalCount());
        }
        if (status != m_catalogStatus) {
            m_catalogStatus = status;
            emit catalogStatusChanged();
        }
        if (!error.isEmpty() && hasCatalog())
            setStatusMessage(status);
    });

    connect(m_backend, &Backend::feedProbed, this, &AppController::feedProbed);

    connect(m_backend, &Backend::opmlImported, this,
            [this](int added, int skipped, const QString &error) {
        Q_UNUSED(skipped)
        if (!error.isEmpty())
            setStatusMessage(tr("Import failed: %1").arg(error));
        else
            setStatusMessage(tr("Imported %n subscription(s)", "", added));
    });

    connect(m_backend, &Backend::opmlExported, this,
            [this](const QString &path, const QString &error) {
        setStatusMessage(error.isEmpty() ? tr("Exported to %1").arg(path)
                                         : tr("Export failed: %1").arg(error));
    });

    connect(m_backend, &Backend::message, this, &AppController::setStatusMessage);
}

AppController::~AppController()
{
    m_thread.quit();
    m_thread.wait(5000);
}

void AppController::applyThumbnailCacheLimit()
{
    if (!m_settings)
        return;
    QMetaObject::invokeMethod(m_backend, "setThumbnailCacheLimit", Qt::QueuedConnection,
                              Q_ARG(qint64, qint64(m_settings->thumbnailCacheMegabytes())
                                                * 1024 * 1024));
}

void AppController::setSettings(Settings *settings)
{
    m_settings = settings;
    if (m_settings) {
        connect(m_settings, &Settings::thumbnailCacheMegabytesChanged,
                this, &AppController::applyThumbnailCacheLimit);
        connect(m_settings, &Settings::catalogConsentChanged,
                this, &AppController::catalogConsentChanged);
        // The models outlive the view reload a language switch causes, so the catalogue's
        // category names and descriptions have to be told about the switch themselves.
        connect(m_settings, &Settings::uiLanguageChanged,
                this, &AppController::applyCatalogueLanguage);
        applyCatalogueLanguage();
    }
}

void AppController::applyCatalogueLanguage()
{
    if (!m_settings)
        return;
    // QLocale::system() rather than QLocale(): the default follows the UI language, and
    // "System" has to mean the device here.
    m_catalog.setUiLanguage(m_settings->uiLanguage().isEmpty()
                                ? QLocale::system().name().left(2)
                                : m_settings->uiLanguage());
}

void AppController::start(const QString &databasePath, const QString &catalogCachePath,
                          const QString &thumbnailDirectory)
{
    m_thread.start();
    QMetaObject::invokeMethod(m_backend, "initialise", Qt::QueuedConnection,
                              Q_ARG(QString, databasePath),
                              Q_ARG(QString, catalogCachePath),
                              Q_ARG(QString, thumbnailDirectory));
}

void AppController::requestThumbnail(qint64 articleId)
{
    QMetaObject::invokeMethod(m_backend, "requestThumbnail", Qt::QueuedConnection,
                              Q_ARG(qint64, articleId));
}

void AppController::setStatusMessage(const QString &text)
{
    if (text == m_statusMessage)
        return;
    m_statusMessage = text;
    emit statusMessageChanged();
}

void AppController::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void AppController::setCatalogError(const QString &error)
{
    if (error == m_catalogError)
        return;
    m_catalogError = error;
    emit catalogErrorChanged();
}

void AppController::refresh()
{
    if (!m_settings)
        return;
    QMetaObject::invokeMethod(m_backend, "refreshFeeds", Qt::QueuedConnection,
                              Q_ARG(int, m_settings->clusterThreshold()),
                              Q_ARG(int, m_settings->clusterWindowHours()),
                              Q_ARG(int, m_settings->retentionDays()));
}

bool AppController::needsCatalogConsent() const
{
    return m_settings && m_settings->catalogConsent() == Settings::Unasked;
}

bool AppController::catalogDeclined() const
{
    return m_settings && m_settings->catalogConsent() == Settings::Denied;
}

void AppController::setCatalogConsent(bool granted)
{
    if (!m_settings)
        return;
    m_settings->setCatalogConsent(granted ? Settings::Granted : Settings::Denied);
    emit catalogConsentChanged();
    if (granted)
        refreshCatalog(true);
    else
        setCatalogError(QString());
}

void AppController::refreshCatalog(bool force)
{
    if (!m_settings)
        return;
    if (m_settings->catalogConsent() != Settings::Granted) {
        // Nothing is contacted before the user has agreed. Not an error: declining is a
        // supported choice, and adding feeds by URL never needs the catalogue (§10).
        return;
    }
    QMetaObject::invokeMethod(m_backend, "refreshCatalog", Qt::QueuedConnection,
                              Q_ARG(QString, m_settings->catalogUrl()),
                              Q_ARG(bool, force));
}

void AppController::subscribeFromCatalog(int row)
{
    const QVariantMap entry = m_catalog.entryAt(row);
    if (entry.isEmpty())
        return;
    QMetaObject::invokeMethod(m_backend, "subscribe", Qt::QueuedConnection,
                              Q_ARG(QString, entry.value(QStringLiteral("url")).toString()),
                              Q_ARG(QString, entry.value(QStringLiteral("title")).toString()),
                              Q_ARG(QString, entry.value(QStringLiteral("lang")).toString()),
                              Q_ARG(QString, QStringLiteral("catalogue")),
                              // Carried through so the article list can filter by region.
                              Q_ARG(QString, entry.value(QStringLiteral("category")).toString()));
}

void AppController::unsubscribeUrl(const QString &url)
{
    for (int row = 0; row < m_subscriptions.rowCount(); ++row) {
        const QModelIndex index = m_subscriptions.index(row);
        if (m_subscriptions.data(index, model::SubscriptionModel::UrlRole).toString() == url) {
            unsubscribe(m_subscriptions.data(index, model::SubscriptionModel::FeedIdRole)
                            .toLongLong());
            return;
        }
    }
}

void AppController::unsubscribe(qint64 feedId)
{
    QMetaObject::invokeMethod(m_backend, "unsubscribe", Qt::QueuedConnection,
                              Q_ARG(qint64, feedId));
}

void AppController::setFeedEnabled(qint64 feedId, bool enabled)
{
    QMetaObject::invokeMethod(m_backend, "setFeedEnabled", Qt::QueuedConnection,
                              Q_ARG(qint64, feedId), Q_ARG(bool, enabled));
}

void AppController::setFeedLanguage(qint64 feedId, const QString &lang)
{
    QMetaObject::invokeMethod(m_backend, "setFeedLanguage", Qt::QueuedConnection,
                              Q_ARG(qint64, feedId), Q_ARG(QString, lang));
}

void AppController::probeFeed(const QString &url)
{
    QMetaObject::invokeMethod(m_backend, "probeFeed", Qt::QueuedConnection,
                              Q_ARG(QString, url));
}

void AppController::addFeed(const QString &url, const QString &lang, const QString &title)
{
    // The user picked the language, so it is manual and survives every later refresh.
    QMetaObject::invokeMethod(m_backend, "subscribe", Qt::QueuedConnection,
                              Q_ARG(QString, url), Q_ARG(QString, title),
                              Q_ARG(QString, lang), Q_ARG(QString, QStringLiteral("manual")),
                              Q_ARG(QString, QString()));   // no region for a hand-added feed
}

void AppController::loadMoreArticles()
{
    if (!m_hasMoreArticles)
        return;
    QMetaObject::invokeMethod(m_backend, "loadMoreClusters", Qt::QueuedConnection);
}

void AppController::prefetchThumbnails(int firstRow, int count)
{
    const QVector<qint64> ids = m_clusters.leadArticleIds(firstRow, count);
    for (const qint64 id : ids) {
        QMetaObject::invokeMethod(m_backend, "requestThumbnail", Qt::QueuedConnection,
                                  Q_ARG(qint64, id));
    }
}

void AppController::markClusterRead(qint64 clusterId, bool read)
{
    // The row updates straight away; the database catches up on the worker thread. A
    // reader tapping an article should not wait for a write to come back.
    m_clusters.setRowRead(clusterId, read);
    QMetaObject::invokeMethod(m_backend, "markClusterRead", Qt::QueuedConnection,
                              Q_ARG(qint64, clusterId), Q_ARG(bool, read));
}

bool AppController::isClusterUnread(qint64 clusterId) const
{
    const int row = m_clusters.indexOfCluster(clusterId);
    if (row < 0)
        return false;
    return m_clusters.data(m_clusters.index(row), model::ClusterModel::UnreadRole).toBool();
}

// --- Filter --------------------------------------------------------------------------

void AppController::pushFilter()
{
    QMetaObject::invokeMethod(m_backend, "setClusterFilter", Qt::QueuedConnection,
                              Q_ARG(bool, m_filterUnreadOnly),
                              Q_ARG(QString, m_filterCategory),
                              Q_ARG(qint64, m_filterFeedId),
                              Q_ARG(qint64, qint64(m_filterMaxAgeHours) * 3600));
    emit filterChanged();
}

void AppController::setFilterUnreadOnly(bool unreadOnly)
{
    if (unreadOnly == m_filterUnreadOnly)
        return;
    m_filterUnreadOnly = unreadOnly;
    pushFilter();
}

void AppController::setFilterCategory(const QString &category)
{
    if (category == m_filterCategory)
        return;
    m_filterCategory = category;
    pushFilter();
}

void AppController::setFilterFeedId(qint64 feedId)
{
    if (feedId == m_filterFeedId)
        return;
    m_filterFeedId = feedId;
    pushFilter();
}

void AppController::setFilterMaxAgeHours(int hours)
{
    if (hours == m_filterMaxAgeHours)
        return;
    m_filterMaxAgeHours = hours;
    pushFilter();
}

bool AppController::filterActive() const
{
    return m_filterUnreadOnly || !m_filterCategory.isEmpty() || m_filterFeedId >= 0
        || m_filterMaxAgeHours > 0;
}

QString AppController::filterSummary() const
{
    QStringList parts;
    if (m_filterUnreadOnly)
        parts << tr("unread");
    if (!m_filterCategory.isEmpty())
        parts << m_catalog.categoryName(m_filterCategory);
    if (m_filterFeedId >= 0) {
        const int row = m_subscriptions.indexOfFeed(m_filterFeedId);
        if (row >= 0) {
            parts << m_subscriptions.data(m_subscriptions.index(row),
                                          model::SubscriptionModel::TitleRole).toString();
        }
    }
    if (m_filterMaxAgeHours > 0) {
        parts << (m_filterMaxAgeHours < 48
                      ? tr("%n hour(s)", "", m_filterMaxAgeHours)
                      : tr("%n day(s)", "", m_filterMaxAgeHours / 24));
    }
    return parts.join(QStringLiteral(" · "));
}

void AppController::clearFilter()
{
    if (!filterActive())
        return;
    m_filterUnreadOnly = false;
    m_filterCategory.clear();
    m_filterFeedId = -1;
    m_filterMaxAgeHours = 0;
    pushFilter();
}

void AppController::markAllRead()
{
    m_clusters.setAllRead();
    QMetaObject::invokeMethod(m_backend, "markAllRead", Qt::QueuedConnection);
}

void AppController::search(const QString &text)
{
    QMetaObject::invokeMethod(m_backend, "search", Qt::QueuedConnection,
                              Q_ARG(QString, text));
}

void AppController::loadClusterMembers(qint64 clusterId)
{
    QMetaObject::invokeMethod(m_backend, "loadClusterMembers", Qt::QueuedConnection,
                              Q_ARG(qint64, clusterId));
}

QString AppController::relativeTime(qint64 published) const
{
    return i18n::relativeTime(published, QDateTime::currentMSecsSinceEpoch() / 1000);
}

bool AppController::canOpen(const QString &link) const
{
    return feed::isFetchableUrl(link);
}

QString AppController::sourceColour(const QString &source) const
{
    // Section 8: a missing thumbnail becomes a source-coloured tile, not a broken-image
    // icon or a blank gap. The colour has to be stable for a source across restarts, so
    // it comes from a hash of the name rather than from a counter.
    if (source.isEmpty())
        return QStringLiteral("#606060");

    const QByteArray digest =
        QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Md5);
    const int hue = static_cast<quint8>(digest.at(0)) * 360 / 256;
    // Fixed saturation and lightness keep every tile legible against Silica's dark
    // ambience and stop one source shouting louder than another.
    return QColor::fromHsl(hue, 110, 90).name();
}

void AppController::importOpml(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatusMessage(tr("Cannot read %1").arg(filePath));
        return;
    }
    const QByteArray xml = file.readAll();

    // Feeds whose language the file does not state get this and are flagged for review,
    // rather than the import stopping once per feed to ask (§7).
    const QString fallback = m_settings && !m_settings->uiLanguage().isEmpty()
        ? m_settings->uiLanguage()
        : QLocale::system().name().left(2);

    QMetaObject::invokeMethod(m_backend, "importOpml", Qt::QueuedConnection,
                              Q_ARG(QByteArray, xml), Q_ARG(QString, fallback));
}

void AppController::exportOpml(const QString &filePath)
{
    QMetaObject::invokeMethod(m_backend, "exportOpml", Qt::QueuedConnection,
                              Q_ARG(QString, filePath));
}

QStringList AppController::opmlCandidates() const
{
    QDir dir(opmlFolder());
    if (!dir.exists())
        return QStringList();

    // .xml as well as .opml: plenty of readers export "subscriptions.xml".
    dir.setNameFilters(QStringList() << QStringLiteral("*.opml") << QStringLiteral("*.xml"));
    dir.setFilter(QDir::Files | QDir::Readable);
    dir.setSorting(QDir::Time);

    QStringList paths;
    for (const QFileInfo &info : dir.entryInfoList())
        paths.append(info.absoluteFilePath());
    return paths;
}

QString AppController::opmlFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString AppController::defaultOpmlExportPath() const
{
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents + QStringLiteral("/mynews-subscriptions.opml");
}

QStringList AppController::suggestedLanguages() const
{
    // Whatever the catalogue uses, so the common cases are one tap. Deliberately the
    // whole catalogue rather than the filtered view: the add-feed page has no filter.
    QStringList codes = m_catalog.languages();
    for (const QString &fallback : {QStringLiteral("de"), QStringLiteral("en")}) {
        if (!codes.contains(fallback))
            codes.append(fallback);
    }
    const QString device = QLocale::system().name().left(2);
    if (!device.isEmpty() && !codes.contains(device))
        codes.prepend(device);
    return codes;
}

QString AppController::languageDisplayName(const QString &code) const
{
    const QString name = i18n::languageDisplayName(code);
    return name.isEmpty() ? code : QStringLiteral("%1 (%2)").arg(name, code);
}

} // namespace app

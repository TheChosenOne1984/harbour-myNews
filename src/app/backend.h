#ifndef APP_BACKEND_H
#define APP_BACKEND_H

#include <QObject>
#include <QString>
#include <QVector>

#include "catalog/catalog.h"
#include "cluster/clusterer.h"
#include "feed/feedstore.h"

namespace feed {
class FeedFetcher;
class FeedRefresher;
}
namespace catalog { class CatalogService; }

namespace app {

/*!
 * \brief Everything that must not run on the GUI thread.
 *
 * Section 3: all fetching, parsing and clustering happens off the GUI thread. This object
 * is moved to a worker thread by AppController and only ever reached through queued
 * signals — no method here may be called directly from the GUI thread.
 *
 * It owns the writing FeedStore. SQLite connections cannot cross threads, which is why
 * the models on the GUI side are handed their rows instead of querying.
 */
class Backend : public QObject
{
    Q_OBJECT

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

public slots:
    /*! Opens the database and reads the cached catalogue. Must run on the worker thread. */
    void initialise(const QString &databasePath, const QString &catalogCachePath);

    void reloadFeeds();

    void refreshFeeds(int hammingThreshold, int windowHours);
    void refreshCatalog(const QString &url, bool force);

    void subscribe(const QString &url, const QString &title, const QString &lang,
                   const QString &langSource);
    void unsubscribe(qint64 feedId);
    void setFeedEnabled(qint64 feedId, bool enabled);
    void setFeedLanguage(qint64 feedId, const QString &lang);

    /*!
     * Fetches a URL once and reports what it is, so the add-feed page can pre-fill the
     * language picker instead of asking a question the user may not be able to answer.
     */
    void probeFeed(const QString &url);

    void importOpml(const QByteArray &xml, const QString &fallbackLang);
    void exportOpml(const QString &filePath);

    /*! Retention (§4). Cutoff is computed by the caller from the setting. */
    void purge(int retentionDays);

signals:
    void initialised(bool ok, const QString &error);
    void feedsChanged(const QVector<feed::FeedRecord> &feeds);
    void catalogChanged(const catalog::Catalog &catalog);

    void busyChanged(bool busy);
    void refreshFinished(int feedsOk, int feedsFailed, int articlesAdded,
                         const QString &error);
    void catalogRefreshFinished(bool changed, const QString &error);

    /*! \a lang is what the document declares, empty when it declares nothing. */
    void feedProbed(const QString &url, const QString &title, const QString &lang,
                    const QString &error);

    void opmlImported(int added, int skipped, const QString &error);
    void opmlExported(const QString &filePath, const QString &error);
    void purged(int articlesDeleted);

    void message(const QString &text);

private:
    void setBusy(bool busy);
    void emitFeeds();

    feed::FeedStore *m_store = nullptr;
    feed::FeedFetcher *m_fetcher = nullptr;
    feed::FeedRefresher *m_refresher = nullptr;
    catalog::CatalogService *m_catalog = nullptr;

    /*! Request id used for one-off probe fetches; distinct from feed ids. */
    static constexpr qint64 ProbeRequestId = -2000;
    QString m_probeUrl;
    bool m_busy = false;
};

} // namespace app

Q_DECLARE_METATYPE(QVector<feed::FeedRecord>)
Q_DECLARE_METATYPE(catalog::Catalog)

#endif // APP_BACKEND_H

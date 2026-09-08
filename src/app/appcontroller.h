#ifndef APP_APPCONTROLLER_H
#define APP_APPCONTROLLER_H

#include <QObject>
#include <QString>
#include <QThread>

#include "app/settings.h"
#include "model/catalogmodel.h"
#include "model/clustermodel.h"
#include "model/subscriptionmodel.h"

namespace app {

class Backend;

/*!
 * \brief What QML talks to.
 *
 * Lives on the GUI thread, owns the worker thread and the Backend on it, and forwards
 * every request as a queued call. Nothing here touches SQLite or the network: the models
 * are handed their rows by the worker.
 *
 * Exposed to QML as a context property, so no QML file needs to know any of this exists
 * beyond the properties and invokables below.
 */
class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(model::SubscriptionModel *subscriptions READ subscriptions CONSTANT)
    Q_PROPERTY(model::CatalogModel *catalog READ catalog CONSTANT)
    Q_PROPERTY(model::ClusterModel *clusters READ clusters CONSTANT)
    Q_PROPERTY(app::Settings *settings READ settings CONSTANT)

    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

    /*! True once a catalogue has been loaded, from cache or network. */
    Q_PROPERTY(bool hasCatalog READ hasCatalog NOTIFY hasCatalogChanged)

    /*!
     * Why the catalogue is unavailable, for the first-run empty state.
     *
     * Section 10: nothing is bundled, so a first start without a reachable host has an
     * empty catalogue — and it has to say why rather than merely looking broken.
     */
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogErrorChanged)

    Q_PROPERTY(int subscriptionCount READ subscriptionCount NOTIFY subscriptionCountChanged)

    // The bounds the settings sliders offer. Exposed from the algorithm rather than
    // written into QML, so the limits and the clamp cannot drift apart.
    Q_PROPERTY(int clusterThresholdMinimum READ clusterThresholdMinimum CONSTANT)
    Q_PROPERTY(int clusterThresholdMaximum READ clusterThresholdMaximum CONSTANT)
    Q_PROPERTY(int clusterWindowMinimum READ clusterWindowMinimum CONSTANT)
    Q_PROPERTY(int clusterWindowMaximum READ clusterWindowMaximum CONSTANT)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)

    /*! True while there are older stories the list has not loaded yet. */
    Q_PROPERTY(bool hasMoreArticles READ hasMoreArticles NOTIFY hasMoreArticlesChanged)

    /*! Headline of the newest cluster, for the cover page. */
    Q_PROPERTY(QString leadHeadline READ leadHeadline NOTIFY leadHeadlineChanged)

    // --- Filter. A day of twenty feeds is over a thousand articles; without these the
    // list is a wall rather than something to read.
    Q_PROPERTY(bool filterUnreadOnly READ filterUnreadOnly WRITE setFilterUnreadOnly
                   NOTIFY filterChanged)
    Q_PROPERTY(QString filterCategory READ filterCategory WRITE setFilterCategory
                   NOTIFY filterChanged)
    Q_PROPERTY(qint64 filterFeedId READ filterFeedId WRITE setFilterFeedId NOTIFY filterChanged)
    Q_PROPERTY(int filterMaxAgeHours READ filterMaxAgeHours WRITE setFilterMaxAgeHours
                   NOTIFY filterChanged)
    Q_PROPERTY(bool filterActive READ filterActive NOTIFY filterChanged)
    /*! One line naming what is filtered, for the list header. */
    Q_PROPERTY(QString filterSummary READ filterSummary NOTIFY filterChanged)
    Q_PROPERTY(QStringList subscribedCategories READ subscribedCategories
                   NOTIFY subscribedCategoriesChanged)

    /*! Result of the last catalogue fetch, so a manual reload says what happened. */
    Q_PROPERTY(QString catalogStatus READ catalogStatus NOTIFY catalogStatusChanged)

    /*! True while the user has not yet decided about contacting the catalogue host. */
    Q_PROPERTY(bool needsCatalogConsent READ needsCatalogConsent NOTIFY catalogConsentChanged)
    Q_PROPERTY(bool catalogDeclined READ catalogDeclined NOTIFY catalogConsentChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    /*! Starts the worker thread and opens the database. */
    void start(const QString &databasePath, const QString &catalogCachePath,
               const QString &thumbnailDirectory);

    model::SubscriptionModel *subscriptions() { return &m_subscriptions; }
    model::CatalogModel *catalog() { return &m_catalog; }
    model::ClusterModel *clusters() { return &m_clusters; }
    Settings *settings() { return m_settings; }
    void setSettings(Settings *settings);

    bool isReady() const { return m_ready; }
    bool isBusy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    bool hasCatalog() const { return m_catalog.totalCount() > 0; }
    QString catalogError() const { return m_catalogError; }
    int subscriptionCount() const { return m_subscriptions.rowCount(); }
    int unreadCount() const { return m_unreadCount; }
    bool hasMoreArticles() const { return m_hasMoreArticles; }

    /*! Loads the next page. Cheap and idempotent -- the backend ignores it when done. */
    Q_INVOKABLE void loadMoreArticles();
    int clusterThresholdMinimum() const { return cluster::Params::MinThreshold; }
    int clusterThresholdMaximum() const { return cluster::Params::MaxThreshold; }
    int clusterWindowMinimum() const { return cluster::Params::MinWindowHours; }
    int clusterWindowMaximum() const { return cluster::Params::MaxWindowHours; }
    QString leadHeadline() const { return m_leadHeadline; }

    bool filterUnreadOnly() const { return m_filterUnreadOnly; }
    void setFilterUnreadOnly(bool unreadOnly);
    QString filterCategory() const { return m_filterCategory; }
    void setFilterCategory(const QString &category);
    qint64 filterFeedId() const { return m_filterFeedId; }
    void setFilterFeedId(qint64 feedId);
    int filterMaxAgeHours() const { return m_filterMaxAgeHours; }
    void setFilterMaxAgeHours(int hours);
    bool filterActive() const;
    QString filterSummary() const;
    QStringList subscribedCategories() const { return m_subscribedCategories; }
    Q_INVOKABLE void clearFilter();

    QString catalogStatus() const { return m_catalogStatus; }
    bool needsCatalogConsent() const;
    bool catalogDeclined() const;

    // --- Actions, all asynchronous -------------------------------------------------

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCatalog(bool force = true);

    /*!
     * Records the user's decision about contacting the catalogue host, and fetches
     * immediately when it is yes. Called from the consent prompt, nowhere else.
     */
    Q_INVOKABLE void setCatalogConsent(bool granted);

    Q_INVOKABLE void subscribeFromCatalog(int row);
    Q_INVOKABLE void unsubscribeUrl(const QString &url);
    Q_INVOKABLE void unsubscribe(qint64 feedId);
    Q_INVOKABLE void setFeedEnabled(qint64 feedId, bool enabled);
    Q_INVOKABLE void setFeedLanguage(qint64 feedId, const QString &lang);

    /*! Fetches a URL once so the add-feed page can pre-fill the language picker. */
    Q_INVOKABLE void probeFeed(const QString &url);
    Q_INVOKABLE void addFeed(const QString &url, const QString &lang, const QString &title);

    /*!
     * Asks for a row's thumbnail. Called when the row becomes visible, never in advance
     * for the whole list (section 8).
     */
    Q_INVOKABLE void requestThumbnail(qint64 articleId);

    /*!
     * Asks for the pictures of a range of rows ahead of the viewport.
     *
     * Requesting only what is on screen loses every image the moment a reader flicks
     * quickly: the rows scroll past before their fetches come back. A window ahead of the
     * viewport keeps up with a flick without becoming a request for the whole list.
     */
    Q_INVOKABLE void prefetchThumbnails(int firstRow, int count);

    /*! How far ahead prefetching reaches. */
    Q_PROPERTY(int thumbnailPrefetchRows READ thumbnailPrefetchRows CONSTANT)
    int thumbnailPrefetchRows() const { return 50; }

    Q_INVOKABLE void markClusterRead(qint64 clusterId, bool read = true);

    /*! Reads a cluster's own read state, for the thumbnail toggle. */
    Q_INVOKABLE bool isClusterUnread(qint64 clusterId) const;
    Q_INVOKABLE void markAllRead();

    /*! Asks for the members of a cluster; answered by clusterMembersLoaded(). */
    Q_INVOKABLE void loadClusterMembers(qint64 clusterId);

    /*! Short translated age, e.g. "5 min". Follows the chosen UI language, unlike
     *  Silica's formatter, which follows the device locale. */
    Q_INVOKABLE QString relativeTime(qint64 published) const;

    /*! A stable colour per source name, for the thumbnail placeholder (section 8). */
    Q_INVOKABLE QString sourceColour(const QString &source) const;

    Q_INVOKABLE void importOpml(const QString &filePath);
    Q_INVOKABLE void exportOpml(const QString &filePath);
    Q_INVOKABLE QString defaultOpmlExportPath() const;

    /*! Language codes offered when adding a feed: the shipped UI languages plus
     *  whatever the catalogue uses, so the common cases are one tap. */
    Q_INVOKABLE QStringList suggestedLanguages() const;
    Q_INVOKABLE QString languageDisplayName(const QString &code) const;

signals:
    void readyChanged();
    void busyChanged();
    void statusMessageChanged();
    void hasCatalogChanged();
    void catalogErrorChanged();
    void subscriptionCountChanged();
    void catalogConsentChanged();
    void unreadCountChanged();
    void hasMoreArticlesChanged();
    void filterChanged();
    void subscribedCategoriesChanged();
    void catalogStatusChanged();
    void leadHeadlineChanged();

    /*! Members of the cluster asked for by loadClusterMembers(). */
    void clusterMembersLoaded(qint64 clusterId, const QVariantList &members);

    /*! Result of probeFeed(), for the add-feed page. */
    void feedProbed(const QString &url, const QString &title, const QString &lang,
                    const QString &error);

    void refreshFinished(int feedsOk, int feedsFailed, int articlesAdded);

private:
    void setStatusMessage(const QString &text);
    void setBusy(bool busy);
    void setCatalogError(const QString &error);
    void applyThumbnailCacheLimit();

    QThread m_thread;
    Backend *m_backend;
    Settings *m_settings = nullptr;
    model::SubscriptionModel m_subscriptions;
    model::CatalogModel m_catalog;
    model::ClusterModel m_clusters;
    int m_unreadCount = 0;
    bool m_hasMoreArticles = false;
    QString m_leadHeadline;

    bool m_filterUnreadOnly = false;
    QString m_filterCategory;
    qint64 m_filterFeedId = -1;
    int m_filterMaxAgeHours = 0;
    QStringList m_subscribedCategories;
    QString m_catalogStatus;

    void pushFilter();

    bool m_ready = false;
    bool m_busy = false;
    QString m_statusMessage;
    QString m_catalogError;
};

} // namespace app

#endif // APP_APPCONTROLLER_H

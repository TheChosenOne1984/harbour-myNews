#ifndef APP_APPCONTROLLER_H
#define APP_APPCONTROLLER_H

#include <QObject>
#include <QString>
#include <QThread>

#include "app/settings.h"
#include "model/catalogmodel.h"
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

    /*! True while the user has not yet decided about contacting the catalogue host. */
    Q_PROPERTY(bool needsCatalogConsent READ needsCatalogConsent NOTIFY catalogConsentChanged)
    Q_PROPERTY(bool catalogDeclined READ catalogDeclined NOTIFY catalogConsentChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    /*! Starts the worker thread and opens the database. */
    void start(const QString &databasePath, const QString &catalogCachePath);

    model::SubscriptionModel *subscriptions() { return &m_subscriptions; }
    model::CatalogModel *catalog() { return &m_catalog; }
    Settings *settings() { return m_settings; }
    void setSettings(Settings *settings);

    bool isReady() const { return m_ready; }
    bool isBusy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    bool hasCatalog() const { return m_catalog.totalCount() > 0; }
    QString catalogError() const { return m_catalogError; }
    int subscriptionCount() const { return m_subscriptions.rowCount(); }
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

    /*! Result of probeFeed(), for the add-feed page. */
    void feedProbed(const QString &url, const QString &title, const QString &lang,
                    const QString &error);

    void refreshFinished(int feedsOk, int feedsFailed, int articlesAdded);

private:
    void setStatusMessage(const QString &text);
    void setBusy(bool busy);
    void setCatalogError(const QString &error);

    QThread m_thread;
    Backend *m_backend;
    Settings *m_settings = nullptr;
    model::SubscriptionModel m_subscriptions;
    model::CatalogModel m_catalog;

    bool m_ready = false;
    bool m_busy = false;
    QString m_statusMessage;
    QString m_catalogError;
};

} // namespace app

#endif // APP_APPCONTROLLER_H

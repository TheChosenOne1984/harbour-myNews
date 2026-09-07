#include "appcontroller.h"

#include <QFile>
#include <QLocale>
#include <QStandardPaths>
#include <QDir>

#include "app/backend.h"
#include "i18n/languagenames.h"

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
        if (!ok)
            setStatusMessage(error);
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

    connect(m_backend, &Backend::busyChanged, this, [this](bool busy) { setBusy(busy); });

    connect(m_backend, &Backend::refreshFinished, this,
            [this](int ok, int failed, int added, const QString &error) {
        if (!error.isEmpty())
            setStatusMessage(error);
        emit refreshFinished(ok, failed, added);
    });

    connect(m_backend, &Backend::catalogRefreshFinished, this,
            [this](bool changed, const QString &error) {
        Q_UNUSED(changed)
        setCatalogError(error);
        if (!error.isEmpty() && hasCatalog()) {
            // We still have the cached copy, so this is a notice rather than a failure.
            setStatusMessage(tr("Could not update the catalogue: %1").arg(error));
        }
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

void AppController::setSettings(Settings *settings)
{
    m_settings = settings;
    if (m_settings) {
        connect(m_settings, &Settings::catalogConsentChanged,
                this, &AppController::catalogConsentChanged);
    }
    if (m_settings) {
        m_catalog.setUiLanguage(m_settings->uiLanguage().isEmpty()
                                    ? QLocale().name().left(2)
                                    : m_settings->uiLanguage());
    }
}

void AppController::start(const QString &databasePath, const QString &catalogCachePath)
{
    m_thread.start();
    QMetaObject::invokeMethod(m_backend, "initialise", Qt::QueuedConnection,
                              Q_ARG(QString, databasePath),
                              Q_ARG(QString, catalogCachePath));
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
                              Q_ARG(int, m_settings->clusterWindowHours()));
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
                              Q_ARG(QString, QStringLiteral("catalogue")));
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
                              Q_ARG(QString, lang), Q_ARG(QString, QStringLiteral("manual")));
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
        : QLocale().name().left(2);

    QMetaObject::invokeMethod(m_backend, "importOpml", Qt::QueuedConnection,
                              Q_ARG(QByteArray, xml), Q_ARG(QString, fallback));
}

void AppController::exportOpml(const QString &filePath)
{
    QMetaObject::invokeMethod(m_backend, "exportOpml", Qt::QueuedConnection,
                              Q_ARG(QString, filePath));
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
    const QString device = QLocale().name().left(2);
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

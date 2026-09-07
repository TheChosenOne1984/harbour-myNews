#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <QObject>
#include <QString>

#include "cluster/clusterer.h"

class QSettings;

namespace app {

/*!
 * \brief Persisted user settings, exposed to QML as a singleton context property.
 *
 * Backed by QSettings under the application name, which Sailjail whitelists as
 * ~/.config/harbour-mynews. Every property notifies, so QML bindings follow changes
 * without polling.
 *
 * The clustering parameters are here because section 6 requires them to be tunable: the
 * defaults were measured against real feeds (see CLUSTERING.md) but are explicitly not
 * claimed to be right for every subscription list.
 */
class Settings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(QString catalogUrl READ catalogUrl WRITE setCatalogUrl NOTIFY catalogUrlChanged)
    Q_PROPERTY(bool catalogEnabled READ catalogEnabled WRITE setCatalogEnabled
                   NOTIFY catalogEnabledChanged)
    Q_PROPERTY(QString defaultCatalogUrl READ defaultCatalogUrl CONSTANT)
    Q_PROPERTY(int retentionDays READ retentionDays WRITE setRetentionDays
                   NOTIFY retentionDaysChanged)
    Q_PROPERTY(int clusterThreshold READ clusterThreshold WRITE setClusterThreshold
                   NOTIFY clusterThresholdChanged)
    Q_PROPERTY(int clusterWindowHours READ clusterWindowHours WRITE setClusterWindowHours
                   NOTIFY clusterWindowHoursChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    /*! Overrides the QSettings location. For tests. */
    Settings(const QString &filePath, QObject *parent);
    ~Settings() override;

    /*! Empty means "follow the device locale". */
    QString uiLanguage() const;
    void setUiLanguage(const QString &code);

    /*!
     * Where the feed catalogue is fetched from.
     *
     * A setting rather than a constant (section 10): nothing is bundled, so this URL is
     * load-bearing at first run, and a user must be able to point somewhere else if the
     * default host disappears or they curate their own list.
     */
    QString catalogUrl() const;
    void setCatalogUrl(const QString &url);
    static QString defaultCatalogUrl();

    /*! The catalogue is the only request not aimed at a publisher, so it can be refused. */
    bool catalogEnabled() const;
    void setCatalogEnabled(bool enabled);

    /*! Articles older than this are purged, with their orphaned thumbnails. */
    int retentionDays() const;
    void setRetentionDays(int days);

    //! Hamming distance, 0-64. Measured default is 14; above 16 clusters run away.
    int clusterThreshold() const;
    void setClusterThreshold(int bits);

    //! Time window. Barely load-bearing at sane thresholds, a safety rail above them.
    int clusterWindowHours() const;
    void setClusterWindowHours(int hours);

    /*! The two above, in the form the clusterer takes. */
    cluster::Params clusterParams() const;

    /*! Forgets everything, restoring the measured defaults. */
    Q_INVOKABLE void resetToDefaults();

signals:
    void uiLanguageChanged();
    void catalogUrlChanged();
    void catalogEnabledChanged();
    void retentionDaysChanged();
    void clusterThresholdChanged();
    void clusterWindowHoursChanged();

private:
    QSettings *m_settings;
};

} // namespace app

#endif // APP_SETTINGS_H

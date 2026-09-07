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
    Q_PROPERTY(int catalogConsent READ catalogConsentInt WRITE setCatalogConsentInt
                   NOTIFY catalogConsentChanged)
    Q_PROPERTY(bool catalogEnabled READ catalogEnabled NOTIFY catalogConsentChanged)
    Q_PROPERTY(QString catalogHost READ catalogHost NOTIFY catalogUrlChanged)
    Q_PROPERTY(QString defaultCatalogUrl READ defaultCatalogUrl CONSTANT)
    Q_PROPERTY(int retentionDays READ retentionDays WRITE setRetentionDays
                   NOTIFY retentionDaysChanged)
    Q_PROPERTY(int thumbnailCacheMegabytes READ thumbnailCacheMegabytes
                   WRITE setThumbnailCacheMegabytes NOTIFY thumbnailCacheMegabytesChanged)
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

    /*!
     * Whether the user has agreed to the catalogue being downloaded.
     *
     * The catalogue is the only request the app makes to anything other than a publisher,
     * and it goes to a host the user has no relationship with. So it is not a switch that
     * defaults to on: nothing is fetched until the user has been told which address will
     * be contacted and has agreed. Asked when the catalogue is first needed, never at
     * startup — someone who only ever adds feeds by URL is never asked at all.
     */
    enum Consent
    {
        Unasked = 0,   //!< never fetch; the UI must ask first
        Granted = 1,
        Denied = 2,
    };
    Q_ENUM(Consent)

    Consent catalogConsent() const;
    void setCatalogConsent(Consent consent);

    /*! True only once consent has actually been given. */
    bool catalogEnabled() const { return catalogConsent() == Granted; }

    /*! Host of the current catalogue URL, so the consent prompt can name it. */
    QString catalogHost() const;

    /*! Articles older than this are purged, with their orphaned thumbnails. */
    int retentionDays() const;
    void setRetentionDays(int days);

    /*! Cap on the thumbnail cache. Storage is finite on a phone and the user owns it. */
    int thumbnailCacheMegabytes() const;
    void setThumbnailCacheMegabytes(int megabytes);

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
    void catalogConsentChanged();
    void retentionDaysChanged();
    void thumbnailCacheMegabytesChanged();
    void clusterThresholdChanged();
    void clusterWindowHoursChanged();

private:
    int catalogConsentInt() const { return static_cast<int>(catalogConsent()); }
    void setCatalogConsentInt(int consent) { setCatalogConsent(static_cast<Consent>(consent)); }

    QSettings *m_settings;
};

} // namespace app

#endif // APP_SETTINGS_H

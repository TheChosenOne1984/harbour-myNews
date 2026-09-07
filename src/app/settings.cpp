#include "settings.h"

#include <QSettings>
#include <QUrl>

namespace app {
namespace {

const QString KeyUiLanguage = QStringLiteral("ui/language");
const QString KeyCatalogUrl = QStringLiteral("catalog/url");
const QString KeyCatalogConsent = QStringLiteral("catalog/consent");
const QString KeyRetentionDays = QStringLiteral("storage/retentionDays");
const QString KeyClusterThreshold = QStringLiteral("cluster/hammingThreshold");
const QString KeyClusterWindow = QStringLiteral("cluster/windowHours");

constexpr int DefaultRetentionDays = 14;

int clampInt(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(this))
{
}

Settings::Settings(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings(filePath, QSettings::IniFormat, this))
{
}

Settings::~Settings() = default;

QString Settings::defaultCatalogUrl()
{
    // A default, not a constant: the user can point this anywhere, which is what keeps a
    // URL the first run depends on from being a single point of failure (section 10).
    // Verified to answer 304 to a conditional GET, including for the weak ETag it issues
    // when the response is gzipped -- which is the path the app actually takes.
    return QStringLiteral("https://catalog.cf-my-news.workers.dev");
}

QString Settings::uiLanguage() const
{
    return m_settings->value(KeyUiLanguage, QString()).toString();
}

void Settings::setUiLanguage(const QString &code)
{
    if (code == uiLanguage())
        return;
    m_settings->setValue(KeyUiLanguage, code);
    emit uiLanguageChanged();
}

QString Settings::catalogUrl() const
{
    const QString stored = m_settings->value(KeyCatalogUrl, QString()).toString().trimmed();
    return stored.isEmpty() ? defaultCatalogUrl() : stored;
}

void Settings::setCatalogUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed == catalogUrl())
        return;
    // Storing empty means "back to the default", so clearing the field in the UI does the
    // obvious thing instead of breaking the catalogue.
    m_settings->setValue(KeyCatalogUrl,
                         trimmed == defaultCatalogUrl() ? QString() : trimmed);
    emit catalogUrlChanged();
}

Settings::Consent Settings::catalogConsent() const
{
    // Defaults to Unasked, never to Granted: an app that contacts a third party before
    // the user has agreed has already done the thing consent was supposed to gate.
    const int stored = m_settings->value(KeyCatalogConsent, int(Unasked)).toInt();
    if (stored == int(Granted) || stored == int(Denied))
        return static_cast<Consent>(stored);
    return Unasked;
}

void Settings::setCatalogConsent(Consent consent)
{
    if (consent == catalogConsent())
        return;
    m_settings->setValue(KeyCatalogConsent, int(consent));
    emit catalogConsentChanged();
}

QString Settings::catalogHost() const
{
    return QUrl(catalogUrl()).host();
}

int Settings::retentionDays() const
{
    return clampInt(m_settings->value(KeyRetentionDays, DefaultRetentionDays).toInt(), 1, 365);
}

void Settings::setRetentionDays(int days)
{
    const int clamped = clampInt(days, 1, 365);
    if (clamped == retentionDays())
        return;
    m_settings->setValue(KeyRetentionDays, clamped);
    emit retentionDaysChanged();
}

int Settings::clusterThreshold() const
{
    const cluster::Params defaults;
    return clampInt(m_settings->value(KeyClusterThreshold, defaults.hammingThreshold).toInt(),
                    0, 64);
}

void Settings::setClusterThreshold(int bits)
{
    const int clamped = clampInt(bits, 0, 64);
    if (clamped == clusterThreshold())
        return;
    m_settings->setValue(KeyClusterThreshold, clamped);
    emit clusterThresholdChanged();
}

int Settings::clusterWindowHours() const
{
    const cluster::Params defaults;
    return clampInt(m_settings->value(KeyClusterWindow,
                                      int(defaults.windowSeconds / 3600)).toInt(), 1, 8760);
}

void Settings::setClusterWindowHours(int hours)
{
    const int clamped = clampInt(hours, 1, 8760);
    if (clamped == clusterWindowHours())
        return;
    m_settings->setValue(KeyClusterWindow, clamped);
    emit clusterWindowHoursChanged();
}

cluster::Params Settings::clusterParams() const
{
    cluster::Params params;
    params.hammingThreshold = clusterThreshold();
    params.windowSeconds = static_cast<qint64>(clusterWindowHours()) * 3600;
    return params;
}

void Settings::resetToDefaults()
{
    m_settings->remove(KeyUiLanguage);
    m_settings->remove(KeyCatalogUrl);
    // Consent is deliberately NOT reset: withdrawing a decision the user made about
    // contacting a third party is not something "restore defaults" should do silently.
    m_settings->remove(KeyRetentionDays);
    m_settings->remove(KeyClusterThreshold);
    m_settings->remove(KeyClusterWindow);

    emit uiLanguageChanged();
    emit catalogUrlChanged();
    emit retentionDaysChanged();
    emit clusterThresholdChanged();
    emit clusterWindowHoursChanged();
}

} // namespace app

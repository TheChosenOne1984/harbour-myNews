#include "subscriptionmodel.h"

namespace model {

SubscriptionModel::SubscriptionModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SubscriptionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_feeds.size();
}

QHash<int, QByteArray> SubscriptionModel::roleNames() const
{
    return {
        {FeedIdRole, "feedId"},
        {UrlRole, "url"},
        {TitleRole, "title"},
        {SiteUrlRole, "siteUrl"},
        {LangRole, "lang"},
        {LangSourceRole, "langSource"},
        {LanguageIsConfirmedRole, "languageIsConfirmed"},
        {EnabledRole, "enabled"},
        {HealthRole, "health"},
        {LastStatusRole, "lastStatus"},
        {LastFetchRole, "lastFetch"},
        {FailureCountRole, "failureCount"},
    };
}

SubscriptionModel::Health SubscriptionModel::healthOf(const feed::FeedRecord &feed)
{
    if (!feed.enabled)
        return Disabled;
    if (feed.lastFetch == 0)
        return Unknown;
    if (feed.failureCount == 0)
        return Ok;
    return feed.failureCount >= BrokenAfterFailures ? Broken : Failing;
}

QVariant SubscriptionModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_feeds.size())
        return QVariant();

    const feed::FeedRecord &feed = m_feeds.at(index.row());
    switch (role) {
    case FeedIdRole:
        return feed.id;
    case UrlRole:
        return feed.url;
    case TitleRole:
        // A feed that has never been fetched has no title yet; showing the URL beats
        // showing an empty row.
        return feed.title.isEmpty() ? feed.url : feed.title;
    case SiteUrlRole:
        return feed.siteUrl;
    case LangRole:
        return feed.lang;
    case LangSourceRole:
        return feed.langSource;
    case LanguageIsConfirmedRole:
        // "declared" means we took the document's word for it, which after an OPML import
        // may be the device language instead. Those are the rows worth reviewing.
        return feed.langSource == QLatin1String("manual")
            || feed.langSource == QLatin1String("catalogue");
    case EnabledRole:
        return feed.enabled;
    case HealthRole:
        return static_cast<int>(healthOf(feed));
    case LastStatusRole:
        return feed.lastStatus;
    case LastFetchRole:
        return feed.lastFetch;
    case FailureCountRole:
        return feed.failureCount;
    default:
        break;
    }
    return QVariant();
}

void SubscriptionModel::setFeeds(const QVector<feed::FeedRecord> &feeds)
{
    const bool sizeChanged = feeds.size() != m_feeds.size();
    beginResetModel();
    m_feeds = feeds;
    endResetModel();
    if (sizeChanged)
        emit countChanged();
}

int SubscriptionModel::indexOfFeed(qint64 feedId) const
{
    for (int i = 0; i < m_feeds.size(); ++i) {
        if (m_feeds.at(i).id == feedId)
            return i;
    }
    return -1;
}

QSet<QString> SubscriptionModel::subscribedUrls() const
{
    QSet<QString> urls;
    for (const feed::FeedRecord &feed : m_feeds)
        urls.insert(feed.url);
    return urls;
}

} // namespace model

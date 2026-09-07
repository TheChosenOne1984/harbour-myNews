#ifndef MODEL_SUBSCRIPTIONMODEL_H
#define MODEL_SUBSCRIPTIONMODEL_H

#include <QAbstractListModel>
#include <QSet>
#include <QVector>

#include "feed/feedstore.h"

namespace model {

/*!
 * \brief The user's subscriptions, for the subscriptions page.
 *
 * Holds rows handed to it by the controller rather than querying SQLite itself. Section 3
 * asks for models backed by SQLite queries and for nothing heavy on the GUI thread; a
 * second database connection on the GUI thread would satisfy the letter of the first and
 * work against the second. A subscription list is a few dozen rows, so keeping them in
 * memory costs nothing.
 */
class SubscriptionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    /*!
     * How a feed is doing, so the subscriptions page can show it.
     *
     * Section 5 requires persistent failure to be surfaced here rather than retried
     * silently forever, which means "it broke once" and "it has been broken for days"
     * must not look the same.
     */
    enum Health
    {
        Ok,        //!< last fetch succeeded
        Unknown,   //!< never fetched yet
        Failing,   //!< a few failures in a row; still being retried
        Broken,    //!< failing long enough that the user should look at it
        Disabled   //!< switched off by the user
    };
    Q_ENUM(Health)

    /*! Failures in a row before a feed is called Broken rather than Failing. */
    static constexpr int BrokenAfterFailures = 5;

    enum Roles
    {
        FeedIdRole = Qt::UserRole + 1,
        UrlRole,
        TitleRole,
        SiteUrlRole,
        LangRole,
        LangSourceRole,
        LanguageIsConfirmedRole,  //!< false for values we guessed, e.g. after OPML import
        EnabledRole,
        HealthRole,
        LastStatusRole,
        LastFetchRole,
        FailureCountRole,
    };

    explicit SubscriptionModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setFeeds(const QVector<feed::FeedRecord> &feeds);

    /*! Row index of a feed id, or -1. */
    Q_INVOKABLE int indexOfFeed(qint64 feedId) const;

    /*! Subscribed URLs, for cross-referencing against the catalogue. */
    QSet<QString> subscribedUrls() const;

    static Health healthOf(const feed::FeedRecord &feed);

signals:
    void countChanged();

private:
    QVector<feed::FeedRecord> m_feeds;
};

} // namespace model

#endif // MODEL_SUBSCRIPTIONMODEL_H

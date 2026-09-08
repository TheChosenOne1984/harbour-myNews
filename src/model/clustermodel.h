#ifndef MODEL_CLUSTERMODEL_H
#define MODEL_CLUSTERMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QVector>

#include "feed/feedstore.h"

namespace model {

/*!
 * \brief The article list: one row per cluster, newest first.
 *
 * The differentiating view of the app — when several sources report the same story it is
 * one row with the sources listed beneath it, not one row each.
 *
 * Like the other models it is handed its rows by the controller, so no database
 * connection lives on the GUI thread.
 */
class ClusterModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    /*!
     * Sources shown in full before the row switches to "and N more".
     * Section 9: with more than three sources, show the first two plus a count.
     */
    static constexpr int MaxVisibleSources = 3;

    enum Roles
    {
        ClusterIdRole = Qt::UserRole + 1,
        LeadArticleIdRole,
        TitleRole,
        TeaserRole,
        LinkRole,
        ImageUrlRole,
        ImageStateRole,
        ThumbnailPathRole,   //!< local file, empty until one has been cached
        PublishedRole,
        MemberCountRole,
        UnreadRole,
        SourcesRole,          //!< every contributing source
        VisibleSourcesRole,   //!< the ones the pill row shows
        HiddenSourceCountRole //!< how many the pill row leaves out, 0 when none
    };

    explicit ClusterModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setClusters(const QVector<feed::ClusterSummary> &clusters);

    /*! Fills in a thumbnail that has just been cached, without refetching the page. */
    void setRowThumbnail(qint64 articleId, const QString &path);

    /*! Marks one row read without refetching the page. */
    void setRowRead(qint64 clusterId, bool read);
    void setAllRead();

    Q_INVOKABLE int indexOfCluster(qint64 clusterId) const;

    /*! Lead article ids for a range of rows, for prefetching their pictures. */
    QVector<qint64> leadArticleIds(int firstRow, int count) const;

signals:
    void countChanged();

private:
    int rowOfArticle(qint64 articleId) const;

    QVector<feed::ClusterSummary> m_clusters;
    QHash<qint64, QString> m_thumbnails;   //!< lead article id -> file path
};

} // namespace model

#endif // MODEL_CLUSTERMODEL_H

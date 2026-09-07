#ifndef MODEL_CLUSTERMODEL_H
#define MODEL_CLUSTERMODEL_H

#include <QAbstractListModel>
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

    /*! Marks one row read without refetching the page. */
    void setRowRead(qint64 clusterId, bool read);
    void setAllRead();

    Q_INVOKABLE int indexOfCluster(qint64 clusterId) const;

signals:
    void countChanged();

private:
    QVector<feed::ClusterSummary> m_clusters;
};

} // namespace model

#endif // MODEL_CLUSTERMODEL_H

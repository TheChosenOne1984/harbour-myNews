#include "clustermodel.h"

namespace model {

ClusterModel::ClusterModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ClusterModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_clusters.size();
}

QHash<int, QByteArray> ClusterModel::roleNames() const
{
    return {
        {ClusterIdRole, "clusterId"},
        {LeadArticleIdRole, "leadArticleId"},
        {TitleRole, "title"},
        {TeaserRole, "teaser"},
        {LinkRole, "link"},
        {ImageUrlRole, "imageUrl"},
        {ImageStateRole, "imageState"},
        {PublishedRole, "published"},
        {MemberCountRole, "memberCount"},
        {UnreadRole, "unread"},
        {SourcesRole, "sources"},
        {VisibleSourcesRole, "visibleSources"},
        {HiddenSourceCountRole, "hiddenSourceCount"},
    };
}

QVariant ClusterModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_clusters.size())
        return QVariant();

    const feed::ClusterSummary &cluster = m_clusters.at(index.row());
    switch (role) {
    case ClusterIdRole:
        return cluster.clusterId;
    case LeadArticleIdRole:
        return cluster.leadArticleId;
    case TitleRole:
        return cluster.title;
    case TeaserRole:
        return cluster.teaser;
    case LinkRole:
        return cluster.link;
    case ImageUrlRole:
        return cluster.imageUrl;
    case ImageStateRole:
        return cluster.imageState;
    case PublishedRole:
        return cluster.published;
    case MemberCountRole:
        return cluster.memberCount;
    case UnreadRole:
        return cluster.unread;
    case SourcesRole:
        return cluster.sources;
    case VisibleSourcesRole:
        // Section 9: more than three sources shows the first two plus a count, so the row
        // cannot grow past its fixed height however many publishers ran the story.
        if (cluster.sources.size() > MaxVisibleSources)
            return QStringList(cluster.sources.mid(0, MaxVisibleSources - 1));
        return cluster.sources;
    case HiddenSourceCountRole:
        if (cluster.sources.size() > MaxVisibleSources)
            return cluster.sources.size() - (MaxVisibleSources - 1);
        return 0;
    default:
        break;
    }
    return QVariant();
}

void ClusterModel::setClusters(const QVector<feed::ClusterSummary> &clusters)
{
    const bool sizeChanged = clusters.size() != m_clusters.size();
    beginResetModel();
    m_clusters = clusters;
    endResetModel();
    if (sizeChanged)
        emit countChanged();
}

void ClusterModel::setRowRead(qint64 clusterId, bool read)
{
    const int row = indexOfCluster(clusterId);
    if (row < 0 || m_clusters.at(row).unread != read)
        return;
    // Only the flag changes, so no reset: resetting would drop the reader back to the top
    // of the list every time they opened an article.
    m_clusters[row].unread = !read;
    emit dataChanged(index(row), index(row), {UnreadRole});
}

void ClusterModel::setAllRead()
{
    if (m_clusters.isEmpty())
        return;
    for (feed::ClusterSummary &cluster : m_clusters)
        cluster.unread = false;
    emit dataChanged(index(0), index(m_clusters.size() - 1), {UnreadRole});
}

int ClusterModel::indexOfCluster(qint64 clusterId) const
{
    for (int i = 0; i < m_clusters.size(); ++i) {
        if (m_clusters.at(i).clusterId == clusterId)
            return i;
    }
    return -1;
}

} // namespace model

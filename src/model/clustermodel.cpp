#include "clustermodel.h"

#include <QSet>

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
        {ThumbnailPathRole, "thumbnailPath"},
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
    case ThumbnailPathRole:
        return m_thumbnails.value(cluster.leadArticleId);
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
    // A reset scrolls the list back to the top, which throws away the reader's place. Most
    // updates -- an article marked read, a refresh that found nothing new -- leave the
    // rows themselves identical, so those are applied in place and the position survives.
    if (clusters.size() == m_clusters.size()) {
        bool sameRows = true;
        for (int i = 0; i < clusters.size(); ++i) {
            if (clusters.at(i).clusterId != m_clusters.at(i).clusterId) {
                sameRows = false;
                break;
            }
        }
        if (sameRows) {
            m_clusters = clusters;
            if (!m_clusters.isEmpty())
                emit dataChanged(index(0), index(m_clusters.size() - 1));
            return;
        }
    }

    const bool sizeChanged = clusters.size() != m_clusters.size();
    beginResetModel();
    m_clusters = clusters;
    // Paths already known stay: a refresh must not make cached thumbnails disappear and
    // be requested all over again.
    endResetModel();
    if (sizeChanged)
        emit countChanged();
}

void ClusterModel::appendClusters(const QVector<feed::ClusterSummary> &clusters)
{
    if (clusters.isEmpty())
        return;

    // Guard against a page that overlaps what is already loaded, which would otherwise
    // show the same story twice.
    QSet<qint64> known;
    for (const feed::ClusterSummary &cluster : m_clusters)
        known.insert(cluster.clusterId);

    QVector<feed::ClusterSummary> fresh;
    fresh.reserve(clusters.size());
    for (const feed::ClusterSummary &cluster : clusters) {
        if (!known.contains(cluster.clusterId))
            fresh.append(cluster);
    }
    if (fresh.isEmpty())
        return;

    beginInsertRows(QModelIndex(), m_clusters.size(),
                    m_clusters.size() + fresh.size() - 1);
    m_clusters += fresh;
    endInsertRows();
    emit countChanged();
}

QVector<qint64> ClusterModel::leadArticleIds(int firstRow, int count) const
{
    QVector<qint64> ids;
    if (count <= 0)
        return ids;

    const int from = qMax(0, firstRow);
    const int to = qMin(m_clusters.size(), from + count);
    ids.reserve(to - from);
    for (int i = from; i < to; ++i) {
        // Nothing to fetch for a row whose picture is already on disk.
        if (!m_thumbnails.contains(m_clusters.at(i).leadArticleId))
            ids.append(m_clusters.at(i).leadArticleId);
    }
    return ids;
}

int ClusterModel::rowOfArticle(qint64 articleId) const
{
    for (int i = 0; i < m_clusters.size(); ++i) {
        if (m_clusters.at(i).leadArticleId == articleId)
            return i;
    }
    return -1;
}

void ClusterModel::setRowThumbnail(qint64 articleId, const QString &path)
{
    if (m_thumbnails.value(articleId) == path)
        return;
    m_thumbnails.insert(articleId, path);

    const int row = rowOfArticle(articleId);
    if (row < 0)
        return;
    // One role on one row: the image appears without the list moving under the reader.
    emit dataChanged(index(row), index(row), {ThumbnailPathRole});
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

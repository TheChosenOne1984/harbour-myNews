#include "clusterer.h"

#include "simhash.h"

#include <algorithm>
#include <functional>

#include <QHash>

namespace cluster {
namespace {

bool isCandidate(const Article &a, const Article &b, const Params &params)
{
    if (a.lang != b.lang)
        return false;
    const qint64 delta = a.published - b.published;
    return (delta < 0 ? -delta : delta) <= params.windowSeconds;
}

/*! Indices sorted by published ascending, so batch input behaves like arrival order. */
QVector<int> chronological(const QVector<Article> &articles)
{
    QVector<int> order;
    order.reserve(articles.size());
    for (int i = 0; i < articles.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [&articles](int lhs, int rhs) {
        return articles[lhs].published < articles[rhs].published;
    });
    return order;
}

} // namespace

int chooseLead(const QVector<Article> &articles, const QVector<int> &members)
{
    int best = -1;
    for (const int index : members) {
        if (best < 0) {
            best = index;
            continue;
        }
        const Article &candidate = articles[index];
        const Article &incumbent = articles[best];

        if (candidate.hasImage != incumbent.hasImage) {
            if (candidate.hasImage)
                best = index;
            continue;
        }
        if (candidate.sourcePriority != incumbent.sourcePriority) {
            if (candidate.sourcePriority < incumbent.sourcePriority)
                best = index;
            continue;
        }
        if (candidate.published < incumbent.published)
            best = index;
    }
    return best;
}

QVector<Cluster> clusterOnline(const QVector<Article> &articles, const Params &params)
{
    QVector<Cluster> clusters;
    QVector<int> clusterOf(articles.size(), -1);

    // Only articles already assigned are candidates, mirroring the app: the store holds
    // what arrived earlier, and a linear scan over a 48h window is a few hundred rows.
    QVector<int> assigned;
    assigned.reserve(articles.size());

    for (const int index : chronological(articles)) {
        const Article &incoming = articles[index];

        int bestMatch = -1;
        int bestDistance = params.hammingThreshold + 1;

        for (const int other : assigned) {
            if (!isCandidate(incoming, articles[other], params))
                continue;
            const int distance = hammingDistance(incoming.simhash, articles[other].simhash);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestMatch = other;
            }
        }

        if (bestMatch >= 0 && bestDistance <= params.hammingThreshold) {
            const int target = clusterOf[bestMatch];
            clusters[target].members.append(index);
            clusterOf[index] = target;
        } else {
            Cluster fresh;
            fresh.members.append(index);
            clusterOf[index] = clusters.size();
            clusters.append(fresh);
        }
        assigned.append(index);
    }

    for (Cluster &c : clusters)
        c.lead = chooseLead(articles, c.members);
    return clusters;
}

QVector<Cluster> clusterBatch(const QVector<Article> &articles, const Params &params)
{
    QVector<int> parent(articles.size());
    for (int i = 0; i < articles.size(); ++i)
        parent[i] = i;

    std::function<int(int)> find = [&parent, &find](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    for (int i = 0; i < articles.size(); ++i) {
        for (int j = i + 1; j < articles.size(); ++j) {
            if (!isCandidate(articles[i], articles[j], params))
                continue;
            if (hammingDistance(articles[i].simhash, articles[j].simhash) > params.hammingThreshold)
                continue;
            const int ri = find(i);
            const int rj = find(j);
            if (ri != rj)
                parent[ri] = rj;
        }
    }

    QHash<int, int> rootToCluster;
    QVector<Cluster> clusters;
    for (const int index : chronological(articles)) {
        const int root = find(index);
        auto it = rootToCluster.find(root);
        if (it == rootToCluster.end()) {
            it = rootToCluster.insert(root, clusters.size());
            clusters.append(Cluster());
        }
        clusters[*it].members.append(index);
    }

    for (Cluster &c : clusters)
        c.lead = chooseLead(articles, c.members);
    return clusters;
}

} // namespace cluster

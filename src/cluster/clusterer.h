#ifndef CLUSTER_CLUSTERER_H
#define CLUSTER_CLUSTERER_H

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace cluster {

/*! One article as far as clustering is concerned. */
struct Article
{
    int index = -1;          //!< position in the input vector
    QString lang;            //!< BCP-47; candidates must match exactly (section 6)
    qint64 published = 0;    //!< unix seconds
    quint64 simhash = 0;
    int sourcePriority = 0;  //!< subscription order; lower wins the lead tie-break
    bool hasImage = false;   //!< preferred for the lead article (section 6.6)
};

/*! Tunable parameters. Both must be user-visible settings (section 6). */
struct Params
{
    /*!
     * Maximum Hamming distance to be considered the same story.
     *
     * 14 was measured, not guessed: on a 966-article corpus of real German feeds the
     * quality of the groupings is flat up to 14 and collapses at 16, where the largest
     * cluster jumps from 4 articles to 27. See CLUSTERING.md.
     */
    int hammingThreshold = 14;

    /*!
     * Articles further apart in time than this never cluster, which keeps anniversary
     * pieces and follow-ups off the original story.
     *
     * Measurement showed this is a safety rail rather than a tuning knob: between 12h
     * and 96h the result moves by two clusters at the recommended threshold. It earns
     * its keep only at looser thresholds, where it limits the damage.
     */
    qint64 windowSeconds = 48 * 3600;
};

struct Cluster
{
    QVector<int> members;    //!< indices into the input vector
    int lead = -1;           //!< representative article
};

/*!
 * \brief Online, greedy assignment -- what the app does as articles arrive.
 *
 * Each article joins the cluster of its closest match below the threshold, or starts a
 * new one. Order-dependent, and existing clusters never merge. That is a deliberate
 * simplification: see clusterBatch() for what it costs.
 *
 * Input is processed in ascending published order regardless of vector order, so a
 * batch import produces the same result as the same articles arriving over time.
 */
QVector<Cluster> clusterOnline(const QVector<Article> &articles, const Params &params);

/*!
 * \brief Batch clustering by transitive closure -- the reference to measure against.
 *
 * Union-find over every within-window, same-language pair below the threshold. Not what
 * the app runs; it exists so the harness can quantify how much the online variant gives
 * up. Transitivity also makes it prone to chaining unrelated stories together, so a
 * larger cluster count here is not automatically better.
 */
QVector<Cluster> clusterBatch(const QVector<Article> &articles, const Params &params);

/*!
 * \brief Picks the representative: an article with a cached image first, then lowest
 *        source priority, then earliest published (section 6.6).
 */
int chooseLead(const QVector<Article> &articles, const QVector<int> &members);

} // namespace cluster

#endif // CLUSTER_CLUSTERER_H

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

/*!
 * Tunable parameters (section 6).
 *
 * "Tunable" here means fine-tuning, not free rein. The threshold has a measured cliff:
 * past it the clusterer stops grouping stories and starts merging unrelated ones, and a
 * user who slid past it would experience that as the app being broken rather than as a
 * setting they chose. The offered range therefore stops at the last value that still
 * behaves, and the setting clamps to it -- a hand-edited configuration file cannot get
 * around it either.
 */
struct Params
{
    /*!
     * Maximum Hamming distance to be considered the same story.
     *
     * Measured against two corpora of real German feeds a day apart. Up to 13 neither
     * showed a single false grouping. 14 gains a few correct merges but produced two
     * wrong ones on the second corpus; 16 reaches 18% wrong pairs and 17 a third. The
     * default is the highest value that was clean on both. See CLUSTERING.md.
     */
    int hammingThreshold = 13;

    /*! What the settings page offers, and what the setting clamps to. */
    static constexpr int MinThreshold = 8;
    static constexpr int MaxThreshold = 14;

    /*!
     * Articles further apart in time than this never cluster, which keeps anniversary
     * pieces and follow-ups off the original story.
     *
     * Measurement showed this is a safety rail rather than a tuning knob: between 12h and
     * 96h the result moves by two clusters at the recommended threshold. Left widely
     * adjustable because, unlike the threshold, no value of it breaks anything -- a short
     * window simply groups less.
     */
    qint64 windowSeconds = 48 * 3600;

    static constexpr int MinWindowHours = 6;
    static constexpr int MaxWindowHours = 168;
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

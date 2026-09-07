#ifndef CLUSTER_NORMALIZE_H
#define CLUSTER_NORMALIZE_H

#include <QString>

namespace cluster {

/*!
 * \brief Normalises text for clustering: NFKC, case folding, punctuation removal,
 *        whitespace collapse.
 *
 * Deliberately language-agnostic (project_description.md section 6.1): no stopword
 * removal and no stemming, because both need per-language data and neither survives
 * contact with a language the user added themselves.
 *
 * The app and the tuning harness must call exactly this function. If they normalised
 * differently, a threshold tuned in the harness would not mean the same thing in the
 * app.
 */
QString normalize(const QString &text);

/*!
 * \brief Removes a leading kicker such as "Wahl in Sachsen-Anhalt: " from a headline.
 *
 * European news feeds prefix headlines with a topic label before a colon. When one
 * story dominates the cycle, dozens of unrelated articles carry the same kicker, and on
 * a short headline that shared prefix is most of the n-grams -- so they cluster
 * together although they report different things. Measured on real feeds; see the
 * clustering harness.
 *
 * This keys on punctuation, not on vocabulary, so it stays language-agnostic. It is
 * conservative: the prefix must be short and the remainder must still be substantial,
 * which leaves ordinary titles that merely contain a colon untouched.
 */
QString stripKicker(const QString &title);

/*! Convenience: the normalised form of a headline plus its teaser. */
QString normalizeArticle(const QString &title, const QString &teaser);

/*!
 * \brief The canonical text the app hashes for clustering.
 *
 * Kicker removed, then normalised -- and deliberately *without* the teaser, which
 * measurement showed to hurt: teasers are frequently missing or boilerplate, and an
 * article whose teaser is empty stops matching an identically-titled copy that has one.
 * See CLUSTERING.md for the numbers behind this.
 */
QString clusterSubject(const QString &title);

} // namespace cluster

#endif // CLUSTER_NORMALIZE_H

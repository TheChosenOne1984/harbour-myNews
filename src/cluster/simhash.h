#ifndef CLUSTER_SIMHASH_H
#define CLUSTER_SIMHASH_H

#include <QString>
#include <QtGlobal>

namespace cluster {

/*! Character n-gram width. See task0-findings / the harness for how this was chosen. */
constexpr int DefaultNGramSize = 5;

/*!
 * How repeated n-grams contribute to the hash.
 *
 * Raw frequency is the textbook formulation, but it lets very common n-grams -- in
 * German, sequences like "der " or "en d" -- dominate, which drags unrelated documents
 * of the same language toward each other. Damping or ignoring repetition counters that
 * without needing per-language stopword lists.
 */
enum class Weighting
{
    Raw,     //!< weight = occurrence count
    Log,     //!< weight = 1 + log2(count), damped
    Binary   //!< weight = 1, repetition ignored
};

constexpr Weighting DefaultWeighting = Weighting::Binary;

/*!
 * \brief 64-bit SimHash over character n-grams.
 *
 * Character n-grams rather than word tokens: word tokenisation would need per-language
 * stopword lists and falls apart entirely on languages without word boundaries.
 *
 * \param normalized text that has already been through cluster::normalize().
 */
quint64 simhash(const QString &normalized,
                int ngramSize = DefaultNGramSize,
                Weighting weighting = DefaultWeighting);

/*! Number of differing bits. 0 means identical, 64 means opposite. */
inline int hammingDistance(quint64 a, quint64 b)
{
    return __builtin_popcountll(a ^ b);
}

} // namespace cluster

#endif // CLUSTER_SIMHASH_H

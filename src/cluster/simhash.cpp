#include "simhash.h"

#include <QHash>

#include <cmath>

namespace cluster {
namespace {

/*!
 * FNV-1a over the UTF-16 code units of one n-gram.
 *
 * Not qHash: it is only 32-bit on Qt 5.6 and is randomly seeded per process, which
 * would make simhashes stored in the database meaningless across restarts.
 */
quint64 fnv1a(const QChar *data, int size)
{
    quint64 hash = 0xcbf29ce484222325ULL;
    for (int i = 0; i < size; ++i) {
        const ushort unit = data[i].unicode();
        hash ^= static_cast<quint64>(unit & 0xff);
        hash *= 0x100000001b3ULL;
        hash ^= static_cast<quint64>(unit >> 8);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

int weightFor(int count, Weighting weighting)
{
    switch (weighting) {
    case Weighting::Binary:
        return 1;
    case Weighting::Log:
        return 1 + static_cast<int>(std::log2(static_cast<double>(count)));
    case Weighting::Raw:
        break;
    }
    return count;
}

} // namespace

quint64 simhash(const QString &normalized, int ngramSize, Weighting weighting)
{
    if (ngramSize < 1)
        ngramSize = DefaultNGramSize;

    if (normalized.isEmpty())
        return 0ULL;
    if (normalized.size() < ngramSize)
        return fnv1a(normalized.constData(), normalized.size());

    QHash<quint64, int> frequency;
    const QChar *data = normalized.constData();
    const int last = normalized.size() - ngramSize;
    for (int i = 0; i <= last; ++i)
        ++frequency[fnv1a(data + i, ngramSize)];

    int accumulator[64] = {0};
    for (auto it = frequency.constBegin(); it != frequency.constEnd(); ++it) {
        const quint64 hash = it.key();
        const int weight = weightFor(it.value(), weighting);
        for (int bit = 0; bit < 64; ++bit)
            accumulator[bit] += (hash >> bit) & 1ULL ? weight : -weight;
    }

    quint64 result = 0;
    for (int bit = 0; bit < 64; ++bit) {
        if (accumulator[bit] > 0)
            result |= (1ULL << bit);
    }
    return result;
}

} // namespace cluster

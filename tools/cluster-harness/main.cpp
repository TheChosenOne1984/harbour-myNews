/*
 * Standalone tuning harness for the clustering algorithm
 * (project_description.md section 6, "Recommended build order").
 *
 * Reads a JSON dump produced by tools/fetch-corpus.py, runs the *same* clustering code
 * the app uses (src/cluster), and prints the resulting clusters. Iterating here is far
 * faster than on a device, and because the algorithm sources are shared, a threshold
 * tuned here means the same thing in the app.
 *
 *   cluster-harness corpus.json [--threshold N] [--window HOURS] [--mode online|batch|compare]
 *                               [--sweep] [--min-size N] [--limit N] [--quiet]
 */

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <algorithm>

#include "cluster/clusterer.h"
#include "cluster/normalize.h"
#include "cluster/simhash.h"

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

struct Record
{
    QString source;
    QString title;
    QString teaser;
    QString link;
    QSet<QString> titleWords;   //!< normalised title split on spaces, for evaluation
    QString normalizedTitle;
};

struct HashConfig
{
    int ngramSize = cluster::DefaultNGramSize;
    cluster::Weighting weighting = cluster::DefaultWeighting;
    bool includeTeaser = true;
    bool stripKicker = false;
};

struct Corpus
{
    QVector<cluster::Article> articles;
    QVector<Record> records;
    QStringList sourceOrder;
};

QSet<QString> wordsOf(const QString &normalized)
{
    QSet<QString> words;
    for (const QString &w : normalized.split(QLatin1Char(' '), QString::SkipEmptyParts)) {
        if (w.size() > 2)   // drop "in", "am", "zu" and their equivalents in any language
            words.insert(w);
    }
    return words;
}

bool loadCorpus(const QString &path, const HashConfig &config, Corpus *corpus)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "cannot open " << path << ": " << file.errorString() << "\n";
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        err << "cannot parse " << path << ": " << parseError.errorString() << "\n";
        return false;
    }

    const QJsonArray array = doc.object().value(QStringLiteral("articles")).toArray();
    corpus->articles.reserve(array.size());
    corpus->records.reserve(array.size());

    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();

        Record record;
        record.source = obj.value(QStringLiteral("source")).toString();
        record.title = obj.value(QStringLiteral("title")).toString();
        record.teaser = obj.value(QStringLiteral("teaser")).toString();
        record.link = obj.value(QStringLiteral("link")).toString();

        if (!corpus->sourceOrder.contains(record.source))
            corpus->sourceOrder.append(record.source);

        cluster::Article article;
        article.index = corpus->articles.size();
        article.lang = obj.value(QStringLiteral("lang")).toString();
        article.published = static_cast<qint64>(obj.value(QStringLiteral("published")).toDouble());
        article.hasImage = !obj.value(QStringLiteral("image_url")).toString().isEmpty();
        article.sourcePriority = corpus->sourceOrder.indexOf(record.source);
        record.normalizedTitle = cluster::normalize(record.title);
        record.titleWords = wordsOf(record.normalizedTitle);

        const QString hashTitle = config.stripKicker ? cluster::stripKicker(record.title)
                                                     : record.title;
        // With no flags this is exactly cluster::clusterSubject() -- the harness must
        // default to what the app actually does, or tuning drifts away from reality.
        const QString subject = config.includeTeaser
            ? cluster::normalizeArticle(hashTitle, record.teaser)
            : cluster::normalize(hashTitle);
        article.simhash = cluster::simhash(subject, config.ngramSize, config.weighting);

        corpus->articles.append(article);
        corpus->records.append(record);
    }
    return true;
}

struct Stats
{
    int clusters = 0;
    int multi = 0;          //!< clusters with more than one member
    int grouped = 0;        //!< articles inside a multi-member cluster
    int largest = 0;
    int crossSource = 0;    //!< multi-member clusters spanning >1 source
    qint64 micros = 0;
};

QStringList sourcesOf(const Corpus &corpus, const cluster::Cluster &c)
{
    QStringList sources;
    for (const int index : c.members) {
        const QString &s = corpus.records[index].source;
        if (!sources.contains(s))
            sources.append(s);
    }
    return sources;
}

Stats summarise(const Corpus &corpus, const QVector<cluster::Cluster> &clusters, qint64 micros)
{
    Stats stats;
    stats.clusters = clusters.size();
    stats.micros = micros;
    for (const cluster::Cluster &c : clusters) {
        if (c.members.size() > 1) {
            ++stats.multi;
            stats.grouped += c.members.size();
            if (sourcesOf(corpus, c).size() > 1)
                ++stats.crossSource;
        }
        stats.largest = qMax(stats.largest, c.members.size());
    }
    return stats;
}

/*!
 * Evaluation without hand-labelled ground truth.
 *
 * Recall proxy: two articles whose *normalised titles are identical* are the same story
 * beyond argument -- wire copy republished verbatim. Every such pair must end up in one
 * cluster; the fraction that does is a floor on recall.
 *
 * Precision proxy: for every pair the clusterer did group, the word-level Jaccard
 * overlap of their titles. Genuine same-story pairs share content words. A grouped pair
 * sharing almost nothing is a false positive with very high probability. This is a
 * proxy, not a verdict -- a paraphrased headline can be a true match with low overlap --
 * but it moves consistently with quality, which is what tuning needs.
 */
struct Eval
{
    int duplicatePairs = 0;
    int duplicatePairsGrouped = 0;
    int groupedPairs = 0;
    int strongPairs = 0;    //!< Jaccard >= 0.30
    int weakPairs = 0;      //!< Jaccard <  0.10, almost certainly wrong
    double meanJaccard = 0.0;

    double recallProxy() const
    {
        return duplicatePairs ? double(duplicatePairsGrouped) / duplicatePairs : 1.0;
    }
    double weakRate() const
    {
        return groupedPairs ? double(weakPairs) / groupedPairs : 0.0;
    }
};

double jaccard(const QSet<QString> &a, const QSet<QString> &b)
{
    if (a.isEmpty() || b.isEmpty())
        return 0.0;
    int intersection = 0;
    for (const QString &w : a) {
        if (b.contains(w))
            ++intersection;
    }
    const int unionSize = a.size() + b.size() - intersection;
    return unionSize ? double(intersection) / unionSize : 0.0;
}

Eval evaluate(const Corpus &corpus, const QVector<cluster::Cluster> &clusters)
{
    Eval eval;

    QVector<int> clusterOf(corpus.records.size(), -1);
    for (int c = 0; c < clusters.size(); ++c) {
        for (const int index : clusters[c].members)
            clusterOf[index] = c;
    }

    // Recall proxy over exact normalised-title duplicates.
    QHash<QString, QVector<int>> byTitle;
    for (int i = 0; i < corpus.records.size(); ++i) {
        if (!corpus.records[i].normalizedTitle.isEmpty())
            byTitle[corpus.records[i].normalizedTitle].append(i);
    }
    for (auto it = byTitle.constBegin(); it != byTitle.constEnd(); ++it) {
        const QVector<int> &group = it.value();
        for (int i = 0; i < group.size(); ++i) {
            for (int j = i + 1; j < group.size(); ++j) {
                ++eval.duplicatePairs;
                if (clusterOf[group[i]] == clusterOf[group[j]])
                    ++eval.duplicatePairsGrouped;
            }
        }
    }

    // Precision proxy over every pair the clusterer grouped.
    double jaccardSum = 0.0;
    for (const cluster::Cluster &c : clusters) {
        for (int i = 0; i < c.members.size(); ++i) {
            for (int j = i + 1; j < c.members.size(); ++j) {
                const double overlap = jaccard(corpus.records[c.members[i]].titleWords,
                                               corpus.records[c.members[j]].titleWords);
                ++eval.groupedPairs;
                jaccardSum += overlap;
                if (overlap >= 0.30)
                    ++eval.strongPairs;
                else if (overlap < 0.10)
                    ++eval.weakPairs;
            }
        }
    }
    if (eval.groupedPairs)
        eval.meanJaccard = jaccardSum / eval.groupedPairs;
    return eval;
}

void printStats(const QString &label, const Stats &s, int total)
{
    out << QString("%1  clusters=%2  multi=%3  grouped=%4/%5  cross-source=%6  largest=%7  %8 ms\n")
               .arg(label, -8)
               .arg(s.clusters, 4)
               .arg(s.multi, 4)
               .arg(s.grouped, 4)
               .arg(total)
               .arg(s.crossSource, 4)
               .arg(s.largest, 3)
               .arg(s.micros / 1000.0, 0, 'f', 1);
}

void printClusters(const Corpus &corpus, const QVector<cluster::Cluster> &clusters,
                   int minSize, int limit)
{
    QVector<const cluster::Cluster *> interesting;
    for (const cluster::Cluster &c : clusters) {
        if (c.members.size() >= minSize)
            interesting.append(&c);
    }
    std::sort(interesting.begin(), interesting.end(),
              [](const cluster::Cluster *a, const cluster::Cluster *b) {
                  return a->members.size() > b->members.size();
              });

    int shown = 0;
    for (const cluster::Cluster *c : interesting) {
        if (limit > 0 && shown >= limit)
            break;
        ++shown;

        const QStringList sources = sourcesOf(corpus, *c);
        out << "\n" << QString("== %1 articles, %2 source(s): %3\n")
                          .arg(c->members.size())
                          .arg(sources.size())
                          .arg(sources.join(QStringLiteral(", ")));
        for (const int index : c->members) {
            const bool lead = (index == c->lead);
            out << QString("   %1 [%2] %3\n")
                       .arg(lead ? QStringLiteral("*") : QStringLiteral(" "))
                       .arg(corpus.records[index].source, -14)
                       .arg(corpus.records[index].title);
        }
    }
    if (limit > 0 && interesting.size() > shown)
        out << QString("\n... and %1 more\n").arg(interesting.size() - shown);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cluster-harness"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Tunes the myNews clustering algorithm against a real corpus.");
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("corpus"),
                                 QStringLiteral("JSON dump from tools/fetch-corpus.py"));

    QCommandLineOption thresholdOpt(QStringList() << "t" << "threshold",
        QStringLiteral("Hamming threshold, 0-64 (default 14)"), "N", "14");
    QCommandLineOption windowOpt(QStringList() << "w" << "window",
        QStringLiteral("Time window in hours (default 48)"), "HOURS", "48");
    QCommandLineOption modeOpt(QStringList() << "m" << "mode",
        QStringLiteral("online, batch or compare (default online)"), "MODE", "online");
    QCommandLineOption sweepOpt(QStringLiteral("sweep"),
        QStringLiteral("Sweep thresholds and windows instead of printing clusters"));
    QCommandLineOption minSizeOpt(QStringLiteral("min-size"),
        QStringLiteral("Only print clusters with at least N members (default 2)"), "N", "2");
    QCommandLineOption limitOpt(QStringLiteral("limit"),
        QStringLiteral("Print at most N clusters (default 25, 0 = all)"), "N", "25");
    QCommandLineOption quietOpt(QStringLiteral("quiet"),
        QStringLiteral("Statistics only, no cluster listing"));
    QCommandLineOption ngramOpt(QStringLiteral("ngram"),
        QStringLiteral("Character n-gram width (default 5)"), "N", "5");
    QCommandLineOption weightOpt(QStringLiteral("weight"),
        QStringLiteral("n-gram weighting: raw, log or binary (default binary)"), "W", "binary");
    QCommandLineOption fieldOpt(QStringLiteral("field"),
        QStringLiteral("hash over 'title' or 'title+teaser' (default title)"),
        "F", "title");
    QCommandLineOption keepKickerOpt(QStringLiteral("keep-kicker"),
        QStringLiteral("Keep the leading \"Topic: \" prefix (the app strips it)"));
    QCommandLineOption gridOpt(QStringLiteral("grid"),
        QStringLiteral("Compare n-gram/weighting/field configurations by evaluation metrics"));

    parser.addOption(thresholdOpt);
    parser.addOption(windowOpt);
    parser.addOption(modeOpt);
    parser.addOption(sweepOpt);
    parser.addOption(minSizeOpt);
    parser.addOption(limitOpt);
    parser.addOption(quietOpt);
    parser.addOption(ngramOpt);
    parser.addOption(weightOpt);
    parser.addOption(fieldOpt);
    parser.addOption(keepKickerOpt);
    parser.addOption(gridOpt);
    parser.process(app);

    if (parser.positionalArguments().isEmpty()) {
        err << "no corpus given\n";
        parser.showHelp(2);
    }

    cluster::Params params;
    params.hammingThreshold = parser.value(thresholdOpt).toInt();
    params.windowSeconds = static_cast<qint64>(parser.value(windowOpt).toDouble() * 3600);

    HashConfig config;
    config.ngramSize = parser.value(ngramOpt).toInt();
    config.includeTeaser = (parser.value(fieldOpt) != QLatin1String("title"));
    config.stripKicker = !parser.isSet(keepKickerOpt);
    const QString weightName = parser.value(weightOpt);
    if (weightName == QLatin1String("raw"))
        config.weighting = cluster::Weighting::Raw;
    else if (weightName == QLatin1String("log"))
        config.weighting = cluster::Weighting::Log;
    else
        config.weighting = cluster::Weighting::Binary;

    const QString corpusPath = parser.positionalArguments().first();

    if (parser.isSet(gridOpt)) {
        out << "\ngrid: which hashing configuration separates stories best?\n";
        out << "  (recall = exact-duplicate pairs grouped; weak = grouped pairs sharing <10% of title words)\n\n";
        out << "  ngram  weight  field         kick  thr  clusters  multi  x-src  recall   weak  meanJ\n";
        const QVector<int> ngrams{3, 4, 5, 6, 8};
        const QVector<QPair<QString, cluster::Weighting>> weightings{
            {QStringLiteral("raw"), cluster::Weighting::Raw},
            {QStringLiteral("binary"), cluster::Weighting::Binary}};
        const QVector<QPair<QString, bool>> kickers{
            {QStringLiteral("keep"), false},
            {QStringLiteral("strip"), true}};
        const QVector<QPair<QString, bool>> fields{
            {QStringLiteral("title"), false},
            {QStringLiteral("title+teaser"), true}};

        for (const int ngram : ngrams) {
            for (const auto &weighting : weightings) {
                for (const auto &field : fields)
                for (const auto &kicker : kickers) {
                    HashConfig cfg;
                    cfg.ngramSize = ngram;
                    cfg.weighting = weighting.second;
                    cfg.includeTeaser = field.second;
                    cfg.stripKicker = kicker.second;

                    Corpus gridCorpus;
                    if (!loadCorpus(corpusPath, cfg, &gridCorpus))
                        return 1;

                    for (const int threshold : QVector<int>{10, 12, 14, 16}) {
                        cluster::Params p;
                        p.hammingThreshold = threshold;
                        p.windowSeconds = params.windowSeconds;
                        const QVector<cluster::Cluster> result =
                            cluster::clusterOnline(gridCorpus.articles, p);
                        const Stats st = summarise(gridCorpus, result, 0);
                        const Eval ev = evaluate(gridCorpus, result);
                        out << QString("  %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11\n")
                                   .arg(ngram, 6)
                                   .arg(weighting.first, 7)
                                   .arg(field.first, -13)
                                   .arg(kicker.first, 5)
                                   .arg(threshold, 4)
                                   .arg(st.clusters, 9)
                                   .arg(st.multi, 6)
                                   .arg(st.crossSource, 6)
                                   .arg(ev.recallProxy() * 100, 7, 'f', 1)
                                   .arg(ev.weakRate() * 100, 6, 'f', 1)
                                   .arg(ev.meanJaccard, 6, 'f', 2);
                    }
                }
            }
        }
        return 0;
    }

    Corpus corpus;
    if (!loadCorpus(corpusPath, config, &corpus))
        return 1;

    const int total = corpus.articles.size();
    out << QString("corpus: %1 articles from %2 sources\n")
               .arg(total).arg(corpus.sourceOrder.size());
    if (parser.isSet(sweepOpt)) {
        out << "\nsweep (online) with the configured hashing:\n";
        out << "  window  thr  clusters  multi  x-src  grouped  largest  recall   weak  meanJ\n";
        const QVector<int> windows{12, 24, 48, 96};
        const QVector<int> thresholds{8, 10, 12, 14, 16, 18, 20, 22, 24};
        for (const int hours : windows) {
            for (const int threshold : thresholds) {
                cluster::Params p;
                p.windowSeconds = static_cast<qint64>(hours) * 3600;
                p.hammingThreshold = threshold;
                const QVector<cluster::Cluster> result = cluster::clusterOnline(corpus.articles, p);
                const Stats st = summarise(corpus, result, 0);
                const Eval ev = evaluate(corpus, result);
                out << QString("  %1h %2 %3 %4 %5 %6 %7 %8 %9 %10\n")
                           .arg(hours, 5).arg(threshold, 5).arg(st.clusters, 9)
                           .arg(st.multi, 6).arg(st.crossSource, 6).arg(st.grouped, 8)
                           .arg(st.largest, 8)
                           .arg(ev.recallProxy() * 100, 7, 'f', 1)
                           .arg(ev.weakRate() * 100, 6, 'f', 1)
                           .arg(ev.meanJaccard, 6, 'f', 2);
            }
        }
        return 0;
    }

    const QString mode = parser.value(modeOpt);
    const bool wantOnline = (mode == QLatin1String("online") || mode == QLatin1String("compare"));
    const bool wantBatch = (mode == QLatin1String("batch") || mode == QLatin1String("compare"));

    out << QString("params: threshold=%1  window=%2h\n\n")
               .arg(params.hammingThreshold).arg(params.windowSeconds / 3600.0, 0, 'g', 3);

    QVector<cluster::Cluster> online;
    if (wantOnline) {
        QElapsedTimer timer;
        timer.start();
        online = cluster::clusterOnline(corpus.articles, params);
        printStats(QStringLiteral("online"), summarise(corpus, online, timer.nsecsElapsed() / 1000), total);
    }
    if (wantBatch) {
        QElapsedTimer timer;
        timer.start();
        const QVector<cluster::Cluster> batch = cluster::clusterBatch(corpus.articles, params);
        printStats(QStringLiteral("batch"), summarise(corpus, batch, timer.nsecsElapsed() / 1000), total);
        if (!wantOnline)
            online = batch;
    }

    if (!online.isEmpty()) {
        const Eval ev = evaluate(corpus, online);
        out << QString("\nevaluation: exact-duplicate pairs %1/%2 grouped (%3%)"
                       "   grouped pairs %4: strong %5, weak %6 (%7%)   mean Jaccard %8\n")
                   .arg(ev.duplicatePairsGrouped).arg(ev.duplicatePairs)
                   .arg(ev.recallProxy() * 100, 0, 'f', 1)
                   .arg(ev.groupedPairs).arg(ev.strongPairs).arg(ev.weakPairs)
                   .arg(ev.weakRate() * 100, 0, 'f', 1)
                   .arg(ev.meanJaccard, 0, 'f', 2);
    }

    if (!parser.isSet(quietOpt))
        printClusters(corpus, online, parser.value(minSizeOpt).toInt(), parser.value(limitOpt).toInt());

    return 0;
}

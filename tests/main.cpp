#include <QCoreApplication>

#include "testrunner.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    failures += runFeedParserTests(argc, argv);
    failures += runFeedStoreTests(argc, argv);
    failures += runFeedFetcherTests(argc, argv);
    failures += runCatalogTests(argc, argv);
    failures += runModelTests(argc, argv);
    failures += runOpmlTests(argc, argv);
    failures += runClusterViewTests(argc, argv);
    failures += runThumbnailTests(argc, argv);
    failures += runClusterTests(argc, argv);
    return failures;
}

#ifndef TESTS_TESTRUNNER_H
#define TESTS_TESTRUNNER_H

/*!
 * One binary, several test classes. QTEST_MAIN would give each file its own main(), so
 * each suite exposes a runner instead and main.cpp adds up the failures.
 */
int runFeedParserTests(int argc, char **argv);
int runFeedStoreTests(int argc, char **argv);
int runFeedFetcherTests(int argc, char **argv);
int runClusterTests(int argc, char **argv);

#endif // TESTS_TESTRUNNER_H

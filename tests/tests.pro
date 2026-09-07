# Headless unit tests. Built for the aarch64 target and run through
# scratchbox2's qemu, so they exercise the same Qt and the same libraries as
# the device without needing one:
#
#   cd tests
#   sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
#   sfdk tools exec SailfishOS-5.1.0.11-aarch64 $PWD/tst_mynews
#
# Never packaged for Harbour.

TEMPLATE = app
TARGET = tst_mynews
CONFIG += console testcase
CONFIG -= app_bundle
QT += core testlib sql network
QT -= gui

QMAKE_CXXFLAGS += -std=c++17

INCLUDEPATH += $$PWD/../src

# So a test can check the catalogue file that will actually be hosted.
DEFINES += MYNEWS_SOURCE_DIR=\\\"$$PWD/..\\\"

SOURCES += \
    main.cpp \
    tst_feedparser.cpp \
    tst_feedstore.cpp \
    tst_feedfetcher.cpp \
    tst_catalog.cpp \
    tst_cluster.cpp \
    $$PWD/../src/feed/feedparser.cpp \
    $$PWD/../src/feed/feedstore.cpp \
    $$PWD/../src/feed/feedfetcher.cpp \
    $$PWD/../src/catalog/catalog.cpp \
    $$PWD/../src/catalog/catalogservice.cpp \
    $$PWD/../src/app/settings.cpp \
    $$PWD/../src/cluster/normalize.cpp \
    $$PWD/../src/cluster/simhash.cpp \
    $$PWD/../src/cluster/clusterer.cpp

HEADERS += \
    testrunner.h \
    $$PWD/../src/feed/feedparser.h \
    $$PWD/../src/feed/feedstore.h \
    $$PWD/../src/feed/feedfetcher.h \
    $$PWD/../src/catalog/catalog.h \
    $$PWD/../src/catalog/catalogservice.h \
    $$PWD/../src/app/settings.h \
    $$PWD/../src/cluster/normalize.h \
    $$PWD/../src/cluster/simhash.h \
    $$PWD/../src/cluster/clusterer.h

target.path = /usr/bin
INSTALLS += target

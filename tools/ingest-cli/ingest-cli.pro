# End-to-end driver for the ingestion core. Development tool, never packaged for Harbour.
TEMPLATE = app
TARGET = ingest-cli
CONFIG += console
CONFIG -= app_bundle
QT += core network sql
QT -= gui

QMAKE_CXXFLAGS += -std=c++17
INCLUDEPATH += $$PWD/../../src

SOURCES += \
    main.cpp \
    $$PWD/../../src/feed/feedparser.cpp \
    $$PWD/../../src/feed/feedstore.cpp \
    $$PWD/../../src/feed/feedfetcher.cpp \
    $$PWD/../../src/feed/feedrefresher.cpp \
    $$PWD/../../src/cluster/normalize.cpp \
    $$PWD/../../src/cluster/simhash.cpp \
    $$PWD/../../src/cluster/clusterer.cpp

HEADERS += \
    $$PWD/../../src/feed/feedparser.h \
    $$PWD/../../src/feed/feedstore.h \
    $$PWD/../../src/feed/feedfetcher.h \
    $$PWD/../../src/feed/feedrefresher.h \
    $$PWD/../../src/cluster/normalize.h \
    $$PWD/../../src/cluster/simhash.h \
    $$PWD/../../src/cluster/clusterer.h

target.path = /usr/bin
INSTALLS += target

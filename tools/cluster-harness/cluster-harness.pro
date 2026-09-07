# Desktop/target tuning harness for the clustering algorithm. Never packaged for
# Harbour -- it exists so thresholds can be tuned against real feed data before the
# algorithm is wired into the app (project_description.md section 6).
#
# It compiles the *same* sources the app uses, so a threshold tuned here transfers.

TEMPLATE = app
TARGET = cluster-harness
CONFIG += console
CONFIG -= app_bundle
QT += core
QT -= gui

QMAKE_CXXFLAGS += -std=c++17

INCLUDEPATH += $$PWD/../../src

SOURCES += \
    main.cpp \
    $$PWD/../../src/cluster/normalize.cpp \
    $$PWD/../../src/cluster/simhash.cpp \
    $$PWD/../../src/cluster/clusterer.cpp

HEADERS += \
    $$PWD/../../src/cluster/normalize.h \
    $$PWD/../../src/cluster/simhash.h \
    $$PWD/../../src/cluster/clusterer.h

target.path = /usr/bin
INSTALLS += target

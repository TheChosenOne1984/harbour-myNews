# myNews -- cross-source clustering news reader for Sailfish OS
#
# Build target: SailfishOS-5.1.0.11 (Jolla has published no 5.2 SDK target; see
# task0-findings.md section 0). Qt is 5.6.3, but the toolchain is GCC 13.4, so C++17
# is available -- Qt 5.6's qmake does not understand "CONFIG += c++17", hence the
# explicit flag below.

TARGET = harbour-mynews

CONFIG += sailfishapp sailfishapp_i18n link_pkgconfig

# sailfishapp must be listed explicitly: link_pkgconfig is resolved before
# sailfishapp.prf gets to append itself, so relying on the prf alone fails to link
# with "undefined reference to SailfishApp::main". See task0-findings.md section 9.
PKGCONFIG += sailfishapp

# Added together with the article detail page (milestone 4). Verified to build and to
# pass Harbour validation, but the emulator has no WebView, so it stays out until the
# page that needs it exists.
# PKGCONFIG += qt5embedwidget

QT += sql network concurrent

QMAKE_CXXFLAGS += -std=c++17

SOURCES += \
    src/main.cpp \
    src/app/appcontroller.cpp \
    src/app/backend.cpp \
    src/app/settings.cpp \
    src/catalog/catalog.cpp \
    src/catalog/catalogservice.cpp \
    src/cluster/clusterer.cpp \
    src/cluster/normalize.cpp \
    src/cluster/simhash.cpp \
    src/feed/feedfetcher.cpp \
    src/feed/feedparser.cpp \
    src/feed/feedrefresher.cpp \
    src/feed/feedstore.cpp \
    src/feed/opml.cpp \
    src/i18n/languagemanager.cpp \
    src/i18n/languagenames.cpp \
    src/model/catalogmodel.cpp \
    src/model/subscriptionmodel.cpp

HEADERS += \
    src/app/appcontroller.h \
    src/app/backend.h \
    src/app/settings.h \
    src/catalog/catalog.h \
    src/catalog/catalogservice.h \
    src/cluster/clusterer.h \
    src/cluster/normalize.h \
    src/cluster/simhash.h \
    src/feed/feedfetcher.h \
    src/feed/feedparser.h \
    src/feed/feedrefresher.h \
    src/feed/feedstore.h \
    src/feed/opml.h \
    src/i18n/languagemanager.h \
    src/i18n/languagenames.h \
    src/model/catalogmodel.h \
    src/model/subscriptionmodel.h

INCLUDEPATH += $$PWD/src

DISTFILES += \
    qml/harbour-mynews.qml \
    qml/cover/CoverPage.qml \
    qml/pages/AddFeedPage.qml \
    qml/pages/CatalogPage.qml \
    qml/pages/FeedPage.qml \
    qml/pages/LanguagePickerPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/SubscriptionsPage.qml \
    rpm/harbour-mynews.spec \
    translations/*.ts

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# Adding a language is this line plus the .ts file -- no code change; the language
# picker discovers installed .qm files at runtime.
TRANSLATIONS += translations/harbour-mynews-de.ts

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
    src/i18n/languagemanager.cpp

HEADERS += \
    src/i18n/languagemanager.h

DISTFILES += \
    qml/harbour-mynews.qml \
    qml/cover/CoverPage.qml \
    qml/pages/FeedPage.qml \
    qml/pages/SettingsPage.qml \
    rpm/harbour-mynews.spec \
    translations/*.ts

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# Adding a language is this line plus the .ts file -- no code change; the language
# picker discovers installed .qm files at runtime.
TRANSLATIONS += translations/harbour-mynews-de.ts

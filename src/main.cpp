#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <QStandardPaths>

#include <sailfishapp.h>

#include "app/appcontroller.h"
#include "app/settings.h"
#include "i18n/languagemanager.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    // Keeps QSettings, the database and the cache inside the paths Sailjail whitelists
    // for this application.
    app->setApplicationName(QStringLiteral("harbour-mynews"));
    app->setOrganizationName(QStringLiteral("harbour-mynews"));

    app::Settings settings;
    LanguageManager languages;
    // The choice is remembered, so it has to be applied before the first page loads --
    // otherwise every start falls back to "System" whatever the reader picked. No view is
    // set yet, so this installs the translator without reloading anything.
    languages.setLanguage(settings.uiLanguage());

    app::AppController controller;
    controller.setSettings(&settings);

    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    // SailfishApp::createView() rather than SailfishApp::main(): the language switch
    // needs the view pointer in order to reload the root component.
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    languages.setView(view.data());

    QQmlContext *context = view->rootContext();
    context->setContextProperty(QStringLiteral("Languages"), &languages);
    context->setContextProperty(QStringLiteral("Settings"), &settings);
    context->setContextProperty(QStringLiteral("App"), &controller);

    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-mynews.qml")));
    view->show();

    controller.start(dataDir + QStringLiteral("/mynews.db"),
                     cacheDir + QStringLiteral("/catalog.json"),
                     // Section 8: thumbnails belong under CacheLocation, so the system
                     // may reclaim them. A missing file is simply fetched again.
                     cacheDir + QStringLiteral("/thumbnails"));

    return app->exec();
}

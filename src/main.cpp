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
                     cacheDir + QStringLiteral("/catalog.json"));

    return app->exec();
}

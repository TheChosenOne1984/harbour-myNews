#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>

#include <sailfishapp.h>

#include "i18n/languagemanager.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

    // Keeps QSettings and the cache inside the paths Sailjail whitelists for this
    // application (~/.config/harbour-mynews, ~/.cache/harbour-mynews).
    app->setApplicationName(QStringLiteral("harbour-mynews"));
    app->setOrganizationName(QStringLiteral("harbour-mynews"));

    LanguageManager languages;

    // SailfishApp::createView() rather than SailfishApp::main(): the language switch
    // needs the view pointer in order to reload the root component.
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    languages.setView(view.data());
    view->rootContext()->setContextProperty(QStringLiteral("Languages"), &languages);
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-mynews.qml")));
    view->show();

    return app->exec();
}

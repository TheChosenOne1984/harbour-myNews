#include "languagemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QQmlEngine>
#include <QQuickView>
#include <QTranslator>
#include <QUrl>

#include <sailfishapp.h>

#include "i18n/languagenames.h"

namespace {

/*! Basename of the .qm files, which is also the installed application name. */
const QLatin1String CatalogName("harbour-mynews");

QString translationsDir()
{
    return SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
}

} // namespace

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
    m_available << QString(); // "System" is always offered and always first.

    const QDir dir(translationsDir());
    const QStringList files = dir.entryList(
        QStringList() << CatalogName + QStringLiteral("-*.qm"), QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        QString code = fileName.mid(CatalogName.size() + 1); // drop "harbour-mynews-"
        code.chop(3);                                        // drop ".qm"
        if (!code.isEmpty())
            m_available << code;
    }

    install(QString());
}

LanguageManager::~LanguageManager() = default;

QString LanguageManager::displayName(const QString &code) const
{
    if (code.isEmpty())
        return tr("System");

    const QString name = i18n::languageDisplayName(code);
    return name.isEmpty() ? code : name;
}

void LanguageManager::install(const QString &code)
{
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }

    const QString dir = translationsDir();
    QTranslator *translator = new QTranslator(this);

    // Failing to load is the correct outcome for English and for any locale we do not
    // ship: the source strings are already English.
    const bool loaded = code.isEmpty()
        ? translator->load(QLocale(), CatalogName, QStringLiteral("-"), dir)
        : translator->load(CatalogName + QLatin1Char('-') + code, dir);

    if (loaded) {
        QCoreApplication::installTranslator(translator);
        m_translator = translator;
    } else {
        delete translator;
    }

    m_current = code;
}

void LanguageManager::reloadView()
{
    if (!m_view)
        return;

    const QUrl source = m_view->source();
    m_view->setSource(QUrl());
    m_view->engine()->clearComponentCache();
    m_view->setSource(source);
}

void LanguageManager::setLanguage(const QString &code)
{
    if (code == m_current)
        return;

    install(code);
    reloadView();
    emit languageChanged();
}

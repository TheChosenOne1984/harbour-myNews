#ifndef I18N_LANGUAGEMANAGER_H
#define I18N_LANGUAGEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QQuickView;
class QTranslator;

/*!
 * \brief Runtime UI-language switching.
 *
 * English is the source language; shipped translations live in
 * /usr/share/harbour-mynews/translations as harbour-mynews-<code>.qm. The list of
 * languages is discovered by scanning that directory at startup, so shipping a new
 * language is a .ts file plus a TRANSLATIONS line -- never a code change.
 *
 * An empty language code means "System": follow the device locale, falling back to
 * the untranslated English source when the device locale has no .qm. It is the
 * default.
 *
 * \note Qt 5.6 has no QQmlEngine::retranslate() (that arrived in 5.10), so swapping
 * the QTranslator leaves every qsTr() binding stale. Rather than burdening every
 * string in the codebase with the "+ tr.emptyString" trick, setLanguage() reloads the
 * root QML component. C++-side models and the database connection are unaffected --
 * only the view is rebuilt -- at the cost of returning the user to the main page.
 * Verified working in the emulator; see task0-findings.md section 11.
 */
class LanguageManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY languageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)

public:
    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager() override;

    /*! Empty string when following the device locale. */
    QString currentLanguage() const { return m_current; }

    /*! Language codes, always starting with the empty string for "System". */
    QStringList availableLanguages() const { return m_available; }

    /*! Endonym for a code ("Deutsch"), or the translated word for "System". */
    Q_INVOKABLE QString displayName(const QString &code) const;

    /*! Switches language and reloads the view. No-op if already current. */
    Q_INVOKABLE void setLanguage(const QString &code);

    /*! Must be called before the view loads its source. */
    void setView(QQuickView *view) { m_view = view; }

signals:
    void languageChanged();

private:
    void install(const QString &code);
    void reloadView();

    QString m_current;
    QStringList m_available;
    QTranslator *m_translator = nullptr;
    QQuickView *m_view = nullptr;
};

#endif // I18N_LANGUAGEMANAGER_H

#ifndef CATALOG_CATALOG_H
#define CATALOG_CATALOG_H

#include <QByteArray>
#include <QString>
#include <QHash>
#include <QVector>

namespace catalog {

/*! One offerable feed. */
struct Entry
{
    QString url;
    QString title;
    QString lang;         //!< BCP-47 primary subtag; mandatory (section 7)
    QString category;     //!< id of a Category
    QString siteUrl;
    QString description;  //!< English, the source language
    QHash<QString, QString> localisedDescriptions;   //!< BCP-47 -> description

    bool isValid() const { return !url.isEmpty() && !title.isEmpty() && !lang.isEmpty(); }

    /*! The description for a UI language, falling back to English. */
    QString displayDescription(const QString &lang) const;
};

/*! A grouping, with a display name that can be localised. */
struct Category
{
    QString id;
    QString name;         //!< English, the source language
    QHash<QString, QString> localisedNames;   //!< BCP-47 -> name

    /*! The name for a UI language, falling back to English. */
    QString displayName(const QString &lang) const;
};

/*!
 * \brief The curated starter list.
 *
 * Fetched on first start and cached; never bundled (section 10). JSON rather than OPML
 * because it has to carry a language, a region and a category per feed, and OPML has
 * nowhere to put them.
 */
struct Catalog
{
    int version = 0;
    QString updated;
    QVector<Category> categories;
    QVector<Entry> entries;
    QString error;

    bool isValid() const { return error.isEmpty() && !entries.isEmpty(); }
    const Category *category(const QString &id) const;
};

/*! Highest format version this build understands. */
constexpr int SupportedVersion = 1;

/*!
 * \brief Parses a catalogue document.
 *
 * Entries missing a url, title or language are skipped rather than failing the whole
 * document: one bad row in a remotely hosted file must not leave every user without a
 * starter list.
 */
Catalog parseCatalog(const QByteArray &json);

} // namespace catalog

#endif // CATALOG_CATALOG_H

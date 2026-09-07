#include "catalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "i18n/languagenames.h"

namespace catalog {
namespace {

const QString LocalisedNamePrefix = QStringLiteral("name_");
const QString LocalisedDescriptionPrefix = QStringLiteral("description_");

/*!
 * Picks the value for a UI language: exact match, then the base language of a regional
 * variant, then the English source.
 */
QString localised(const QHash<QString, QString> &values, const QString &lang,
                  const QString &fallback)
{
    if (!lang.isEmpty()) {
        auto it = values.constFind(lang);
        if (it != values.constEnd())
            return it.value();
        const int dash = lang.indexOf(QLatin1Char('-'));
        if (dash > 0) {
            it = values.constFind(lang.left(dash));
            if (it != values.constEnd())
                return it.value();
        }
    }
    return fallback;
}

} // namespace

QString Entry::displayDescription(const QString &lang) const
{
    return localised(localisedDescriptions, lang, description);
}

QString Category::displayName(const QString &lang) const
{
    return localised(localisedNames, lang, name);
}

const Category *Catalog::category(const QString &id) const
{
    for (const Category &entry : categories) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

Catalog parseCatalog(const QByteArray &json)
{
    Catalog result;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        result.error = parseError.error == QJsonParseError::NoError
            ? QStringLiteral("catalogue is not a JSON object")
            : parseError.errorString();
        return result;
    }

    const QJsonObject root = doc.object();
    result.version = root.value(QStringLiteral("version")).toInt();
    result.updated = root.value(QStringLiteral("updated")).toString();

    if (result.version <= 0) {
        result.error = QStringLiteral("catalogue has no version");
        return result;
    }
    if (result.version > SupportedVersion) {
        // Refusing is safer than guessing: a newer format may mean fields whose absence
        // changes meaning. The cached copy stays in use.
        result.error = QStringLiteral("catalogue format version %1 is newer than %2")
                           .arg(result.version)
                           .arg(SupportedVersion);
        return result;
    }

    for (const QJsonValue &value : root.value(QStringLiteral("categories")).toArray()) {
        const QJsonObject object = value.toObject();
        Category category;
        category.id = object.value(QStringLiteral("id")).toString();
        category.name = object.value(QStringLiteral("name")).toString();
        if (category.id.isEmpty() || category.name.isEmpty())
            continue;
        // Localised names follow the same convention as the UI translations: English is
        // the source, "name_<lang>" overrides it.
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key().startsWith(LocalisedNamePrefix) && it.value().isString()) {
                category.localisedNames.insert(it.key().mid(LocalisedNamePrefix.size()),
                                               it.value().toString());
            }
        }
        result.categories.append(category);
    }

    for (const QJsonValue &value : root.value(QStringLiteral("feeds")).toArray()) {
        const QJsonObject object = value.toObject();
        Entry entry;
        entry.url = object.value(QStringLiteral("url")).toString().trimmed();
        entry.title = object.value(QStringLiteral("title")).toString().trimmed();
        // A catalogue is hand-written, so it may well say "en-GB"; the app partitions on
        // the primary subtag only.
        entry.lang = i18n::normalizeLanguageTag(object.value(QStringLiteral("lang")).toString());
        entry.category = object.value(QStringLiteral("category")).toString();
        entry.siteUrl = object.value(QStringLiteral("site_url")).toString();
        entry.description = object.value(QStringLiteral("description")).toString();
        // Same convention as the category names and the UI translations: English is the
        // source, "description_<lang>" overrides it.
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key().startsWith(LocalisedDescriptionPrefix) && it.value().isString()) {
                entry.localisedDescriptions.insert(
                    it.key().mid(LocalisedDescriptionPrefix.size()), it.value().toString());
            }
        }
        // One malformed row must not cost every user their starter list.
        if (entry.isValid())
            result.entries.append(entry);
    }

    if (result.entries.isEmpty())
        result.error = QStringLiteral("catalogue contains no usable feeds");
    return result;
}

} // namespace catalog

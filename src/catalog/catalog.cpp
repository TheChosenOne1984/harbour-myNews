#include "catalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "i18n/languagenames.h"

namespace catalog {
namespace {

const QString LocalisedPrefix = QStringLiteral("name_");

} // namespace

QString Category::displayName(const QString &lang) const
{
    if (!lang.isEmpty()) {
        auto it = localisedNames.constFind(lang);
        if (it != localisedNames.constEnd())
            return it.value();
        // "de-AT" falls back to "de" before falling back to English.
        const int dash = lang.indexOf(QLatin1Char('-'));
        if (dash > 0) {
            it = localisedNames.constFind(lang.left(dash));
            if (it != localisedNames.constEnd())
                return it.value();
        }
    }
    return name;
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
            if (it.key().startsWith(LocalisedPrefix) && it.value().isString()) {
                category.localisedNames.insert(it.key().mid(LocalisedPrefix.size()),
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
        // One malformed row must not cost every user their starter list.
        if (entry.isValid())
            result.entries.append(entry);
    }

    if (result.entries.isEmpty())
        result.error = QStringLiteral("catalogue contains no usable feeds");
    return result;
}

} // namespace catalog

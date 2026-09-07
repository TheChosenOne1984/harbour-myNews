#include "catalogmodel.h"

namespace model {

CatalogModel::CatalogModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CatalogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QHash<int, QByteArray> CatalogModel::roleNames() const
{
    return {
        {UrlRole, "url"},
        {TitleRole, "title"},
        {LangRole, "lang"},
        {CategoryRole, "category"},
        {CategoryNameRole, "categoryName"},
        {SiteUrlRole, "siteUrl"},
        {DescriptionRole, "description"},
        {SubscribedRole, "subscribed"},
    };
}

QVariant CatalogModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_visible.size())
        return QVariant();

    const catalog::Entry &entry = m_catalog.entries.at(m_visible.at(index.row()));
    switch (role) {
    case UrlRole:
        return entry.url;
    case TitleRole:
        return entry.title;
    case LangRole:
        return entry.lang;
    case CategoryRole:
        return entry.category;
    case CategoryNameRole:
        return categoryName(entry.category);
    case SiteUrlRole:
        return entry.siteUrl;
    case DescriptionRole:
        return entry.displayDescription(m_uiLanguage);
    case SubscribedRole:
        return m_subscribed.contains(entry.url);
    default:
        break;
    }
    return QVariant();
}

void CatalogModel::setCatalog(const catalog::Catalog &catalog)
{
    beginResetModel();
    m_catalog = catalog;
    rebuild();
    endResetModel();
    emit countChanged();
}

void CatalogModel::setSubscribedUrls(const QSet<QString> &urls)
{
    if (urls == m_subscribed)
        return;
    m_subscribed = urls;
    if (!m_visible.isEmpty()) {
        // Only the flag changed, so the rows stay put -- no reset, which would scroll the
        // page back to the top every time the user subscribes to something.
        emit dataChanged(index(0), index(m_visible.size() - 1), {SubscribedRole});
    }
}

void CatalogModel::setCategoryFilter(const QString &categoryId)
{
    if (categoryId == m_categoryFilter)
        return;
    beginResetModel();
    m_categoryFilter = categoryId;
    rebuild();
    endResetModel();
    emit categoryFilterChanged();
    emit countChanged();
}

void CatalogModel::setUiLanguage(const QString &lang)
{
    if (lang == m_uiLanguage)
        return;
    m_uiLanguage = lang;
    if (!m_visible.isEmpty()) {
        emit dataChanged(index(0), index(m_visible.size() - 1),
                         {CategoryNameRole, DescriptionRole});
    }
    emit uiLanguageChanged();
}

void CatalogModel::rebuild()
{
    m_visible.clear();
    m_visible.reserve(m_catalog.entries.size());
    for (int i = 0; i < m_catalog.entries.size(); ++i) {
        if (m_categoryFilter.isEmpty()
            || m_catalog.entries.at(i).category == m_categoryFilter) {
            m_visible.append(i);
        }
    }
}

QStringList CatalogModel::categoryIds() const
{
    QStringList ids;
    for (const catalog::Category &category : m_catalog.categories)
        ids.append(category.id);
    return ids;
}

QString CatalogModel::categoryName(const QString &categoryId) const
{
    if (const catalog::Category *category = m_catalog.category(categoryId))
        return category->displayName(m_uiLanguage);
    return categoryId;
}

QStringList CatalogModel::languages() const
{
    QStringList codes;
    for (const catalog::Entry &entry : m_catalog.entries) {
        if (!entry.lang.isEmpty() && !codes.contains(entry.lang))
            codes.append(entry.lang);
    }
    return codes;
}

QVariantMap CatalogModel::entryAt(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_visible.size())
        return map;

    const catalog::Entry &entry = m_catalog.entries.at(m_visible.at(row));
    map.insert(QStringLiteral("url"), entry.url);
    map.insert(QStringLiteral("title"), entry.title);
    map.insert(QStringLiteral("lang"), entry.lang);
    map.insert(QStringLiteral("category"), entry.category);
    map.insert(QStringLiteral("siteUrl"), entry.siteUrl);
    return map;
}

} // namespace model

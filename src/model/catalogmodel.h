#ifndef MODEL_CATALOGMODEL_H
#define MODEL_CATALOGMODEL_H

#include <QAbstractListModel>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include "catalog/catalog.h"

namespace model {

/*!
 * \brief The curated starter list, with a subscribed flag per entry.
 *
 * The user picks from it; subscribing to everything is not the default (section 10). The
 * model is a read-only view — acting on a row goes through the controller, which owns the
 * store.
 */
class CatalogModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter
                   NOTIFY categoryFilterChanged)
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)

public:
    enum Roles
    {
        UrlRole = Qt::UserRole + 1,
        TitleRole,
        LangRole,
        CategoryRole,
        CategoryNameRole,   //!< localised for the current UI language
        SiteUrlRole,
        DescriptionRole,
        SubscribedRole,
    };

    explicit CatalogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int totalCount() const { return m_catalog.entries.size(); }

    void setCatalog(const catalog::Catalog &catalog);

    /*! Which entries already have a subscription, matched by URL. */
    void setSubscribedUrls(const QSet<QString> &urls);

    /*! Empty shows everything. */
    QString categoryFilter() const { return m_categoryFilter; }
    void setCategoryFilter(const QString &categoryId);

    QString uiLanguage() const { return m_uiLanguage; }
    void setUiLanguage(const QString &lang);

    /*! Category ids in catalogue order, for a filter control. */
    Q_INVOKABLE QStringList categoryIds() const;
    Q_INVOKABLE QString categoryName(const QString &categoryId) const;

    /*! Everything needed to subscribe to a row, for the controller. */
    Q_INVOKABLE QVariantMap entryAt(int row) const;

signals:
    void countChanged();
    void categoryFilterChanged();
    void uiLanguageChanged();

private:
    void rebuild();

    catalog::Catalog m_catalog;
    QVector<int> m_visible;      //!< indices into m_catalog.entries
    QSet<QString> m_subscribed;
    QString m_categoryFilter;
    QString m_uiLanguage;
};

} // namespace model

#endif // MODEL_CATALOGMODEL_H

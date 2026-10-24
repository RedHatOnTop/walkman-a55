#ifndef ROWMODEL_H
#define ROWMODEL_H

#include <QtCore/QAbstractListModel>
#include <QtCore/QVariantMap>

// A list model whose rows are maps; the role names are the rows' keys.
class RowStore : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit RowStore(const QStringList &roles = {}, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return m_roles; }
    void replace(const QVector<QVariantMap> &rows);
    void setRow(int index, const QVariantMap &row);
    QVariantMap row(int index) const;
    const QVector<QVariantMap> &rows() const { return m_rows; }
    int size() const { return m_rows.size(); }

signals:
    void countChanged();

protected:
    QVector<QVariantMap> m_rows;

private:
    void addRoles(const QVariantMap &row);
    QHash<int, QByteArray> m_roles;
};

// The lists the stock ViewModels handed to option menus and the library
// icon grid, which read them through invokables (count(), level(), ...).
class RowModel : public RowStore
{
    Q_OBJECT
public:
    using RowStore::RowStore;
    Q_INVOKABLE int count() const { return m_rows.size(); }
    Q_INVOKABLE QVariantMap get(int index) const { return row(index); }
    Q_INVOKABLE int level(int index) const { return row(index).value("level", 1).toInt(); }
    Q_INVOKABLE QString listData(int index) const { return row(index).value("text").toString(); }
    Q_INVOKABLE int parentIndex(int index) const { return row(index).value("parent", -1).toInt(); }
    Q_INVOKABLE int listIndex(int index) const { return row(index).value("index", index).toInt(); }
    Q_INVOKABLE int type(int index) const { return row(index).value("type", 0).toInt(); }
    Q_INVOKABLE QString coverIcon(int index) const { return row(index).value("icon").toString(); }
    Q_INVOKABLE void move(int from, int to);
};

// The lists dialogs and context menus read like a QML ListModel (count is a property).
class ListRows : public RowStore
{
    Q_OBJECT
    Q_PROPERTY(int count READ size NOTIFY countChanged)
public:
    using RowStore::RowStore;
    Q_INVOKABLE QVariantMap get(int index) const { return row(index); }
};

#endif

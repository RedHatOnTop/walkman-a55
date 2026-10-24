#include "rowmodel.h"

RowStore::RowStore(const QStringList &roles, QObject *parent) : QAbstractListModel(parent)
{
    for (const QString &role : roles)
        m_roles.insert(Qt::UserRole + m_roles.size() + 1, role.toUtf8());
}

int RowStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant RowStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    return m_rows.at(index.row()).value(QString::fromUtf8(m_roles.value(role)));
}

void RowStore::addRoles(const QVariantMap &row)
{
    for (auto it = row.begin(); it != row.end(); ++it) {
        const QByteArray name = it.key().toUtf8();
        if (!m_roles.values().contains(name))
            m_roles.insert(Qt::UserRole + m_roles.size() + 1, name);
    }
}

void RowStore::replace(const QVector<QVariantMap> &rows)
{
    beginResetModel();
    // Role names must be fixed before a view sees the model; rows may bring
    // keys the constructor did not list.
    for (const QVariantMap &row : rows)
        addRoles(row);
    const bool sizeChanged = rows.size() != m_rows.size();
    m_rows = rows;
    endResetModel();
    if (sizeChanged)
        emit countChanged();
}

void RowStore::setRow(int index, const QVariantMap &row)
{
    if (index < 0 || index >= m_rows.size())
        return;
    m_rows[index] = row;
    emit dataChanged(this->index(index), this->index(index));
}

QVariantMap RowStore::row(int index) const
{
    return index >= 0 && index < m_rows.size() ? m_rows.at(index) : QVariantMap();
}

void RowModel::move(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_rows.size() || to >= m_rows.size() || from == to)
        return;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_rows.move(from, to);
    endMoveRows();
}

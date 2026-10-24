#include "backkey.h"

#include <QtQuick/QQuickItem>

#include <algorithm>

void BackKey::add(QObject *bar)
{
    if (auto *item = qobject_cast<QQuickItem *>(bar))
        m_bars.append(item);
}

void BackKey::remove(QObject *bar)
{
    m_bars.erase(std::remove_if(m_bars.begin(), m_bars.end(), [bar](const QPointer<QQuickItem> &item) {
        return !item || item.data() == bar;
    }), m_bars.end());
}

// Paint order, last painted first: children above their parent, higher z
// and later siblings above earlier ones.
static void paintOrder(QQuickItem *item, QVector<QQuickItem *> &order)
{
    if (!item->isVisible())
        return;
    order.append(item);
    QList<QQuickItem *> children = item->childItems();
    std::stable_sort(children.begin(), children.end(),
                     [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
    for (QQuickItem *child : children)
        paintOrder(child, order);
}

QQuickItem *BackKey::topmost() const
{
    if (!m_root)
        return nullptr;
    QVector<QQuickItem *> order;
    paintOrder(m_root, order);
    for (int i = order.size() - 1; i >= 0; --i) {
        for (const QPointer<QQuickItem> &bar : m_bars) {
            if (bar.data() == order.at(i))
                return bar;
        }
    }
    return nullptr;
}

bool BackKey::press()
{
    QQuickItem *bar = topmost();
    if (!bar)
        return false;
    const QVariantMap properties = bar->property("properties").toMap();
    const QVariantMap back = properties.value("BackButton").toMap();
    if (!back.value("Enabled", true).toBool() || !properties.value("Enabled", true).toBool())
        return bar->property("placement").toString() != "top";
    QMetaObject::invokeMethod(bar, "back");
    return true;
}

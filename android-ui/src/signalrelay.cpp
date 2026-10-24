#include "signalrelay.h"

#include <QtCore/QSet>
#include <QtQuick/QQuickItem>

SignalRelay::SignalRelay(QObject *source, Handler handler, QObject *parent)
    : QObject(parent), m_handler(std::move(handler))
{
    const QMetaObject *meta = source->metaObject();
    QSet<int> notifiers;
    for (int i = 0; i < meta->propertyCount(); ++i) {
        if (meta->property(i).hasNotifySignal())
            notifiers.insert(meta->property(i).notifySignalIndex());
    }
    const int first = QQuickItem::staticMetaObject.methodCount();
    const int slotBase = QObject::staticMetaObject.methodCount();
    for (int i = first; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() != QMetaMethod::Signal || notifiers.contains(i))
            continue;
        if (QMetaObject::connect(source, i, this, slotBase + m_signals.size(), Qt::DirectConnection))
            m_signals.append(method);
    }
}

int SignalRelay::qt_metacall(QMetaObject::Call call, int id, void **arguments)
{
    id = QObject::qt_metacall(call, id, arguments);
    if (id < 0 || call != QMetaObject::InvokeMetaMethod)
        return id;
    if (id < m_signals.size()) {
        const QMetaMethod &method = m_signals.at(id);
        QVariantList args;
        for (int i = 0; i < method.parameterCount(); ++i) {
            const int type = method.parameterType(i);
            args.append(type == QMetaType::QVariant ? *static_cast<QVariant *>(arguments[i + 1])
                                                    : QVariant(type, arguments[i + 1]));
        }
        m_handler(method.name(), args);
    }
    return id - m_signals.size();
}

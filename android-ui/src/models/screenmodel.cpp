#include "screenmodel.h"

#include <QtCore/QDebug>

#include "../appcontext.h"
#include "../screencontroller.h"
#include "../signalrelay.h"

ScreenModel::ScreenModel(const QString &id, AppContext *app, QObject *parent)
    : QObject(parent), m_id(id), m_app(app)
{
}

void ScreenModel::attach(QQuickItem *view)
{
    for (const QPointer<SignalRelay> &relay : m_relays)
        delete relay.data();
    m_relays.clear();
    m_view = view;
    for (auto it = m_values.begin(); it != m_values.end(); ++it)
        push(it.key(), it.value());
    m_relays.append(new SignalRelay(view, [this](const QByteArray &name, const QVariantList &args) {
        handle(name, args);
    }, this));
    attached();
}

void ScreenModel::refreshView()
{
    for (auto it = m_values.begin(); it != m_values.end(); ++it)
        push(it.key(), it.value());
}

void ScreenModel::relayChild(const QString &objectName)
{
    if (!m_view)
        return;
    QObject *child = m_view->findChild<QObject *>(objectName);
    if (!child)
        return;
    const QString prefix = objectName + '.';
    m_relays.append(new SignalRelay(child, [this, prefix](const QByteArray &name, const QVariantList &args) {
        handle((prefix + QString::fromUtf8(name)).toUtf8(), args);
    }, this));
}

void ScreenModel::push(const QString &name, const QVariant &value)
{
    if (!m_view)
        return;
    // Assigning directly, as the stock Binding-based binder effectively did
    // on Qt 5.3; it replaces whatever binding the view declared.
    if (m_view->metaObject()->indexOfProperty(name.toUtf8().constData()) < 0) {
        qWarning().noquote() << m_id << "has no property" << name;
        return;
    }
    m_view->setProperty(name.toUtf8().constData(), value);
}

void ScreenModel::set(const QString &name, const QVariant &value)
{
    const auto it = m_values.constFind(name);
    if (it != m_values.constEnd() && it.value() == value && value.userType() != QMetaType::QObjectStar)
        return;
    m_values.insert(name, value);
    push(name, value);
}

void ScreenModel::setValues(const QVariantMap &values)
{
    for (auto it = values.begin(); it != values.end(); ++it)
        set(it.key(), it.value());
}

QVariant ScreenModel::call(const char *function, const QVariantList &args)
{
    if (!m_view)
        return {};
    QVariant result;
    QVariant a[4];
    for (int i = 0; i < args.size() && i < 4; ++i)
        a[i] = args.at(i);
    const int count = args.size();
    bool ok = false;
    switch (count) {
    case 0: ok = QMetaObject::invokeMethod(m_view, function, Q_RETURN_ARG(QVariant, result)); break;
    case 1: ok = QMetaObject::invokeMethod(m_view, function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a[0])); break;
    case 2: ok = QMetaObject::invokeMethod(m_view, function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a[0]),
                                           Q_ARG(QVariant, a[1])); break;
    default: ok = QMetaObject::invokeMethod(m_view, function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a[0]),
                                            Q_ARG(QVariant, a[1]), Q_ARG(QVariant, a[2])); break;
    }
    if (!ok)
        qWarning().noquote() << m_id << "cannot call" << function;
    return result;
}

bool ScreenModel::navigate(const QString &action, int condition)
{
    return m_app->controller()->doAction(action, condition);
}

void ScreenModel::handle(const QByteArray &signal, const QVariantList &)
{
    QString action = QString::fromUtf8(signal);
    if (action.isEmpty() || action.contains('.'))
        return;
    action[0] = action.at(0).toUpper();
    if (m_app->screens()->hasAction(m_id, action))
        navigate(action);
}

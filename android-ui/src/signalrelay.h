#ifndef SIGNALRELAY_H
#define SIGNALRELAY_H

#include <QtCore/QMetaMethod>
#include <QtCore/QObject>
#include <QtCore/QVariantList>

#include <functional>

// Forwards every signal a QML view declares to one handler, by name. The
// stock C++ connected its ViewModel slots to view signals by name string
// ("2touchListAt(int)"); the replacement ViewModels dispatch on the name.
// Property-change notifiers are skipped: they are not view events.
class SignalRelay : public QObject
{
public:
    using Handler = std::function<void(const QByteArray &name, const QVariantList &args)>;
    SignalRelay(QObject *source, Handler handler, QObject *parent = nullptr);
    int qt_metacall(QMetaObject::Call call, int id, void **arguments) override;

private:
    Handler m_handler;
    QVector<QMetaMethod> m_signals;
};

#endif

#ifndef SCREENMODEL_H
#define SCREENMODEL_H

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariantMap>
#include <QtQuick/QQuickItem>

#include <functional>

class AppContext;
class SignalRelay;

// One per Sony view (screen, window or popup): the values the view's root
// properties take, and what its signals do. Stands in for the app's
// StandardViewModel subclasses, which bound to their views by name only.
class ScreenModel : public QObject
{
    Q_OBJECT
public:
    ScreenModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    QString id() const { return m_id; }
    QVariantMap values() const { return m_values; }
    QQuickItem *view() const { return m_view; }

    struct MenuItem {
        QString text;
        std::function<void()> run;
    };

    void attach(QQuickItem *view);
    // Assigns every value again. A stock view clears its own lists when it
    // becomes unavailable and expects its ViewModel to refill them.
    void refreshView();
    // The option menu the window's Option button opens on this screen.
    virtual QVector<MenuItem> menu() { return {}; }
    // A transition to this screen is beginning.
    virtual void entering() {}
    virtual void activate() {}
    virtual void deactivate() {}
    // A view signal, by name. The default maps goToX/GoToX signals and any
    // signal named like one of the screen's transition actions onto it.
    virtual void handle(const QByteArray &signal, const QVariantList &args);

protected:
    virtual void attached() {}
    void set(const QString &name, const QVariant &value);
    void setValues(const QVariantMap &values);
    QVariant value(const QString &name) const { return m_values.value(name); }
    QVariant call(const char *function, const QVariantList &args = {});
    bool navigate(const QString &action, int condition = -1);
    void relayChild(const QString &objectName);
    AppContext *app() const { return m_app; }

private:
    void push(const QString &name, const QVariant &value);
    const QString m_id;
    AppContext *const m_app;
    QVariantMap m_values;
    QPointer<QQuickItem> m_view;
    QList<QPointer<SignalRelay>> m_relays;
};

#endif

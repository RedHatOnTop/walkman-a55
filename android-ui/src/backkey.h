#ifndef BACKKEY_H
#define BACKKEY_H

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVector>
#include <QtQuick/QQuickItem>

// Android's back key in place of the Walkman's Back button. Every
// NavigationBar registers here; the key acts as a tap on the Back button of
// the topmost bar that is showing (a dialog's, a popup's, else the window's),
// so each context closes the way the stock button closed it.
class BackKey : public QObject
{
    Q_OBJECT
public:
    explicit BackKey(QQuickItem *root, QObject *parent = nullptr) : QObject(parent), m_root(root) {}
    void setRoot(QQuickItem *root) { m_root = root; }
    Q_INVOKABLE void add(QObject *bar);
    Q_INVOKABLE void remove(QObject *bar);
    // True when a bar took the key; false means nothing is left to go back
    // to and the app should move to the background.
    bool press();

signals:
    void unhandled();

private:
    QQuickItem *topmost() const;
    QPointer<QQuickItem> m_root;
    QVector<QPointer<QQuickItem>> m_bars;
};

#endif

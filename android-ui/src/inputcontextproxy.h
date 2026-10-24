#ifndef INPUTCONTEXTPROXY_H
#define INPUTCONTEXTPROXY_H

#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>
#include <QtQuick/QQuickItem>

// Stands in for Sony's ext.lib.inputcontextproxy, which loaded the Walkman's
// own keyboard into components/ImePanelPopup.qml. Android's keyboard edits
// the field instead, so the panel the popup positions is an empty item.
class InputContextProxyHelper : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE bool openInputContextPlugin(const QString &, const QString &, bool) { return true; }
    Q_INVOKABLE QQuickItem *createInputPanel(QQuickItem *parent)
    {
        auto *panel = new QQuickItem(parent);
        panel->setParentItem(parent);
        return panel;
    }
    Q_INVOKABLE void showInputPanel() { QGuiApplication::inputMethod()->show(); }
};

#endif

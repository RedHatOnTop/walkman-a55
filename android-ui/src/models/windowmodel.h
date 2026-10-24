#ifndef WINDOWMODEL_H
#define WINDOWMODEL_H

#include "screenmodel.h"

class RowModel;
class RowStore;

// The chrome around every screen of one window: header, transport, the
// navigation bar (its buttons now in the header) and its option menu.
class WindowModel : public ScreenModel
{
    Q_OBJECT
public:
    WindowModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    QVariantMap navigation() const;
    void handle(const QByteArray &signal, const QVariantList &args) override;

protected:
    void attached() override;

private:
    void refresh();
    void refreshStatus();
    QVariantMap header() const;
    QVariantMap transport() const;
    QVariantMap button(const QString &name, const QString &image, bool enabled) const;
    RowModel *m_options;
    RowStore *m_status;
    QVector<MenuItem> m_menu;
};

#endif

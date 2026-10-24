#ifndef LIBRARYTOPMODEL_H
#define LIBRARYTOPMODEL_H

#include "screenmodel.h"

class RowModel;

// sid_0901_library/Contents.qml: the category icon pages. An icon's id is
// the "selectedIcon" condition in ScreenSettings.json.
class LibraryTopModel : public ScreenModel
{
    Q_OBJECT
public:
    LibraryTopModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    void handle(const QByteArray &signal, const QVariantList &args) override;
    QVector<MenuItem> menu() override;
    void activate() override;

private:
    void load();
    void save();
    RowModel *m_icons;
    QStringList m_hidden;
    QStringList m_order;
};

// sid_0902_library/LibraryIconSelect.qml: which categories the icon pages
// show. Every visit starts from the saved choice.
class LibraryIconSelectModel : public ScreenModel
{
    Q_OBJECT
public:
    LibraryIconSelectModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    void handle(const QByteArray &signal, const QVariantList &args) override;
    void entering() override;

private:
    void reset();
    RowModel *m_rows;
};

#endif

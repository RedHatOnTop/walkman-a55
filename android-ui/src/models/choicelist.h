#ifndef CHOICELIST_H
#define CHOICELIST_H

#include "../rowmodel.h"

// A radio list as the stock radio views and list dialogs read it: one row per
// choice (label, item_id, isChecked) and the id of the checked row, which the
// pull-down list reads as `listData.checkedItemId` when it opens.
class ChoiceList : public ListRows
{
    Q_OBJECT
    Q_PROPERTY(int checkedItemId READ checkedItemId NOTIFY checkedChanged)
public:
    explicit ChoiceList(QObject *parent = nullptr);
    ChoiceList(const QStringList &labels, int checked, QObject *parent = nullptr);
    // Row i has item_id i.
    void setChoices(const QStringList &labels, int checked);
    void setChecked(int id);
    int checkedItemId() const { return m_checked; }
    QString checkedLabel() const { return row(m_checked).value("label").toString(); }

signals:
    void checkedChanged();

private:
    int m_checked = -1;
};

#endif

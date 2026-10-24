#include "choicelist.h"

ChoiceList::ChoiceList(QObject *parent) : ListRows({"label", "item_id", "isChecked"}, parent)
{
}

ChoiceList::ChoiceList(const QStringList &labels, int checked, QObject *parent) : ChoiceList(parent)
{
    setChoices(labels, checked);
}

void ChoiceList::setChoices(const QStringList &labels, int checked)
{
    QVector<QVariantMap> rows;
    for (int i = 0; i < labels.size(); ++i)
        rows.append({{"label", labels.at(i)}, {"item_id", i}, {"isChecked", i == checked}});
    replace(rows);
    if (m_checked != checked) {
        m_checked = checked;
        emit checkedChanged();
    }
}

void ChoiceList::setChecked(int id)
{
    if (id == m_checked)
        return;
    m_checked = id;
    // Rows change in place so a list that is open keeps its scroll position.
    for (int i = 0; i < size(); ++i) {
        QVariantMap changed = row(i);
        if (changed.value("isChecked").toBool() != (i == id)) {
            changed.insert("isChecked", i == id);
            setRow(i, changed);
        }
    }
    emit checkedChanged();
}

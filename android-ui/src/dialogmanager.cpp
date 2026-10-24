#include "dialogmanager.h"

#include <QtCore/QHash>

static QString dialogFor(const QString &messageId)
{
    static const QHash<QString, QString> types{
        {"kMid0044", "OkCancelDialog"},
        {"kMid0045", "TimeOutDialog"},
        {"kMid9200", "YesNoDialog"},
        {"kMid9211", "OptionMenu"},
        {"kMid9212", "ContextMenu"},
        {"kMid9213", "RadioButtonListOkDialog"},
        {"kMid9214", "RadioButtonListDialog"},
        {"kMid9215", "ListDialog"},
    };
    return types.value(messageId, "OkDialog");
}

QString DialogManager::open(const QString &message_id, const QString &top_text, const QString &bottom_text,
                            const QVariant &model)
{
    if (isOpen()) {
        emit openFailed(dialogFor(message_id));
        return "NoneDialog";
    }
    m_current = dialogFor(message_id);
    m_priority = 1;
    emit dialogChanged();
    // TimeOutDialog's Timer runs whatever its interval: Qt 5.15 fires a 0 ms
    // one at once, so the notice would close as it opens.
    const int interval = m_current == "TimeOutDialog" ? 2000 : 0;
    emit dialogOpen(m_current, top_text, bottom_text, interval, model, m_navigation ? m_navigation() : QVariantMap());
    return m_current;
}

void DialogManager::message(const QString &text, const QString &title)
{
    if (isOpen())
        forceClose();
    m_current = "OkDialog";
    m_priority = 1;
    emit dialogChanged();
    emit dialogOpen(m_current, title, text, 0, QVariant(), m_navigation ? m_navigation() : QVariantMap());
}

// The list dialogs emit accepted right after selected; closing here would
// drop that second signal.
void DialogManager::selectedSlot(const QVariant &item_id)
{
    emit selected(item_id);
}

void DialogManager::forceClose()
{
    if (!isOpen())
        return;
    emit dialogForceClose();
    initialize();
}

void DialogManager::initialize()
{
    if (m_current == "NoneDialog")
        return;
    m_current = "NoneDialog";
    m_priority = 0;
    emit dialogChanged();
    emit dialogInitialize();
}

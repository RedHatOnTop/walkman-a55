#ifndef DIALOGMANAGER_H
#define DIALOGMANAGER_H

#include <QtCore/QObject>
#include <QtCore/QVariantMap>

#include <functional>

// Stands in for the app's DialogManager. framework/Dialog.qml draws the
// dialogs; views open them through DialogWrapper with a message id, which
// selects the dialog type (the ids are the ones the stock views pass).
class DialogManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int priority READ priority NOTIFY dialogChanged)
    Q_PROPERTY(QString current READ current NOTIFY dialogChanged)
public:
    explicit DialogManager(QObject *parent = nullptr) : QObject(parent)
    {
        connect(this, &DialogManager::reqDialogClose, this, &DialogManager::forceClose);
    }
    void setNavigation(std::function<QVariantMap()> navigation) { m_navigation = std::move(navigation); }
    int priority() const { return m_priority; }
    QString current() const { return m_current; }
    bool isOpen() const { return m_current != "NoneDialog"; }
    void message(const QString &text, const QString &title = {});

    Q_INVOKABLE QString open(const QString &message_id, const QString &top_text, const QString &bottom_text,
                             const QVariant &model);
    Q_INVOKABLE void forceClose();
    Q_INVOKABLE void setProgress(int value) { emit dialogSetProgress(value); }
    Q_INVOKABLE void setProgressDialogCancelEnabled(bool enabled) { emit dialogSetProgressDialogCancelEnabled(enabled); }
    Q_INVOKABLE void optionMenuBack() { emit menuBack(); }
    Q_INVOKABLE QString getDialogName() const { return m_current; }

public slots:
    void acceptedSlot() { emit accepted(); initialize(); }
    void rejectedSlot() { emit rejected(); initialize(); }
    void confirmedSlot() { emit confirmed(); initialize(); }
    void closedSlot() { emit closed(); initialize(); }
    void progressCompletedSlot() { emit progressCompleted(); initialize(); }
    void progressCanceledSlot() { emit progressCanceled(); initialize(); }
    void selectedSlot(const QVariant &item_id);
    void turnOffSlot() { emit turnOff(); initialize(); }
    void menuBackSlot() { emit menuBack(); }

signals:
    void dialogOpen(const QString &dialog, const QString &top_text, const QString &bottom_text, int interval,
                    const QVariant &model, const QVariantMap &properties);
    void dialogSetProgress(int value);
    void dialogSetProgressDialogCancelEnabled(bool cancel_button_enabled);
    void dialogForceClose();
    void dialogInitialize();
    void accepted();
    void rejected();
    void confirmed();
    void closed();
    void progressCompleted();
    void progressCanceled();
    void selected(const QVariant &item_id);
    void turnOff();
    void menuBack();
    void openFailed(const QString &dialog);
    void reqDialogClose();
    void dialogChanged();

private:
    void initialize();
    std::function<QVariantMap()> m_navigation;
    QString m_current = "NoneDialog";
    int m_priority = 0;
};

#endif

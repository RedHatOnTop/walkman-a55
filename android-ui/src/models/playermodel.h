#ifndef PLAYERMODEL_H
#define PLAYERMODEL_H

#include "screenmodel.h"

// The four playback screens (standard, spectrum, analog and digital level
// meters). They share MusicPlayerViewModelBase's values and option menu.
class PlayerModel : public ScreenModel
{
    Q_OBJECT
public:
    PlayerModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    void handle(const QByteArray &signal, const QVariantList &args) override;
    QVector<MenuItem> menu() override;

private:
    void refresh();
};

#endif

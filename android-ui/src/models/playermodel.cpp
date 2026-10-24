#include "playermodel.h"

#include "../../library/musicdb.h"
#include "../../playback/player.h"
#include "../appcontext.h"
#include "../screencontroller.h"

PlayerModel::PlayerModel(const QString &id, AppContext *app, QObject *parent) : ScreenModel(id, app, parent)
{
    PlayerController *player = app->player();
    connect(player, &PlayerController::trackChanged, this, &PlayerModel::refresh);
    connect(player, &PlayerController::queueChanged, this, &PlayerModel::refresh);
    connect(player, &PlayerController::positionChanged, this, [this] {
        set("currently_playing_time", qint64(this->app()->player()->position()));
    });
    connect(player, &PlayerController::seekRejected, this, [this] { call("onNotifySeekError"); });
    refresh();
}

void PlayerModel::refresh()
{
    PlayerController *player = app()->player();
    QVariantMap meta = player->metadata();
    if (!player->hasTrack())
        meta.insert("play_title", QString());
    setValues({{"meta_data", meta},
               {"total_playback_time", qint64(player->duration())},
               {"currently_playing_time", qint64(player->position())},
               {"currentry_playtrack_num", player->hasTrack() ? player->queueIndex() + 1 : 0},
               {"total_playtrack_num", player->queueCount()},
               {"entry_id", player->hasTrack() ? QString::number(player->track()) : QString()},
               {"update_db_counter", app()->library()->counter()},
               {"is_activated", player->hasTrack()}});
}

void PlayerModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "released")
        app()->player()->seek(args.value(0).toLongLong());
    else
        ScreenModel::handle(signal, args);
}

QVector<ScreenModel::MenuItem> PlayerModel::menu()
{
    const char *context = "dmpapp::MusicPlayerViewModelBase";
    return {
        {sonyText(context, "010020"), [this] { navigate("SettingTop"); }},
        {sonyText(context, "020171"), [this] { app()->message(sonyText("dmpapp::MusicPlayerViewModelBase", "020171")); }},
        {sonyText(context, "020172"), [this] { app()->message(sonyText("dmpapp::MusicPlayerViewModelBase", "020172")); }},
        {sonyText(context, "020246"), [this] { app()->openPopup("NowPlayingContentDetailedInfoPopup"); }},
        {sonyText(context, "020145"), [this] { app()->openPopup("LyricDispPopup"); }},
        {sonyText(context, "010057"), [this] { app()->openPopup("MusicPlayerHelpGuide"); }},
    };
}

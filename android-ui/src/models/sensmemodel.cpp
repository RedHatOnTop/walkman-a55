#include "sensmemodel.h"

#include <QtCore/QRandomGenerator>
#include <QtCore/QTime>

#include <algorithm>

#include "../../library/musicdb.h"
#include "../../library/sensme.h"
#include "../../playback/player.h"
#include "../appcontext.h"
#include "../rowmodel.h"

namespace {

struct Slot {
    const char *image;
    int channel;
};

// The stock strip, in HgrmMediaPlayerApp's thumbnail table order; the last
// slot is Shuffle All.
const Slot strip[SensMePlayerModel::slotCount] = {
    {"active", SensMe::Active},   {"emotional", SensMe::Emotional}, {"lounge", SensMe::Lounge},
    {"dance", SensMe::Dance},     {"extreme", SensMe::Extreme},     {"upbeat", SensMe::Upbeat},
    {"relax", SensMe::Relax},     {"mellow", SensMe::Mellow},       {"morning", SensMe::Morning},
    {"daytime", SensMe::Daytime}, {"evening", SensMe::Evening},     {"night", SensMe::Night},
    {"midnight", SensMe::Midnight}, {"shuffle_all_songs", -1},
};
const int shuffleAll = SensMePlayerModel::slotCount - 1;

// Sony's timetable for the time-of-day channels.
int timeSlot(int hour)
{
    const int channel = hour < 5 ? SensMe::Midnight : hour < 10 ? SensMe::Morning : hour < 16 ? SensMe::Daytime
        : hour < 19 ? SensMe::Evening : SensMe::Night;
    for (int slot = 0; slot < shuffleAll; ++slot) {
        if (strip[slot].channel == channel)
            return slot;
    }
    return shuffleAll;
}

}

SensMePlayerModel::SensMePlayerModel(const QString &id, AppContext *app, QObject *parent)
    : ScreenModel(id, app, parent), m_list(new RowModel({"kChannelIndexRole", "kChannelIconPathRole"}, this))
{
    PlayerController *player = app->player();
    connect(player, &PlayerController::trackChanged, this, &SensMePlayerModel::refresh);
    connect(player, &PlayerController::queueChanged, this, &SensMePlayerModel::refresh);
    connect(player, &PlayerController::positionChanged, this, [this] {
        set("currentPlayTime", qint64(this->app()->player()->position()));
    });
    connect(player, &PlayerController::seekRejected, this, [this] { call("onNotifySeekError"); });
    connect(app->library(), &MusicDb::changed, this, &SensMePlayerModel::rebuild);
    setValues({{"sensMeChannelList", QVariant::fromValue<QObject *>(m_list)}, {"channelIconIndex", 0}});
    rebuild();
    refresh();
}

void SensMePlayerModel::rebuild()
{
    const MusicDb *db = app()->library();
    for (QVector<int> &members : m_members)
        members.clear();
    m_unanalyzed = 0;
    for (int id = 0; id < db->count(); ++id) {
        m_members[shuffleAll].append(id);
        const QByteArray &features = db->track(id).tags.sensme;
        if (features.isEmpty()) {
            ++m_unanalyzed;
            continue;
        }
        const int mask = SensMe::channels(features);
        for (int slot = 0; slot < shuffleAll && mask > 0; ++slot) {
            if (mask & (1 << strip[slot].channel))
                m_members[slot].append(id);
        }
    }
    QVector<int> shown;
    QVariantList disabled;
    QVector<QVariantMap> rows;
    for (int slot = 0; slot < slotCount; ++slot) {
        if (m_members[slot].isEmpty()) {
            disabled.append(slot);
            continue;
        }
        shown.append(slot);
        rows.append({{"kChannelIndexRole", slot},
                     {"kChannelIconPathRole",
                      QString("qrc:/assets/images/sensme_player/ic_sensme_channel_thumbnail_%1.png").arg(strip[slot].image)}});
    }
    // A reset model puts the strip back on its first channel.
    if (shown != m_shown) {
        m_shown = shown;
        m_list->replace(rows);
    }
    setValues({{"channelCount", m_shown.size()}, {"disabledChannelIndexList", disabled}});
    showCurrent();
}

void SensMePlayerModel::refresh()
{
    PlayerController *player = app()->player();
    QVariantMap meta = player->metadata();
    if (!player->hasTrack())
        meta.insert("play_title", QString());
    setValues({{"meta_data", meta},
               {"totalPlaybackTime", qint64(player->duration())},
               {"currentPlayTime", qint64(player->position())},
               {"currentPlayTrackNum", player->hasTrack() ? player->queueIndex() + 1 : 0},
               {"totalPlayTrackNum", player->queueCount()}});
    showCurrent();
}

void SensMePlayerModel::showCurrent()
{
    const int index = m_shown.indexOf(app()->player()->channel());
    if (index < 0)
        return;
    set("channelIconIndex", index);
    call("onUpdateChannelIconIndex");
}

void SensMePlayerModel::play(int slot)
{
    QVector<int> tracks = m_members[slot];
    if (tracks.isEmpty())
        return;
    std::shuffle(tracks.begin(), tracks.end(), *QRandomGenerator::global());
    const qint32 highlight = app()->library()->track(tracks.first()).tags.highlightMs;
    app()->player()->playChannel(slot, tracks, qMax(0, highlight));
}

void SensMePlayerModel::entering()
{
    rebuild();
    if (m_unanalyzed > 0 && !m_told) {
        m_told = true;
        app()->message(sonyText("QObject", "020194"));
    }
    if (m_shown.contains(app()->player()->channel()))
        return;
    const int now = timeSlot(QTime::currentTime().hour());
    play(m_shown.contains(now) ? now : shuffleAll);
}

void SensMePlayerModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "selectChannelAt") {
        const int slot = m_shown.value(args.value(0).toInt(), -1);
        if (slot >= 0 && slot != app()->player()->channel())
            play(slot);
    } else if (signal == "released") {
        app()->player()->seek(args.value(0).toLongLong());
    } else if (signal != "pressed" && signal != "cancelSelectChannel") {
        ScreenModel::handle(signal, args);
    }
}

QVector<ScreenModel::MenuItem> SensMePlayerModel::menu()
{
    const char *context = "dmpapp::SensMePlayerViewModel";
    PlayerController *player = app()->player();
    return {
        {sonyText(context, "010020"), [this] { navigate("SettingTop"); }},
        {sonyText(context, "020171"), [this, player] { if (player->hasTrack()) app()->addToBookmarks({player->track()}); }},
        {sonyText(context, "020172"), [this, player] { if (player->hasTrack()) app()->addToPlaylist({player->track()}); }},
    };
}

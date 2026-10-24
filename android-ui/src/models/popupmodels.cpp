#include "popupmodels.h"

#include <QtCore/QFileInfo>
#include <QtCore/QTimer>

#include "../../library/lyrics.h"
#include "../../library/musicdb.h"
#include "../../playback/platform.h"
#include "../../playback/player.h"
#include "../appcontext.h"
#include "../rowmodel.h"
#include "../screencontroller.h"
#include "browsemodel.h"
#include "windowmodel.h"

void PopupModel::attached()
{
    if (auto *window = qobject_cast<WindowModel *>(app()->model(app()->controller()->windowID()))) {
        for (const char *name : {"navigation", "navigation_bar"}) {
            if (view()->metaObject()->indexOfProperty(name) >= 0)
                set(name, window->navigation());
        }
    }
    // The framed popups (details, lyrics) sit 16 px under the stock 66 px
    // header; they keep that gap under this build's header, and end as far
    // above the screen's bottom as they ended above the stock 760 px content
    // area (patch 0005 stretches their frames).
    const QVariant top = view()->property("popup_area_position_y");
    const QVariant height = view()->property("popup_area_height");
    if (top.isValid() && height.isValid() && !value("popup_area_position_y").isValid()) {
        const qreal y = top.toReal() + app()->controller()->y() - 66;
        const qreal stockBottom = top.toReal() + height.toReal();
        const qreal bottom = view()->height() - qMax<qreal>(760 - stockBottom, 8);
        set("popup_area_position_y", y);
        set("popup_area_height", qMin(height.toReal(), bottom - y));
    }
}

void PopupModel::close()
{
    app()->controller()->ClosePopup(id());
}

void PopupModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal.startsWith("close") || signal == "backButtonClicked") {
        close();
    } else if (signal == "libraryTopButtonClicked") {
        close();
        app()->controller()->BackToTop();
    } else {
        ScreenModel::handle(signal, args);
    }
}

void DetailsModel::attached()
{
    PopupModel::attached();
    const int id = this->id().startsWith("NowPlaying") ? app()->player()->track() : app()->detailsTrack();
    const MusicDb *db = app()->library();
    if (!db->valid(id))
        return;
    const LibraryTrack &track = db->track(id);
    const TrackTags &tags = track.tags;
    const auto text = [](const char *message) { return sonyText("QObject", message); };
    const QString unknown = text("000006");
    const auto value = [&unknown](const QString &v) { return v.isEmpty() ? unknown : v; };
    QVector<QVariantMap> rows{
        {{"item_title", text("020007")}, {"item_body", track.title()}},
        {{"item_title", text("020008")}, {"item_body", value(tags.artist)}},
        {{"item_title", text("020009")}, {"item_body", value(tags.album)}},
        {{"item_title", text("020010")}, {"item_body", value(tags.genre)}},
        {{"item_title", text("020011")}, {"item_body", tags.year > 0 ? QString::number(tags.year) : unknown}},
        {{"item_title", text("020012")}, {"item_body", value(tags.composer)}},
        {{"item_title", text("020013")}, {"item_body", durationText(tags.durationMs)}},
        {{"item_title", text("020015")}, {"item_body", QFileInfo(track.path).fileName()}},
        {{"item_title", text("020016")}, {"item_body", value(tags.codec)}},
    };
    if (tags.bitrate > 0)
        rows.append({{"item_title", text("020017")}, {"item_body", QString("%1 kbps").arg(tags.bitrate)}});
    if (tags.sampleRate > 0)
        rows.append({{"item_title", text("020018")},
                     {"item_body", QString::number(tags.sampleRate / 1000.0, 'f', tags.sampleRate % 1000 ? 1 : 0) + " kHz"}});
    if (tags.bitsPerSample > 0)
        rows.append({{"item_title", text("020019")}, {"item_body", QString("%1 bit").arg(tags.bitsPerSample)}});
    rows.append({{"item_title", text("000072")}, {"item_body", db->storages().value(track.storage).name}});
    auto *list = new ListRows({"item_title", "item_body"}, this);
    list->replace(rows);
    set("contentDetailedInfoList", QVariant::fromValue<QObject *>(list));
}

LibraryHelpModel::LibraryHelpModel(const QString &id, AppContext *app, QObject *parent)
    : PopupModel(id, app, parent)
{
    set("library_helpguide_info", QVariantMap{{"helpguide_file", "qrc:/ic_music_library_helpguide_no_funcbar.png"},
                                              {"helpguide_available_hp_amp", false},
                                              {"helpguide_available_function_icon", false}});
}

void TextEntryModel::prepare(const QString &title, const QString &initial)
{
    // hasVolumeBar: false for the A55's header, which has no volume area.
    setValues({{"inputTitle", title}, {"initInputContent", initial}, {"inputMaxLength", 128}, {"hasVolumeBar", false}});
}

void TextEntryModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "okButtonClicked") {
        close();
        app()->textEntered(args.value(0).toString().trimmed());
    } else if (signal == "cancelButtonClicked") {
        close();
    } else {
        PopupModel::handle(signal, args);
    }
}

VolumeModel::VolumeModel(const QString &id, AppContext *app, QObject *parent)
    : PopupModel(id, app, parent), m_repeat(new QTimer(this))
{
    m_repeat->setInterval(100);
    connect(m_repeat, &QTimer::timeout, this, [this] { step(m_direction); });
    connect(app->player(), &PlayerController::settingsChanged, this, &VolumeModel::refresh);
    setValues({{"minVolume", 0}, {"maxVolume", 120}, {"avlsMaxVolume", PlayerController::avlsLimit},
               {"is_high_gain", false}, {"is_available_volume_bar", false}});
    refresh();
}

void VolumeModel::refresh()
{
    setValues({{"volume", app()->player()->volume()}, {"isAvls", app()->player()->avls()}});
}

void VolumeModel::step(int delta)
{
    app()->player()->setVolume(app()->player()->volume() + delta);
}

void VolumeModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "upButtonClicked" || signal == "downButtonClicked") {
        step(signal.startsWith("up") ? 1 : -1);
    } else if (signal == "upButtonPressAndHold" || signal == "downButtonPressAndHold") {
        m_direction = signal.startsWith("up") ? 1 : -1;
        m_repeat->start();
    } else if (signal == "upButtonReleased" || signal == "downButtonReleased") {
        m_repeat->stop();
    } else if (signal == "volumeDialChanged") {
        app()->player()->setVolume(args.value(0).toInt());
    } else {
        PopupModel::handle(signal, args);
    }
}

namespace {
// SyncLyricDisp centres a line by scrolling, so three empty rows come before
// the first line and after the last one.
constexpr int kBuffer = 3;
}

LyricsModel::LyricsModel(const QString &id, AppContext *app, QObject *parent)
    : PopupModel(id, app, parent), m_rows(new ListRows({"list_data"}, this)), m_hold(new QTimer(this))
{
    // After the user scrolls the lyrics, the highlight keeps following the
    // song but the list stays where they left it for a while.
    m_hold->setSingleShot(true);
    m_hold->setInterval(5000);
    connect(app->player(), &PlayerController::trackChanged, this, [this] {
        if (view())
            load();
    });
    connect(app->player(), &PlayerController::positionChanged, this, &LyricsModel::follow);
    // Settings > Playback Settings > Display Lyrics > Keep Screen On.
    connect(app->controller(), &ScreenController::updatePopup, this, [this] {
        const bool keep = this->app()->controller()->popups().contains(this->id())
                          && this->app()->prefs()->value("settings/keepScreenOn", false).toBool();
        if (keep != m_keepingScreenOn) {
            m_keepingScreenOn = keep;
            keepScreenOn(keep);
        }
    });
}

void LyricsModel::attached()
{
    PopupModel::attached();
    set("lyric_list", QVariant::fromValue<QObject *>(m_rows));
    load();
}

void LyricsModel::load()
{
    const PlayerController *player = app()->player();
    const MusicDb *db = app()->library();
    m_lyrics = {};
    m_line = -1;
    m_hold->stop();
    bool broken = false;
    if (player->hasTrack() && db->valid(player->track())) {
        const LibraryTrack &track = db->track(player->track());
        m_lyrics = loadLyrics(track.path, track.tags.lyrics);
        broken = m_lyrics.lines.isEmpty() && hasLyricsFile(track.path);
    }
    QVector<QVariantMap> rows;
    if (!m_lyrics.lines.isEmpty()) {
        const QVariantMap blank{{"list_data", QString()}};
        rows.fill(blank, kBuffer);
        for (const QString &line : m_lyrics.lines)
            rows.append({{"list_data", line}});
        rows.append(QVector<QVariantMap>(kBuffer, blank));
    }
    m_rows->replace(rows);
    if (rows.isEmpty()) {
        call(broken ? "lyricDispInvalid" : "lyricDispEmpty");
    } else if (m_lyrics.timed()) {
        m_line = m_lyrics.lineAt(player->position());
        set("sync_lyric_current_index", m_line + kBuffer);
        call("lyricDispScrollAnimationOff");
    } else {
        call("lyricDispScrollPositionReset");
    }
}

void LyricsModel::follow()
{
    if (!view() || !m_lyrics.timed())
        return;
    const int line = m_lyrics.lineAt(app()->player()->position());
    if (line == m_line)
        return;
    m_line = line;
    set("sync_lyric_current_index", line + kBuffer);
    call(m_hold->isActive() ? "lyricDispNotScroll" : "lyricDispScrollAnimationOn");
}

void LyricsModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "indexJump") {
        const int line = args.value(0).toInt() - kBuffer;
        if (m_lyrics.timed() && line >= 0 && line < m_lyrics.times.size()) {
            m_hold->stop();
            app()->player()->seek(m_lyrics.times.at(line));
        }
    } else if (signal == "scrolled") {
        m_hold->start();
    } else if (signal == "optionMenuButtonClicked") {
    } else {
        PopupModel::handle(signal, args);
    }
}

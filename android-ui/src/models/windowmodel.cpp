#include "windowmodel.h"

#include <QtCore/QTimer>

#include "../../playback/player.h"
#include "../appcontext.h"
#include "../rowmodel.h"
#include "../screencontroller.h"

WindowModel::WindowModel(const QString &id, AppContext *app, QObject *parent)
    : ScreenModel(id, app, parent),
      m_options(new RowModel({"text", "level", "parent", "index", "type", "icon"}, this)),
      m_status(new RowStore({"QmlIntRole", "ParamIntFrameSize", "ParamAnimationImageRole", "ParamQvariantRole"}, this))
{
    connect(app, &AppContext::windowChanged, this, &WindowModel::refresh);
    PlayerController *player = app->player();
    connect(player, &PlayerController::transportChanged, this, &WindowModel::refresh);
    connect(player, &PlayerController::trackChanged, this, &WindowModel::refresh);
    connect(player, &PlayerController::settingsChanged, this, &WindowModel::refresh);
    refresh();
}

QVariantMap WindowModel::button(const QString &name, const QString &image, bool enabled) const
{
    return {{"objectName", name}, {"Enabled", enabled}, {"Visible", true},
            {"normalImage", "qrc:/" + image + "_normal.png"},
            {"pressedImage", "qrc:/" + image + "_pressed.png"},
            {"disableImage", "qrc:/" + image + "_disable.png"}};
}

QVariantMap WindowModel::navigation() const
{
    const QString screen = app()->currentScreen();
    const bool onPlayer = app()->screens()->aliases.value("MusicPlayers").contains(screen);
    return {{"objectName", "navigationBar"}, {"Enabled", true}, {"Visible", true},
            {"BackButton", button("BackButton", "btn_navi_back", true)},
            {"NowPlayingButton", button("NowPlayingButton", "btn_audio_navi_player",
                                        !onPlayer && app()->player()->hasTrack())},
            {"FunctionTopButton", button("FunctionTopButton", "btn_audio_navi_library", screen != "MusicPlayerTop")},
            {"OptionButton", button("OptionButton", "btn_navi_option", !m_menu.isEmpty())},
            {"option", QVariant::fromValue<QObject *>(m_options)}};
}

// The A55's header: one info row (StatusBar) instead of the taller models'
// volume area. The full-width volume meter over it (StatusVolumeBar) shows
// while volume keys are held, and Android's volume keys never reach the app.
QVariantMap WindowModel::header() const
{
    return {{"objectName", "headerArea"}, {"useVolumeArea", false}, {"stateIconList", QVariant::fromValue<QObject *>(m_status)},
            {"isEnableStatusBarMouseArea", true}, {"volume", app()->player()->volume()}, {"maxVolume", 120},
            {"visibleVolumubar", false}, {"isHighGain", false}, {"isAvlsOn", app()->player()->avls()},
            {"avlsVolumeMax", PlayerController::avlsLimit}, {"volumeOperateInfo", QVariant()}};
}

// The info row's icons, by StatusBar's qml numbers: 1 the playback state
// (and the AVLS mark after the volume), 2 the volume. The frame widths put the
// volume where the A55 draws it; the battery at the row's end is left to
// Android's status bar.
void WindowModel::refreshStatus()
{
    const PlayerController *player = app()->player();
    const QString state = player->playing() ? "stat_notify_audio_track_play.png"
                          : player->hasTrack() ? "stat_notify_audio_track_pause.png" : "";
    QVector<QVariantMap> rows{
        {{"QmlIntRole", 1}, {"ParamIntFrameSize", 138}, {"ParamAnimationImageRole", state}, {"ParamQvariantRole", QVariant()}},
        {{"QmlIntRole", 2}, {"ParamIntFrameSize", 70}, {"ParamAnimationImageRole", ""},
         {"ParamQvariantRole", QVariantList{0, player->volume(), false}}}};
    if (player->avls()) {
        rows.append({{"QmlIntRole", 1}, {"ParamIntFrameSize", 64}, {"ParamAnimationImageRole", "stat_notify_avls_on.png"},
                     {"ParamQvariantRole", QVariant()}});
    }
    if (m_status->size() != rows.size()) {
        m_status->replace(rows);
        return;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (m_status->row(i) != rows.at(i))
            m_status->setRow(i, rows.at(i));
    }
}

QVariantMap WindowModel::transport() const
{
    QVariantMap values = app()->player()->transport();
    // The SensMe player is not in the MusicPlayers alias, but it shows the
    // track itself too, and its stock layout (585 px under the 28 px status
    // bar) leaves room for the controls only without their title row.
    const QString screen = app()->currentScreen();
    const bool onPlayer = app()->screens()->aliases.value("MusicPlayers").contains(screen)
                          || screen == "SensMePlayer";
    values.insert("dispMode", onPlayer ? 1 : 0);
    return values;
}

void WindowModel::refresh()
{
    if (app()->controller()->windowID() != id() && view())
        return;
    m_menu = app()->currentModel()->menu();
    QVector<QVariantMap> rows;
    for (int i = 0; i < m_menu.size(); ++i)
        rows.append({{"text", m_menu.at(i).text}, {"level", 1}, {"parent", -1}, {"index", i}, {"type", 0}, {"icon", ""}});
    m_options->replace(rows);
    refreshStatus();
    set("headerArea", header());
    set("navigationBar", navigation());
    if (id() == "MusicWindow") {
        set("basicPlayerControls", transport());
        set("pageIndicator", QVariantMap{{"objectName", "pageIndicator"}, {"mark_count", 0}, {"now_index", 0},
                                         {"preview", false}});
        set("sourceDirectOrClearAudio",
            QVariantMap{{"objectName", "sourceDirectOrClearAudio"}, {"is_source_direct", false},
                        {"is_clear_audio", false}, {"is_source_direct_preview", false},
                        {"is_clear_audio_preview", false}, {"is_enable_direct_sound", false},
                        {"is_enable_clear_audio", false}});
    }
}

void WindowModel::attached()
{
    for (const char *child : {"navigationBar", "basicPlayerControls", "headerArea"})
        relayChild(child);
    QTimer::singleShot(0, this, [this] { app()->controller()->ReconfigureScreenByWindow(view()); });
}

void WindowModel::handle(const QByteArray &signal, const QVariantList &args)
{
    ScreenController *controller = app()->controller();
    PlayerController *player = app()->player();
    if (signal == "navigationBar.back")
        controller->Back();
    else if (signal == "navigationBar.play")
        controller->GoToPlayback();
    else if (signal == "navigationBar.library")
        controller->BackToTop();
    else if (signal == "navigationBar.option") {
        const int index = args.value(0).toInt();
        if (index >= 0 && index < m_menu.size() && m_menu.at(index).run)
            m_menu.at(index).run();
    } else if (signal == "basicPlayerControls.prevButtonClicked")
        player->previous();
    else if (signal == "basicPlayerControls.playButtonClicked")
        player->togglePlayback();
    else if (signal == "basicPlayerControls.nextButtonClicked")
        player->next();
    else if (signal == "basicPlayerControls.shuffleButtonClicked")
        player->setShuffle(!player->shuffle());
    else if (signal == "basicPlayerControls.repeatButtonClicked")
        player->cycleRepeat();
    else if (signal == "basicPlayerControls.prevButtonLongPressed")
        player->startScan(-1);
    else if (signal == "basicPlayerControls.nextButtonLongPressed")
        player->startScan(1);
    else if (signal == "basicPlayerControls.prevButtonReleased" || signal == "basicPlayerControls.nextButtonReleased")
        player->stopScan();
    else if (signal == "headerArea.statusBarClicked" || signal == "headerArea.volumeBarClicked")
        app()->openPopup("VolumePopup");
}

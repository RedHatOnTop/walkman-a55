#include "appcontext.h"

#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>

#include <QtCore/QFileInfo>
#include <QtCore/QTimer>

#include "../library/musicdb.h"
#include "../playback/player.h"
#include "backkey.h"
#include "dialogmanager.h"
#include "models/browsemodel.h"
#include "models/librarytopmodel.h"
#include "models/popupmodels.h"
#include "models/queuemodels.h"
#include "models/settingsmodels.h"
#include "models/playermodel.h"
#include "models/sensmemodel.h"
#include "models/windowmodel.h"
#include "rowmodel.h"
#include "screencontroller.h"

namespace {

const QStringList soundScreens{"SixBandEqualizerFromPlayer", "ToneControlFromPlayer", "TenBandEqualizerFromPlayer",
                               "VptFromPlayer", "DseeHXFromPlayer", "LenearizerFromPlayer",
                               "DynamicNormalizerFromPlayer", "VinylProcessorFromPlayer"};
const QHash<QString, int> playerScreens{{"MusicPlayer", 1}, {"MusicPlayerSpectrum", 2},
                                        {"MusicPlayerLevelMeter", 3}, {"MusicPlayerDigitalLevelMeter", 13}};

}

AppContext::AppContext(const ScreenSettings *screens, QSettings *prefs, PlayerController *player,
                       MusicDb *library, const QString &language, QObject *parent)
    : QObject(parent), m_screens(screens), m_prefs(prefs), m_player(player), m_library(library),
      m_controller(new ScreenController(screens, this)), m_dialogs(new DialogManager(this)),
      m_backKey(new BackKey(nullptr, this)), m_font(new AppFont(language, this)),
      m_holdToast(new HoldToastManager(this))
{
    m_controller->setConditions([this](const QString &screen, const QString &action) {
        return condition(screen, action);
    });
    // The player's swipe neighbourhood, as the stock app built it at startup;
    // the WM1 series' equalizers (ten-band and tone control) without the
    // six-band one or VPT.
    const QStringList absent{"SixBandEqualizerFromPlayer", "VptFromPlayer"};
    QStringList early;
    for (auto it = screens->screens.begin(); it != screens->screens.end(); ++it) {
        if (it->earlyLoad && it->window == "MusicWindow" && !absent.contains(it.key()))
            early.append(it.key());
    }
    early.sort();
    m_controller->setEarlyLoad(early);
    m_dialogs->setNavigation([this] {
        auto *window = qobject_cast<WindowModel *>(model(m_controller->windowID()));
        return window ? window->navigation() : QVariantMap();
    });
    connect(m_controller, &ScreenController::viewLoaded, this, &AppContext::viewLoaded);
    connect(m_controller, &ScreenController::screenChanged, this, &AppContext::screenChanged);
    connect(m_controller, &ScreenController::backgroundRequested, this, &AppContext::backgroundRequested);
    connect(m_controller, &ScreenController::beginTransition, this,
            [this](const QString &, const QString &to) {
                model(to)->entering();
                model(to)->refreshView();
            });
    // A choice runs once its dialog has closed, so it may open the next one.
    connect(m_dialogs, &DialogManager::selected, this, [this](const QVariant &item) {
        auto selected = std::move(m_selected);
        m_selected = nullptr;
        if (selected)
            QTimer::singleShot(0, this, [selected, item] { selected(item); });
    });
    connect(m_dialogs, &DialogManager::accepted, this, [this] {
        auto confirmed = std::move(m_confirmed);
        m_confirmed = nullptr;
        if (confirmed)
            QTimer::singleShot(0, this, confirmed);
    });
    connect(m_dialogs, &DialogManager::dialogInitialize, this, [this] {
        m_confirmed = nullptr;
        if (m_selected)
            QTimer::singleShot(0, this, [this] { if (!m_dialogs->isOpen()) m_selected = nullptr; });
    });
    connect(m_player, &PlayerController::failed, this, &AppContext::message);
}

void AppContext::install(QQmlEngine *engine)
{
    QQmlContext *context = engine->rootContext();
    context->setContextProperty("screenController", m_controller);
    context->setContextProperty("dialogManager", m_dialogs);
    context->setContextProperty("appFont", m_font);
    context->setContextProperty("holdToastManager", m_holdToast);
    context->setContextProperty("backKey", m_backKey);
}

void AppContext::setRoot(QQuickItem *root)
{
    m_backKey->setRoot(root);
}

void AppContext::start()
{
    m_controller->start(m_prefs->value("ui/startScreen", "MusicPlayerTop").toString());
}

QString AppContext::currentScreen() const
{
    return m_controller->screenID();
}

ScreenModel *AppContext::create(const QString &id)
{
    if (m_screens->windowIds.contains(id))
        return new WindowModel(id, this, this);
    if (id == "MusicPlayerTop")
        return new LibraryTopModel(id, this, this);
    if (id == "LibraryIconSelect")
        return new LibraryIconSelectModel(id, this, this);
    if (BrowseModel::handles(id))
        return new BrowseModel(id, this, this);
    if (id == "ImePanelPopup")
        return new TextEntryModel(id, this, this);
    if (id == "VolumePopup")
        return new VolumeModel(id, this, this);
    if (id == "LibraryTopHelpGuide")
        return new LibraryHelpModel(id, this, this);
    if (id.endsWith("ContentDetailedInfoPopup"))
        return new DetailsModel(id, this, this);
    if (id == "LyricDispPopup")
        return new LyricsModel(id, this, this);
    if (m_screens->popupIds.contains(id))
        return new PopupModel(id, this, this);
    if (ScreenModel *settings = createSettingsModel(id, this))
        return settings;
    if (id == "TrackSequence")
        return new TrackSequenceModel(id, this, this);
    if (id == "Bookmark")
        return new BookmarkModel(id, this, this);
    if (playerScreens.contains(id))
        return new PlayerModel(id, this, this);
    if (id == "SensMePlayer")
        return new SensMePlayerModel(id, this, this);
    return new ScreenModel(id, this, this);
}

ScreenModel *AppContext::model(const QString &id)
{
    auto it = m_models.find(id);
    if (it == m_models.end())
        it = m_models.insert(id, create(id));
    return it.value();
}

void AppContext::viewLoaded(const QString &, const QString &id, QQuickItem *view)
{
    model(id)->attach(view);
}

void AppContext::screenChanged(const QString &screen)
{
    for (auto it = m_models.begin(); it != m_models.end(); ++it) {
        if (it.key() != screen)
            it.value()->deactivate();
    }
    const int sound = soundScreens.indexOf(screen);
    if (sound >= 0)
        m_prefs->setValue("ui/soundScreen", sound + 1);
    if (screen == "TenBandEqualizerFromPlayer" || screen == "ToneControlFromPlayer")
        m_prefs->setValue("ui/equalizerScreen", screen);
    if (playerScreens.contains(screen))
        m_prefs->setValue("ui/playerScreen", playerScreens.value(screen));
    model(screen)->activate();
    emit windowChanged();
    // The library top and the player show their help guide the first time
    // they are displayed.
    const QString guide = m_screens->popupFor(screen, "Displayed", 1);
    if (guide.endsWith("HelpGuide") && !m_prefs->value("helpGuide/" + guide).toBool()) {
        m_prefs->setValue("helpGuide/" + guide, true);
        QTimer::singleShot(0, this, [this, guide] { openPopup(guide); });
    }
}

int AppContext::playerCondition() const
{
    // A SensMe channel plays on the SensMe player (condition 4).
    if (m_player->channel() >= 0)
        return 4;
    return m_prefs->value("ui/playerScreen", 1).toInt();
}

int AppContext::soundCondition() const
{
    // The ten-band equalizer opens first, as on the WM1 series.
    return m_prefs->value("ui/soundScreen", 3).toInt();
}

// The sound pages in swipe order, wrapping around: the equalizer page (the
// ten-band one or tone control, whichever was shown last), DSEE HX, DC phase
// linearizer, dynamic normalizer, vinyl processor. The table holds every
// model's pages under numbered conditions; this picks the WM1 series' set,
// which the app keeps over the A55's six-band equalizer.
QString AppContext::soundNeighbour(const QString &screen, const QString &action) const
{
    const int step = action == "SwipeLeft" ? 1 : action == "SwipeRight" ? -1 : 0;
    if (step == 0)
        return {};
    const QStringList pages{m_prefs->value("ui/equalizerScreen", "TenBandEqualizerFromPlayer").toString(),
                            "DseeHXFromPlayer", "LenearizerFromPlayer", "DynamicNormalizerFromPlayer",
                            "VinylProcessorFromPlayer"};
    const bool equalizer = screen == "TenBandEqualizerFromPlayer" || screen == "ToneControlFromPlayer";
    const int index = equalizer ? 0 : pages.indexOf(screen);
    if (index < 0)
        return {};
    return pages.at((index + step + pages.size()) % pages.size());
}

int AppContext::condition(const QString &screen, const QString &action) const
{
    if (action == "NonHistoryBack")
        return 6;
    const QVector<Transition> entries = m_screens->transitions.value(screen);
    const QString page = soundNeighbour(screen, action);
    for (const Transition &entry : entries) {
        if (entry.action == action && !page.isEmpty() && entry.next == page)
            return entry.condition;
    }
    const QStringList players = m_screens->aliases.value("MusicPlayers");
    for (const Transition &entry : entries) {
        if (entry.action != action)
            continue;
        if (players.contains(entry.next))
            return playerCondition();
        if (soundScreens.contains(entry.next))
            return soundCondition();
    }
    return 0;
}

void AppContext::message(const QString &text)
{
    m_dialogs->message(text);
}

void AppContext::openPopup(const QString &popup)
{
    m_controller->showPopup(popup);
}

bool AppContext::pressBack()
{
    return m_backKey->press();
}

void AppContext::openMenu(const QString &title, const QVector<ScreenModel::MenuItem> &items)
{
    auto *rows = new ListRows({"menu_text", "menu_id"}, this);
    QVector<QVariantMap> list;
    for (int i = 0; i < items.size(); ++i)
        list.append({{"menu_text", items.at(i).text}, {"menu_id", i}});
    rows->replace(list);
    if (m_dialogs->open("kMid9212", title, {}, QVariant::fromValue<QObject *>(rows)) == "NoneDialog") {
        rows->deleteLater();
        return;
    }
    connect(m_dialogs, &DialogManager::dialogInitialize, rows, &QObject::deleteLater);
    m_selected = [items](const QVariant &item) {
        const int index = item.toInt();
        if (index >= 0 && index < items.size() && items.at(index).run)
            items.at(index).run();
    };
}

void AppContext::chooseFrom(const QString &title, const QStringList &labels, std::function<void(int)> chosen)
{
    auto *rows = new ListRows({"label", "item_id"}, this);
    QVector<QVariantMap> list;
    for (int i = 0; i < labels.size(); ++i)
        list.append({{"label", labels.at(i)}, {"item_id", i}});
    rows->replace(list);
    if (m_dialogs->open("kMid9215", title, {}, QVariant::fromValue<QObject *>(rows)) == "NoneDialog") {
        rows->deleteLater();
        return;
    }
    connect(m_dialogs, &DialogManager::dialogInitialize, rows, &QObject::deleteLater);
    m_selected = [chosen](const QVariant &item) { chosen(item.toInt()); };
}

QVector<int> AppContext::bookmarks(int list) const
{
    QVector<int> tracks;
    for (const QString &path : m_prefs->value(QString("bookmarks/%1").arg(list + 1)).toStringList()) {
        const int id = m_library->find(path);
        if (id >= 0)
            tracks.append(id);
    }
    return tracks;
}

void AppContext::setBookmarks(int list, const QVector<int> &tracks)
{
    QStringList paths;
    for (int id : tracks)
        paths.append(m_library->track(id).path);
    m_prefs->setValue(QString("bookmarks/%1").arg(list + 1), paths);
    emit bookmarksChanged();
}

QStringList AppContext::bookmarkNames() const
{
    QStringList names;
    for (int i = 0; i < bookmarkLists; ++i)
        names.append(sonyText("QObject", QByteArray::number(20182 + i).prepend("0").constData()));
    return names;
}

void AppContext::addToBookmarkList(int list, const QVector<int> &tracks)
{
    if (list < 0 || list >= bookmarkLists)
        return;
    setBookmarks(list, bookmarks(list) + tracks);
    message(sonyText("dmpapp::PlaylistMessage", "000093").replace("%s", bookmarkNames().value(list)));
}

void AppContext::addToBookmarks(const QVector<int> &tracks)
{
    chooseFrom(sonyText("dmpapp::ContentsViewModelContextMenu", "020034"), bookmarkNames(),
               [this, tracks](int list) { addToBookmarkList(list, tracks); });
}

QStringList AppContext::playlistChoices() const
{
    QStringList names{sonyText("dmpapp::PlaylistListModel", "020181")};
    for (const Playlist &playlist : m_library->playlists()) {
        if (playlist.editable)
            names.append(playlist.name);
    }
    return names;
}

void AppContext::addToPlaylistChoice(int choice, const QVector<int> &tracks)
{
    QVector<int> editable;
    const QVector<Playlist> playlists = m_library->playlists();
    for (int i = 0; i < playlists.size(); ++i) {
        if (playlists.at(i).editable)
            editable.append(i);
    }
    const auto added = [this](bool ok, const QString &name) {
        message(sonyText("dmpapp::PlaylistMessage", ok ? "000093" : "000094").replace("%s", name));
    };
    if (choice == 0) {
        const QString base = sonyText("dmpapp::PlaylistListModel", "020181");
        QString name = base;
        for (int n = 2; playlistNamed(name) && n < 100; ++n)
            name = QString("%1 %2").arg(base).arg(n);
        askText(base, name, [this, tracks, added](const QString &name) {
            if (playlistNamed(name))
                message(sonyText("QObject", "020250"));
            else
                added(m_library->createPlaylist(name, tracks), name);
        });
    } else if (choice > 0 && choice <= editable.size()) {
        const int playlist = editable.at(choice - 1);
        added(m_library->addToPlaylist(playlist, tracks), playlists.at(playlist).name);
    }
}

bool AppContext::playlistNamed(const QString &name) const
{
    for (const Playlist &playlist : m_library->playlists()) {
        if (playlist.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void AppContext::renamePlaylist(int playlist)
{
    const QString current = m_library->playlists().value(playlist).name;
    askText(sonyText("dmpapp::ContentsViewModelContextMenu", "020263"), current,
            [this, playlist, current](const QString &name) {
        if (name == current)
            return;
        if (playlistNamed(name) && name.compare(current, Qt::CaseInsensitive) != 0)
            message(sonyText("QObject", "020250"));
        else if (m_library->renamePlaylist(playlist, name))
            message(sonyText("QObject", "020204"));
    });
}

void AppContext::deletePlaylist(int playlist)
{
    const QString name = m_library->playlists().value(playlist).name;
    confirm(sonyText("QObject", "020196").replace("%s", name), [this, playlist] { m_library->deletePlaylist(playlist); });
}

void AppContext::confirm(const QString &text, std::function<void()> yes)
{
    if (m_dialogs->open("kMid9200", QString(), text, QVariant()) != "NoneDialog")
        m_confirmed = std::move(yes);
}

void AppContext::askText(const QString &title, const QString &initial, std::function<void(const QString &)> entered)
{
    m_entered = std::move(entered);
    if (auto *entry = qobject_cast<TextEntryModel *>(model("ImePanelPopup")))
        entry->prepare(title, initial);
    openPopup("ImePanelPopup");
}

void AppContext::textEntered(const QString &text)
{
    auto entered = std::move(m_entered);
    m_entered = nullptr;
    if (entered && !text.isEmpty())
        QTimer::singleShot(0, this, [entered, text] { entered(text); });
}

void AppContext::addToPlaylist(const QVector<int> &tracks)
{
    chooseFrom(sonyText("dmpapp::ContentsViewModelContextMenu", "020034"), playlistChoices(),
               [this, tracks](int choice) { addToPlaylistChoice(choice, tracks); });
}

void AppContext::showDetails(int track)
{
    m_detailsTrack = track;
    openPopup("TrackListContentDetailedInfoPopup");
}

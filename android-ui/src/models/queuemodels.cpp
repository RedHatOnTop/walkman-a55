#include "queuemodels.h"

#include <QtCore/QTimer>

#include "../../library/musicdb.h"
#include "../../playback/player.h"
#include "../appcontext.h"
#include "../rowmodel.h"
#include "browsemodel.h"

namespace {

QVector<QVariantMap> menuRows(const QStringList &texts)
{
    QVector<QVariantMap> rows;
    for (int i = 0; i < texts.size(); ++i)
        rows.append({{"menu_text", texts.at(i)}, {"menu_id", i}});
    return rows;
}

QVector<QVariantMap> choiceRows(const QStringList &labels)
{
    QVector<QVariantMap> rows;
    for (int i = 0; i < labels.size(); ++i)
        rows.append({{"label", labels.at(i)}, {"item_id", i}});
    return rows;
}

// A view's own signal, raised once the dialog that led to it has closed.
void raiseLater(ScreenModel *model, const char *signal)
{
    QTimer::singleShot(0, model, [model, signal] {
        if (model->view())
            QMetaObject::invokeMethod(model->view(), signal);
    });
}

}

TrackSequenceModel::TrackSequenceModel(const QString &id, AppContext *app, QObject *parent)
    : ScreenModel(id, app, parent), m_list(new BrowseListModel(this)),
      m_context(new ListRows({"menu_text", "menu_id"}, this)), m_bookmarks(new ListRows({"label", "item_id"}, this)),
      m_playlists(new ListRows({"label", "item_id"}, this))
{
    const char *context = "dmpapp::TrackSequenceViewModel";
    m_context->replace(menuRows({sonyText(context, "020171"), sonyText(context, "020172"), sonyText(context, "020246")}));
    PlayerController *player = app->player();
    connect(player, &PlayerController::queueChanged, this, &TrackSequenceModel::rebuild);
    connect(player, &PlayerController::trackChanged, this, [this] {
        set("view_index", this->app()->player()->queueIndex());
    });
    setValues({{"context_list", QVariant::fromValue<QObject *>(m_context)},
               {"bookmarkDialogList", QVariant::fromValue<QObject *>(m_bookmarks)},
               {"playlistDialogList", QVariant::fromValue<QObject *>(m_playlists)},
               {"syncScroll", true}, {"is_visible_bottom_separator", false}});
    rebuild();
}

void TrackSequenceModel::rebuild()
{
    const QVector<int> queue = app()->player()->queue();
    if (queue != m_queue) {
        m_queue = queue;
        QVector<BrowseRow> rows;
        for (int id : queue)
            rows.append(app()->library()->valid(id) ? trackRow(app()->library(), id) : BrowseRow());
        m_list->replace(rows);
    }
    set("list", QVariant::fromValue<QObject *>(m_list));
    set("view_index", app()->player()->queueIndex());
}

void TrackSequenceModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "touchListAt") {
        app()->player()->playQueueRow(args.value(0).toInt());
    } else if (signal == "callContextAt") {
        m_selected = args.value(0).toInt();
    } else if (signal == "createContextMenu") {
        m_bookmarks->replace(choiceRows(app()->bookmarkNames()));
        m_playlists->replace(choiceRows(app()->playlistChoices()));
    } else if (signal == "contextItemSelected") {
        const int track = m_queue.value(m_selected, -1);
        switch (args.value(0).toInt()) {
        case 0: raiseLater(this, "showBookmarkListDialog"); break;
        case 1: raiseLater(this, "showPlaylistDialog"); break;
        case 2: if (track >= 0) app()->showDetails(track); break;
        }
    } else if (signal == "bookmarkListSelected") {
        const int track = m_queue.value(m_selected, -1);
        if (track >= 0)
            app()->addToBookmarkList(args.value(0).toInt(), {track});
    } else if (signal == "playlistSelected") {
        const int track = m_queue.value(m_selected, -1);
        if (track >= 0)
            app()->addToPlaylistChoice(args.value(0).toInt(), {track});
    } else if (signal == "enableSyncScroll") {
        set("syncScroll", true);
    } else if (signal == "disableSyncScroll") {
        set("syncScroll", false);
    } else {
        ScreenModel::handle(signal, args);
    }
}

QVector<ScreenModel::MenuItem> TrackSequenceModel::menu()
{
    const char *context = "dmpapp::TrackSequenceViewModel";
    return {
        {sonyText(context, "010020"), [this] { navigate("SettingTop"); }},
        {sonyText(context, "020170"), [this] { app()->addToBookmarks(m_queue); }},
        {sonyText(context, "020175"), [this] { app()->addToPlaylist(m_queue); }},
    };
}

BookmarkModel::BookmarkModel(const QString &id, AppContext *app, QObject *parent)
    : ScreenModel(id, app, parent), m_list(new BrowseListModel(this)),
      m_context(new ListRows({"menu_text", "menu_id"}, this)), m_bookmarks(new ListRows({"label", "item_id"}, this)),
      m_playlists(new ListRows({"label", "item_id"}, this))
{
    m_number = qBound(0, app->prefs()->value("bookmarks/current", 0).toInt(), AppContext::bookmarkLists - 1);
    m_context->replace(menuRows({sonyText("QObject", "020234"), sonyText("dmpapp::ContentsViewModelContextMenu", "020172"),
                                 sonyText("dmpapp::ContentsViewModelContextMenu", "020246"),
                                 sonyText("QObject", "020176")}));
    connect(app, &AppContext::bookmarksChanged, this, &BookmarkModel::rebuild);
    connect(m_list, &BrowseListModel::rowMoved, this, [this](int from, int to) { m_tracks.move(from, to); });
    connect(app->library(), &MusicDb::changed, this, &BookmarkModel::rebuild);
    setValues({{"contextList", QVariant::fromValue<QObject *>(m_context)},
               {"bookmarkDialogList", QVariant::fromValue<QObject *>(m_bookmarks)},
               {"playlistDialogList", QVariant::fromValue<QObject *>(m_playlists)},
               {"sortenable", false}, {"is_visible_bottom_separator", false}});
    rebuild();
}

void BookmarkModel::activate()
{
    rebuild();
}

void BookmarkModel::rebuild()
{
    m_tracks = app()->bookmarks(m_number);
    QVector<BrowseRow> rows;
    for (int id : m_tracks)
        rows.append(trackRow(app()->library(), id));
    m_list->replace(rows);
    setValues({{"bookmarkList", QVariant::fromValue<QObject *>(m_list)}, {"bookmarkListNo", m_number + 1},
               {"nodeCount", m_tracks.size()}, {"update_db_counter", app()->library()->counter()}});
}

void BookmarkModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "touchListAt") {
        app()->player()->playTracks(m_tracks, args.value(0).toInt());
        navigate("selectedItem");
    } else if (signal == "prevScreen" || signal == "nextScreen") {
        const int step = signal == "nextScreen" ? 1 : AppContext::bookmarkLists - 1;
        m_number = (m_number + step) % AppContext::bookmarkLists;
        app()->prefs()->setValue("bookmarks/current", m_number);
        rebuild();
    } else if (signal == "createContextMenu") {
        m_bookmarks->replace(choiceRows(app()->bookmarkNames()));
        m_playlists->replace(choiceRows(app()->playlistChoices()));
    } else if (signal == "contextMenuSelected") {
        m_selected = args.value(0).toInt();
        const int track = m_tracks.value(m_selected, -1);
        switch (args.value(1).toInt()) {
        case 0: raiseLater(this, "showBookmarkDialog"); break;
        case 1: raiseLater(this, "showPlaylistDialog"); break;
        case 2: if (track >= 0) app()->showDetails(track); break;
        case 3:
            if (m_selected >= 0 && m_selected < m_tracks.size()) {
                QVector<int> tracks = m_tracks;
                tracks.removeAt(m_selected);
                app()->setBookmarks(m_number, tracks);
            }
            break;
        }
    } else if (signal == "dialogMenuSelected") {
        const int track = m_tracks.value(m_selected, -1);
        if (track >= 0)
            app()->addToBookmarkList(args.value(0).toInt(), {track});
    } else if (signal == "playlistSelected") {
        const int track = m_tracks.value(m_selected, -1);
        if (track >= 0)
            app()->addToPlaylistChoice(args.value(0).toInt(), {track});
    } else if (signal == "sortEnd") {
        m_sorting = false;
        set("sortenable", false);
        app()->setBookmarks(m_number, m_tracks);
    } else {
        ScreenModel::handle(signal, args);
    }
}

QVector<ScreenModel::MenuItem> BookmarkModel::menu()
{
    QVector<MenuItem> items{{sonyText("dmpapp::ContentsViewModel", "010020"), [this] { navigate("SettingTop"); }}};
    if (m_tracks.isEmpty())
        return items;
    items.append({sonyText("dmpapp::ContentsViewModel", "020033"), [this] {
        m_sorting = true;
        set("sortenable", true);
    }});
    items.append({sonyText("QObject", "020169"), [this] {
        app()->setBookmarks(m_number, {});
    }});
    return items;
}

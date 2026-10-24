#include "browsemodel.h"

#include <QtCore/QTimer>

#include "../../library/musicdb.h"
#include "../../playback/player.h"
#include "../appcontext.h"
#include "../rowmodel.h"
#include "../screencontroller.h"

namespace {

const char listContext[] = "dmpapp::ContentsViewModelList";
const char menuContext[] = "dmpapp::ContentsViewModelContextMenu";

const QHash<QString, BrowseModel::Kind> &kinds()
{
    static const QHash<QString, BrowseModel::Kind> table{
        {"AllTrackList", BrowseModel::Tracks}, {"AlbumTrackList", BrowseModel::Tracks},
        {"ArtistAlbumTrackList", BrowseModel::Tracks}, {"GenreTrackList", BrowseModel::Tracks},
        {"GenreArtistAlbumTrackList", BrowseModel::Tracks}, {"ReleaseYearArtistTrackList", BrowseModel::Tracks},
        {"ComposerAlbumTrackList", BrowseModel::Tracks}, {"HighResolutionTrackList", BrowseModel::Tracks},
        {"HighResolutionAlbumTrackList", BrowseModel::Tracks},
        {"HighResolutionArtistAlbumTrackList", BrowseModel::Tracks},
        {"RecentTransportTrackList", BrowseModel::Tracks}, {"PlayListTrackList", BrowseModel::Tracks},
        {"AlbumList", BrowseModel::Albums}, {"HighResolutionAlbumList", BrowseModel::Albums},
        {"RecentTransportList", BrowseModel::RecentAlbums},
        {"ArtistAlbumList", BrowseModel::AlbumsWithAll}, {"GenreArtistAlbumList", BrowseModel::AlbumsWithAll},
        {"ComposerAlbumList", BrowseModel::AlbumsWithAll},
        {"HighResolutionArtistAlbumList", BrowseModel::AlbumsWithAll},
        {"ArtistList", BrowseModel::Artists}, {"HighResolutionArtistList", BrowseModel::Artists},
        {"GenreArtistList", BrowseModel::ArtistsWithAll}, {"ReleaseYearArtistList", BrowseModel::ArtistsWithAll},
        {"GenreList", BrowseModel::Categories}, {"ReleaseYearList", BrowseModel::Categories},
        {"ComposerList", BrowseModel::Categories}, {"PlayList", BrowseModel::Playlists},
        {"HighResolutionList", BrowseModel::HiResTop}, {"StorageTop", BrowseModel::Storages},
        {"FirstLayerFolderList", BrowseModel::Folders}, {"SecondLayerFolderList", BrowseModel::Folders},
        {"ThirdLayerFolderList", BrowseModel::Folders}, {"FourthLayerFolderList", BrowseModel::Folders},
        {"FifthLayerFolderList", BrowseModel::Folders}, {"SixthLayerFolderList", BrowseModel::Folders},
        {"SeventhLayerFolderList", BrowseModel::Folders}, {"EighthLayerFolderList", BrowseModel::Folders},
    };
    return table;
}

// The category tops take their titles from the library top's own names.
const QHash<QString, const char *> &topTitles()
{
    static const QHash<QString, const char *> table{
        {"AllTrackList", "020036"}, {"AlbumList", "020037"}, {"ArtistList", "020038"}, {"GenreList", "020039"},
        {"ReleaseYearList", "020040"}, {"ComposerList", "020041"}, {"PlayList", "020042"},
        {"RecentTransportList", "020043"}, {"HighResolutionList", "020044"}, {"StorageTop", "020045"},
    };
    return table;
}

}

QString durationText(qint64 ms)
{
    if (ms <= 0)
        return QCoreApplication::translate(listContext, "020021");
    const qint64 seconds = (ms + 500) / 1000;
    if (seconds >= 3600)
        return QString("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

BrowseRow trackRow(const MusicDb *db, int id)
{
    const LibraryTrack &track = db->track(id);
    BrowseRow row;
    row.name = track.title();
    row.additional = track.tags.artist;
    row.codec = track.tags.codec;
    row.duration = durationText(track.tags.durationMs);
    row.hiRes = track.isHiRes();
    row.cover = QString::number(id);
    return row;
}

int BrowseListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> BrowseListModel::roleNames() const
{
    return {{CacheRole, "IsExistContentsCacheRole"}, {NameRole, "NameQstringRole"},
            {AdditionalRole, "AdditionalTextQstringRole"}, {CountRole, "ContentsNumUint32Role"},
            {CodecRole, "CodecUint32Role"}, {DurationRole, "TrackDurationUint32Role"},
            {HiResRole, "IsHighResolutionRole"}, {CoverRole, "MainCoverArtRole"}, {Sub1Role, "SubCoverArtRole1"},
            {Sub2Role, "SubCoverArtRole2"}, {ArtistCoverRole, "MainCoverArtArtistRole"},
            {ArtistSub1Role, "SubCoverArtArtistRole1"}, {ArtistSub2Role, "SubCoverArtArtistRole2"},
            {StorageIconRole, "StorageIconRole"}, {FileMetaRole, "FileMetaInformationRole"},
            {FileMetaTypoRole, "FileMetaInfomationRole"}, {EditableRole, "IsEditPlaylistRole"},
            {CounterRole, "DataChangedCounterRole"}};
}

QVariant BrowseListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const BrowseRow &row = m_rows.at(index.row());
    switch (role) {
    case CacheRole:
        if (m_flushing)
            return true;
        m_pending.insert(index.row());
        if (!m_scheduled) {
            m_scheduled = true;
            QTimer::singleShot(0, const_cast<BrowseListModel *>(this), &BrowseListModel::flush);
        }
        return false;
    case NameRole: return row.name;
    case AdditionalRole: return row.additional;
    case CountRole: return row.count;
    case CodecRole: return row.codec;
    case DurationRole: return row.duration;
    case HiResRole: return row.hiRes;
    case CoverRole:
    case ArtistCoverRole: return row.cover;
    case Sub1Role:
    case ArtistSub1Role: return row.sub1;
    case Sub2Role:
    case ArtistSub2Role: return row.sub2;
    case StorageIconRole: return row.icon;
    case FileMetaRole:
    case FileMetaTypoRole: return row.count;
    case EditableRole: return row.editable;
    case CounterRole: return 0;
    }
    return {};
}

void BrowseListModel::flush()
{
    m_scheduled = false;
    const QSet<int> rows = m_pending;
    m_pending.clear();
    m_flushing = true;
    for (int row : rows) {
        if (row < m_rows.size())
            emit dataChanged(index(row), index(row), {CacheRole});
    }
    m_flushing = false;
}

void BrowseListModel::move(int from, int to, int count)
{
    if (count != 1 || from < 0 || to < 0 || from >= m_rows.size() || to >= m_rows.size() || from == to)
        return;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_rows.move(from, to);
    endMoveRows();
    emit rowMoved(from, to);
}

void BrowseListModel::replace(const QVector<BrowseRow> &rows)
{
    beginResetModel();
    m_rows = rows;
    m_pending.clear();
    endResetModel();
}

BrowseModel::BrowseModel(const QString &id, AppContext *app, QObject *parent)
    : ScreenModel(id, app, parent), m_kind(kinds().value(id)), m_list(new BrowseListModel(this))
{
    const char *title = topTitles().value(id);
    if (title)
        m_scope.title = sonyText("dmpapp::LibraryTopModelList", title);
    connect(m_list, &BrowseListModel::rowMoved, this, [this](int from, int to) {
        m_rows.move(from, to);
        m_entries.move(from, to);
        m_scope.tracks.move(from, to);
    });
    connect(app->library(), &MusicDb::changed, this, [this] {
        if (this->app()->currentScreen() == this->id())
            rebuild();
        else
            m_built = -1;
    });
}

bool BrowseModel::handles(const QString &id)
{
    return kinds().contains(id);
}

void BrowseModel::setScope(const BrowseScope &scope)
{
    m_scope = scope;
    m_built = -1;
}

void BrowseModel::activate()
{
    if (m_built != app()->library()->counter() || m_artists != artistField())
        rebuild();
}

// Settings > Playback Settings > Artist List Display: album artists (0) or
// song artists (1).
MusicDb::Field BrowseModel::artistField() const
{
    return app()->prefs()->value("settings/artistDisplayMode", 0).toInt() == 1 ? MusicDb::Artist : MusicDb::AlbumArtist;
}

QVector<int> BrowseModel::scopeTracks() const
{
    return m_scope.all ? app()->library()->select() : m_scope.tracks;
}

void BrowseModel::addTrackRows(const QVector<int> &tracks)
{
    for (int id : tracks) {
        const BrowseRow row = trackRow(app()->library(), id);
        m_rows.append(row);
        m_entries.append({tracks, row.name, {}, id, -1, 0});
    }
}

void BrowseModel::addGroups(const QVector<LibraryGroup> &groups, bool withAll, bool artists)
{
    if (withAll && !groups.isEmpty()) {
        BrowseRow all;
        all.name = sonyText(listContext, "020047");
        QVector<int> tracks;
        for (const LibraryGroup &group : groups)
            tracks += group.tracks;
        all.count = QString::number(artists ? groups.size() : tracks.size());
        all.cover = QString::number(groups.value(0).cover);
        all.sub1 = QString::number(groups.value(1, groups.value(0)).cover);
        all.sub2 = QString::number(groups.value(2, groups.value(0)).cover);
        m_rows.append(all);
        m_entries.append({tracks, m_scope.title, {}, -1, -1, 1});
    }
    for (const LibraryGroup &group : groups) {
        BrowseRow row;
        row.name = group.name;
        row.additional = group.detail;
        row.count = QString::number(group.tracks.size());
        row.cover = QString::number(group.cover);
        m_rows.append(row);
        m_entries.append({group.tracks, group.name, {}, -1, -1, withAll ? 2 : 0});
    }
}

void BrowseModel::rebuild()
{
    MusicDb *db = app()->library();
    m_rows.clear();
    m_entries.clear();
    const QVector<int> tracks = scopeTracks();
    QSet<int> inScope;
    if (!m_scope.all)
        inScope = QSet<int>(tracks.begin(), tracks.end());
    const MusicDb::Predicate match = m_scope.all ? MusicDb::Predicate() : [inScope](int id) { return inScope.contains(id); };
    QStringList initials;
    m_artists = artistField();

    switch (m_kind) {
    case Tracks:
        addTrackRows(tracks);
        break;
    case Albums:
        addGroups(db->albums(match), false, false);
        break;
    case RecentAlbums:
        addGroups(db->recentAlbums(), false, false);
        break;
    case AlbumsWithAll:
        addGroups(db->albums(match), true, false);
        break;
    case Artists:
        addGroups(db->group(m_artists, match), false, true);
        break;
    case ArtistsWithAll:
        addGroups(db->group(m_artists, match), true, true);
        break;
    case Categories: {
        const MusicDb::Field field = id() == "GenreList" ? MusicDb::Genre
            : id() == "ComposerList" ? MusicDb::Composer : MusicDb::Year;
        QVector<LibraryGroup> groups = db->group(field, match);
        // ReleaseYearView reads "0" as an unknown year.
        if (field == MusicDb::Year) {
            for (LibraryGroup &group : groups) {
                if (group.name.isEmpty())
                    group.name = "0";
            }
        }
        addGroups(groups, false, false);
        break;
    }
    case Playlists: {
        const QVector<Playlist> playlists = db->playlists();
        for (int i = 0; i < playlists.size(); ++i) {
            BrowseRow row;
            row.name = playlists.at(i).name;
            row.count = QString::number(playlists.at(i).tracks.size());
            row.editable = playlists.at(i).editable;
            m_rows.append(row);
            m_entries.append({playlists.at(i).tracks, row.name, {}, -1, i, 0});
        }
        break;
    }
    case HiResTop: {
        const QVector<int> hiRes = db->select([db](int id) { return db->track(id).isHiRes(); });
        const char *context = "dmpapp::HighResolutionTopViewModel";
        auto *list = new ListRows({"item_text", "item_id", "icon_path"}, this);
        QVector<QVariantMap> rows;
        const char *names[] = {"020036", "020037", "020038"};
        const char *icons[] = {"all", "albums", "artists"};
        for (int i = 0; i < 3; ++i) {
            rows.append({{"item_text", sonyText(context, names[i])}, {"item_id", i + 1},
                         {"icon_path", QString("qrc:/opt2/appresouce/btn_music_library_filter_hi_res_%1_normal.png")
                                           .arg(icons[i])}});
            m_entries.append({hiRes, sonyText(context, names[i]), {}, -1, -1, i + 1});
        }
        list->replace(rows);
        set("highResolutionList", QVariant::fromValue<QObject *>(list));
        set("title", m_scope.title);
        m_built = db->counter();
        return;
    }
    case Storages: {
        const QVector<Storage> storages = db->storages();
        for (int i = 0; i < storages.size(); ++i) {
            BrowseRow row;
            row.name = storages.at(i).name;
            row.icon = storages.at(i).icon;
            row.count = QString::number(db->storageTracks(i));
            m_rows.append(row);
            m_entries.append({{}, row.name, storages.at(i).path, -1, -1, 0});
        }
        m_list->replace(m_rows);
        set("storageTopList", QVariant::fromValue<QObject *>(m_list));
        set("title", m_scope.title);
        m_built = db->counter();
        return;
    }
    case Folders: {
        QVector<int> files;
        for (const FolderEntry &entry : db->folder(m_scope.folder)) {
            BrowseRow row;
            row.name = entry.name;
            if (entry.folder) {
                row.cover = entry.cover >= 0 ? QString::number(entry.cover) : QString("folder");
                row.count = QString::number(entry.count);
                m_entries.append({{}, entry.name, entry.path, -1, -1, 0});
            } else {
                row.cover = QString::number(entry.track);
                row.count = durationText(db->track(entry.track).tags.durationMs);
                files.append(entry.track);
                m_entries.append({{}, entry.name, {}, entry.track, -1, 1});
            }
            m_rows.append(row);
        }
        // A song plays the folder's songs, in folder order.
        for (Entry &entry : m_entries) {
            if (entry.track >= 0)
                entry.tracks = files;
        }
        set("storage_icon_path", m_scope.storageIcon);
        break;
    }
    }

    for (const BrowseRow &row : m_rows)
        initials.append(MusicDb::initial(row.name));
    m_list->replace(m_rows);
    set("list", QVariant::fromValue<QObject *>(m_list));
    set("initial_char_list", initials);
    set("title", m_scope.title);
    set("update_db_counter", db->counter());
    set("isAllRowExists", m_kind == AlbumsWithAll || m_kind == ArtistsWithAll);
    m_built = db->counter();
}

void BrowseModel::open(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    const Entry &entry = m_entries.at(index);
    if (entry.track >= 0) {
        const int position = entry.tracks.indexOf(entry.track);
        app()->player()->playTracks(entry.tracks, qMax(0, position), true, m_scope.range, m_scope.list);
        navigate("selectedItem");
        return;
    }
    const Transition *next = app()->screens()->find(id(), "nextFilter", entry.condition);
    if (!next)
        return;
    BrowseScope scope;
    scope.title = entry.title;
    scope.all = false;
    scope.tracks = entry.tracks;
    scope.playlist = entry.playlist;
    // This level's groups, without its "All" row.
    if (m_kind != HiResTop && m_kind != Storages && m_kind != Folders && entry.condition != 1) {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries.at(i).condition != entry.condition)
                continue;
            if (i == index)
                scope.list = scope.range.size();
            scope.range.append(m_entries.at(i).tracks);
        }
    }
    if (m_kind == Storages || m_kind == Folders) {
        scope.folder = entry.folder;
        scope.storageIcon = m_kind == Storages ? app()->library()->storages().value(index).icon : m_scope.storageIcon;
    }
    if (auto *target = qobject_cast<BrowseModel *>(app()->model(next->next)))
        target->setScope(scope);
    navigate("nextFilter", entry.condition);
}

QVector<int> BrowseModel::rowTracks(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return {};
    const Entry &entry = m_entries.at(index);
    if (entry.track >= 0)
        return {entry.track};
    if (!entry.folder.isEmpty()) {
        const MusicDb *db = app()->library();
        const QString prefix = entry.folder + '/';
        return db->select([db, &prefix, &entry](int id) {
            const QString &folder = db->track(id).folder;
            return folder == entry.folder || folder.startsWith(prefix);
        });
    }
    return entry.tracks;
}

void BrowseModel::contextMenu(int index)
{
    const QVector<int> tracks = rowTracks(index);
    if (tracks.isEmpty())
        return;
    QVector<MenuItem> items{
        {sonyText(menuContext, "020171"), [this, tracks] { app()->addToBookmarks(tracks); }},
        {sonyText(menuContext, "020172"), [this, tracks] { app()->addToPlaylist(tracks); }},
    };
    if (m_entries.at(index).track >= 0) {
        const int track = m_entries.at(index).track;
        items.append({sonyText(menuContext, "020246"), [this, track] { app()->showDetails(track); }});
    }
    if (m_kind == Tracks && m_scope.playlist >= 0
        && app()->library()->playlists().value(m_scope.playlist).editable) {
        const int playlist = m_scope.playlist;
        items.append({sonyText(menuContext, "020179"), [this, playlist, index] {
            app()->library()->removeFromPlaylist(playlist, index);
            m_scope.tracks = app()->library()->playlists().value(playlist).tracks;
            rebuild();
        }});
    }
    if (m_kind == Playlists && app()->library()->playlists().value(m_entries.at(index).playlist).editable) {
        const int playlist = m_entries.at(index).playlist;
        items.append({sonyText(menuContext, "020180"), [this, playlist] { app()->renamePlaylist(playlist); }});
        items.append({sonyText(menuContext, "000071"), [this, playlist] { app()->deletePlaylist(playlist); }});
    }
    app()->openMenu(m_rows.value(index).name, items);
}

void BrowseModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "touchListAt") {
        open(m_kind == HiResTop ? args.value(0).toInt() - 1 : args.value(0).toInt());
    } else if (signal == "createContextMenu") {
        contextMenu(args.value(0).toInt());
    } else if (signal == "sortEnd" && m_scope.playlist >= 0) {
        app()->library()->setPlaylistTracks(m_scope.playlist, m_scope.tracks);
        set("sortenable", false);
    } else {
        ScreenModel::handle(signal, args);
    }
}

QVector<ScreenModel::MenuItem> BrowseModel::menu()
{
    QVector<MenuItem> items{{sonyText("dmpapp::ContentsViewModel", "010020"), [this] { navigate("SelectSettingTop"); }}};
    if (m_kind == Tracks && m_scope.playlist >= 0 && m_rows.size() > 1
        && app()->library()->playlists().value(m_scope.playlist).editable)
        items.append({sonyText("dmpapp::ContentsViewModel", "020033"), [this] { set("sortenable", true); }});
    return items;
}

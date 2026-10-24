#ifndef BROWSEMODEL_H
#define BROWSEMODEL_H

#include <QtCore/QAbstractListModel>
#include <QtCore/QSet>

#include "../../library/musicdb.h"
#include "screenmodel.h"

struct BrowseRow {
    QString name;
    QString additional;
    QString count;
    QString codec;
    QString duration;
    QString cover;
    QString sub1;
    QString sub2;
    QString icon;
    bool hiRes = false;
    bool editable = false;
};

// A song row as every track list shows it.
BrowseRow trackRow(const MusicDb *db, int id);
QString durationText(qint64 ms);

// The list the stock browse views bind to. A row's delegate draws itself only
// when IsExistContentsCacheRole turns true, as the stock model's asynchronous
// cache made it: every newly created delegate first reads false, and the role
// turns true for it on the next pass of the event loop.
class BrowseListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        CacheRole = Qt::UserRole + 1, NameRole, AdditionalRole, CountRole, CodecRole, DurationRole, HiResRole,
        CoverRole, Sub1Role, Sub2Role, ArtistCoverRole, ArtistSub1Role, ArtistSub2Role, StorageIconRole,
        FileMetaRole, FileMetaTypoRole, EditableRole, CounterRole
    };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void replace(const QVector<BrowseRow> &rows);
    const QVector<BrowseRow> &rows() const { return m_rows; }
    Q_INVOKABLE int count() const { return m_rows.size(); }
    Q_INVOKABLE int contents_count() const { return m_rows.size(); }
    // SCmnSortingItem drags rows with this.
    Q_INVOKABLE void move(int from, int to, int count = 1);

signals:
    void rowMoved(int from, int to);

private:
    void flush();
    QVector<BrowseRow> m_rows;
    mutable QSet<int> m_pending;
    mutable bool m_scheduled = false;
    bool m_flushing = false;
};

// What the level above narrowed a browse screen to.
struct BrowseScope {
    QString title;
    bool all = true;
    QVector<int> tracks;
    QString folder;
    QString storageIcon;
    int playlist = -1;
    // The lists this one was picked among and its place there, for the
    // player's All Range.
    QVector<QVector<int>> range;
    int list = -1;
};

// Every library list screen: the category tops, their artist, album and
// track levels, playlists, the Hi-Res lists, storages and eight folder levels.
class BrowseModel : public ScreenModel
{
    Q_OBJECT
public:
    enum Kind { Tracks, Albums, AlbumsWithAll, Artists, ArtistsWithAll, Categories, RecentAlbums, Playlists,
                HiResTop, Storages, Folders };

    BrowseModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    static bool handles(const QString &id);
    void setScope(const BrowseScope &scope);
    void activate() override;
    void handle(const QByteArray &signal, const QVariantList &args) override;
    QVector<MenuItem> menu() override;

private:
    struct Entry {
        QVector<int> tracks;
        QString title;
        QString folder;
        int track = -1;
        int playlist = -1;
        int condition = 0;
    };
    void rebuild();
    MusicDb::Field artistField() const;
    QVector<int> scopeTracks() const;
    void addGroups(const QVector<LibraryGroup> &groups, bool withAll, bool artists);
    void addTrackRows(const QVector<int> &tracks);
    void open(int index);
    void contextMenu(int index);
    QVector<int> rowTracks(int index) const;

    Kind m_kind;
    BrowseScope m_scope;
    BrowseListModel *m_list;
    QVector<BrowseRow> m_rows;
    QVector<Entry> m_entries;
    int m_built = -1;
    MusicDb::Field m_artists = MusicDb::AlbumArtist;
};

#endif

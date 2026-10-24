#ifndef MUSICDB_H
#define MUSICDB_H

#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtCore/QVector>
#include <QtGui/QImage>

#include <functional>

#include "tagreader.h"

struct Storage {
    QString name;
    QString path;
    QString icon;
};

struct LibraryTrack {
    QString path;
    QString folder;
    int storage = 0;
    qint64 modified = 0;
    qint64 size = 0;
    TrackTags tags;

    QString title() const;
    QString albumArtist() const { return tags.albumArtist.isEmpty() ? tags.artist : tags.albumArtist; }
    bool isHiRes() const { return tags.bitsPerSample > 16 || tags.sampleRate > 48000; }
};

// A row of a grouped list: an album, an artist, a genre, a year, ...
struct LibraryGroup {
    QString name;
    QString detail;
    QVector<int> tracks;
    int cover = -1;
};

struct FolderEntry {
    QString name;
    QString path;
    bool folder = false;
    int track = -1;
    int count = 0;
    int cover = -1;
};

struct Playlist {
    QString name;
    QString path;
    bool editable = false;
    QVector<int> tracks;
};

class LibraryScanThread;

// The music database the stock app kept in its own DB: every audio file under
// the storages, read once and cached by path, size and modification time.
class MusicDb : public QObject
{
    Q_OBJECT
public:
    using Predicate = std::function<bool(int id)>;
    enum Field { Artist, AlbumArtist, Genre, Composer, Year };

    MusicDb(const QString &dataDir, QObject *parent = nullptr);
    ~MusicDb() override;

    void setStorages(const QVector<Storage> &storages);
    QVector<Storage> storages() const { return m_storages; }
    void rescan();
    bool busy() const { return m_worker; }
    int counter() const { return m_counter; }

    int count() const { return m_tracks.size(); }
    const LibraryTrack &track(int id) const { return m_tracks.at(id); }
    bool valid(int id) const { return id >= 0 && id < m_tracks.size(); }
    int find(const QString &path) const { return m_byPath.value(path, -1); }

    // Track ids, by title unless the order is given by the grouping.
    QVector<int> select(const Predicate &match = {}) const;
    QVector<LibraryGroup> albums(const Predicate &match = {}) const;
    QVector<LibraryGroup> group(Field field, const Predicate &match = {}) const;
    QVector<LibraryGroup> recentAlbums() const;
    QVector<FolderEntry> folder(const QString &path) const;
    int storageTracks(int storage) const;
    // The first letter a scroll balloon shows for a name (Hangul by initial consonant).
    static QString initial(const QString &name);

    QVector<Playlist> playlists() const { return m_playlists; }
    bool createPlaylist(const QString &name, const QVector<int> &tracks);
    bool addToPlaylist(int playlist, const QVector<int> &tracks);
    bool removeFromPlaylist(int playlist, int row);
    bool setPlaylistTracks(int playlist, const QVector<int> &tracks);
    bool renamePlaylist(int playlist, const QString &name);
    bool deletePlaylist(int playlist);

    // Embedded cover, else a folder image, scaled to fit size. Thread safe.
    QImage cover(int id, int size) const;
    bool hasCover(int id) const;

signals:
    void changed();
    void busyChanged();

private:
    void finished();
    void loadPlaylists(const QStringList &files);
    bool savePlaylist(const Playlist &playlist) const;
    QString playlistDir() const;
    void sortByTitle(QVector<int> *ids) const;
    void sortGroups(QVector<LibraryGroup> *groups) const;

    const QString m_dataDir;
    QVector<Storage> m_storages;
    QVector<LibraryTrack> m_tracks;
    QHash<QString, int> m_byPath;
    QVector<Playlist> m_playlists;
    QPointer<QThread> m_worker;
    bool m_again = false;
    int m_counter = 0;
    mutable QMutex m_coverMutex;
    mutable QHash<QString, QImage> m_folderCovers;
};

#endif

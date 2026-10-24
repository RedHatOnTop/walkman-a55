#include "musicdb.h"

#include <QtCore/QCollator>
#include <QtCore/QDataStream>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMutexLocker>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QTextStream>
#include <QtGui/QImageReader>

#include <algorithm>
#include <numeric>

namespace {

const QStringList audioSuffixes{"mp3", "flac", "m4a", "mp4", "ogg", "oga", "opus", "wav", "aif", "aiff"};
const QStringList playlistSuffixes{"m3u", "m3u8"};
const QStringList folderImages{"cover.jpg", "folder.jpg", "front.jpg", "cover.png", "folder.png", "albumart.jpg"};
// Stock limit for folder browsing.
const int folderDepth = 8;
const quint32 cacheMagic = 0x574d4442;
// Bumped when tag decoding changes, so cached tags are read again (2: CP949 text, 3: SensMe,
// 4: SensMe in FLAC and MP4, 5: YYMMDD and junk years).
const quint32 cacheVersion = 5;

QDataStream &operator<<(QDataStream &out, const TrackTags &t)
{
    return out << t.title << t.artist << t.albumArtist << t.album << t.genre << t.composer << t.year << t.track
               << t.disc << t.durationMs << t.codec << t.sampleRate << t.bitsPerSample << t.bitrate << t.channels
               << t.hasCover << t.lyrics << t.sensme << t.highlightMs;
}

QDataStream &operator>>(QDataStream &in, TrackTags &t)
{
    return in >> t.title >> t.artist >> t.albumArtist >> t.album >> t.genre >> t.composer >> t.year >> t.track
              >> t.disc >> t.durationMs >> t.codec >> t.sampleRate >> t.bitsPerSample >> t.bitrate >> t.channels
              >> t.hasCover >> t.lyrics >> t.sensme >> t.highlightMs;
}

QCollator collator()
{
    QCollator c;
    c.setNumericMode(true);
    c.setCaseSensitivity(Qt::CaseInsensitive);
    return c;
}

// Unknown names sort last, as the stock lists do.
bool nameLess(const QCollator &c, const QString &a, const QString &b)
{
    if (a.isEmpty() != b.isEmpty())
        return b.isEmpty();
    return c.compare(a, b) < 0;
}

bool skipDir(const QString &name)
{
    return name.startsWith('.') || name.compare("Android", Qt::CaseInsensitive) == 0
        || name.compare("LOST.DIR", Qt::CaseInsensitive) == 0;
}

}

QString LibraryTrack::title() const
{
    return tags.title.isEmpty() ? QFileInfo(path).completeBaseName() : tags.title;
}

struct ScanResult {
    QVector<LibraryTrack> tracks;
    QStringList playlists;
};

class LibraryScanThread : public QThread
{
public:
    LibraryScanThread(const QVector<Storage> &storages, const QString &cache, QObject *parent)
        : QThread(parent), m_storages(storages), m_cache(cache) {}
    ScanResult result;

protected:
    void run() override
    {
        QHash<QString, LibraryTrack> cached = readCache();
        for (int i = 0; i < m_storages.size(); ++i)
            walk(m_storages.at(i).path, i, 1, cached);
        writeCache(cached);
    }

private:
    void walk(const QString &path, int storage, int depth, const QHash<QString, LibraryTrack> &cached)
    {
        if (isInterruptionRequested())
            return;
        const QDir dir(path);
        const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const QFileInfo &entry : entries) {
            if (entry.isDir()) {
                if (depth < folderDepth + 1 && !skipDir(entry.fileName()))
                    walk(entry.filePath(), storage, depth + 1, cached);
                continue;
            }
            const QString suffix = entry.suffix().toLower();
            if (playlistSuffixes.contains(suffix)) {
                result.playlists.append(entry.filePath());
                continue;
            }
            if (!audioSuffixes.contains(suffix))
                continue;
            LibraryTrack track;
            track.path = entry.filePath();
            track.folder = entry.path();
            track.storage = storage;
            track.modified = entry.lastModified().toMSecsSinceEpoch();
            track.size = entry.size();
            const auto hit = cached.constFind(track.path);
            if (hit != cached.constEnd() && hit->modified == track.modified && hit->size == track.size)
                track.tags = hit->tags;
            else if (!readTags(track.path, &track.tags))
                continue;
            result.tracks.append(track);
        }
    }

    QHash<QString, LibraryTrack> readCache() const
    {
        QHash<QString, LibraryTrack> tracks;
        QFile file(m_cache);
        if (!file.open(QIODevice::ReadOnly))
            return tracks;
        QDataStream in(&file);
        quint32 magic = 0, version = 0;
        qint32 count = 0;
        in >> magic >> version >> count;
        if (magic != cacheMagic || version != cacheVersion || count < 0)
            return tracks;
        for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
            LibraryTrack track;
            in >> track.path >> track.modified >> track.size >> track.tags;
            tracks.insert(track.path, track);
        }
        if (in.status() != QDataStream::Ok)
            tracks.clear();
        return tracks;
    }

    // Tracks on a storage that is not there this time, such as a card vold has not mounted
    // yet, keep their tags for its return: reading them again took minutes for 10,000 tracks.
    void writeCache(const QHash<QString, LibraryTrack> &cached) const
    {
        if (isInterruptionRequested())
            return;
        QVector<const LibraryTrack *> away;
        for (const LibraryTrack &track : cached) {
            if (!scanned(track.path))
                away.append(&track);
        }
        QSaveFile file(m_cache);
        if (!file.open(QIODevice::WriteOnly))
            return;
        QDataStream out(&file);
        out << cacheMagic << cacheVersion << qint32(result.tracks.size() + away.size());
        for (const LibraryTrack &track : result.tracks)
            out << track.path << track.modified << track.size << track.tags;
        for (const LibraryTrack *track : away)
            out << track->path << track->modified << track->size << track->tags;
        file.commit();
    }

    bool scanned(const QString &path) const
    {
        for (const Storage &storage : m_storages) {
            if (path.startsWith(storage.path + '/'))
                return true;
        }
        return false;
    }

    const QVector<Storage> m_storages;
    const QString m_cache;
};

MusicDb::MusicDb(const QString &dataDir, QObject *parent) : QObject(parent), m_dataDir(dataDir)
{
    QDir().mkpath(m_dataDir);
}

MusicDb::~MusicDb()
{
    if (m_worker) {
        m_worker->requestInterruption();
        m_worker->wait();
    }
}

void MusicDb::setStorages(const QVector<Storage> &storages)
{
    // A card's unmount and its removal arrive as two broadcasts for one change.
    const auto paths = [](const QVector<Storage> &list) {
        QStringList out;
        for (const Storage &storage : list)
            out << storage.path;
        return out;
    };
    if (!m_storages.isEmpty() && paths(storages) == paths(m_storages))
        return;
    m_storages = storages;
    rescan();
}

void MusicDb::rescan()
{
    if (m_worker) {
        m_again = true;
        return;
    }
    m_worker = new LibraryScanThread(m_storages, m_dataDir + "/library.cache", this);
    connect(m_worker, &QThread::finished, this, &MusicDb::finished);
    m_worker->start(QThread::LowPriority);
    emit busyChanged();
}

void MusicDb::finished()
{
    auto *worker = static_cast<LibraryScanThread *>(m_worker.data());
    ScanResult result = std::move(worker->result);
    worker->deleteLater();
    m_worker.clear();
    {
        QMutexLocker lock(&m_coverMutex);
        m_tracks = std::move(result.tracks);
        m_folderCovers.clear();
    }
    m_byPath.clear();
    for (int i = 0; i < m_tracks.size(); ++i)
        m_byPath.insert(m_tracks.at(i).path, i);
    loadPlaylists(result.playlists);
    ++m_counter;
    emit changed();
    emit busyChanged();
    if (m_again) {
        m_again = false;
        rescan();
    }
}

void MusicDb::sortByTitle(QVector<int> *ids) const
{
    const QCollator c = collator();
    std::stable_sort(ids->begin(), ids->end(), [this, &c](int a, int b) {
        return nameLess(c, m_tracks.at(a).title(), m_tracks.at(b).title());
    });
}

void MusicDb::sortGroups(QVector<LibraryGroup> *groups) const
{
    const QCollator c = collator();
    std::stable_sort(groups->begin(), groups->end(), [&c](const LibraryGroup &a, const LibraryGroup &b) {
        return nameLess(c, a.name, b.name);
    });
}

QVector<int> MusicDb::select(const Predicate &match) const
{
    QVector<int> ids;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (!match || match(i))
            ids.append(i);
    }
    sortByTitle(&ids);
    return ids;
}

QVector<LibraryGroup> MusicDb::albums(const Predicate &match) const
{
    QHash<QString, int> index;
    QVector<LibraryGroup> groups;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const LibraryTrack &track = m_tracks.at(i);
        if (match && !match(i))
            continue;
        const QString key = track.tags.album + QChar(0x1f) + track.tags.albumArtist;
        auto it = index.find(key);
        if (it == index.end()) {
            it = index.insert(key, groups.size());
            groups.append({track.tags.album, track.albumArtist(), {}, -1});
        }
        LibraryGroup &group = groups[it.value()];
        group.tracks.append(i);
        if (group.cover < 0 && hasCover(i))
            group.cover = i;
    }
    for (LibraryGroup &group : groups) {
        std::stable_sort(group.tracks.begin(), group.tracks.end(), [this](int a, int b) {
            const TrackTags &x = m_tracks.at(a).tags;
            const TrackTags &y = m_tracks.at(b).tags;
            if (x.disc != y.disc)
                return x.disc < y.disc;
            if (x.track != y.track)
                return x.track < y.track;
            return m_tracks.at(a).path < m_tracks.at(b).path;
        });
        if (group.cover < 0)
            group.cover = group.tracks.first();
    }
    sortGroups(&groups);
    return groups;
}

QVector<LibraryGroup> MusicDb::group(Field field, const Predicate &match) const
{
    QHash<QString, int> index;
    QVector<LibraryGroup> groups;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const LibraryTrack &track = m_tracks.at(i);
        if (match && !match(i))
            continue;
        QString name;
        switch (field) {
        case Artist: name = track.tags.artist; break;
        case AlbumArtist: name = track.albumArtist(); break;
        case Genre: name = track.tags.genre; break;
        case Composer: name = track.tags.composer; break;
        case Year: name = track.tags.year > 0 ? QString::number(track.tags.year) : QString(); break;
        }
        auto it = index.find(name);
        if (it == index.end()) {
            it = index.insert(name, groups.size());
            groups.append({name, {}, {}, -1});
        }
        LibraryGroup &group = groups[it.value()];
        group.tracks.append(i);
        if (group.cover < 0 && hasCover(i))
            group.cover = i;
    }
    for (LibraryGroup &group : groups)
        sortByTitle(&group.tracks);
    sortGroups(&groups);
    return groups;
}

QVector<LibraryGroup> MusicDb::recentAlbums() const
{
    QVector<LibraryGroup> groups = albums();
    QHash<int, qint64> newest;
    for (int g = 0; g < groups.size(); ++g) {
        qint64 latest = 0;
        for (int id : groups.at(g).tracks)
            latest = qMax(latest, m_tracks.at(id).modified);
        newest.insert(g, latest);
    }
    QVector<int> order(groups.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&newest](int a, int b) { return newest.value(a) > newest.value(b); });
    QVector<LibraryGroup> sorted;
    for (int g : order)
        sorted.append(groups.at(g));
    return sorted;
}

QVector<FolderEntry> MusicDb::folder(const QString &path) const
{
    const QString prefix = path.endsWith('/') ? path : path + '/';
    QHash<QString, int> folders;
    QVector<FolderEntry> dirs;
    QVector<FolderEntry> files;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const LibraryTrack &track = m_tracks.at(i);
        if (track.folder == path) {
            files.append({QFileInfo(track.path).fileName(), track.path, false, i, 0, i});
            continue;
        }
        if (!track.folder.startsWith(prefix))
            continue;
        const QString name = track.folder.mid(prefix.size()).section('/', 0, 0);
        auto it = folders.find(name);
        if (it == folders.end()) {
            it = folders.insert(name, dirs.size());
            dirs.append({name, prefix + name, true, -1, 0, -1});
        }
        FolderEntry &entry = dirs[it.value()];
        if (track.folder == entry.path)
            ++entry.count;
        if (entry.cover < 0 && hasCover(i))
            entry.cover = i;
    }
    // A folder's count is its direct subfolders plus its songs, as the stock list shows.
    for (FolderEntry &entry : dirs) {
        QSet<QString> children;
        const QString inner = entry.path + '/';
        for (const LibraryTrack &track : m_tracks) {
            if (track.folder.startsWith(inner))
                children.insert(track.folder.mid(inner.size()).section('/', 0, 0));
        }
        entry.count += children.size();
    }
    const QCollator c = collator();
    const auto byName = [&c](const FolderEntry &a, const FolderEntry &b) { return c.compare(a.name, b.name) < 0; };
    std::stable_sort(dirs.begin(), dirs.end(), byName);
    std::stable_sort(files.begin(), files.end(), byName);
    return dirs + files;
}

int MusicDb::storageTracks(int storage) const
{
    int count = 0;
    for (const LibraryTrack &track : m_tracks)
        count += track.storage == storage;
    return count;
}

QString MusicDb::initial(const QString &name)
{
    if (name.isEmpty())
        return {};
    const QChar first = name.at(0);
    const ushort code = first.unicode();
    if (code >= 0xac00 && code <= 0xd7a3) {
        static const ushort choseong[] = {0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141,
                                          0x3142, 0x3143, 0x3145, 0x3146, 0x3147, 0x3148, 0x3149,
                                          0x314a, 0x314b, 0x314c, 0x314d, 0x314e};
        return QString(QChar(choseong[(code - 0xac00) / 588]));
    }
    if (first.isLetter())
        return QString(first.toUpper());
    return first.isDigit() ? QStringLiteral("#") : QString(first);
}

QString MusicDb::playlistDir() const
{
    return m_dataDir + "/playlists";
}

void MusicDb::loadPlaylists(const QStringList &files)
{
    m_playlists.clear();
    QStringList all;
    for (const QString &file : files)
        all.append(file);
    const QDir own(playlistDir());
    for (const QString &name : own.entryList({"*.m3u8"}, QDir::Files))
        all.append(own.filePath(name));
    for (const QString &file : all) {
        QFile input(file);
        if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        Playlist playlist;
        playlist.name = QFileInfo(file).completeBaseName();
        playlist.path = file;
        playlist.editable = file.startsWith(playlistDir());
        QTextStream stream(&input);
        stream.setCodec("UTF-8");
        const QDir base = QFileInfo(file).dir();
        while (!stream.atEnd()) {
            QString line = stream.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            line.replace('\\', '/');
            const int id = find(QDir::cleanPath(QDir::isAbsolutePath(line) ? line : base.filePath(line)));
            if (id >= 0)
                playlist.tracks.append(id);
        }
        m_playlists.append(playlist);
    }
    const QCollator c = collator();
    std::stable_sort(m_playlists.begin(), m_playlists.end(), [&c](const Playlist &a, const Playlist &b) {
        return c.compare(a.name, b.name) < 0;
    });
}

bool MusicDb::savePlaylist(const Playlist &playlist) const
{
    QDir().mkpath(playlistDir());
    QSaveFile file(playlist.path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "#EXTM3U\n";
    for (int id : playlist.tracks)
        out << m_tracks.at(id).path << '\n';
    out.flush();
    return file.commit();
}

bool MusicDb::createPlaylist(const QString &name, const QVector<int> &tracks)
{
    Playlist playlist;
    playlist.name = name;
    playlist.path = playlistDir() + '/' + QString(name).replace('/', '_') + ".m3u8";
    playlist.editable = true;
    playlist.tracks = tracks;
    if (QFileInfo::exists(playlist.path) || !savePlaylist(playlist))
        return false;
    m_playlists.append(playlist);
    ++m_counter;
    emit changed();
    return true;
}

bool MusicDb::addToPlaylist(int index, const QVector<int> &tracks)
{
    if (index < 0 || index >= m_playlists.size() || !m_playlists.at(index).editable)
        return false;
    Playlist updated = m_playlists.at(index);
    updated.tracks += tracks;
    if (!savePlaylist(updated))
        return false;
    m_playlists[index] = updated;
    ++m_counter;
    emit changed();
    return true;
}

bool MusicDb::removeFromPlaylist(int index, int row)
{
    if (index < 0 || index >= m_playlists.size() || !m_playlists.at(index).editable)
        return false;
    Playlist updated = m_playlists.at(index);
    if (row < 0 || row >= updated.tracks.size())
        return false;
    updated.tracks.removeAt(row);
    if (!savePlaylist(updated))
        return false;
    m_playlists[index] = updated;
    ++m_counter;
    emit changed();
    return true;
}

bool MusicDb::setPlaylistTracks(int index, const QVector<int> &tracks)
{
    if (index < 0 || index >= m_playlists.size() || !m_playlists.at(index).editable)
        return false;
    Playlist updated = m_playlists.at(index);
    updated.tracks = tracks;
    if (!savePlaylist(updated))
        return false;
    m_playlists[index] = updated;
    ++m_counter;
    emit changed();
    return true;
}

bool MusicDb::renamePlaylist(int index, const QString &name)
{
    if (index < 0 || index >= m_playlists.size() || !m_playlists.at(index).editable)
        return false;
    Playlist &playlist = m_playlists[index];
    const QString target = playlistDir() + '/' + QString(name).replace('/', '_') + ".m3u8";
    if (QFileInfo::exists(target) || !QFile::rename(playlist.path, target))
        return false;
    playlist.name = name;
    playlist.path = target;
    ++m_counter;
    emit changed();
    return true;
}

bool MusicDb::deletePlaylist(int index)
{
    if (index < 0 || index >= m_playlists.size() || !m_playlists.at(index).editable)
        return false;
    if (!QFile::remove(m_playlists.at(index).path))
        return false;
    m_playlists.removeAt(index);
    ++m_counter;
    emit changed();
    return true;
}

bool MusicDb::hasCover(int id) const
{
    return m_tracks.at(id).tags.hasCover;
}

QImage MusicDb::cover(int id, int size) const
{
    QString path;
    QString folder;
    bool embedded = false;
    {
        QMutexLocker lock(&m_coverMutex);
        if (id < 0 || id >= m_tracks.size())
            return {};
        path = m_tracks.at(id).path;
        folder = m_tracks.at(id).folder;
        embedded = m_tracks.at(id).tags.hasCover;
    }
    QImage image;
    if (embedded)
        image.loadFromData(readCover(path));
    if (image.isNull()) {
        QMutexLocker lock(&m_coverMutex);
        const auto hit = m_folderCovers.constFind(folder);
        if (hit != m_folderCovers.constEnd()) {
            image = hit.value();
        } else {
            const QDir dir(folder);
            const QStringList found = dir.entryList(folderImages, QDir::Files);
            if (!found.isEmpty()) {
                QImageReader reader(dir.filePath(found.first()));
                const QSize full = reader.size();
                if (full.isValid())
                    reader.setScaledSize(full.scaled(400, 400, Qt::KeepAspectRatio));
                image = reader.read();
            }
            m_folderCovers.insert(folder, image);
        }
    }
    if (!image.isNull() && size > 0 && (image.width() > size || image.height() > size))
        image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

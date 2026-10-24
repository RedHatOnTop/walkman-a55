#include "player.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QRandomGenerator>
#include <QtCore/QUrl>

#include "../library/lyrics.h"
#include "../library/musicdb.h"
#include "platform.h"

namespace {

QString qobjectText(const char *id)
{
    return QCoreApplication::translate("QObject", id);
}

QString rate(int hz)
{
    const double khz = hz / 1000.0;
    return QString::number(khz, 'f', hz % 1000 ? 1 : 0) + "kHz";
}

}

constexpr int PlayerController::avlsLimit;

PlayerController::PlayerController(QMediaPlayer *media, MusicDb *db, QSettings *settings, QObject *parent)
    : QObject(parent), m_media(media), m_db(db), m_settings(settings)
{
    m_avls = settings->value("playback/avls", false).toBool();
    setVolume(settings->value("playback/volume", 40).toInt());
    m_shuffle = settings->value("playback/shuffle", false).toBool();
    m_repeat = qBound(0, settings->value("playback/repeat", 0).toInt(), 2);
    m_allRange = settings->value("playback/allRange", true).toBool();
    m_media->setAudioRole(QAudio::MusicRole);
    m_media->setNotifyInterval(250);
    connect(m_media, &QMediaPlayer::positionChanged, this, &PlayerController::positionChanged);
    connect(m_media, &QMediaPlayer::durationChanged, this, &PlayerController::trackChanged);
    connect(m_media, &QMediaPlayer::seekableChanged, this, [this](bool seekable) {
        if (seekable && m_pendingSeek > 0) {
            m_media->setPosition(m_pendingSeek);
            m_pendingSeek = -1;
        }
    });
    connect(m_media, &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State state) {
        if (state != QMediaPlayer::StoppedState)
            m_stopped = false;
        emit transportChanged();
        emit statusChanged();
        save();
    });
    connect(m_media, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        emit statusChanged();
        if (status == QMediaPlayer::EndOfMedia) {
            QTimer::singleShot(0, this, [this] {
                if (m_media->mediaStatus() == QMediaPlayer::EndOfMedia)
                    move(1, true);
            });
        }
    });
    connect(m_media, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this](QMediaPlayer::Error error) {
        if (error == QMediaPlayer::FormatError)
            emit failed(qobjectText("020195"));
        else if (error == QMediaPlayer::ResourceError && hasTrack() && !QFileInfo::exists(m_db->track(m_track).path))
            emit failed(qobjectText("020253"));
        else if (error != QMediaPlayer::NoError)
            emit failed(qobjectText("020258"));
    });
    connect(m_db, &MusicDb::changed, this, &PlayerController::libraryChanged);
    m_scan.setInterval(200);
    connect(&m_scan, &QTimer::timeout, this, [this] {
        if (m_media->isSeekable())
            m_media->setPosition(qBound<qint64>(0, m_media->position() + m_scanDirection * 3000, m_media->duration()));
    });
    m_saveTimer.setInterval(2000);
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &PlayerController::save);
    connect(m_media, &QMediaPlayer::positionChanged, &m_saveTimer, [this] {
        if (!m_saveTimer.isActive())
            m_saveTimer.start();
    });
}

qint64 PlayerController::position() const
{
    return m_pendingSeek >= 0 ? m_pendingSeek : m_media->position();
}

qint64 PlayerController::duration() const
{
    if (m_media->duration() > 0)
        return m_media->duration();
    return hasTrack() ? m_db->track(m_track).tags.durationMs : 0;
}

QString PlayerController::title() const
{
    return hasTrack() ? m_db->track(m_track).title() : QString();
}

QString PlayerController::artist() const
{
    return hasTrack() ? m_db->track(m_track).tags.artist : QString();
}

QString PlayerController::album() const
{
    return hasTrack() ? m_db->track(m_track).tags.album : QString();
}

bool PlayerController::hiRes() const
{
    return hasTrack() && m_db->track(m_track).isHiRes();
}

QString PlayerController::qualityText() const
{
    if (!hasTrack())
        return {};
    const TrackTags &tags = m_db->track(m_track).tags;
    if (tags.bitsPerSample > 0 && tags.sampleRate > 0)
        return QString("%1 %2/%3bit").arg(tags.codec, rate(tags.sampleRate)).arg(tags.bitsPerSample);
    if (tags.bitrate > 0)
        return QString("%1 %2 kbps").arg(tags.codec).arg(tags.bitrate);
    return tags.codec;
}

QVector<int> PlayerController::queue() const
{
    QVector<int> ids;
    for (int index : m_queue.order)
        ids.append(m_tracks.value(index, -1));
    return ids;
}

QVariantMap PlayerController::transport() const
{
    return {{"objectName", "basicPlayerControls"}, {"dispMode", 1}, {"playTitle", title()},
            {"playState", playing()}, {"playerButtonEnabled", hasTrack()}, {"shuffleState", m_shuffle},
            {"shuffleVisible", true}, {"repeatModeState", m_repeat}, {"repeatModeVisible", true}};
}

QVariantMap PlayerController::metadata() const
{
    const bool lyrics = hasTrack()
        && (!m_db->track(m_track).tags.lyrics.isEmpty() || hasLyricsFile(m_db->track(m_track).path));
    return {{"play_title", title()}, {"artist_name", artist()}, {"album_name", album()},
            {"is_high_resolution", hiRes()}, {"sound_quality_string", qualityText()},
            {"lyrics_status", lyrics ? 2 : 0}};
}

// Loading another track passes through StoppedState too, so only stop()
// ends the session; next while paused must keep the media keys working, and a
// track restored at launch gets them before anything has played (the play
// button on the lock screen or with the screen off).
bool PlayerController::sessionActive() const
{
    return hasTrack() && !m_stopped;
}

void PlayerController::playTracks(const QVector<int> &tracks, int index, bool autoplay,
                                  const QVector<QVector<int>> &range, int list)
{
    if (index < 0 || index >= tracks.size())
        return;
    m_range = list >= 0 && list < range.size() ? range : QVector<QVector<int>>();
    m_list = m_range.isEmpty() ? -1 : list;
    setQueue(tracks, index, -1);
    loadCurrent(autoplay);
}

void PlayerController::playChannel(int channel, const QVector<int> &tracks, qint64 startMs)
{
    if (tracks.isEmpty())
        return;
    m_range.clear();
    m_list = -1;
    setQueue(tracks, 0, channel);
    loadCurrent(true, startMs);
}

void PlayerController::setQueue(const QVector<int> &tracks, int index, int channel)
{
    m_channel = channel;
    m_tracks = tracks;
    m_paths.clear();
    for (int id : tracks)
        m_paths.append(m_db->track(id).path);
    m_queue = withShuffle(withRepeat(createQueue(tracks.size(), index), m_repeat), m_shuffle,
                          QRandomGenerator::global()->generate());
    emit queueChanged();
}

void PlayerController::playQueueRow(int row)
{
    if (row < 0 || row >= m_queue.order.size())
        return;
    m_queue.cursor = row;
    loadCurrent(true);
}

bool PlayerController::loadCurrent(bool autoplay, qint64 position)
{
    const int index = queueTrack(m_queue);
    if (index < 0 || index >= m_tracks.size() || !m_db->valid(m_tracks.at(index))) {
        m_track = -1;
        m_media->setMedia(QMediaContent());
        emit trackChanged();
        emit transportChanged();
        return false;
    }
    m_track = m_tracks.at(index);
    m_pendingSeek = position > 0 ? position : -1;
    m_media->setMedia(QUrl::fromLocalFile(m_db->track(m_track).path));
    emit queueChanged();
    emit trackChanged();
    emit transportChanged();
    if (autoplay && requestPlaybackFocus())
        m_media->play();
    save();
    return true;
}

void PlayerController::togglePlayback()
{
    if (playing()) {
        pause();
        return;
    }
    if (!hasTrack()) {
        if (m_db->count() == 0)
            emit failed(qobjectText("020200"));
        else
            playTracks(m_db->select(), 0);
        return;
    }
    if (m_media->mediaStatus() == QMediaPlayer::EndOfMedia)
        m_media->setPosition(0);
    if (requestPlaybackFocus())
        m_media->play();
}

void PlayerController::pause()
{
    m_media->pause();
}

void PlayerController::stop()
{
    save();
    m_stopped = true;
    m_media->stop();
    releasePlaybackFocus();
}

void PlayerController::move(int direction, bool automatic)
{
    if (automatic && m_allRange && m_queue.repeat != 1 && m_queue.cursor == m_queue.order.size() - 1
        && playNextList())
        return;
    const QueueState next = advanceQueue(m_queue, direction, automatic);
    if (queueTrack(next) < 0) {
        if (automatic) {
            // The end of the queue: stop on its first track, as the stock player does.
            m_queue.cursor = 0;
            loadCurrent(false);
        }
        return;
    }
    if (automatic && queueTrack(next) == queueTrack(m_queue)) {
        m_media->setPosition(0);
        m_media->play();
        return;
    }
    const bool autoplay = automatic || playing();
    m_queue = next;
    loadCurrent(autoplay);
}

// All Range: the end of a list goes on with the next list of its range;
// with repeat all the range wraps, as one list would.
bool PlayerController::playNextList()
{
    if (m_range.size() < 2)
        return false;
    int list = m_list + 1;
    if (list >= m_range.size()) {
        if (m_queue.repeat != 2)
            return false;
        list = 0;
    }
    const QVector<QVector<int>> range = m_range;
    playTracks(range.at(list), 0, true, range, list);
    return true;
}

void PlayerController::next()
{
    move(1, false);
}

void PlayerController::previous()
{
    if (position() > 3000 && seekable()) {
        m_media->setPosition(0);
        return;
    }
    move(-1, false);
}

void PlayerController::seek(qint64 milliseconds)
{
    if (!hasTrack() || !seekable() || milliseconds < 0 || milliseconds > duration()) {
        emit seekRejected();
        return;
    }
    m_media->setPosition(milliseconds);
}

void PlayerController::setVolume(int value)
{
    m_volume = qBound(0, value, m_avls ? avlsLimit : 120);
    const qreal gain = QAudio::convertVolume(m_volume / 120.0, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale);
    m_media->setVolume(qRound(gain * 100));
    m_settings->setValue("playback/volume", m_volume);
    emit settingsChanged();
}

void PlayerController::setAvls(bool value)
{
    m_avls = value;
    m_settings->setValue("playback/avls", value);
    setVolume(m_volume);
}

void PlayerController::setShuffle(bool value)
{
    m_shuffle = value;
    m_queue = withShuffle(m_queue, value, QRandomGenerator::global()->generate());
    m_settings->setValue("playback/shuffle", value);
    emit settingsChanged();
    emit transportChanged();
    emit queueChanged();
    save();
}

void PlayerController::setRepeatMode(int value)
{
    m_repeat = qBound(0, value, 2);
    m_queue = withRepeat(m_queue, m_repeat);
    m_settings->setValue("playback/repeat", m_repeat);
    emit settingsChanged();
    emit transportChanged();
}

void PlayerController::cycleRepeat()
{
    static const int next[] = {2, 0, 1};
    setRepeatMode(next[m_repeat]);
}

void PlayerController::setAllRange(bool value)
{
    m_allRange = value;
    m_settings->setValue("playback/allRange", value);
    emit settingsChanged();
}

void PlayerController::startScan(int direction)
{
    m_scanDirection = direction < 0 ? -1 : 1;
    m_scan.start();
}

void PlayerController::stopScan()
{
    m_scan.stop();
}

void PlayerController::platformError(const QString &message)
{
    m_media->pause();
    emit failed(message);
}

void PlayerController::save()
{
    if (!m_restored)
        return;
    QStringList order;
    for (int index : m_queue.order)
        order.append(QString::number(index));
    m_settings->setValue("playback/queue", m_paths);
    m_settings->setValue("playback/order", order);
    m_settings->setValue("playback/cursor", m_queue.cursor);
    m_settings->setValue("playback/shuffled", m_queue.shuffled);
    m_settings->setValue("playback/position", position());
    m_settings->setValue("playback/channel", m_channel);
}

void PlayerController::restore()
{
    m_paths = m_settings->value("playback/queue").toStringList();
    const QStringList order = m_settings->value("playback/order").toStringList();
    m_tracks.clear();
    for (const QString &path : m_paths)
        m_tracks.append(m_db->find(path));
    m_queue = QueueState();
    for (const QString &index : order) {
        const int value = index.toInt();
        if (value >= 0 && value < m_tracks.size())
            m_queue.order.append(value);
    }
    if (m_queue.order.size() != m_tracks.size())
        m_queue = createQueue(m_tracks.size(), 0);
    m_queue.cursor = qBound(-1, m_settings->value("playback/cursor", 0).toInt(), m_queue.order.size() - 1);
    m_queue.repeat = m_repeat;
    m_queue.shuffled = m_settings->value("playback/shuffled", false).toBool() && m_shuffle;
    m_channel = m_settings->value("playback/channel", -1).toInt();
    m_restored = true;
    emit queueChanged();
    loadCurrent(false, m_settings->value("playback/position", 0).toLongLong());
}

void PlayerController::libraryChanged()
{
    if (!m_restored) {
        restore();
        return;
    }
    // Ids are positions in the rescanned library; follow the files. The
    // range's other lists are ids too, and end with the current one.
    for (int i = 0; i < m_paths.size(); ++i)
        m_tracks[i] = m_db->find(m_paths.at(i));
    m_range.clear();
    m_list = -1;
    const int current = queueTrack(m_queue);
    const int id = current >= 0 ? m_tracks.at(current) : -1;
    if (id != m_track) {
        m_track = id;
        if (id < 0)
            stop();
        emit trackChanged();
        emit transportChanged();
    }
    emit queueChanged();
}

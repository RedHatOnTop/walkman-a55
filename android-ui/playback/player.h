#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>
#include <QtMultimedia/QMediaPlayer>

#include "queue.h"

class MusicDb;

// Playback of a queue of library tracks. Repeat modes follow the stock
// RepeatMode enum: 0 off, 1 the current track, 2 all.
class PlayerController : public QObject
{
    Q_OBJECT
public:
    PlayerController(QMediaPlayer *media, MusicDb *db, QSettings *settings, QObject *parent = nullptr);

    bool playing() const { return m_media->state() == QMediaPlayer::PlayingState; }
    bool hasTrack() const { return m_track >= 0; }
    int track() const { return m_track; }
    qint64 position() const;
    qint64 duration() const;
    bool seekable() const { return m_media->isSeekable(); }
    QString title() const;
    QString artist() const;
    QString album() const;
    // "FLAC 96kHz/24bit", "MP3 320 kbps": the stock sound quality label.
    QString qualityText() const;
    bool hiRes() const;
    // The stock 0-120 scale, mapped onto a logarithmic gain.
    int volume() const { return m_volume; }
    // AVLS caps the volume at avlsLimit (the stock app's AVLS maximum).
    static constexpr int avlsLimit = 58;
    bool avls() const { return m_avls; }
    bool shuffle() const { return m_shuffle; }
    int repeatMode() const { return m_repeat; }
    // Playback Range: All Range plays on into the next list of the range a
    // track was picked from; Selected Range stops with the track's own list.
    bool allRange() const { return m_allRange; }
    // The queue in play order (shuffled when shuffle is on).
    QVector<int> queue() const;
    int queueIndex() const { return m_queue.cursor; }
    int queueCount() const { return m_queue.order.size(); }
    QVariantMap transport() const;
    QVariantMap metadata() const;
    bool sessionActive() const;

    // range: the lists the tracks' list was picked among (the albums of an
    // album list, say), list: its place there; for All Range.
    void playTracks(const QVector<int> &tracks, int index, bool autoplay = true,
                    const QVector<QVector<int>> &range = {}, int list = -1);
    // A SensMe channel's tracks in the order given, the first from startMs.
    void playChannel(int channel, const QVector<int> &tracks, qint64 startMs);
    // The SensMe channel the queue came from, -1 for a library list.
    int channel() const { return m_channel; }
    void playQueueRow(int row);
    void togglePlayback();
    void pause();
    void stop();
    void next();
    void previous();
    void seek(qint64 milliseconds);
    void setVolume(int value);
    void setAvls(bool value);
    void setShuffle(bool value);
    void setRepeatMode(int value);
    void cycleRepeat();
    void setAllRange(bool value);
    // Fast-forward or rewind while a transport button is held.
    void startScan(int direction);
    void stopScan();
    void platformError(const QString &message);

signals:
    void transportChanged();
    void trackChanged();
    void positionChanged();
    void queueChanged();
    void settingsChanged();
    void statusChanged();
    void seekRejected();
    void failed(const QString &message);

private:
    void setQueue(const QVector<int> &tracks, int index, int channel);
    bool loadCurrent(bool autoplay, qint64 position = 0);
    void move(int direction, bool automatic);
    bool playNextList();
    void restore();
    void save();
    void libraryChanged();

    QMediaPlayer *m_media;
    MusicDb *m_db;
    QSettings *m_settings;
    QVector<int> m_tracks;
    QStringList m_paths;
    QueueState m_queue;
    QVector<QVector<int>> m_range;
    int m_list = -1;
    int m_channel = -1;
    int m_track = -1;
    int m_repeat = 0;
    int m_volume = 40;
    bool m_avls = false;
    bool m_shuffle = false;
    bool m_allRange = true;
    bool m_restored = false;
    bool m_stopped = false;
    qint64 m_pendingSeek = -1;
    QTimer m_scan;
    QTimer m_saveTimer;
    int m_scanDirection = 0;
};

#endif

#ifndef TAGREADER_H
#define TAGREADER_H

#include <QtCore/QByteArray>
#include <QtCore/QString>

struct TrackTags {
    QString title, artist, albumArtist, album, genre, composer;
    int year = 0;            // first 4-digit year of date/year tags
    int track = 0;           // "3/10" -> 3
    int disc = 0;
    qint64 durationMs = 0;
    QString codec;           // "FLAC" "MP3" "AAC" "ALAC" "Vorbis" "Opus" "WAV" "AIFF"
    int sampleRate = 0;      // Hz of the source (Opus: OpusHead input rate if nonzero, else 48000)
    int bitsPerSample = 0;   // lossless/PCM only, 0 for lossy
    int bitrate = 0;         // kbps, average
    int channels = 0;
    bool hasCover = false;   // an embedded picture exists
    QString lyrics;          // unsynchronised lyrics if present (USLT, LYRICS/UNSYNCEDLYRICS, ©lyr)
    QByteArray sensme;       // SensMe::parse features of a 12 TONE ANALYSIS (ID3 GEOB), empty if not analyzed
    qint32 highlightMs = -1; // where SensMe Channels start the track, -1 if unknown
};

// Reads tags and stream info without decoding audio. False with *error set
// when the file is not a supported audio file or is truncated/corrupt.
bool readTags(const QString &path, TrackTags *tags, QString *error = nullptr);
// The embedded front cover (picture type 3 preferred, else the first
// picture) as raw image bytes (JPEG/PNG), empty if none.
QByteArray readCover(const QString &path);

#endif

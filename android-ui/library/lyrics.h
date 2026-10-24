#ifndef LYRICS_H
#define LYRICS_H

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

// A song's lyrics: timed when they came from LRC lines ("[01:02.50]text"),
// otherwise plain lines with no times.
struct Lyrics {
    QStringList lines;
    QVector<qint64> times;
    bool timed() const { return !times.isEmpty(); }
    // The line playing at position (ms), the first line before it starts.
    int lineAt(qint64 position) const;
};

// The .lrc file beside the audio file (same base name) wins over the
// lyrics tag; either may be LRC or plain text.
Lyrics loadLyrics(const QString &audioPath, const QString &tagLyrics);
bool hasLyricsFile(const QString &audioPath);
Lyrics parseLyrics(const QString &text);

#endif

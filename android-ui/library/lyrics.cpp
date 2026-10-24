#include "lyrics.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QTextCodec>

#include <algorithm>

namespace {

QString lyricsFile(const QString &audioPath)
{
    const QFileInfo audio(audioPath);
    for (const char *suffix : {".lrc", ".LRC", ".Lrc"}) {
        const QString path = audio.dir().filePath(audio.completeBaseName() + suffix);
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

// UTF-8 (with or without a BOM, or UTF-16 with one); Korean LRC files made
// on Windows are often CP949, which is tried before Latin-1.
QString decode(const QByteArray &data)
{
    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    QTextCodec *codec = QTextCodec::codecForUtfText(data, utf8);
    QTextCodec::ConverterState state;
    const QString text = codec->toUnicode(data.constData(), data.size(), &state);
    if (codec != utf8 || state.invalidChars == 0)
        return text;
    if (QTextCodec *korean = QTextCodec::codecForName("CP949"))
        return korean->toUnicode(data);
    return QString::fromLatin1(data);
}

}

int Lyrics::lineAt(qint64 position) const
{
    const auto next = std::upper_bound(times.begin(), times.end(), position);
    return qMax(0, int(next - times.begin()) - 1);
}

Lyrics parseLyrics(const QString &text)
{
    static const QRegularExpression stamp(R"(^\[(\d+):(\d+)(?:[.:](\d+))?\])");
    static const QRegularExpression offsetTag(R"(^\[offset:\s*([+-]?\d+)\s*\])", QRegularExpression::CaseInsensitiveOption);
    QVector<QPair<qint64, QString>> timed;
    QStringList plain;
    qint64 offset = 0;
    const QStringList rows = text.split(QRegularExpression("\r\n|\r|\n"));
    for (const QString &row : rows) {
        QString line = row.trimmed();
        const QRegularExpressionMatch tag = offsetTag.match(line);
        if (tag.hasMatch()) {
            offset = tag.captured(1).toLongLong();
            continue;
        }
        QVector<qint64> stamps;
        for (QRegularExpressionMatch m = stamp.match(line); m.hasMatch(); m = stamp.match(line)) {
            const QString fraction = m.captured(3);
            const qint64 ms = fraction.isEmpty() ? 0 : fraction.left(3).leftJustified(3, '0').toLongLong();
            stamps.append(m.captured(1).toLongLong() * 60000 + m.captured(2).toLongLong() * 1000 + ms);
            line = line.mid(m.capturedLength());
        }
        if (!stamps.isEmpty()) {
            for (qint64 at : stamps)
                timed.append({at, line.trimmed()});
        } else if (!line.startsWith('[')) {
            plain.append(line);
        }
    }
    Lyrics lyrics;
    if (!timed.isEmpty()) {
        std::stable_sort(timed.begin(), timed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        // A positive offset shows the lyrics earlier.
        for (const auto &entry : timed) {
            lyrics.times.append(qMax<qint64>(0, entry.first - offset));
            lyrics.lines.append(entry.second);
        }
        return lyrics;
    }
    while (!plain.isEmpty() && plain.first().isEmpty())
        plain.removeFirst();
    while (!plain.isEmpty() && plain.last().isEmpty())
        plain.removeLast();
    lyrics.lines = plain;
    return lyrics;
}

bool hasLyricsFile(const QString &audioPath)
{
    return !lyricsFile(audioPath).isEmpty();
}

Lyrics loadLyrics(const QString &audioPath, const QString &tagLyrics)
{
    const QString path = lyricsFile(audioPath);
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && file.size() < (1 << 20)) {
            const Lyrics lyrics = parseLyrics(decode(file.readAll()));
            if (!lyrics.lines.isEmpty())
                return lyrics;
        }
    }
    return parseLyrics(tagLyrics);
}

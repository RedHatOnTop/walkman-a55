#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <QtGui/QImage>

#include "../tagreader.h"

namespace {

const char *const kSources[] = {"cover.png", "cover.jpg", "big_cover.png"};

struct Expected {
    TrackTags tags;
    bool hasCover = false;
    QSize coverSize;
    int ffprobeBitrate = 0;
    bool lossy = false;
};

int number(const QJsonValue &v)
{
    return v.isString() ? v.toString().toInt() : v.toInt();
}

int leadingInt(const QString &s)
{
    int n = 0;
    while (n < s.size() && s.at(n).isDigit())
        ++n;
    return s.left(n).toInt();
}

int yearOf(const QString &s)
{
    for (int i = 0; i + 4 <= s.size(); ++i) {
        const QString four = s.mid(i, 4);
        bool ok = false;
        const int year = four.toInt(&ok);
        if (ok && four.at(0).isDigit() && four.at(3).isDigit())
            return year;
    }
    return 0;
}

QMap<QString, QString> readTagMap(const QJsonObject &object)
{
    QMap<QString, QString> map;
    const QJsonObject tags = object.value("tags").toObject();
    for (auto it = tags.begin(); it != tags.end(); ++it)
        map.insert(it.key().toLower(), it.value().toString());
    return map;
}

bool probe(const QString &path, QJsonObject *json)
{
    QProcess ffprobe;
    ffprobe.start("ffprobe", {"-v", "error", "-show_format", "-show_streams", "-of", "json", path});
    if (!ffprobe.waitForFinished(30000) || ffprobe.exitCode() != 0)
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(ffprobe.readAllStandardOutput());
    *json = doc.object();
    return !doc.isNull();
}

bool buildExpected(const QString &dir, const QString &name, const QJsonObject &json, Expected *e, QString *why)
{
    const QJsonObject format = json.value("format").toObject();
    const QJsonArray streams = json.value("streams").toArray();
    QJsonObject audio;
    QMap<QString, QString> tags = readTagMap(format);
    for (const QJsonValue &value : streams) {
        const QJsonObject stream = value.toObject();
        if (stream.value("disposition").toObject().value("attached_pic").toInt() == 1) {
            e->hasCover = true;
            e->coverSize = QSize(stream.value("width").toInt(), stream.value("height").toInt());
        } else if (audio.isEmpty() && stream.value("codec_type").toString() == "audio") {
            audio = stream;
            const QMap<QString, QString> own = readTagMap(stream);
            for (auto it = own.begin(); it != own.end(); ++it) {
                if (!tags.contains(it.key()))
                    tags.insert(it.key(), it.value());
            }
        }
    }
    if (audio.isEmpty()) {
        *why = "ffprobe found no audio stream";
        return false;
    }

    auto tag = [&](std::initializer_list<const char *> keys) {
        for (const char *key : keys) {
            if (tags.contains(key))
                return tags.value(key);
        }
        return QString();
    };
    TrackTags &t = e->tags;
    t.title = tag({"title"});
    t.artist = tag({"artist", "author"});
    t.albumArtist = tag({"album_artist", "albumartist", "album artist"});
    t.album = tag({"album"});
    t.genre = tag({"genre"});
    t.composer = tag({"composer"});
    t.year = yearOf(tag({"date", "year"}));
    t.track = leadingInt(tag({"track", "tracknumber"}));
    t.disc = leadingInt(tag({"disc", "discnumber"}));
    t.lyrics = tag({"lyrics", "unsyncedlyrics"});
    for (auto it = tags.begin(); t.lyrics.isEmpty() && it != tags.end(); ++it) {
        if (it.key().startsWith("lyrics"))
            t.lyrics = it.value();
    }

    const QString codec = audio.value("codec_name").toString();
    const QString container = format.value("format_name").toString();
    if (codec == "flac") t.codec = "FLAC";
    else if (codec == "mp3" || codec == "mp2" || codec == "mp1") t.codec = "MP3";
    else if (codec == "aac") t.codec = "AAC";
    else if (codec == "alac") t.codec = "ALAC";
    else if (codec == "vorbis") t.codec = "Vorbis";
    else if (codec == "opus") t.codec = "Opus";
    else if (codec.startsWith("pcm_")) t.codec = container == "aiff" ? "AIFF" : "WAV";
    e->lossy = t.codec == "MP3" || t.codec == "AAC" || t.codec == "Vorbis" || t.codec == "Opus";

    t.sampleRate = number(audio.value("sample_rate"));
    t.channels = number(audio.value("channels"));
    const int raw = number(audio.value("bits_per_raw_sample"));
    t.bitsPerSample = e->lossy ? 0 : (raw > 0 ? raw : number(audio.value("bits_per_sample")));
    t.durationMs = qRound64(format.value("duration").toString().toDouble() * 1000);
    e->ffprobeBitrate = number(audio.value("bit_rate")) / 1000;

    QFile expect(QDir(dir).filePath(name + ".expect"));
    if (expect.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(expect.readAll()).split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const int eq = line.indexOf('=');
            const QString key = line.left(eq);
            QString value = line.mid(eq + 1);
            value.replace("\\n", "\n");
            if (key == "title") t.title = value;
            else if (key == "artist") t.artist = value;
            else if (key == "albumArtist") t.albumArtist = value;
            else if (key == "album") t.album = value;
            else if (key == "genre") t.genre = value;
            else if (key == "composer") t.composer = value;
            else if (key == "lyrics") t.lyrics = value;
            else if (key == "year") t.year = value.toInt();
            else if (key == "track") t.track = value.toInt();
            else if (key == "disc") t.disc = value.toInt();
            else if (key == "highlightMs") t.highlightMs = value.toInt();
            else if (key == "sensme") t.sensme = QByteArray::fromHex(value.toLatin1());
            else if (key == "coverFile") {
                e->hasCover = true;
                e->coverSize = QImage(QDir(dir).filePath(value)).size();
            }
        }
    }
    return true;
}

template <typename T>
void compare(QStringList *problems, const char *field, const T &got, const T &want)
{
    if (!(got == want))
        problems->append(QString("%1 got '%2' want '%3'").arg(field).arg(got).arg(want));
}

void checkCover(const QString &dir, const QString &path, const Expected &e, QStringList *problems)
{
    const QByteArray cover = readCover(path);
    if (!e.hasCover) {
        if (!cover.isEmpty())
            problems->append("readCover returned bytes for a file without a picture");
        return;
    }
    QImage image;
    if (!image.loadFromData(cover)) {
        problems->append(QString("readCover bytes do not load as an image (%1 bytes)").arg(cover.size()));
        return;
    }
    if (image.size() != e.coverSize)
        problems->append(QString("cover size %1x%2 want %3x%4")
                             .arg(image.width()).arg(image.height()).arg(e.coverSize.width()).arg(e.coverSize.height()));
    bool identical = false;
    for (const char *source : kSources) {
        QFile f(QDir(dir).filePath(source));
        if (f.open(QIODevice::ReadOnly) && f.readAll() == cover) {
            identical = true;
            if (QImage(f.fileName()).size() != image.size())
                problems->append("cover differs in size from its source image");
        }
    }
    if (!identical)
        problems->append("cover bytes match none of the source images");
}

QStringList check(const QString &dir, const QString &name)
{
    const QString path = QDir(dir).filePath(name);
    QStringList problems;
    TrackTags got;
    QString error;
    const bool ok = readTags(path, &got, &error);

    if (name.startsWith("bad_")) {
        if (ok)
            problems << "readTags succeeded on a damaged file";
        if (error.isEmpty())
            problems << "no error message";
        if (!readCover(path).isEmpty())
            problems << "readCover returned bytes";
        return problems;
    }
    if (!ok)
        return QStringList{"readTags failed: " + error};

    QJsonObject json;
    Expected e;
    QString why;
    if (!probe(path, &json) || !buildExpected(dir, name, json, &e, &why))
        return QStringList{"ffprobe: " + (why.isEmpty() ? QString("failed") : why)};
    const TrackTags &w = e.tags;

    compare(&problems, "title", got.title, w.title);
    compare(&problems, "artist", got.artist, w.artist);
    compare(&problems, "albumArtist", got.albumArtist, w.albumArtist);
    compare(&problems, "album", got.album, w.album);
    compare(&problems, "genre", got.genre, w.genre);
    compare(&problems, "composer", got.composer, w.composer);
    compare(&problems, "lyrics", got.lyrics, w.lyrics);
    compare(&problems, "year", got.year, w.year);
    compare(&problems, "track", got.track, w.track);
    compare(&problems, "disc", got.disc, w.disc);
    compare(&problems, "highlightMs", got.highlightMs, w.highlightMs);
    compare(&problems, "sensme", QString::fromLatin1(got.sensme.toHex()), QString::fromLatin1(w.sensme.toHex()));
    compare(&problems, "sampleRate", got.sampleRate, w.sampleRate);
    compare(&problems, "channels", got.channels, w.channels);
    compare(&problems, "bitsPerSample", got.bitsPerSample, w.bitsPerSample);
    if (!w.codec.isEmpty())
        compare(&problems, "codec", got.codec, w.codec);
    compare(&problems, "hasCover", int(got.hasCover), int(e.hasCover));

    if (qAbs(got.durationMs - w.durationMs) > 60)
        problems << QString("durationMs got %1 want %2 +-60").arg(got.durationMs).arg(w.durationMs);
    if (got.bitrate <= 0)
        problems << "bitrate not set";
    else if (e.ffprobeBitrate > 0 && qAbs(got.bitrate - e.ffprobeBitrate) * 20 > e.ffprobeBitrate)
        problems << QString("bitrate got %1 want %2 +-5%").arg(got.bitrate).arg(e.ffprobeBitrate);
    else if (!e.lossy && w.codec != "WAV" && w.codec != "AIFF"
             && got.bitrate > got.sampleRate * got.channels * got.bitsPerSample / 1000)
        problems << QString("lossless bitrate %1 exceeds PCM").arg(got.bitrate);

    checkCover(dir, path, e, &problems);
    return problems;
}

struct FuzzStats {
    int accepted = 0;
    int rejected = 0;
};

// Damaged copies must be rejected or parsed, never crash or hang; ASan/UBSan do the judging.
void mutate(const QString &dir, const QString &name, int rounds, const QString &scratch, FuzzStats *stats)
{
    QFile source(QDir(dir).filePath(name));
    if (!source.open(QIODevice::ReadOnly))
        return;
    const QByteArray original = source.readAll();
    quint32 state = 2463534242u;
    for (const QChar ch : name)
        state = state * 31 + ch.unicode();
    auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };

    const QString victim = QDir(scratch).filePath("victim." + QFileInfo(name).suffix());
    auto probeVictim = [&victim, stats] {
        TrackTags tags;
        QString error;
        ++(readTags(victim, &tags, &error) ? stats->accepted : stats->rejected);
        readCover(victim);
    };
    auto run = [&victim, &probeVictim](const QByteArray &bytes) {
        QFile f(victim);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        f.write(bytes);
        f.close();
        probeVictim();
    };

    for (int len = 0; len < qMin(200, original.size()); ++len)
        run(original.left(len));
    for (int i = 1; i <= 24; ++i)
        run(original.left(int(qint64(original.size()) * i / 25)));

    // Length bombs: every 4-byte field near the start and the end becomes an extreme value.
    run(original);
    QFile patched(victim);
    if (patched.open(QIODevice::ReadWrite)) {
        QList<int> offsets;
        for (int at = 0; at < qMin(256, original.size() - 3); ++at)
            offsets << at;
        for (int at = qMax(256, original.size() - 512); at + 4 <= original.size(); ++at)
            offsets << at;
        const char patterns[][4] = {{'\xFF', '\xFF', '\xFF', '\xFF'}, {'\x7F', '\xFF', '\xFF', '\xFF'},
                                    {0, 0, 0, 0}, {'\x80', 0, 0, 0}};
        for (int at : offsets) {
            for (const auto &pattern : patterns) {
                patched.seek(at);
                patched.write(pattern, 4);
                patched.flush();
                probeVictim();
            }
            patched.seek(at);
            patched.write(original.constData() + at, 4);
            patched.flush();
        }
    }

    for (int round = 0; round < rounds; ++round) {
        QByteArray bytes = original;
        const int flips = 1 + int(next() % 16);
        const int region = round % 2 ? qMin(bytes.size(), 4096) : bytes.size();
        for (int i = 0; i < flips && region > 0; ++i) {
            const int at = int(next() % quint32(region));
            const quint32 kind = next() % 3;
            bytes[at] = kind == 0 ? char(next()) : kind == 1 ? char(0xFF) : char(0);
        }
        if (round % 3 == 0)
            bytes.truncate(int(next() % quint32(bytes.size() + 1)));
        run(bytes);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    out.setCodec("UTF-8");
    if (argc < 2) {
        out << "usage: tst_tagreader <fixture dir> [fuzz rounds]\n";
        return 2;
    }
    const QString dir = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
    const int rounds = argc > 2 ? QString(argv[2]).toInt() : 40;

    QStringList names;
    for (const QFileInfo &info : QDir(dir).entryInfoList(QDir::Files, QDir::Name)) {
        const QString suffix = info.suffix().toLower();
        if (suffix != "png" && suffix != "jpg" && suffix != "expect" && suffix != "ffmeta")
            names << info.fileName();
    }

    int failures = 0;
    for (const QString &name : names) {
        const QStringList problems = check(dir, name);
        if (problems.isEmpty()) {
            out << "PASS " << name << "\n";
        } else {
            ++failures;
            out << "FAIL " << name << ": " << problems.join("; ") << "\n";
        }
        out.flush();
    }

    if (rounds > 0) {
        QTemporaryDir scratch(QDir(dir).filePath("../fuzz-XXXXXX"));
        if (!scratch.isValid()) {
            out << "FAIL fuzz: cannot create scratch directory\n";
            return 1;
        }
        FuzzStats stats;
        for (const QString &name : names)
            mutate(dir, name, rounds, scratch.path(), &stats);
        out << "PASS fuzz: " << names.size() << " files, " << stats.accepted + stats.rejected << " damaged copies ("
            << stats.accepted << " still parsed, " << stats.rejected << " rejected), no crash\n";
    }

    out << (failures ? "FAILED: " : "OK: ") << names.size() - failures << "/" << names.size() << " files passed\n";
    return failures ? 1 : 0;
}

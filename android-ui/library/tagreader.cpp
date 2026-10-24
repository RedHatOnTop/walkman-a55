#include "tagreader.h"

#include "sensme.h"

#include <QtCore/QBuffer>
#include <QtCore/QDate>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtCore/QTextCodec>

#include <algorithm>
#include <cmath>

namespace {

const qint64 kMaxRead = 32 << 20;
const qint64 kScanLimit = 1 << 20;

struct Stream {
    QIODevice *dev;
    qint64 size;

    // Exactly `length` bytes at `pos`, or an empty array if the range does not fit the stream.
    QByteArray read(qint64 pos, qint64 length) const
    {
        if (pos < 0 || length <= 0 || length > kMaxRead || pos > size - length || !dev->seek(pos))
            return {};
        const QByteArray data = dev->read(length);
        return data.size() == length ? data : QByteArray();
    }
};

uchar u8(const QByteArray &b, qint64 off)
{
    return off >= 0 && off < b.size() ? uchar(b.at(int(off))) : 0;
}

quint64 bigEndian(const QByteArray &b, qint64 off, int bytes)
{
    quint64 v = 0;
    for (int i = 0; i < bytes; ++i)
        v = (v << 8) | u8(b, off + i);
    return v;
}

quint64 littleEndian(const QByteArray &b, qint64 off, int bytes)
{
    quint64 v = 0;
    for (int i = bytes - 1; i >= 0; --i)
        v = (v << 8) | u8(b, off + i);
    return v;
}

quint32 be16(const QByteArray &b, qint64 off) { return quint32(bigEndian(b, off, 2)); }
quint32 be24(const QByteArray &b, qint64 off) { return quint32(bigEndian(b, off, 3)); }
quint32 be32(const QByteArray &b, qint64 off) { return quint32(bigEndian(b, off, 4)); }
quint64 be64(const QByteArray &b, qint64 off) { return bigEndian(b, off, 8); }
quint32 le16(const QByteArray &b, qint64 off) { return quint32(littleEndian(b, off, 2)); }
quint32 le32(const QByteArray &b, qint64 off) { return quint32(littleEndian(b, off, 4)); }
quint64 le64(const QByteArray &b, qint64 off) { return littleEndian(b, off, 8); }

qint64 syncsafe(const QByteArray &b, qint64 off)
{
    return (qint64(u8(b, off) & 0x7F) << 21) | (qint64(u8(b, off + 1) & 0x7F) << 14)
        | (qint64(u8(b, off + 2) & 0x7F) << 7) | qint64(u8(b, off + 3) & 0x7F);
}

bool fail(QString *error, const char *message)
{
    if (error)
        *error = QString::fromLatin1(message);
    return false;
}

const QString kSeparator = QStringLiteral("; ");

QString stripNuls(QString s)
{
    int n = s.size();
    while (n > 0 && s.at(n - 1).unicode() == 0)
        --n;
    s.truncate(n);
    return s;
}

// An explicit non-negative size keeps Qt's strlen branch (size -1) out of the inlined code, where
// GCC reports it as a bogus -Wstringop-overread on empty arrays.
QString utf8(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes.constData(), qMax(0, bytes.size()));
}

QString latin1(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.constData(), qMax(0, bytes.size()));
}

// A KS X 1001 Hangul syllable (lead 0xB0-0xC8) or symbol (lead 0xA1-0xAC, such as ♡ or …),
// with a trail byte 0xA1-0xFE.
bool hasKsPair(const QByteArray &bytes)
{
    for (int i = 0; i + 1 < bytes.size(); ++i) {
        const uchar lead = uchar(bytes.at(i));
        if (lead < 0x80)
            continue;
        const uchar trail = uchar(bytes.at(i + 1));
        if (((lead >= 0xb0 && lead <= 0xc8) || (lead >= 0xa1 && lead <= 0xac)) && trail >= 0xa1 && trail <= 0xfe)
            return true;
        ++i;
    }
    return false;
}

// Korean Windows taggers wrote CP949 into the same fields. Taken as CP949 only when the bytes
// decode cleanly and hold a KS X 1001 Hangul or symbol pair: Latin-1 text almost never pairs
// two high bytes that way, while "Ärzte" (a valid UHC-extension pair) and "Größe" (a Hanja
// pair) stay Latin-1. ID3v1's 30-byte fields can end in half a character, which is dropped.
QString cp949(const QByteArray &bytes)
{
    static QTextCodec *const codec = QTextCodec::codecForName("CP949");
    if (!codec || !hasKsPair(bytes))
        return {};
    QTextCodec::ConverterState state(QTextCodec::ConvertInvalidToNull);
    const QString decoded = codec->toUnicode(bytes.constData(), bytes.size(), &state);
    // Qt's own CP949 codec (Android) turns an unmapped KS X 1001 code into U+FFFD without
    // counting it as invalid.
    if (state.invalidChars || state.remainingChars > 1 || decoded.contains(QChar(QChar::ReplacementCharacter)))
        return {};
    return decoded;
}

// Taggers often put UTF-8 into fields declared as Latin-1; valid UTF-8 with high bytes is
// far more likely to be that than Latin-1 text.
QString legacyText(const QByteArray &bytes)
{
    const QString decoded = utf8(bytes);
    if (!decoded.contains(QChar(QChar::ReplacementCharacter)))
        return decoded;
    const QString korean = cp949(bytes);
    return korean.isNull() ? latin1(bytes) : korean;
}

// The first run of four or more digits. Melon writes TYER as YYMMDD
// ("170324"), so six digits that read as a month and a day are a date in the
// hundred years up to next year. Anything later, or before 1000, is junk
// ("20708744"). The floor of 2026 keeps real years when the clock was reset.
int parseYear(const QString &s)
{
    static const int latest = std::max(QDate::currentDate().year(), 2026) + 1;
    for (int i = 0; i < s.size(); ++i) {
        int n = 0;
        while (i + n < s.size() && s.at(i + n).unicode() >= '0' && s.at(i + n).unicode() <= '9')
            ++n;
        if (n < 4) {
            i += n;
            continue;
        }
        int year = s.mid(i, 4).toInt();
        if (n == 6) {
            const int month = s.mid(i + 2, 2).toInt(), day = s.mid(i + 4, 2).toInt();
            if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
                year = 2000 + s.mid(i, 2).toInt();
                if (year > latest)
                    year -= 100;
            }
        }
        return year >= 1000 && year <= latest ? year : 0;
    }
    return 0;
}

int leadingInt(const QString &s)
{
    const QString t = s.trimmed();
    int n = 0;
    while (n < t.size() && n < 9 && t.at(n).unicode() >= '0' && t.at(n).unicode() <= '9')
        ++n;
    return t.left(n).toInt();
}

const char *const kGenres[] = {
    "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
    "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial",
    "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop",
    "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental", "Acid", "House", "Game",
    "Sound Clip", "Gospel", "Noise", "Alternative Rock", "Bass", "Soul", "Punk", "Space", "Meditative",
    "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic", "Darkwave", "Techno-Industrial",
    "Electronic", "Pop-Folk", "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
    "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave",
    "Psychedelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka",
    "Retro", "Musical", "Rock & Roll", "Hard Rock",
    "Folk", "Folk-Rock", "National Folk", "Swing", "Fast Fusion", "Bebop", "Latin", "Revival", "Celtic",
    "Bluegrass", "Avantgarde", "Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock",
    "Slow Rock", "Big Band", "Chorus", "Easy Listening", "Acoustic", "Humour", "Speech", "Chanson",
    "Opera", "Chamber Music", "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove", "Satire",
    "Slow Jam", "Club", "Tango", "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul",
    "Freestyle", "Duet", "Punk Rock", "Drum Solo", "A capella", "Euro-House", "Dance Hall", "Goa",
    "Drum & Bass", "Club-House", "Hardcore Techno", "Terror", "Indie", "BritPop", "Afro-Punk",
    "Polsk Punk", "Beat", "Christian Gangsta Rap", "Heavy Metal", "Black Metal", "Crossover",
    "Contemporary Christian", "Christian Rock", "Merengue", "Salsa", "Thrash Metal", "Anime", "Jpop",
    "Synthpop", "Abstract", "Art Rock", "Baroque", "Bhangra", "Big Beat", "Breakbeat", "Chillout",
    "Downtempo", "Dub", "EBM", "Eclectic", "Electro", "Electroclash", "Emo", "Experimental", "Garage",
    "Global", "IDM", "Illbient", "Industro-Goth", "Jam Band", "Krautrock", "Leftfield", "Lounge",
    "Math Rock", "New Romantic", "Nu-Breakz", "Post-Punk", "Post-Rock", "Psytrance", "Shoegaze",
    "Space Rock", "Trop Rock", "World Music", "Neoclassical", "Audiobook", "Audio Theatre",
    "Neue Deutsche Welle", "Podcast", "Indie Rock", "G-Funk", "Dubstep", "Garage Rock", "Psybient",
};

QString genreName(int index)
{
    return index >= 0 && index < int(sizeof kGenres / sizeof *kGenres) ? QString::fromLatin1(kGenres[index])
                                                                      : QString();
}

// "(13)", "13", "(13)Pop", "(51)(39)", "(RX)"; anything else is already a name.
QString resolveGenre(const QString &raw)
{
    QString text = raw.trimmed();
    QStringList names;
    while (text.startsWith(QLatin1Char('(')) && !text.startsWith(QLatin1String("(("))) {
        const int close = text.indexOf(QLatin1Char(')'));
        if (close < 0)
            break;
        const QString ref = text.mid(1, close - 1);
        bool numeric = false;
        const int index = ref.toInt(&numeric);
        if (numeric) {
            const QString name = genreName(index);
            names << (name.isEmpty() ? ref : name);
        } else if (ref == QLatin1String("RX")) {
            names << QStringLiteral("Remix");
        } else if (ref == QLatin1String("CR")) {
            names << QStringLiteral("Cover");
        } else {
            break;
        }
        text = text.mid(close + 1);
    }
    if (text.startsWith(QLatin1String("((")))
        text.remove(0, 1);
    if (!text.isEmpty() && names.isEmpty()) {
        bool numeric = false;
        const int index = text.toInt(&numeric);
        const QString name = numeric ? genreName(index) : QString();
        return name.isEmpty() ? text : name;
    }
    return text.isEmpty() ? names.join(kSeparator) : text;
}

struct Collector {
    TrackTags *tags;
    bool wantCover;
    QByteArray cover;
    int coverType = -1;

    // Records that a picture exists; true when the caller should now supply its bytes.
    bool offerPicture(int type)
    {
        tags->hasCover = true;
        return wantCover && (coverType < 0 || (type == 3 && coverType != 3));
    }

    void setCover(int type, const QByteArray &data)
    {
        if (data.isEmpty())
            return;
        cover = data;
        coverType = type;
    }

    // A 12 TONE ANALYSIS blob. The media store takes the first exact match
    // (rank 2), else the first one it accepts loosely (rank 1); a blob that
    // yields no features leaves the track unanalyzed.
    void offerSmfmf(const QByteArray &blob, int rank)
    {
        if (rank <= smfmfRank)
            return;
        smfmfRank = rank;
        if (!SensMe::parse(blob, &tags->sensme, &tags->highlightMs)) {
            tags->sensme.clear();
            tags->highlightMs = -1;
        }
    }
    int smfmfRank = 0;
};

void fillMissing(TrackTags *dst, const TrackTags &src)
{
    for (QString TrackTags::*field : {&TrackTags::title, &TrackTags::artist, &TrackTags::albumArtist,
                                      &TrackTags::album, &TrackTags::genre, &TrackTags::composer,
                                      &TrackTags::lyrics}) {
        if ((dst->*field).isEmpty())
            dst->*field = src.*field;
    }
    for (int TrackTags::*field : {&TrackTags::year, &TrackTags::track, &TrackTags::disc}) {
        if (!(dst->*field))
            dst->*field = src.*field;
    }
}

struct Picture {
    int type = 0;
    qint64 offset = 0;
    qint64 length = 0;
};

// FLAC PICTURE block layout; `d` may hold only the head of a block `total` bytes long.
bool parsePicture(const QByteArray &d, qint64 total, Picture *p)
{
    if (d.size() < 32)
        return false;
    p->type = int(be32(d, 0));
    qint64 pos = 8 + qint64(be32(d, 4));
    if (pos + 4 > d.size())
        return false;
    pos += 4 + qint64(be32(d, pos));
    pos += 16;
    if (pos + 4 > d.size())
        return false;
    p->length = be32(d, pos);
    p->offset = pos + 4;
    return p->length > 0 && p->length <= total - p->offset;
}

void addEncodedPicture(const QByteArray &base64, Collector *c)
{
    const bool whole = c->wantCover;
    const QByteArray block = QByteArray::fromBase64(whole ? base64 : base64.left(4096));
    Picture p;
    if (parsePicture(block, whole ? block.size() : qint64(base64.size()) / 4 * 3, &p)
        && c->offerPicture(p.type))
        c->setCover(p.type, block.mid(int(p.offset), int(p.length)));
}

int terminatorWidth(int encoding) { return encoding == 1 || encoding == 2 ? 2 : 1; }

int findTerminator(const QByteArray &d, int from, int encoding)
{
    const int width = terminatorWidth(encoding);
    for (int i = from; i + width <= d.size(); i += width) {
        if (d.at(i) == 0 && (width == 1 || d.at(i + 1) == 0))
            return i;
    }
    return -1;
}

QString decodeUtf16(const QByteArray &d, bool *bigEndianOrder)
{
    int i = 0;
    if (d.size() >= 2) {
        if (u8(d, 0) == 0xFF && u8(d, 1) == 0xFE) {
            *bigEndianOrder = false;
            i = 2;
        } else if (u8(d, 0) == 0xFE && u8(d, 1) == 0xFF) {
            *bigEndianOrder = true;
            i = 2;
        }
    }
    QString out;
    out.reserve((d.size() - i) / 2);
    for (; i + 1 < d.size(); i += 2) {
        const ushort hi = u8(d, i + (*bigEndianOrder ? 0 : 1));
        const ushort lo = u8(d, i + (*bigEndianOrder ? 1 : 0));
        out.append(QChar(ushort((hi << 8) | lo)));
    }
    return out;
}

QString decodeText(int encoding, const QByteArray &d, bool *bigEndianOrder)
{
    switch (encoding) {
    case 1:
    case 2:
        return stripNuls(decodeUtf16(d, bigEndianOrder));
    case 3:
        return stripNuls(utf8(d));
    default:
        return stripNuls(legacyText(d));
    }
}

// NUL-separated values; each UTF-16 value may carry its own byte order mark.
QStringList decodeStrings(int encoding, const QByteArray &d)
{
    QStringList out;
    const int width = terminatorWidth(encoding);
    bool bigEndianOrder = encoding == 2;
    int start = 0;
    for (int i = 0; i + width <= d.size(); i += width) {
        if (d.at(i) == 0 && (width == 1 || d.at(i + 1) == 0)) {
            out << decodeText(encoding, d.mid(start, i - start), &bigEndianOrder);
            start = i + width;
        }
    }
    if (start < d.size())
        out << decodeText(encoding, d.mid(start), &bigEndianOrder);
    out.removeAll(QString());
    return out;
}

QByteArray removeUnsync(const QByteArray &in)
{
    QByteArray out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        out.append(in.at(i));
        if (uchar(in.at(i)) == 0xFF && i + 1 < in.size() && in.at(i + 1) == 0)
            ++i;
    }
    return out;
}

enum Id3Field { NoField, Title, Artist, AlbumArtist, Album, Genre, Composer, YearDrc, YearTyer, YearTdor,
                Track, Disc };

Id3Field textField(const QByteArray &id)
{
    static const struct {
        const char *id;
        Id3Field field;
    } table[] = {
        {"TIT2", Title}, {"TT2", Title}, {"TPE1", Artist}, {"TP1", Artist}, {"TPE2", AlbumArtist},
        {"TP2", AlbumArtist}, {"TALB", Album}, {"TAL", Album}, {"TCON", Genre}, {"TCO", Genre},
        {"TCOM", Composer}, {"TCM", Composer}, {"TDRC", YearDrc}, {"TYER", YearTyer}, {"TYE", YearTyer},
        {"TDOR", YearTdor}, {"TRCK", Track}, {"TRK", Track}, {"TPOS", Disc}, {"TPA", Disc},
    };
    for (const auto &entry : table) {
        if (id == entry.id)
            return entry.field;
    }
    return NoField;
}

void applyText(Id3Field field, const QStringList &values, TrackTags *t, int *years)
{
    if (values.isEmpty())
        return;
    const QString joined = values.join(kSeparator);
    switch (field) {
    case Title: if (t->title.isEmpty()) t->title = joined; break;
    case Artist: if (t->artist.isEmpty()) t->artist = joined; break;
    case AlbumArtist: if (t->albumArtist.isEmpty()) t->albumArtist = joined; break;
    case Album: if (t->album.isEmpty()) t->album = joined; break;
    case Composer: if (t->composer.isEmpty()) t->composer = joined; break;
    case Genre:
        if (t->genre.isEmpty()) {
            QStringList names;
            for (const QString &value : values)
                names << resolveGenre(value);
            t->genre = names.join(kSeparator);
        }
        break;
    case YearDrc: if (!years[0]) years[0] = parseYear(joined); break;
    case YearTyer: if (!years[1]) years[1] = parseYear(joined); break;
    case YearTdor: if (!years[2]) years[2] = parseYear(joined); break;
    case Track: if (!t->track) t->track = leadingInt(joined); break;
    case Disc: if (!t->disc) t->disc = leadingInt(joined); break;
    default: break;
    }
}

bool parseApic(const QByteArray &p, bool v22, int *type, int *dataOffset)
{
    if (p.size() < 5)
        return false;
    const int encoding = u8(p, 0);
    int pos = 1;
    if (v22) {
        pos += 3;
    } else {
        const int end = p.indexOf('\0', pos);
        if (end < 0)
            return false;
        pos = end + 1;
    }
    if (pos >= p.size())
        return false;
    *type = u8(p, pos++);
    const int end = findTerminator(p, pos, encoding);
    if (end < 0)
        return false;
    *dataOffset = end + terminatorWidth(encoding);
    return true;
}

struct Id3Header {
    int major = 0;
    int flags = 0;
    qint64 bodySize = 0;
    qint64 total = 0;
};

bool readId3Header(const Stream &s, qint64 pos, Id3Header *h)
{
    const QByteArray b = s.read(pos, 10);
    if (!b.startsWith("ID3") || u8(b, 3) < 2 || u8(b, 3) > 4 || u8(b, 4) == 0xFF)
        return false;
    for (int i = 6; i < 10; ++i) {
        if (u8(b, i) & 0x80)
            return false;
    }
    h->major = u8(b, 3);
    h->flags = u8(b, 5);
    h->bodySize = syncsafe(b, 6);
    h->total = 10 + h->bodySize + (h->major == 4 && (h->flags & 0x10) ? 10 : 0);
    return true;
}

bool validFrameId(const QByteArray &id)
{
    for (char ch : id) {
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')))
            return false;
    }
    return true;
}

void parseId3(const Stream &s, qint64 pos, const Id3Header &h, Collector *c)
{
    TrackTags *t = c->tags;
    const int major = h.major;
    if (major == 2 && (h.flags & 0x40))
        return;

    qint64 begin = pos + 10;
    qint64 end = begin + h.bodySize;
    QByteArray decoded;
    QBuffer buffer;
    Stream frames = s;
    if (major < 4 && (h.flags & 0x80)) {
        decoded = removeUnsync(s.read(begin, h.bodySize));
        if (decoded.isEmpty())
            return;
        buffer.setData(decoded);
        buffer.open(QIODevice::ReadOnly);
        frames = Stream{&buffer, decoded.size()};
        begin = 0;
        end = decoded.size();
    }
    if (major >= 3 && (h.flags & 0x40)) {
        const QByteArray ext = frames.read(begin, 4);
        begin += major == 3 ? 4 + qint64(be32(ext, 0)) : syncsafe(ext, 0);
    }

    int years[3] = {0, 0, 0};
    const int idLength = major == 2 ? 3 : 4;
    const int headerLength = major == 2 ? 6 : 10;
    while (begin + headerLength <= end) {
        const QByteArray fh = frames.read(begin, headerLength);
        if (fh.isEmpty() || fh.at(0) == 0)
            break;
        const QByteArray id = fh.left(idLength);
        if (!validFrameId(id))
            break;
        qint64 size = 0;
        if (major == 2) {
            size = be24(fh, 3);
        } else if (major == 3) {
            size = be32(fh, 4);
        } else {
            size = syncsafe(fh, 4);
            // Some v2.4 writers store plain big-endian sizes.
            for (int i = 4; i < 8; ++i) {
                if (u8(fh, i) & 0x80)
                    size = be32(fh, 4);
            }
        }
        const int formatFlags = major >= 3 ? u8(fh, 9) : 0;
        const qint64 dataPos = begin + headerLength;
        if (size > end - dataPos)
            break;
        begin = dataPos + size;

        qint64 skip = 0;
        bool unsynced = false;
        if (major == 3) {
            if (formatFlags & 0xC0)
                continue;
            skip = formatFlags & 0x20 ? 1 : 0;
        } else if (major == 4) {
            if (formatFlags & 0x0C)
                continue;
            skip = (formatFlags & 0x40 ? 1 : 0) + (formatFlags & 0x01 ? 4 : 0);
            unsynced = formatFlags & 0x02;
        }
        if (size <= skip)
            continue;
        const qint64 payloadPos = dataPos + skip;
        const qint64 payloadSize = size - skip;
        auto payload = [&](qint64 length) {
            const QByteArray raw = frames.read(payloadPos, length);
            return unsynced ? removeUnsync(raw) : raw;
        };

        const Id3Field field = textField(id);
        if (field != NoField) {
            const QByteArray p = payload(payloadSize);
            if (p.size() >= 2)
                applyText(field, decodeStrings(u8(p, 0), p.mid(1)), t, years);
        } else if (id == "USLT" || id == "ULT") {
            const QByteArray p = payload(payloadSize);
            const int encoding = u8(p, 0);
            const int term = findTerminator(p, 4, encoding);
            if (p.size() >= 5 && t->lyrics.isEmpty() && term >= 0) {
                bool bigEndianOrder = encoding == 2;
                t->lyrics = decodeText(encoding, p.mid(term + terminatorWidth(encoding)), &bigEndianOrder);
            }
        } else if (id == "TXXX" || id == "TXX") {
            // ffmpeg and some taggers store lyrics here instead of in USLT.
            const QByteArray p = payload(payloadSize);
            const int encoding = u8(p, 0);
            const int term = findTerminator(p, 1, encoding);
            if (term < 0 || !t->lyrics.isEmpty())
                continue;
            bool bigEndianOrder = encoding == 2;
            const QString name = decodeText(encoding, p.mid(1, term - 1), &bigEndianOrder).toUpper();
            if (name == QLatin1String("USLT") || name == QLatin1String("LYRICS")
                || name == QLatin1String("UNSYNCEDLYRICS"))
                t->lyrics = decodeText(encoding, p.mid(term + terminatorWidth(encoding)), &bigEndianOrder);
        } else if ((id == "GEOB" || id == "GEO") && c->smfmfRank < 2) {
            // Music Center for PC (and Walkman Manager) store the 12 TONE
            // ANALYSIS here: MIME application/SMFMF in any case, preferably
            // described USR_SMFMF.
            const QByteArray head = payload(qMin<qint64>(payloadSize, 512));
            const int encoding = u8(head, 0);
            const int width = terminatorWidth(encoding);
            const int mimeEnd = head.indexOf('\0', 1);
            const int nameEnd = mimeEnd < 0 ? -1 : findTerminator(head, mimeEnd + 1, encoding);
            const int descriptionEnd = nameEnd < 0 ? -1 : findTerminator(head, nameEnd + width, encoding);
            if (descriptionEnd < 0 || head.mid(1, mimeEnd - 1).toLower() != "application/smfmf")
                continue;
            bool bigEndianOrder = encoding == 2;
            const QString description =
                decodeText(encoding, head.mid(nameEnd + width, descriptionEnd - nameEnd - width), &bigEndianOrder);
            c->offerSmfmf(payload(payloadSize).mid(descriptionEnd + width),
                          description == QLatin1String("USR_SMFMF") ? 2 : 1);
        } else if (id == "APIC" || id == "PIC") {
            const QByteArray p = payload(c->wantCover ? payloadSize : qMin<qint64>(payloadSize, 4096));
            int type = 0;
            int dataOffset = 0;
            if (!parseApic(p, id == "PIC", &type, &dataOffset))
                continue;
            const qint64 dataLength = (c->wantCover ? p.size() : payloadSize) - dataOffset;
            if (dataLength > 0 && c->offerPicture(type))
                c->setCover(type, p.mid(dataOffset));
        }
    }
    if (!t->year)
        t->year = years[0] ? years[0] : years[1] ? years[1] : years[2];
}

// Walks the ID3v2 tags in front of the audio; parses them when `c` is given.
bool leadingId3(const Stream &s, Collector *c, qint64 *end, QString *error)
{
    *end = 0;
    for (int i = 0; i < 8; ++i) {
        Id3Header h;
        if (!readId3Header(s, *end, &h))
            break;
        if (h.total > s.size - *end)
            return fail(error, "Truncated ID3 tag");
        if (c)
            parseId3(s, *end, h, c);
        *end += h.total;
    }
    return true;
}

bool readId3v1(const Stream &s, TrackTags *t)
{
    const QByteArray b = s.read(s.size - 128, 128);
    if (!b.startsWith("TAG"))
        return false;
    auto field = [&](int off, int length) {
        QByteArray f = b.mid(off, length);
        const int nul = f.indexOf('\0');
        if (nul >= 0)
            f.truncate(nul);
        return legacyText(f).trimmed();
    };
    t->title = field(3, 30);
    t->artist = field(33, 30);
    t->album = field(63, 30);
    t->year = parseYear(field(93, 4));
    if (u8(b, 125) == 0 && u8(b, 126) != 0)
        t->track = u8(b, 126);
    t->genre = genreName(u8(b, 127));
    return true;
}

struct MpegHeader {
    int version = 0;    // 0 MPEG-1, 1 MPEG-2, 2 MPEG-2.5
    int layer = 0;
    int bitrate = 0;    // kbps
    int sampleRate = 0;
    int channels = 0;
    int frameLength = 0;
    int samples = 0;
    bool crc = false;
};

const int kBitrates[5][15] = {
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
};
const int kMpegRates[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};

bool parseMpegHeader(quint32 h, MpegHeader *m)
{
    if ((h & 0xFFE00000u) != 0xFFE00000u)
        return false;
    const int versionBits = (h >> 19) & 3;
    const int layerBits = (h >> 17) & 3;
    const int bitrateIndex = (h >> 12) & 15;
    const int rateIndex = (h >> 10) & 3;
    if (versionBits == 1 || layerBits == 0 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3)
        return false;
    m->version = versionBits == 3 ? 0 : versionBits == 2 ? 1 : 2;
    m->layer = 4 - layerBits;
    m->bitrate = kBitrates[m->version == 0 ? m->layer - 1 : (m->layer == 1 ? 3 : 4)][bitrateIndex];
    m->sampleRate = kMpegRates[m->version][rateIndex];
    m->channels = ((h >> 6) & 3) == 3 ? 1 : 2;
    m->crc = !((h >> 16) & 1);
    const int padding = (h >> 9) & 1;
    if (m->layer == 1) {
        m->frameLength = (12000 * m->bitrate / m->sampleRate + padding) * 4;
        m->samples = 384;
    } else {
        const bool half = m->layer == 3 && m->version != 0;
        m->frameLength = (half ? 72000 : 144000) * m->bitrate / m->sampleRate + padding;
        m->samples = half ? 576 : 1152;
    }
    return true;
}

// The frame at `pos` plus the next three must chain, so junk that looks like a header is rejected.
bool frameChain(const Stream &s, qint64 pos, MpegHeader m)
{
    for (int i = 0; i < 3; ++i) {
        pos += m.frameLength;
        if (pos + 4 > s.size)
            return i >= 1;
        const QByteArray b = s.read(pos, 4);
        if (b.startsWith("TAG") || b.startsWith("APET"))
            return i >= 1;
        MpegHeader next;
        if (!parseMpegHeader(be32(b, 0), &next) || next.version != m.version || next.layer != m.layer
            || next.sampleRate != m.sampleRate)
            return false;
        m = next;
    }
    return true;
}

bool findFrame(const Stream &s, qint64 from, qint64 *pos, MpegHeader *m)
{
    const qint64 end = qMin(s.size, from + kScanLimit);
    for (qint64 base = from; base + 4 <= end;) {
        const QByteArray buf = s.read(base, qMin<qint64>(64 * 1024, end - base));
        if (buf.isEmpty())
            return false;
        for (int i = 0; i + 4 <= buf.size(); ++i) {
            if (u8(buf, i) != 0xFF || (u8(buf, i + 1) & 0xE0) != 0xE0)
                continue;
            if (parseMpegHeader(be32(buf, i), m) && frameChain(s, base + i, *m)) {
                *pos = base + i;
                return true;
            }
        }
        base += buf.size() - 3;
    }
    return false;
}

struct VbrHeader {
    bool present = false;
    qint64 frames = 0;
    qint64 trimSamples = 0;    // encoder delay and padding from the LAME extension
};

VbrHeader readVbrHeader(const QByteArray &first, const MpegHeader &m)
{
    VbrHeader v;
    const int sideInfo = m.version == 0 ? (m.channels == 1 ? 17 : 32) : (m.channels == 1 ? 9 : 17);
    for (int offset : {4 + sideInfo, 6 + sideInfo}) {
        if (offset != 4 + sideInfo && !m.crc)
            continue;
        const QByteArray tag = first.mid(offset, 4);
        if (tag == "Xing" || tag == "Info") {
            const quint32 flags = be32(first, offset + 4);
            qint64 pos = offset + 8;
            v.present = true;
            if (flags & 1) {
                v.frames = be32(first, pos);
                pos += 4;
            }
            pos += (flags & 2 ? 4 : 0) + (flags & 4 ? 100 : 0) + (flags & 8 ? 4 : 0);
            const QByteArray encoder = first.mid(int(pos), 4);
            if (encoder == "LAME" || encoder == "Lavf" || encoder == "Lavc") {
                const quint32 trim = be24(first, pos + 21);
                v.trimSamples = (trim >> 12) + (trim & 0xFFF);
            }
            return v;
        }
    }
    if (first.mid(36, 4) == "VBRI") {
        v.present = true;
        v.frames = be32(first, 36 + 14);
    }
    return v;
}

bool parseMp3(const Stream &s, Collector *c, QString *error)
{
    TrackTags *t = c->tags;
    qint64 audioStart = 0;
    if (!leadingId3(s, c, &audioStart, error))
        return false;
    qint64 pos = 0;
    MpegHeader m;
    if (!findFrame(s, audioStart, &pos, &m))
        return fail(error, "Not a supported audio file");

    qint64 audioEnd = s.size;
    TrackTags v1;
    if (readId3v1(s, &v1)) {
        audioEnd -= 128;
        fillMissing(t, v1);
    }

    const QByteArray first = s.read(pos, qMin<qint64>(qMin<qint64>(m.frameLength, s.size - pos), 256));
    const VbrHeader vbr = readVbrHeader(first, m);
    qint64 audioBytes = qMax<qint64>(0, audioEnd - pos);
    if (vbr.present)
        audioBytes = qMax<qint64>(0, audioBytes - m.frameLength);
    if (vbr.frames > 0) {
        const qint64 samples = vbr.frames * m.samples;
        const qint64 playable = samples > vbr.trimSamples ? samples - vbr.trimSamples : samples;
        t->durationMs = playable * 1000 / m.sampleRate;
        t->bitrate = int(std::lround(audioBytes * 8.0 * m.sampleRate / samples / 1000));
    } else {
        t->durationMs = audioBytes * 8 / m.bitrate;
        t->bitrate = m.bitrate;
    }
    t->codec = QStringLiteral("MP3");
    t->sampleRate = m.sampleRate;
    t->channels = m.channels;
    return true;
}

void readVorbisComments(const QByteArray &d, qint64 pos, Collector *c)
{
    TrackTags *t = c->tags;
    if (pos + 4 > d.size())
        return;
    pos += 4 + qint64(le32(d, pos));
    if (pos + 4 > d.size())
        return;
    const quint32 count = qMin<quint32>(le32(d, pos), 65536);
    pos += 4;

    QHash<QByteArray, QStringList> fields;
    for (quint32 i = 0; i < count && pos + 4 <= d.size(); ++i) {
        const qint64 length = le32(d, pos);
        pos += 4;
        if (length > d.size() - pos)
            break;
        const QByteArray entry = d.mid(int(pos), int(length));
        pos += length;
        const int eq = entry.indexOf('=');
        if (eq <= 0)
            continue;
        const QByteArray key = entry.left(eq).toUpper();
        const QByteArray value = entry.mid(eq + 1);
        if (key == "METADATA_BLOCK_PICTURE")
            addEncodedPicture(value, c);
        else
            fields[key].append(utf8(value));
    }

    auto joined = [&](std::initializer_list<const char *> keys) {
        for (const char *key : keys) {
            const auto it = fields.constFind(key);
            if (it != fields.constEnd())
                return it->join(kSeparator);
        }
        return QString();
    };
    auto first = [&](std::initializer_list<const char *> keys) {
        for (const char *key : keys) {
            const auto it = fields.constFind(key);
            if (it != fields.constEnd() && !it->first().isEmpty())
                return it->first();
        }
        return QString();
    };
    t->title = joined({"TITLE"});
    t->artist = joined({"ARTIST"});
    t->albumArtist = joined({"ALBUMARTIST", "ALBUM ARTIST", "ALBUM_ARTIST"});
    t->album = joined({"ALBUM"});
    t->genre = joined({"GENRE"});
    t->composer = joined({"COMPOSER"});
    t->year = parseYear(first({"DATE", "YEAR"}));
    t->track = leadingInt(first({"TRACKNUMBER", "TRACK"}));
    t->disc = leadingInt(first({"DISCNUMBER", "DISC"}));
    t->lyrics = first({"LYRICS", "UNSYNCEDLYRICS"});
}

bool parseFlac(const Stream &s, Collector *c, QString *error)
{
    TrackTags *t = c->tags;
    qint64 pos = 0;
    if (!leadingId3(s, nullptr, &pos, error))
        return false;
    if (s.read(pos, 4) != "fLaC")
        return fail(error, "Not a supported audio file");
    pos += 4;

    bool haveInfo = false;
    qint64 totalSamples = 0;
    for (bool last = false; !last;) {
        const QByteArray h = s.read(pos, 4);
        if (h.isEmpty())
            return fail(error, "Truncated FLAC metadata");
        last = u8(h, 0) & 0x80;
        const int type = u8(h, 0) & 0x7F;
        const qint64 length = be24(h, 1);
        pos += 4;
        if (type == 127 || length > s.size - pos)
            return fail(error, type == 127 ? "Corrupt FLAC metadata" : "Truncated FLAC metadata");
        if (type == 0) {
            const QByteArray info = length >= 34 ? s.read(pos, 34) : QByteArray();
            if (info.isEmpty())
                return fail(error, "Corrupt FLAC STREAMINFO");
            t->sampleRate = int(be32(info, 10) >> 12);
            t->channels = int((u8(info, 12) >> 1) & 7) + 1;
            t->bitsPerSample = int(((u8(info, 12) & 1) << 4) | (u8(info, 13) >> 4)) + 1;
            totalSamples = qint64((quint64(u8(info, 13) & 0xF) << 32) | be32(info, 14));
            haveInfo = t->sampleRate > 0;
        } else if (type == 4 && length >= 8) {
            readVorbisComments(s.read(pos, length), 0, c);
        } else if (type == 2 && length > 4 && s.read(pos, 4) == "SMFM") {
            c->offerSmfmf(s.read(pos + 4, length - 4), 2);
        } else if (type == 6) {
            const QByteArray head = s.read(pos, qMin<qint64>(length, 16384));
            Picture p;
            if (parsePicture(head, length, &p) && c->offerPicture(p.type))
                c->setCover(p.type, s.read(pos + p.offset, p.length));
        }
        pos += length;
    }
    if (!haveInfo)
        return fail(error, "Corrupt FLAC STREAMINFO");

    t->codec = QStringLiteral("FLAC");
    t->durationMs = totalSamples * 1000 / t->sampleRate;
    if (t->durationMs > 0)
        t->bitrate = int(std::lround((s.size - pos) * 8.0 / t->durationMs));
    return true;
}

class OggPackets
{
public:
    explicit OggPackets(const Stream &s) : m_s(s) {}

    quint32 serial() const { return m_serial; }

    bool next(QByteArray *packet)
    {
        packet->clear();
        for (;;) {
            if (m_seg >= m_segments.size()) {
                if (!nextPage())
                    return false;
                continue;
            }
            const int lace = u8(m_segments, m_seg++);
            packet->append(m_body.constData() + m_bodyPos, lace);
            m_bodyPos += lace;
            if (packet->size() > kMaxRead)
                return false;
            if (lace < 255)
                return true;
        }
    }

private:
    bool nextPage()
    {
        for (;;) {
            const QByteArray head = m_s.read(m_pos, 27);
            if (!head.startsWith("OggS"))
                return false;
            const int count = u8(head, 26);
            QByteArray segments;
            if (count) {
                segments = m_s.read(m_pos + 27, count);
                if (segments.isEmpty())
                    return false;
            }
            qint64 bodySize = 0;
            for (int i = 0; i < count; ++i)
                bodySize += u8(segments, i);
            const qint64 bodyStart = m_pos + 27 + count;
            if (bodySize > m_s.size - bodyStart)
                return false;
            m_pos = bodyStart + bodySize;
            const quint32 serial = le32(head, 14);
            if (!m_haveSerial) {
                m_serial = serial;
                m_haveSerial = true;
            }
            if (serial != m_serial)
                continue;
            m_body.clear();
            if (bodySize) {
                m_body = m_s.read(bodyStart, bodySize);
                if (m_body.isEmpty())
                    return false;
            }
            m_segments = segments;
            m_seg = 0;
            m_bodyPos = 0;
            return true;
        }
    }

    const Stream &m_s;
    qint64 m_pos = 0;
    quint32 m_serial = 0;
    bool m_haveSerial = false;
    QByteArray m_segments;
    QByteArray m_body;
    int m_seg = 0;
    int m_bodyPos = 0;
};

qint64 lastGranule(const Stream &s, quint32 serial)
{
    const qint64 length = qMin<qint64>(s.size, 128 * 1024);
    const QByteArray tail = s.read(s.size - length, length);
    int from = tail.size() - 27;
    while (from >= 0) {
        const int i = tail.lastIndexOf("OggS", from);
        if (i < 0)
            break;
        const qint64 granule = qint64(le64(tail, i + 6));
        if (u8(tail, i + 4) == 0 && le32(tail, i + 14) == serial && granule >= 0)
            return granule;
        from = i - 1;
    }
    return -1;
}

bool parseOgg(const Stream &s, Collector *c, QString *error)
{
    TrackTags *t = c->tags;
    OggPackets packets(s);
    QByteArray id;
    if (!packets.next(&id))
        return fail(error, "Truncated Ogg stream");

    const bool vorbis = id.startsWith(QByteArray("\x01" "vorbis", 7));
    const bool opus = id.startsWith("OpusHead");
    if (!vorbis && !opus)
        return fail(error, "Unsupported Ogg stream");

    int preSkip = 0;
    qint64 nominal = 0;
    if (vorbis) {
        t->codec = QStringLiteral("Vorbis");
        t->channels = u8(id, 11);
        t->sampleRate = int(le32(id, 12));
        nominal = qint32(le32(id, 20));
    } else {
        t->codec = QStringLiteral("Opus");
        t->channels = u8(id, 9);
        preSkip = int(le16(id, 10));
        t->sampleRate = le32(id, 12) ? int(le32(id, 12)) : 48000;
    }
    if (id.size() < (vorbis ? 30 : 19) || t->channels <= 0 || t->sampleRate <= 0)
        return fail(error, "Corrupt Ogg header");

    QByteArray comments;
    if (!packets.next(&comments))
        return fail(error, "Truncated Ogg stream");
    const int prefix = vorbis ? 7 : 8;
    if (!comments.startsWith(vorbis ? QByteArray("\x03" "vorbis", 7) : QByteArray("OpusTags")))
        return fail(error, "Corrupt Ogg comment header");
    readVorbisComments(comments, prefix, c);

    const qint64 granule = lastGranule(s, packets.serial());
    if (granule > (Q_INT64_C(1) << 42))
        return fail(error, "Corrupt Ogg granule position");
    if (granule > 0) {
        t->durationMs = vorbis ? granule * 1000 / t->sampleRate : qMax<qint64>(0, granule - preSkip) * 1000 / 48000;
    }
    if (vorbis && nominal > 0)
        t->bitrate = int(std::lround(nominal / 1000.0));
    else if (t->durationMs > 0)
        t->bitrate = int(std::lround(s.size * 8.0 / t->durationMs));
    return true;
}

constexpr quint32 fourcc(char a, char b, char c, char d)
{
    return (quint32(uchar(a)) << 24) | (quint32(uchar(b)) << 16) | (quint32(uchar(c)) << 8) | quint32(uchar(d));
}

struct Box {
    quint32 type = 0;
    qint64 payload = 0;
    qint64 end = 0;
    bool clamped = false;
};

bool nextBox(const Stream &s, qint64 *pos, qint64 limit, Box *box)
{
    if (limit - *pos < 8)
        return false;
    const QByteArray h = s.read(*pos, qMin<qint64>(16, limit - *pos));
    quint64 size = be32(h, 0);
    qint64 headerLength = 8;
    if (size == 1) {
        if (h.size() < 16)
            return false;
        size = be64(h, 8);
        headerLength = 16;
    } else if (size == 0) {
        size = quint64(limit - *pos);
    }
    if (h.isEmpty() || size < quint64(headerLength))
        return false;
    box->type = be32(h, 4);
    box->payload = *pos + headerLength;
    box->clamped = size > quint64(limit - *pos);
    box->end = box->clamped ? limit : *pos + qint64(size);
    *pos = box->end;
    return true;
}

bool findBox(const Stream &s, qint64 from, qint64 to, quint32 type, Box *box)
{
    qint64 pos = from;
    while (nextBox(s, &pos, to, box)) {
        if (box->type == type)
            return true;
    }
    return false;
}

struct Mp4Audio {
    bool found = false;
    QString codec;
    int sampleRate = 0;
    int channels = 0;
    int bits = 0;
    qint64 bitrate = 0;    // bps declared by the stream, 0 if unknown
    qint64 durationMs = 0;
};

bool readDescriptor(const QByteArray &d, int *pos, int *tag, int *length)
{
    if (*pos >= d.size())
        return false;
    *tag = u8(d, (*pos)++);
    int len = 0;
    for (int i = 0; i < 4; ++i) {
        if (*pos >= d.size())
            return false;
        const int b = u8(d, (*pos)++);
        len = (len << 7) | (b & 0x7F);
        if (!(b & 0x80))
            break;
    }
    *length = len;
    return true;
}

void parseEsds(const QByteArray &d, Mp4Audio *a, int *ascRate)
{
    int pos = 4;
    int tag = 0;
    int length = 0;
    if (!readDescriptor(d, &pos, &tag, &length) || tag != 3)
        return;
    pos += 2;
    const int flags = u8(d, pos++);
    if (flags & 0x80)
        pos += 2;
    if (flags & 0x40)
        pos += 1 + u8(d, pos);
    if (flags & 0x20)
        pos += 2;
    if (!readDescriptor(d, &pos, &tag, &length) || tag != 4)
        return;
    a->bitrate = be32(d, pos + 9);
    pos += 13;
    if (!readDescriptor(d, &pos, &tag, &length) || tag != 5 || length < 2)
        return;
    static const int rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000,
                                11025, 8000, 7350};
    const int index = ((u8(d, pos) & 7) << 1) | (u8(d, pos + 1) >> 7);
    if (index < int(sizeof rates / sizeof *rates))
        *ascRate = rates[index];
}

bool parseAudioTrack(const Stream &s, const Box &trak, Mp4Audio *a)
{
    Box mdia, mdhd, minf, stbl, stsd;
    if (!findBox(s, trak.payload, trak.end, fourcc('m', 'd', 'i', 'a'), &mdia)
        || !findBox(s, mdia.payload, mdia.end, fourcc('m', 'd', 'h', 'd'), &mdhd)
        || !findBox(s, mdia.payload, mdia.end, fourcc('m', 'i', 'n', 'f'), &minf)
        || !findBox(s, minf.payload, minf.end, fourcc('s', 't', 'b', 'l'), &stbl)
        || !findBox(s, stbl.payload, stbl.end, fourcc('s', 't', 's', 'd'), &stsd))
        return false;

    const QByteArray header = s.read(mdhd.payload, qMin<qint64>(mdhd.end - mdhd.payload, 32));
    const bool wide = u8(header, 0) == 1;
    const quint32 timescale = be32(header, wide ? 20 : 12);
    const quint64 duration = wide ? be64(header, 24) : be32(header, 16);
    if (timescale && duration < (Q_UINT64_C(1) << 50))
        a->durationMs = qint64(duration * 1000 / timescale);

    const QByteArray table = s.read(stsd.payload, qMin<qint64>(stsd.end - stsd.payload, 65536));
    const QByteArray entry = table.mid(8, int(qMin<quint32>(be32(table, 8), quint32(table.size()))));
    const quint32 format = be32(entry, 4);
    const bool aac = format == fourcc('m', 'p', '4', 'a');
    const bool alac = format == fourcc('a', 'l', 'a', 'c');
    const int soundVersion = int(be16(entry, 16));
    if ((!aac && !alac) || entry.size() < 36 || soundVersion > 1)
        return false;

    a->channels = int(be16(entry, 24));
    a->sampleRate = int(be32(entry, 32) >> 16);
    const int childStart = soundVersion == 1 ? 52 : 36;
    QBuffer buffer;
    buffer.setData(entry);
    buffer.open(QIODevice::ReadOnly);
    const Stream children{&buffer, entry.size()};
    int ascRate = 0;
    Box child;
    for (qint64 pos = childStart; nextBox(children, &pos, entry.size(), &child);) {
        const QByteArray body = children.read(child.payload, child.end - child.payload);
        if (aac && child.type == fourcc('e', 's', 'd', 's')) {
            parseEsds(body, a, &ascRate);
        } else if (alac && child.type == fourcc('a', 'l', 'a', 'c') && body.size() >= 28) {
            a->bits = u8(body, 9);
            a->channels = u8(body, 13) ? u8(body, 13) : a->channels;
            a->bitrate = be32(body, 20);
            a->sampleRate = be32(body, 24) ? int(be32(body, 24)) : a->sampleRate;
        }
    }
    // The 16.16 field cannot hold rates above 65535 and encoders write other values there for
    // such streams; HE-AAC files carry the core rate in the config. The larger one is the output rate.
    a->sampleRate = qMax(a->sampleRate, ascRate);
    if (!a->sampleRate)
        a->sampleRate = int(timescale);
    a->codec = aac ? QStringLiteral("AAC") : QStringLiteral("ALAC");
    a->found = true;
    return true;
}

void parseIlst(const Stream &s, const Box &ilst, Collector *c)
{
    TrackTags *t = c->tags;
    const quint32 covr = fourcc('c', 'o', 'v', 'r');
    qint64 pos = ilst.payload;
    Box item;
    while (nextBox(s, &pos, ilst.end, &item)) {
        qint64 inner = item.payload;
        Box data;
        while (nextBox(s, &inner, item.end, &data)) {
            if (data.type != fourcc('d', 'a', 't', 'a') || data.end - data.payload <= 8)
                continue;
            if (item.type == covr) {
                if (c->offerPicture(3))
                    c->setCover(3, s.read(data.payload + 8, data.end - data.payload - 8));
                continue;
            }
            const QByteArray value = s.read(data.payload, qMin<qint64>(data.end - data.payload, 1 << 20)).mid(8);
            const QString text = utf8(value);
            auto set = [&](QString &dst) {
                if (dst.isEmpty())
                    dst = text;
            };
            if (item.type == fourcc('\xA9', 'n', 'a', 'm'))
                set(t->title);
            else if (item.type == fourcc('\xA9', 'A', 'R', 'T'))
                set(t->artist);
            else if (item.type == fourcc('a', 'A', 'R', 'T'))
                set(t->albumArtist);
            else if (item.type == fourcc('\xA9', 'a', 'l', 'b'))
                set(t->album);
            else if (item.type == fourcc('\xA9', 'g', 'e', 'n'))
                set(t->genre);
            else if (item.type == fourcc('g', 'n', 'r', 'e') && t->genre.isEmpty())
                t->genre = genreName(int(be16(value, 0)) - 1);
            else if (item.type == fourcc('\xA9', 'd', 'a', 'y'))
                t->year = t->year ? t->year : parseYear(text);
            else if (item.type == fourcc('\xA9', 'w', 'r', 't'))
                set(t->composer);
            else if (item.type == fourcc('t', 'r', 'k', 'n'))
                t->track = t->track ? t->track : int(be16(value, 2));
            else if (item.type == fourcc('d', 'i', 's', 'k'))
                t->disc = t->disc ? t->disc : int(be16(value, 2));
            else if (item.type == fourcc('\xA9', 'l', 'y', 'r'))
                set(t->lyrics);
            break;
        }
    }
}

void parseEmbeddedId3(const Stream &s, qint64 pos, qint64 length, Collector *c);

void parseMp4Metadata(const Stream &s, const Box &parent, Collector *c)
{
    Box meta, ilst;
    if (!findBox(s, parent.payload, parent.end, fourcc('m', 'e', 't', 'a'), &meta))
        return;
    if (findBox(s, meta.payload + 4, meta.end, fourcc('i', 'l', 's', 't'), &ilst))
        parseIlst(s, ilst, c);
}

// moov/meta/ID32 (version/flags, language, then an ID3v2 tag): where the
// media store looks for an MP4's 12 TONE ANALYSIS. Only that is taken from it.
void parseMp4Smfmf(const Stream &s, const Box &moov, Collector *c)
{
    Box meta, id32;
    if (!findBox(s, moov.payload, moov.end, fourcc('m', 'e', 't', 'a'), &meta)
        || !findBox(s, meta.payload + 4, meta.end, fourcc('I', 'D', '3', '2'), &id32) || id32.end - id32.payload <= 6)
        return;
    TrackTags scratch;
    Collector id3{&scratch, false, {}};
    parseEmbeddedId3(s, id32.payload + 6, id32.end - id32.payload - 6, &id3);
    if (id3.smfmfRank > 0) {
        c->tags->sensme = scratch.sensme;
        c->tags->highlightMs = scratch.highlightMs;
    }
}

bool parseMp4(const Stream &s, Collector *c, QString *error)
{
    TrackTags *t = c->tags;
    Box moov, box;
    bool haveMoov = false;
    qint64 mdatBytes = 0;
    for (qint64 pos = 0; nextBox(s, &pos, s.size, &box);) {
        if (box.type == fourcc('m', 'o', 'o', 'v') && !haveMoov) {
            if (box.clamped)
                return fail(error, "Truncated MP4 header");
            moov = box;
            haveMoov = true;
        } else if (box.type == fourcc('m', 'd', 'a', 't')) {
            mdatBytes += box.end - box.payload;
        }
    }
    if (!haveMoov)
        return fail(error, "No moov atom");

    Mp4Audio audio;
    Box trak;
    for (qint64 pos = moov.payload; !audio.found && nextBox(s, &pos, moov.end, &trak);) {
        if (trak.type == fourcc('t', 'r', 'a', 'k'))
            parseAudioTrack(s, trak, &audio);
    }
    if (!audio.found || audio.sampleRate <= 0 || audio.channels <= 0)
        return fail(error, "No audio track");

    Box udta;
    if (findBox(s, moov.payload, moov.end, fourcc('u', 'd', 't', 'a'), &udta))
        parseMp4Metadata(s, udta, c);
    parseMp4Metadata(s, moov, c);
    parseMp4Smfmf(s, moov, c);

    t->codec = audio.codec;
    t->sampleRate = audio.sampleRate;
    t->channels = audio.channels;
    t->bitsPerSample = audio.bits;
    t->durationMs = audio.durationMs;
    const bool lossless = audio.codec == QLatin1String("ALAC");
    if (!lossless && audio.bitrate > 0)
        t->bitrate = int(std::lround(audio.bitrate / 1000.0));
    else if (t->durationMs > 0 && mdatBytes > 0)
        t->bitrate = int(std::lround(mdatBytes * 8.0 / t->durationMs));
    else
        t->bitrate = int(std::lround(audio.bitrate / 1000.0));
    return true;
}

void parseEmbeddedId3(const Stream &s, qint64 pos, qint64 length, Collector *c)
{
    Id3Header h;
    if (readId3Header(s, pos, &h) && h.total <= length)
        parseId3(s, pos, h, c);
}

void parseWavInfo(const QByteArray &d, TrackTags *t)
{
    qint64 pos = 0;
    while (pos + 8 <= d.size()) {
        const QByteArray id = d.mid(int(pos), 4);
        const qint64 size = le32(d, pos + 4);
        pos += 8;
        if (size > d.size() - pos)
            break;
        const QString value = stripNuls(legacyText(d.mid(int(pos), int(size))));
        pos += size + (size & 1);
        if (value.isEmpty())
            continue;
        if (id == "INAM")
            t->title = value;
        else if (id == "IART")
            t->artist = value;
        else if (id == "IPRD")
            t->album = value;
        else if (id == "IGNR")
            t->genre = value;
        else if (id == "ICRD")
            t->year = parseYear(value);
        else if (id == "ITRK" || id == "IPRT")
            t->track = leadingInt(value);
        else if (id == "IMUS" || id == "ICMP")
            t->composer = value;
    }
}

bool parseWav(const Stream &s, Collector *c, QString *error)
{
    TrackTags *t = c->tags;
    TrackTags info;
    bool haveFormat = false;
    bool haveData = false;
    int blockAlign = 0;
    quint32 byteRate = 0;
    qint64 dataBytes = 0;
    for (qint64 pos = 12; pos + 8 <= s.size;) {
        const QByteArray h = s.read(pos, 8);
        const QByteArray id = h.left(4);
        const qint64 size = le32(h, 4);
        const qint64 payload = pos + 8;
        const qint64 available = s.size - payload;
        if (id == "data") {
            const bool open = size == 0 || size == 0xFFFFFFFFu;
            if (size > available && !open)
                return fail(error, "Truncated WAV file");
            haveData = true;
            dataBytes = open ? available : size;
            if (open)
                break;
        } else if (size > available) {
            break;
        } else if (id == "fmt " && size >= 16) {
            const QByteArray f = s.read(payload, qMin<qint64>(size, 40));
            int tag = int(le16(f, 0));
            if (tag == 0xFFFE && f.size() >= 26)
                tag = int(le16(f, 24));
            if (tag != 1 && tag != 3)
                return fail(error, "Unsupported WAV format");
            t->channels = int(le16(f, 2));
            t->sampleRate = int(le32(f, 4));
            byteRate = le32(f, 8);
            blockAlign = int(le16(f, 12));
            t->bitsPerSample = int(le16(f, 14));
            const int valid = f.size() >= 20 ? int(le16(f, 18)) : 0;
            if (le16(f, 0) == 0xFFFE && valid > 0 && valid <= t->bitsPerSample)
                t->bitsPerSample = valid;
            haveFormat = t->channels > 0 && t->sampleRate > 0 && t->bitsPerSample > 0;
        } else if (id == "LIST" && size >= 4 && size <= kMaxRead) {
            const QByteArray list = s.read(payload, size);
            if (list.startsWith("INFO"))
                parseWavInfo(list.mid(4), &info);
        } else if (id == "id3 " || id == "ID3 ") {
            parseEmbeddedId3(s, payload, size, c);
        }
        pos = payload + size + (size & 1);
    }
    if (!haveFormat || !haveData)
        return fail(error, "Corrupt WAV file");

    fillMissing(t, info);
    t->codec = QStringLiteral("WAV");
    if (!blockAlign)
        blockAlign = (t->bitsPerSample + 7) / 8 * t->channels;
    t->durationMs = dataBytes / blockAlign * 1000 / t->sampleRate;
    t->bitrate = int(std::lround((byteRate ? byteRate * 8.0 : double(t->sampleRate) * t->channels * t->bitsPerSample) / 1000));
    return true;
}

bool parseAiff(const Stream &s, Collector *c, QString *error)
{
    TrackTags *t = c->tags;
    TrackTags text;
    bool haveFormat = false;
    qint64 frames = 0;
    for (qint64 pos = 12; pos + 8 <= s.size;) {
        const QByteArray h = s.read(pos, 8);
        const QByteArray id = h.left(4);
        const qint64 size = be32(h, 4);
        const qint64 payload = pos + 8;
        const qint64 available = s.size - payload;
        if (id == "SSND") {
            if (size > available)
                return fail(error, "Truncated AIFF file");
        } else if (size > available) {
            break;
        } else if (id == "COMM" && size >= 18) {
            const QByteArray f = s.read(payload, 18);
            t->channels = int(be16(f, 0));
            frames = be32(f, 2);
            t->bitsPerSample = int(be16(f, 6));
            const int exponent = int(be16(f, 8) & 0x7FFF);
            const quint64 mantissa = be64(f, 10);
            const double rate = exponent == 0 || exponent == 0x7FFF
                ? 0.0
                : std::ldexp(double(mantissa), exponent - 16383 - 63);
            t->sampleRate = rate >= 1 && rate < 1e7 ? int(std::lround(rate)) : 0;
            haveFormat = t->channels > 0 && t->sampleRate > 0 && t->bitsPerSample > 0;
        } else if ((id == "NAME" || id == "AUTH") && size <= 65536) {
            const QString value = stripNuls(legacyText(s.read(payload, size)));
            (id == "NAME" ? text.title : text.artist) = value;
        } else if (id == "ID3 " || id == "id3 ") {
            parseEmbeddedId3(s, payload, size, c);
        }
        pos = payload + size + (size & 1);
    }
    if (!haveFormat)
        return fail(error, "Corrupt AIFF file");

    fillMissing(t, text);
    t->codec = QStringLiteral("AIFF");
    t->durationMs = frames * 1000 / t->sampleRate;
    t->bitrate = int(std::lround(double(t->sampleRate) * t->channels * t->bitsPerSample / 1000));
    return true;
}

bool load(const QString &path, TrackTags *tags, QByteArray *cover, QString *error)
{
    *tags = TrackTags();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, "Cannot open file");
    const Stream s{&file, file.size()};
    if (s.size < 12)
        return fail(error, "Not a supported audio file");

    Collector c{tags, cover != nullptr, {}};
    const QByteArray head = s.read(0, 12);
    bool ok = false;
    if (head.startsWith("fLaC")) {
        ok = parseFlac(s, &c, error);
    } else if (head.startsWith("OggS")) {
        ok = parseOgg(s, &c, error);
    } else if (head.startsWith("RIFF") && head.mid(8, 4) == "WAVE") {
        ok = parseWav(s, &c, error);
    } else if (head.startsWith("FORM") && (head.mid(8, 4) == "AIFF" || head.mid(8, 4) == "AIFC")) {
        ok = parseAiff(s, &c, error);
    } else if (head.mid(4, 4) == "ftyp") {
        ok = parseMp4(s, &c, error);
    } else {
        qint64 audioStart = 0;
        if (!leadingId3(s, nullptr, &audioStart, error))
            return false;
        ok = s.read(audioStart, 4) == "fLaC" ? parseFlac(s, &c, error) : parseMp3(s, &c, error);
    }
    if (!ok) {
        *tags = TrackTags();
        return false;
    }
    // Control characters mark a broken tag (seen: Hangul cut to its low 7 bits). Empty, the
    // library falls back to the file name.
    for (QString *field : {&tags->title, &tags->artist, &tags->albumArtist, &tags->album, &tags->genre,
                           &tags->composer}) {
        if (std::any_of(field->cbegin(), field->cend(), [](QChar ch) { return ch.unicode() < 0x20 && ch != '\t'; }))
            field->clear();
    }
    if (cover)
        *cover = c.cover;
    return true;
}

} // namespace

bool readTags(const QString &path, TrackTags *tags, QString *error)
{
    TrackTags scratch;
    return load(path, tags ? tags : &scratch, nullptr, error);
}

QByteArray readCover(const QString &path)
{
    TrackTags tags;
    QByteArray cover;
    return load(path, &tags, &cover, nullptr) ? cover : QByteArray();
}

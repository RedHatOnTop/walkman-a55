#include "sensme.h"

#include <QtCore/QFile>
#include <QtCore/QVector>
#include <QtCore/QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace SensMe {
namespace {

// Feature words as the media store numbers them; words 0..3 (STSA, GBPM,
// STNM) are not used by the classifier and not kept.
const int kWords = 90;
const int kFirstWord = 4;
const int kMissing = 0xff;

enum ChunkType { GBPM, GVNM, STBF, STMM, STSA, STMO, STHF, STNM, ELYR, VNDM, Unknown };

const char *const kTags[] = {"GBPM", "GVNM", "STBF", "STMM", "STSA", "STMO", "STHF", "STNM", "ELYR", "VNDM"};

// Feature slot (word - 14) of each STHF entry 0x800 + n.
const int kSthfSlot[40] = {15, 16, 12, 1, 17, 4, 0, 13, 5, 7, 18, 10, 9, 19, 6, 8, 20, 21, 22, 2,
                           23, 24, 25, 26, 27, 3, 28, 29, 30, 31, 32, 14, 33, 34, 35, 36, 11, 37, 38, 39};

// The 16 words of the cluster distance, in centroid order.
const int kClusterWords[16] = {81, 79, 55, 86, 88, 63, 76, 84, 56, 61, 87, 77, 58, 75, 89, 60};

const int kClusters = 48;

// The immediates of the media store's channel tests, in the order rule() reads them.
const int kLimits = 68;

struct Model {
    double scale[16];
    double centroid[kClusters][16];
    qint32 time[kClusters];
    qint32 mood[kClusters + 1];
    quint8 limit[kLimits];
};

Model model;
bool modelLoaded = false;

struct Chunk {
    int type;
    quint32 pos;
    quint32 length;
};

quint32 be32(const QByteArray &b, quint32 off)
{
    return qFromBigEndian<quint32>(b.constData() + off);
}

int chunkType(const QByteArray &b, quint32 pos)
{
    for (int i = 0; i < Unknown; ++i) {
        if (std::memcmp(b.constData() + pos, kTags[i], 4) == 0)
            return i;
    }
    return Unknown;
}

// The ranges the media store reads while walking past a chunk; a chunk whose
// reads run off the blob ends the walk, as a short read did there.
bool readable(const QByteArray &b, const Chunk &c)
{
    const quint32 data = c.pos + 20;
    const quint32 n = c.length;
    // Offsets wrap at 32 bits, as the media store computes them.
    auto fits = [&](quint32 off, quint32 length) { return quint64(off) + length <= quint64(b.size()); };
    switch (c.type) {
    case GBPM:
        return n != 4 || fits(data, 4);
    case GVNM:
        return n != 8 || fits(data, 8);
    case STBF:
        return n != 36 || fits(data, 36);
    case STNM:
        return n != 6 || fits(data, 6);
    case STSA:
        return !(n % 4 == 0 && n > 7 && n / 4 - 1 < 59) || fits(data, n);
    case STMO:
        return !(n % 16 == 0 && n < 0x404 && n / 4 != 0) || fits(data, n);
    case STHF:
        return !(n % 4 == 0 && n >> 4 < 0x3f && n / 4 != 0) || fits(data, n);
    case ELYR: {
        if (n < 12)
            return true;
        if (!fits(data, 4))
            return false;
        const quint32 first = be32(b, data);
        if (first >= 0xfffffffc || first + 12 > n)
            return true;
        if (!fits(c.pos + first + 24, 4))
            return false;
        const quint32 second = be32(b, c.pos + first + 24);
        const quint32 end = first + second;
        if (second >= end + 8 || end + 12 > n)
            return true;
        return fits(data + end + 8, 4);
    }
    default:
        return true;
    }
}

// The media store walks twice: reading payloads to fetch a chunk, headers
// only to count chunks of a type.
QVector<Chunk> chunks(const QByteArray &b, bool payloads = true)
{
    QVector<Chunk> out;
    quint32 pos = 0;
    while (quint64(pos) + 20 <= quint64(b.size())) {
        if (be32(b, pos) == 0)
            break;
        const Chunk c{chunkType(b, pos), pos, be32(b, pos + 16)};
        if (c.length >= 0x80000000u || (payloads && !readable(b, c)))
            break;
        out.append(c);
        pos += 20 + c.length;
    }
    return out;
}

// The size checks a chunk must pass before its payload is used.
bool valid(const Chunk &c)
{
    const quint32 n = c.length;
    switch (c.type) {
    case GBPM:
        return n == 4;
    case GVNM:
        return n == 8;
    case STBF:
        return n == 36;
    case STMM:
    case VNDM:
        return n != 0;
    case STSA:
        return n % 4 == 0 && n > 7 && n / 4 - 1 < 59;
    case STMO:
        return n % 16 == 0 && n < 0x404;
    case STHF:
        return n % 4 == 0 && n >> 4 < 0x3f;
    case STNM:
        return n == 6;
    default:
        return true;
    }
}

// The first chunk of a type, when it passes its checks; a later one never stands in.
const Chunk *first(const QVector<Chunk> &all, int type)
{
    for (const Chunk &c : all) {
        if (c.type == type)
            return valid(c) ? &c : nullptr;
    }
    return nullptr;
}

struct Words {
    int w[kWords];
    int operator[](int i) const { return w[i]; }
};

bool words(const QByteArray &features, Words *f)
{
    if (features.size() != kFeatureCount)
        return false;
    std::fill(f->w, f->w + kFirstWord, 0);
    for (int i = 0; i < kFeatureCount; ++i)
        f->w[kFirstWord + i] = uchar(features.at(i));
    return true;
}

int cluster(const Words &f)
{
    double x[16];
    for (int i = 0; i < 16; ++i) {
        const int v = f[kClusterWords[i]];
        if (v == kMissing)
            return -1;
        x[i] = double(v) * model.scale[i];
    }
    int best = 0;
    double bestDistance = 0;
    for (int c = 0; c < kClusters; ++c) {
        double d = 0;
        for (int i = 0; i < 16; ++i)
            d += std::fabs(x[i] - model.centroid[c][i]);
        if (best == 0 || d < bestDistance) {
            best = c + 1;
            bestDistance = d;
        }
    }
    return best;
}

bool present(const Words &f, std::initializer_list<int> words)
{
    for (int w : words) {
        if (f[w] == kMissing)
            return false;
    }
    return true;
}

// lo <= v < lo + n, as the media store tests a range.
bool within(int v, int lo, int n)
{
    return unsigned(v - lo) < unsigned(n);
}

// The media store's per-channel rule: 1 in, 0 out, -1 undecided. Moods are
// 0..7 (Active..Extreme); 9 asks whether the track is excluded from all.
int rule(int k, const Words &f, int c)
{
    const quint8 *t = model.limit;
    if (f[78] == 0 || c == t[0])
        return k == 9;
    if (present(f, {34, 32, 24}) && f[24] <= t[1] && f[32] <= t[2] && f[34] >= t[3])
        return k == 9;
    if (present(f, {20, 55, 34}) && f[20] >= t[4] && (f[55] > t[5] || f[34] >= t[6]))
        return k == 9;

    int r = -1;
    switch (k) {
    case 0:
        if (rule(7, f, c) == 1 || rule(6, f, c) == 1)
            return 0;
        if (f[8] == kMissing) {
            if (f[55] == kMissing)
                return -1;
        } else {
            if (f[55] == kMissing)
                return -1;
            if (present(f, {79, 56, 5, 10, 19})) {
                r = 0;
                if (f[8] >= t[7] && f[55] >= t[8] && f[79] >= t[9] && f[19] <= t[10]
                    && (f[10] > t[11] || f[56] > t[12] || f[5] > t[13]))
                    return 1;
            }
        }
        if (present(f, {79, 29}))
            r = f[29] > t[14] && f[55] > t[15] && f[79] > t[16];
        return r;
    case 1:
        if (rule(2, f, c) == 1)
            return 0;
        if (present(f, {19, 55, 6})) {
            r = 0;
            if (f[19] > t[17] && f[55] < t[18] && f[6] < t[19])
                return 1;
        }
        if (present(f, {4, 57, 78})) {
            r = 0;
            if (f[78] < t[20] && f[4] > t[21] && f[57] > t[22])
                return 1;
        }
        if (present(f, {11, 26}))
            r = f[11] > t[23] && f[26] < t[24];
        return r;
    case 2:
        if (present(f, {6, 55, 58, 27}))
            return f[6] > t[25] && f[55] < t[26] && f[58] < t[27] && f[27] < t[28];
        return -1;
    case 3:
        if (rule(7, f, c) == 1 || rule(4, f, c) == 1 || rule(1, f, c) == 1 || rule(2, f, c) == 1)
            return 0;
        if (f[9] == kMissing)
            return -1;
        if (present(f, {55, 78, 27, 32})) {
            r = 0;
            if (f[78] > t[29] && f[55] < t[30] && f[9] > t[31] && f[27] > t[32] && f[32] < t[33])
                return 1;
        }
        if (present(f, {6, 58, 55, 52, 28, 83, 81}))
            r = f[81] > t[34] && f[9] > t[35] && f[6] < t[36] && within(f[55], t[37], t[38]) && f[58] > t[39]
                && f[52] < t[40] && f[28] < t[41] && f[83] < t[42];
        return r;
    case 4:
        if (within(c, t[43], t[44]))
            return 1;
        if (f[19] == kMissing || f[11] == kMissing || f[78] == kMissing || f[55] == kMissing)
            return -1;
        return f[78] > t[45] && f[19] > t[46] && f[11] < t[47] && f[55] < t[48];
    case 5:
        if (c == t[49])
            return 0;
        if (f[51] == kMissing)
            return -1;
        if (f[23] != kMissing && present(f, {18, 25, 20, 48, 42, 29, 57, 34})) {
            r = 0;
            if ((f[51] > t[50] || f[23] > t[51]) && f[18] >= t[52] && f[25] <= t[53] && f[20] <= t[54]
                && (f[48] > t[55] || f[42] > t[56]))
                return 1;
        }
        if (present(f, {33, 55, 79, 24}))
            r = f[24] < t[57] && (f[51] > t[58] || f[33] > t[59]) && f[55] < t[60] && f[79] < t[61];
        return r;
    case 6:
        if (rule(7, f, c) == 1)
            return 0;
        if (c == t[62])
            return 1;
        if (present(f, {19, 24, 53}))
            return f[19] <= t[63] && (f[24] > t[64] || f[53] > t[65]);
        return -1;
    case 7:
        if (present(f, {52, 80}))
            return f[52] > t[66] && f[80] > t[67];
        return -1;
    default:
        return -1;
    }
}

}

bool parse(const QByteArray &smfmf, QByteArray *features, qint32 *sabiMs)
{
    const QVector<Chunk> all = chunks(smfmf);
    Words f;
    std::fill(f.w, f.w + kWords, kMissing);
    bool any = false;
    if (const Chunk *c = first(all, STBF)) {
        for (int i = 0; i < 36; ++i)
            f.w[54 + i] = uchar(smfmf.at(int(c->pos) + 20 + i));
        any = true;
    }
    // The media store's switch over entry types falls through: an entry also
    // fills the slots of every later type, until a later entry overwrites them.
    if (const Chunk *c = first(all, STMO)) {
        for (quint32 off = c->pos + 20; off < c->pos + 20 + c->length; off += 4) {
            const int type = qFromBigEndian<quint16>(smfmf.constData() + off);
            if (uchar(smfmf.at(int(off) + 2)) != 0 || type < 0x100 || type > 0x109)
                continue;
            for (int n = type - 0x100; n < 10; ++n)
                f.w[4 + n] = uchar(smfmf.at(int(off) + 3));
        }
        any = true;
    }
    if (const Chunk *c = first(all, STHF)) {
        for (quint32 off = c->pos + 20; off < c->pos + 20 + c->length; off += 4) {
            const int type = qFromBigEndian<quint16>(smfmf.constData() + off);
            if (type < 0x800 || type > 0x827)
                continue;
            for (int n = type - 0x800; n < 40; ++n)
                f.w[14 + kSthfSlot[n]] = uchar(smfmf.at(int(off) + 2));
        }
        any = true;
    }
    // The highlight is the last STSA by the header count, which a short
    // payload earlier on hides from the fetching walk.
    *sabiMs = -1;
    int last = -1;
    for (const Chunk &c : chunks(smfmf, false))
        last += c.type == STSA;
    for (const Chunk &c : all) {
        if (c.type == STSA && last-- == 0) {
            if (valid(c))
                *sabiMs = qint32(be32(smfmf, c.pos + 20));
            break;
        }
    }
    features->resize(kFeatureCount);
    for (int i = 0; i < kFeatureCount; ++i)
        (*features)[i] = char(f.w[kFirstWord + i]);
    return any;
}

bool loadModel(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    if (data.size() != int(sizeof model.scale + sizeof model.centroid + sizeof model.time + sizeof model.mood
                           + sizeof model.limit))
        return false;
    const char *p = data.constData();
    auto take = [&p](void *to, size_t size) {
        std::memcpy(to, p, size);
        p += size;
    };
    take(model.scale, sizeof model.scale);
    take(model.centroid, sizeof model.centroid);
    take(model.time, sizeof model.time);
    take(model.mood, sizeof model.mood);
    take(model.limit, sizeof model.limit);
    modelLoaded = true;
    return true;
}

int channels(const QByteArray &features, int *clusterOut)
{
    if (clusterOut)
        *clusterOut = 0;
    Words f;
    if (!modelLoaded || !words(features, &f))
        return -1;
    const int c = cluster(f);
    if (c < 1)
        return 0;
    if (clusterOut)
        *clusterOut = c;

    const bool excluded = rule(9, f, c) == 1;
    int mask = 0;
    if (!excluded)
        mask |= 1 << model.time[c - 1];
    bool mood = false;
    for (int k = 0; k < 8; ++k) {
        if (rule(k, f, c) == 1) {
            mask |= 1 << (Active + k);
            mood = true;
        }
    }
    const int fallback = model.mood[c];
    if (!mood && !excluded && fallback >= 0 && fallback < 8)
        mask |= 1 << (Active + fallback);
    return mask;
}

}

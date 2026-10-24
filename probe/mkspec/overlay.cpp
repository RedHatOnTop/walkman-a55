#include "overlay.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QResource>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QVector>

#include <algorithm>

#include <link.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

QT_BEGIN_NAMESPACE
bool qRegisterResourceData(int, const unsigned char *, const unsigned char *, const unsigned char *);
bool qUnregisterResourceData(int, const unsigned char *, const unsigned char *, const unsigned char *);
QT_END_NAMESPACE

#define OLOG(fmt, ...) fprintf(stderr, "[wmmod] " fmt "\n", ##__VA_ARGS__)

namespace {

enum { Compressed = 1, Directory = 2, NodeSize = 14, MaxNodes = 100000 };

quint32 be32(const uchar *p) { return quint32(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
quint16 be16(const uchar *p) { return quint16(p[0] << 8 | p[1]); }

// rcc's qt_hash() over UTF-16 code units.
quint32 qtHash(const QString &s)
{
    quint32 h = 0;
    for (QChar c : s) {
        h = (h << 4) + c.unicode();
        h ^= (h & 0xf0000000) >> 23;
        h &= 0x0fffffff;
    }
    return h;
}

struct Root
{
    const uchar *tree = nullptr;
    const uchar *names = nullptr;
    const uchar *namesEnd = nullptr;
    const uchar *data = nullptr;
    QHash<QString, quint32> files;  // path -> data offset
};

// Parses rcc v1 tables inside [begin, end). A .rcc header gives all three
// offsets. In an executable the compiler emits rcc's three arrays back to
// back, possibly with zero alignment padding (none on the device, 32-byte
// alignment from x86-64 gcc), so the name table is searched for just past
// the tree and the data table just past the names.
class Parser
{
public:
    enum { MaxPadding = 64 };

    Parser(const uchar *begin, const uchar *end) : m_begin(begin), m_end(end) {}

    // Walks the tree and reads every name; names == nullptr searches for them.
    bool parse(const uchar *tree, const uchar *names, Root &root)
    {
        root = Root();
        root.tree = tree;
        QVector<int> todo(1, 0);
        QHash<int, int> parent;
        int maxIndex = 0, seen = 0;
        while (!todo.isEmpty()) {
            const int i = todo.takeLast();
            const uchar *p = tree + NodeSize * i;
            if (++seen > MaxNodes || !in(p, NodeSize))
                return false;
            const quint16 flags = be16(p + 4);
            if (flags & ~(Compressed | Directory))
                return false;
            if (flags & Directory) {
                const quint32 count = be32(p + 6), child = be32(p + 10);
                if (count > 10000 || child == 0 || child <= quint32(i))
                    return false;
                for (quint32 c = child; c < child + count; ++c) {
                    todo.append(c);
                    parent.insert(c, i);
                }
            }
            maxIndex = qMax(maxIndex, i);
        }

        QHash<int, QString> nameOf;
        const uchar *treeEnd = tree + NodeSize * (maxIndex + 1);
        for (const uchar *n = names ? names : treeEnd; ; ++n) {
            if (readNames(tree, n, maxIndex, parent, nameOf, root)) {
                root.names = n;
                break;
            }
            if (names || n - treeEnd >= MaxPadding || !in(n, 1) || *n)
                return false;
        }

        for (auto it = nameOf.constBegin(); it != nameOf.constEnd(); ++it) {
            const uchar *p = tree + NodeSize * it.key();
            if (be16(p + 4) & Directory)
                continue;
            QString path = it.value();
            for (int up = parent.value(it.key()); up != 0; up = parent.value(up))
                path.prepend(nameOf.value(up) + '/');
            root.files.insert('/' + path, be32(p + 10));
        }
        return !root.files.isEmpty();
    }

    // Whether data fits the file entries: rcc writes (u32 size, payload)
    // back to back in offset order.
    bool fitsData(const Root &root, const uchar *data) const
    {
        QVector<quint32> offsets = QVector<quint32>::fromList(root.files.values());
        std::sort(offsets.begin(), offsets.end());
        for (int k = 0; k < offsets.size(); ++k) {
            const uchar *entry = data + offsets[k];
            if (!in(entry, 4) || !in(entry + 4, be32(entry)))
                return false;
            if (k + 1 < offsets.size() && offsets[k + 1] != offsets[k]
                && be32(entry) != offsets[k + 1] - offsets[k] - 4)
                return false;
        }
        return true;
    }

    // Data table candidates past the name table, nearest first.
    QVector<const uchar *> dataCandidates(const Root &root) const
    {
        QVector<const uchar *> found;
        for (const uchar *d = root.namesEnd; d - root.namesEnd < MaxPadding && in(d, 4); ++d) {
            if (fitsData(root, d))
                found.append(d);
            if (*d)
                break;
        }
        return found;
    }

private:
    bool readNames(const uchar *tree, const uchar *names, int maxIndex,
                   const QHash<int, int> &parent, QHash<int, QString> &nameOf, Root &root) const
    {
        nameOf.clear();
        root.namesEnd = names;
        for (int i = 1; i <= maxIndex; ++i) {
            if (!parent.contains(i))
                continue;
            const uchar *q = names + be32(tree + NodeSize * i);
            if (!in(q, 6) || !in(q + 6, 2 * be16(q)))
                return false;
            QString name(be16(q), Qt::Uninitialized);
            for (int k = 0; k < name.size(); ++k)
                name[k] = QChar(be16(q + 6 + 2 * k));
            if (qtHash(name) != be32(q + 2))
                return false;
            nameOf.insert(i, name);
            root.namesEnd = qMax(root.namesEnd, q + 6 + 2 * name.size());
        }
        return true;
    }

    bool in(const uchar *p, quint32 n) const
    {
        return p >= m_begin && p <= m_end && quint32(m_end - p) >= n;
    }

    const uchar *m_begin, *m_end;
};

struct Region { const uchar *begin, *end; };

int executableRegions(dl_phdr_info *info, size_t, void *arg)
{
    // glibc reports the executable first.
    QVector<Region> *regions = static_cast<QVector<Region> *>(arg);
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) &ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD && !(ph.p_flags & PF_W)) {
            const uchar *b = reinterpret_cast<const uchar *>(info->dlpi_addr + ph.p_vaddr);
            regions->append(Region{b, b + ph.p_filesz});
        }
    }
    return 1;
}

// A parsed root is the registered one when Qt serves one of its paths from
// exactly its data table.
bool isRegistered(const Root &root)
{
    for (auto it = root.files.constBegin(); it != root.files.constEnd(); ++it) {
        QResource res(':' + it.key());
        if (res.isValid() && res.data() == root.data + it.value() + 4)
            return true;
    }
    return false;
}

// The executable's registered resource roots. Of the data table candidates
// the one Qt actually serves from wins, which settles single-file roots that
// the size chain cannot.
QVector<Root> appRoots()
{
    static const uchar rootNode[] = {0, 0, 0, 0, 0, Directory};
    QVector<Region> regions;
    dl_iterate_phdr(executableRegions, &regions);
    QVector<Root> roots;
    for (const Region &r : regions) {
        Parser parser(r.begin, r.end);
        for (const uchar *p = r.begin; p + NodeSize <= r.end; ++p) {
            p = static_cast<const uchar *>(memmem(p, r.end - p, rootNode, sizeof rootNode));
            if (!p || p + NodeSize > r.end)
                break;
            Root root;
            if (be32(p + 10) != 1 || !parser.parse(p, nullptr, root))
                continue;
            for (const uchar *data : parser.dataCandidates(root)) {
                root.data = data;
                if (isRegistered(root)) {
                    roots.append(root);
                    break;
                }
            }
        }
    }
    return roots;
}

QStringList g_status;

void note(const QString &line)
{
    g_status << line;
    OLOG("%s", qPrintable(line));
}

QString overlayPath()
{
    const QByteArray env = qgetenv("WMMOD_OVERLAY");
    return env.isEmpty() ? QString(OVERLAY_DIR "/overlay.rcc") : QString::fromLocal8Bit(env);
}

int readCount(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed().toInt() : 0;
}

// fsync, because the next thing after a failed start may be a reboot.
void writeCount(const QString &path, int n)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QByteArray::number(n) + '\n');
        f.flush();
        fsync(f.handle());
    } else {
        note("cannot write " + path);
    }
}

// Resets the bad start count once the event loop has run for ms; a hang or
// crash before that leaves it raised.
void resetWhenHealthy(const QString &path, int ms)
{
    QTimer *timer = new QTimer(QCoreApplication::instance());
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [path, ms, timer] {
        writeCount(path, 0);
        note(QString("up for %1 ms, %2 reset").arg(ms).arg(path));
        timer->deleteLater();
    });
    timer->start(ms);
}

// Registered resources point into these buffers for the life of the process,
// so none is ever freed, not even after a reload replaces it.
const uchar *keep(const QByteArray &buffer)
{
    static QList<QByteArray> buffers;
    buffers << buffer;
    return reinterpret_cast<const uchar *>(buffers.last().constData());
}

bool parseRcc(const uchar *b, int size, Root &root)
{
    Parser parser(b, b + size);
    if (size < 20 || memcmp(b, "qres", 4) || be32(b + 4) != 1
        || !parser.parse(b + be32(b + 8), b + be32(b + 16), root)
        || !parser.fitsData(root, b + be32(b + 12)))
        return false;
    root.data = b + be32(b + 12);
    return true;
}

// The overlay in effect. Its buffer comes from keep().
const uchar *g_current = nullptr;
int g_currentSize = 0;

// The stored bytes of a path's data entry, compressed or not; empty when
// the root does not have the path.
QByteArray entryBytes(const Root &root, const QString &path)
{
    auto it = root.files.constFind(path);
    if (it == root.files.constEnd())
        return QByteArray();
    const uchar *e = root.data + it.value();
    return QByteArray::fromRawData(reinterpret_cast<const char *>(e), 4 + be32(e));
}

struct Placement
{
    int roots = 0, moved = 0, replaced = 0, added = 0;
    QStringList lost;
};

// Registers the overlay in front of the app roots it shares a path with.
Placement placeInFront(const Root &overlay)
{
    Placement r;
    QVector<Root> shadowed;
    for (const Root &root : appRoots()) {
        ++r.roots;
        bool shares = false;
        for (auto it = overlay.files.constBegin(); it != overlay.files.constEnd() && !shares; ++it)
            shares = root.files.contains(it.key());
        if (shares)
            shadowed.append(root);
    }
    r.moved = shadowed.size();

    for (const Root &root : shadowed)
        qUnregisterResourceData(1, root.tree, root.names, root.data);
    qRegisterResourceData(1, overlay.tree, overlay.names, overlay.data);
    for (const Root &root : shadowed)
        qRegisterResourceData(1, root.tree, root.names, root.data);

    for (auto it = overlay.files.constBegin(); it != overlay.files.constEnd(); ++it) {
        bool inApp = false;
        for (const Root &root : shadowed)
            inApp = inApp || root.files.contains(it.key());
        QResource res(':' + it.key());
        if (!res.isValid() || res.data() != overlay.data + it.value() + 4)
            r.lost << it.key();
        else
            ++(inApp ? r.replaced : r.added);
    }
    return r;
}

} // namespace

QString overlayDir()
{
    return QFileInfo(overlayPath()).absolutePath();
}

void overlayStart()
{
    static bool started = false;
    if (started)
        return;
    started = true;

    const QString path = overlayPath();
    const QString disable = overlayDir() + "/disable";
    if (QFile::exists(disable)) {
        note("disabled by " + disable);
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        note(QString("no overlay at %1").arg(path));
        return;
    }

    const QByteArray bytes = file.readAll();
    const uchar *b = keep(bytes);
    Root overlay;
    if (!parseRcc(b, bytes.size(), overlay)) {
        note(QString("%1 is not an rcc v1 file").arg(path));
        return;
    }

    const QString badboots = overlayDir() + "/badboots";
    const int bad = readCount(badboots);
    if (bad >= OVERLAY_MAX_BAD_BOOTS) {
        note(QString("skipped: %1 starts in a row did not stay up; delete %2 to retry").arg(bad).arg(badboots));
        return;
    }
    writeCount(badboots, bad + 1);
    const QByteArray healthyEnv = qgetenv("WMMOD_HEALTHY_MS");
    resetWhenHealthy(badboots, healthyEnv.isEmpty() ? OVERLAY_HEALTHY_MS : healthyEnv.toInt());

    const Placement placed = placeInFront(overlay);
    g_current = b;
    g_currentSize = bytes.size();
    for (const QString &lost : placed.lost)
        note("not served from the overlay: " + lost);
    note(QString("%1: %2 files, %3 replaced, %4 added, %5 not served; %6 app roots, %7 moved behind")
             .arg(path).arg(overlay.files.size()).arg(placed.replaced).arg(placed.added)
             .arg(placed.lost.size()).arg(placed.roots).arg(placed.moved));
}

bool overlayReload(const QString &path, QStringList &changed, QStringList &added, QStringList &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out << "cannot read " + path;
        return false;
    }
    const QByteArray bytes = file.readAll();
    const uchar *b = keep(bytes);
    Root next;
    if (!parseRcc(b, bytes.size(), next)) {
        out << path + " is not an rcc v1 file";
        return false;
    }

    added.clear();
    for (auto it = next.files.constBegin(); it != next.files.constEnd(); ++it)
        if (!QResource(':' + it.key()).isValid())
            added << it.key();
    added.sort();

    Root current;
    const bool haveCurrent = g_current && parseRcc(g_current, g_currentSize, current);
    if (haveCurrent && !qUnregisterResourceData(1, current.tree, current.names, current.data)) {
        out << "the overlay in effect is not registered where it was recorded; nothing changed";
        return false;
    }

    QSet<QString> paths = QSet<QString>::fromList(next.files.keys());
    if (haveCurrent)
        paths += QSet<QString>::fromList(current.files.keys());
    changed.clear();
    for (const QString &p : paths)
        if (!haveCurrent || entryBytes(current, p) != entryBytes(next, p))
            changed << p;
    changed.sort();

    const Placement placed = placeInFront(next);
    g_current = b;
    g_currentSize = bytes.size();
    for (const QString &lost : placed.lost)
        out << "not served from the overlay: " + lost;
    note(QString("reloaded %1: %2 files, %3 changed, %4 replaced, %5 added, %6 not served%7")
             .arg(path).arg(next.files.size()).arg(changed.size()).arg(placed.replaced)
             .arg(placed.added).arg(placed.lost.size())
             .arg(haveCurrent ? QString() : QString("; no overlay was in effect")));
    out << g_status.last();
    return true;
}

QStringList overlayStatus()
{
    return g_status.isEmpty() ? QStringList("overlay not started") : g_status;
}

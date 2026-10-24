// tst_sensme MODEL BLOBS: one line per SMFMF blob (u32 little-endian length +
// blob, repeated), in the format sensme_fuzz.py compares against the
// smuharness run of the device's own libMediaStoreService.so.
#include <QtCore/QFile>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtCore/QtEndian>

#include "../sensme.h"

int main(int argc, char **argv)
{
    QTextStream out(stdout);
    if (argc != 3 || !SensMe::loadModel(QString::fromLocal8Bit(argv[1]))) {
        QTextStream(stderr) << "usage: tst_sensme MODEL BLOBS (model from stage.py)\n";
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[2]));
    if (!file.open(QIODevice::ReadOnly))
        return 2;
    const QByteArray all = file.readAll();
    int pos = 0;
    while (pos + 4 <= all.size()) {
        const int size = int(qFromLittleEndian<quint32>(all.constData() + pos));
        const QByteArray blob = all.mid(pos + 4, size);
        pos += 4 + size;

        QByteArray features;
        qint32 sabi = -1;
        SensMe::parse(blob, &features, &sabi);
        int cluster = 0;
        const int mask = SensMe::channels(features, &cluster);
        QStringList list;
        for (int i = 0; i < SensMe::ChannelCount && mask > 0; ++i) {
            if (mask & (1 << i))
                list << QString::number(i);
        }
        out << "ch=" << (mask < 0 ? -1 : list.size()) << ':' << list.join(',') << " extra=" << cluster
            << " sabi=" << sabi << " feat=";
        for (int i = 0; i < features.size(); ++i)
            out << (i ? "," : "") << QString::number(uchar(features.at(i)), 16);
        out << '\n';
    }
    return 0;
}

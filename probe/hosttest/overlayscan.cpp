// Runs the overlay's resource parser over a file, such as the device's
// HgrmMediaPlayerApp, and prints what it finds in qrcdump.py's format so the
// two can be compared. Without Qt's registry to ask, the nearest data table
// candidate is taken.

#include "../mkspec/overlay.cpp"

#include <QtCore/QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile file(app.arguments().value(1));
    if (!file.open(QIODevice::ReadOnly))
        return 2;
    const QByteArray blob = file.readAll();
    const uchar *b = reinterpret_cast<const uchar *>(blob.constData());
    const uchar *end = b + blob.size();
    static const uchar rootNode[] = {0, 0, 0, 0, 0, Directory};
    Parser parser(b, end);
    int files = 0;
    for (const uchar *p = b; p + NodeSize <= end; ++p) {
        p = static_cast<const uchar *>(memmem(p, end - p, rootNode, sizeof rootNode));
        if (!p || p + NodeSize > end)
            break;
        Root root;
        if (be32(p + 10) != 1 || !parser.parse(p, nullptr, root))
            continue;
        const QVector<const uchar *> data = parser.dataCandidates(root);
        if (data.isEmpty())
            continue;
        printf("tree %#x names %#x data %#x\n", unsigned(p - b), unsigned(root.names - b),
               unsigned(data.first() - b));
        files += root.files.size();
    }
    printf("%d files\n", files);
    return 0;
}

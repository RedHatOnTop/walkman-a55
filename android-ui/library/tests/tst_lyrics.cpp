#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextCodec>

#include <cstdio>

#include "../lyrics.h"

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
}

bool write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const Lyrics timed = parseLyrics("[ti:Title]\n[ar:Artist]\n"
                                     "[00:01.50]one\r\n[00:03:25]two\n[00:05]three\n[00:07.125]four\n"
                                     "[00:10.00][00:20.00]chorus\n[00:30.00]\n");
    check(timed.timed(), "stamped lines are timed");
    check(timed.lines == QStringList({"one", "two", "three", "four", "chorus", "chorus", ""}), "lines in time order, tags dropped");
    check(timed.times == QVector<qint64>({1500, 3250, 5000, 7125, 10000, 20000, 30000}),
          "centiseconds, colon fraction, no fraction, milliseconds");
    check(timed.lineAt(0) == 0, "before the first line shows the first");
    check(timed.lineAt(3250) == 1, "a line starts at its stamp");
    check(timed.lineAt(9999) == 3, "the previous line holds until the next stamp");
    check(timed.lineAt(15000) == 4 && timed.lineAt(25000) == 5, "a repeated line plays at both stamps");
    check(timed.lineAt(99000) == 6, "after the last stamp shows the last line");

    const Lyrics shifted = parseLyrics("[offset:+500]\n[00:00.20]early\n[00:02.00]later\n");
    check(shifted.times == QVector<qint64>({0, 1500}), "a positive offset shows lines earlier, never before 0");
    const Lyrics late = parseLyrics("[OFFSET: -250]\n[00:01.00]x\n");
    check(late.times == QVector<qint64>({1250}), "a negative offset, any case");

    const Lyrics plain = parseLyrics("\n\nfirst line\n\nsecond verse\n\n");
    check(!plain.timed(), "plain text is untimed");
    check(plain.lines == QStringList({"first line", "", "second verse"}), "outer blank lines trimmed, inner kept");
    check(parseLyrics("[ar:Only tags]\n[ti:x]\n").lines.isEmpty(), "only tags is no lyrics");
    check(parseLyrics({}).lines.isEmpty(), "empty text is no lyrics");

    QTemporaryDir dir;
    const QString audio = dir.filePath("01 Song [Live].flac");
    write(audio, "fLaC");
    check(!hasLyricsFile(audio), "no .lrc beside the song");
    check(loadLyrics(audio, "tag line").lines == QStringList({"tag line"}), "the tag when there is no file");

    write(dir.filePath("01 Song [Live].lrc"), "\xEF\xBB\xBF[00:01.00]\xEA\xB0\x80\xEC\x82\xAC\n");
    check(hasLyricsFile(audio), "a .lrc beside a name with brackets");
    const Lyrics fromFile = loadLyrics(audio, "tag line");
    check(fromFile.timed() && fromFile.lines == QStringList({QString::fromUtf8("가사")}), "the file wins over the tag, UTF-8 BOM");

    write(dir.filePath("01 Song [Live].lrc"), "[ar:nothing]\n");
    check(loadLyrics(audio, "tag line").lines == QStringList({"tag line"}), "an empty file falls back to the tag");

    QByteArray utf16("\xFF\xFE", 2);
    const QString text = QString::fromUtf8("[00:02.00]노래");
    utf16.append(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
    write(dir.filePath("01 Song [Live].lrc"), utf16);
    check(loadLyrics(audio, {}).lines == QStringList({QString::fromUtf8("노래")}), "UTF-16 with a BOM");

    write(dir.filePath("01 Song [Live].lrc"), "[00:01.00]\xB0\xA1\xBB\xE7\n");
    const QStringList korean = loadLyrics(audio, {}).lines;
    if (QTextCodec::codecForName("CP949"))
        check(korean == QStringList({QString::fromUtf8("가사")}), "CP949 when the bytes are not UTF-8");
    else
        check(korean.size() == 1, "no CP949 codec: Latin-1, one line");

    std::printf("%s: %d failed\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}

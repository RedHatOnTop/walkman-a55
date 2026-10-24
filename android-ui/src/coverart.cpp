#include "coverart.h"

#include "../library/musicdb.h"

CoverArtProvider::CoverArtProvider(const MusicDb *db) : QQuickImageProvider(QQuickImageProvider::Image), m_db(db)
{
}

QImage CoverArtProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QStringList parts = id.split('?');
    bool ok = false;
    const int track = parts.value(0).toInt(&ok);
    int edge = qMax(requestedSize.width(), requestedSize.height());
    if (edge <= 0)
        edge = parts.value(1).toInt();
    if (parts.value(0) == "folder") {
        const QImage folder(":/ic_music_library_list_folder.png");
        if (size)
            *size = folder.size();
        return folder;
    }
    QImage image = ok ? m_db->cover(track, edge) : QImage();
    if (image.isNull()) {
        image = QImage(edge > 200 ? ":/ic_audio_play_substitute_jacket_picture.png"
                                  : ":/ic_music_library_list_substitute_jacket_picture.png");
        if (edge > 0 && !image.isNull())
            image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size)
        *size = image.size();
    return image;
}

#ifndef COVERART_H
#define COVERART_H

#include <QtQuick/QQuickImageProvider>

class MusicDb;

// image://coverart/<contents id>?<height>?<db counter> and the
// coverart-triple variant the stock CoverArt components request. A contents
// id is a library track id.
class CoverArtProvider : public QQuickImageProvider
{
public:
    explicit CoverArtProvider(const MusicDb *db);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    const MusicDb *m_db;
};

#endif

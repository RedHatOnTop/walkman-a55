#ifndef QUEUEMODELS_H
#define QUEUEMODELS_H

#include "screenmodel.h"

class BrowseListModel;
class ListRows;

// sid_0401_tracksequence: the play queue, right of the player.
class TrackSequenceModel : public ScreenModel
{
    Q_OBJECT
public:
    TrackSequenceModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    void handle(const QByteArray &signal, const QVariantList &args) override;
    QVector<MenuItem> menu() override;

private:
    void rebuild();
    BrowseListModel *m_list;
    ListRows *m_context;
    ListRows *m_bookmarks;
    ListRows *m_playlists;
    QVector<int> m_queue;
    int m_selected = -1;
};

// sid_0601_bookmark: the ten bookmark lists, left of the player.
class BookmarkModel : public ScreenModel
{
    Q_OBJECT
public:
    BookmarkModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    void activate() override;
    void handle(const QByteArray &signal, const QVariantList &args) override;
    QVector<MenuItem> menu() override;

private:
    void rebuild();
    BrowseListModel *m_list;
    ListRows *m_context;
    ListRows *m_bookmarks;
    ListRows *m_playlists;
    QVector<int> m_tracks;
    int m_number = 0;
    int m_selected = -1;
    bool m_sorting = false;
};

#endif

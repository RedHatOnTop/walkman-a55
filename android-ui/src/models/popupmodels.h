#ifndef POPUPMODELS_H
#define POPUPMODELS_H

#include "screenmodel.h"

#include "../../library/lyrics.h"

class ListRows;
class QTimer;

// A popup over the current screen. Its close and Back signals close it;
// the library button also returns to the library top.
class PopupModel : public ScreenModel
{
    Q_OBJECT
public:
    using ScreenModel::ScreenModel;
    void handle(const QByteArray &signal, const QVariantList &args) override;

protected:
    void attached() override;
    void close();
};

// sid_0206_detailedInfo: a song's tags and format.
class DetailsModel : public PopupModel
{
    Q_OBJECT
public:
    using PopupModel::PopupModel;

protected:
    void attached() override;
};

// sid_0936_library/LibraryTopHelpGuide.qml, for a library top with no
// function icon row.
class LibraryHelpModel : public PopupModel
{
    Q_OBJECT
public:
    LibraryHelpModel(const QString &id, AppContext *app, QObject *parent = nullptr);
};

// components/ImePanelPopup.qml: one line of text for AppContext::askText.
class TextEntryModel : public PopupModel
{
    Q_OBJECT
public:
    using PopupModel::PopupModel;
    void prepare(const QString &title, const QString &initial);
    void handle(const QByteArray &signal, const QVariantList &args) override;
};

// components/VolumePopup.qml: the stock 0-120 volume scale.
class VolumeModel : public PopupModel
{
    Q_OBJECT
public:
    VolumeModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    void handle(const QByteArray &signal, const QVariantList &args) override;

private:
    void step(int delta);
    void refresh();
    QTimer *m_repeat;
    int m_direction = 0;
};

// sid_0204_player/SyncLyricDisp.qml: the playing song's lyrics. Timed lyrics
// follow the position, and a tapped line seeks to it.
class LyricsModel : public PopupModel
{
    Q_OBJECT
public:
    LyricsModel(const QString &id, AppContext *app, QObject *parent = nullptr);
    void handle(const QByteArray &signal, const QVariantList &args) override;

protected:
    void attached() override;

private:
    void load();
    void follow();
    ListRows *m_rows;
    QTimer *m_hold;
    Lyrics m_lyrics;
    int m_line = -1;
    bool m_keepingScreenOn = false;
};

#endif

#ifndef APPCONTEXT_H
#define APPCONTEXT_H

#include <QtCore/QCoreApplication>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtQuick/QQuickItem>

#include <functional>

#include "models/screenmodel.h"
#include "screensettings.h"

class BackKey;
class DialogManager;
class MusicDb;
class PlayerController;
class QQmlEngine;
class ScreenController;
class ScreenModel;

// A string from the Sony translations, which keep the ViewModels' own
// strings under their C++ class names ("dmpapp::LibraryTopModelList").
inline QString sonyText(const char *context, const char *id)
{
    return QCoreApplication::translate(context, id);
}

class AppFont : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY languageChanged)
    Q_PROPERTY(QString emptyString READ emptyString NOTIFY languageChanged)
    Q_PROPERTY(QString language_name READ language NOTIFY languageChanged)
public:
    explicit AppFont(const QString &language, QObject *parent = nullptr)
        : QObject(parent), m_language(language) {}
    QString fontFamily() const { return "SST"; }
    QString emptyString() const { return {}; }
    QString language() const { return m_language; }
signals:
    void languageChanged();
private:
    QString m_language;
};

class HoldToastManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant holdToast READ holdToast NOTIFY holdToastChanged)
public:
    using QObject::QObject;
    QVariant holdToast() const { return {}; }
signals:
    void holdToastChanged();
};

// Owns the replacement ViewModels and the state they share: the screen
// controller, dialogs, playback, the library and preferences.
class AppContext : public QObject
{
    Q_OBJECT
public:
    AppContext(const ScreenSettings *screens, QSettings *prefs, PlayerController *player, MusicDb *library,
               const QString &language, QObject *parent = nullptr);
    void install(QQmlEngine *engine);
    void setRoot(QQuickItem *root);
    void start();

    const ScreenSettings *screens() const { return m_screens; }
    ScreenController *controller() const { return m_controller; }
    DialogManager *dialogs() const { return m_dialogs; }
    BackKey *backKey() const { return m_backKey; }
    QSettings *prefs() const { return m_prefs; }
    PlayerController *player() const { return m_player; }
    MusicDb *library() const { return m_library; }
    ScreenModel *model(const QString &id);
    ScreenModel *currentModel() { return model(currentScreen()); }
    QString currentScreen() const;

    // Which of the five player screens and eight sound screens the
    // transition table's numbered conditions select.
    int playerCondition() const;
    int soundCondition() const;
    void message(const QString &text);
    void openPopup(const QString &popup);
    bool pressBack();

    // The stock context menu (a titled list of actions) and list dialog.
    void openMenu(const QString &title, const QVector<ScreenModel::MenuItem> &items);
    void chooseFrom(const QString &title, const QStringList &labels, std::function<void(int)> chosen);
    // The stock yes/no dialog; yes runs once it has closed.
    void confirm(const QString &text, std::function<void()> yes);
    // The stock text entry popup, typed on Android's keyboard; entered runs
    // with the text when OK is pressed.
    void askText(const QString &title, const QString &initial, std::function<void(const QString &)> entered);
    void textEntered(const QString &text);
    void renamePlaylist(int playlist);
    void deletePlaylist(int playlist);
    void addToBookmarks(const QVector<int> &tracks);
    void addToPlaylist(const QVector<int> &tracks);
    // The choices those two offer, for views that open the list dialog themselves.
    QStringList bookmarkNames() const;
    QStringList playlistChoices() const;
    void addToBookmarkList(int list, const QVector<int> &tracks);
    void addToPlaylistChoice(int choice, const QVector<int> &tracks);
    bool playlistNamed(const QString &name) const;
    void showDetails(int track);
    QVector<int> bookmarks(int list) const;
    void setBookmarks(int list, const QVector<int> &tracks);
    static const int bookmarkLists = 10;

    int detailsTrack() const { return m_detailsTrack; }

signals:
    void bookmarksChanged();
    void windowChanged();
    void backgroundRequested();

private:
    void viewLoaded(const QString &kind, const QString &id, QQuickItem *view);
    void screenChanged(const QString &screen);
    ScreenModel *create(const QString &id);
    int condition(const QString &screen, const QString &action) const;
    QString soundNeighbour(const QString &screen, const QString &action) const;

    const ScreenSettings *m_screens;
    QSettings *m_prefs;
    PlayerController *m_player;
    MusicDb *m_library;
    ScreenController *m_controller;
    DialogManager *m_dialogs;
    BackKey *m_backKey;
    AppFont *m_font;
    HoldToastManager *m_holdToast;
    QHash<QString, ScreenModel *> m_models;
    std::function<void(const QVariant &)> m_selected;
    std::function<void()> m_confirmed;
    std::function<void(const QString &)> m_entered;
    int m_detailsTrack = -1;
};

#endif

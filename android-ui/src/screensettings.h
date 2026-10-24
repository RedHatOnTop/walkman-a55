#ifndef SCREENSETTINGS_H
#define SCREENSETTINGS_H

#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>

// The device's ScreenSettings.json: every screen, the window that frames it,
// and the transition table the stock ScreenTransitionController walked.
struct Transition {
    QString action;
    int condition = 0;
    int history = 0;
    QString next;
};

struct ScreenInfo {
    QString window;
    QString viewModel;
    bool earlyLoad = false;
    int layer = 0;
    int alignment = 0;
};

struct PopupRule {
    QString action;
    int condition = 0;
    QString show;
};

class ScreenSettings
{
public:
    QString load(const QString &settingsPath, const QString &layoutsPath);

    QStringList screenIds;
    QStringList windowIds;
    QStringList popupIds;
    QHash<QString, ScreenInfo> screens;
    QHash<QString, QString> windowViewModels;
    QHash<QString, QString> layouts;
    QHash<QString, QVector<Transition>> transitions;
    QHash<QString, QVector<PopupRule>> popups;
    QHash<QString, QStringList> aliases;
    QVariantMap style;

    // The transition for an action, matched on the condition the caller's
    // ViewModel state gives. An entry with condition 0 matches any condition.
    // Rules listed under an alias (e.g. "MusicPlayers") apply to its members.
    const Transition *find(const QString &screen, const QString &action, int condition) const;
    bool hasAction(const QString &screen, const QString &action) const;
    QString popupFor(const QString &screen, const QString &action, int condition) const;
    int layer(const QString &screen) const { return screens.value(screen).layer; }
    QString window(const QString &screen) const { return screens.value(screen).window; }
};

#endif

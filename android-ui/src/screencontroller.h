#ifndef SCREENCONTROLLER_H
#define SCREENCONTROLLER_H

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QVariantMap>
#include <QtQuick/QQuickItem>

#include <functional>

#include "screensettings.h"

// The Loader-side controller a screen reaches as `controller` (dynamic scope),
// and the neighbour slots (upperController, ...) the stock framework read.
class ScreenProxy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString screenID READ screenID CONSTANT)
    Q_PROPERTY(QString windowID READ windowID CONSTANT)
public:
    ScreenProxy(const QString &screen, const QString &window, QObject *parent)
        : QObject(parent), m_screen(screen), m_window(window) {}
    QString screenID() const { return m_screen; }
    QString windowID() const { return m_window; }

signals:
    void activated();
    void inactivated();
    void available(QObject *view_model);
    void unavailable(QObject *view_model);
    void updateScreen();
    void updatedProperty(const QString &id, const QString &name);
    void updatedPropertyData(const QString &id, const QString &name, const QVariant &data);
    void loadedViewModel(QObject *view_model);

private:
    const QString m_screen;
    const QString m_window;
};

// Stands in for dmpapp::ScreenTransitionController: owns the current screen,
// the history, the four swipe neighbours and every transition. qml/AppFrame.qml
// draws what it decides; transitions are begun here and finished by the frame
// once their animation ends.
class ScreenController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int windowWidth READ windowWidth NOTIFY updatedScreenArea)
    Q_PROPERTY(int windowHeight READ windowHeight NOTIFY updatedScreenArea)
    Q_PROPERTY(int width READ width NOTIFY updatedScreenArea)
    Q_PROPERTY(int height READ height NOTIFY updatedScreenArea)
    Q_PROPERTY(int x READ x NOTIFY updatedScreenArea)
    Q_PROPERTY(int y READ y NOTIFY updatedScreenArea)
    Q_PROPERTY(int navigationY READ navigationY NOTIFY updatedScreenArea)
    Q_PROPERTY(QString screenID READ screenID NOTIFY updateScreen)
    Q_PROPERTY(QString currentScreenID READ screenID NOTIFY updateScreen)
    Q_PROPERTY(QString windowID READ windowID NOTIFY updateScreen)
    Q_PROPERTY(QObject *windowController READ windowController NOTIFY updateScreen)
    Q_PROPERTY(QObject *upperController READ upperController NOTIFY updateDeploy)
    Q_PROPERTY(QObject *lowerController READ lowerController NOTIFY updateDeploy)
    Q_PROPERTY(QObject *leftController READ leftController NOTIFY updateDeploy)
    Q_PROPERTY(QObject *rightController READ rightController NOTIFY updateDeploy)
    Q_PROPERTY(QStringList allScreenIDList READ allScreenIDList CONSTANT)
    Q_PROPERTY(QStringList allWindowIDList READ allWindowIDList CONSTANT)
    Q_PROPERTY(QVariantMap allViewLayouts READ allViewLayouts CONSTANT)
    Q_PROPERTY(QVariantMap styleProperties READ styleProperties CONSTANT)
    Q_PROPERTY(QVariantMap properties READ properties NOTIFY updateScreen)
    Q_PROPERTY(QVariantMap windowProperties READ windowProperties NOTIFY updateScreen)
    Q_PROPERTY(QVariantMap popupProperties READ popupProperties NOTIFY updatePopup)
    Q_PROPERTY(QStringList popups READ popups NOTIFY updatePopup)
    Q_PROPERTY(bool enabled MEMBER m_enabled)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(qreal dragOffset READ dragOffset NOTIFY dragOffsetChanged)
    Q_PROPERTY(QStringList preload READ preload NOTIFY updateDeploy)
    // The views the stock app built at startup (earlyload) that this build
    // reaches; the frame builds them in the background, one at a time.
    Q_PROPERTY(QStringList earlyLoad READ earlyLoad NOTIFY earlyLoadChanged)

public:
    enum Direction { Up = -2, Left = -1, None = 0, Right = 1, Down = 2 };
    Q_ENUM(Direction)

    ScreenController(const ScreenSettings *settings, QObject *parent = nullptr);
    void setConditions(std::function<int(const QString &screen, const QString &action)> condition)
    {
        m_condition = std::move(condition);
    }

    int windowWidth() const { return m_windowSize.width(); }
    int windowHeight() const { return m_windowSize.height(); }
    int width() const { return m_area.width(); }
    int height() const { return m_area.height(); }
    int x() const { return m_area.x(); }
    int y() const { return m_area.y(); }
    int navigationY() const { return m_navigationY; }
    QString screenID() const { return m_screen; }
    QString windowID() const { return m_settings->window(m_screen); }
    QObject *windowController() const;
    QObject *upperController() const { return neighbour(Up); }
    QObject *lowerController() const { return neighbour(Down); }
    QObject *leftController() const { return neighbour(Left); }
    QObject *rightController() const { return neighbour(Right); }
    QStringList allScreenIDList() const { return m_settings->screenIds; }
    QStringList allWindowIDList() const { return m_settings->windowIds; }
    QVariantMap allViewLayouts() const;
    QVariantMap styleProperties() const { return m_settings->style; }
    QVariantMap properties() const { return {}; }
    QVariantMap windowProperties() const { return {}; }
    QVariantMap popupProperties() const { return {}; }
    QStringList popups() const { return m_popups; }
    bool busy() const { return !m_pending.isEmpty(); }
    qreal dragOffset() const { return m_dragOffset; }
    QStringList preload() const;
    QStringList earlyLoad() const { return m_earlyLoad; }
    void setEarlyLoad(const QStringList &screens);

    // The screen a swipe in this direction leads to, "" if none. Direction is
    // the finger's travel inverted, as in the stock framework: Right (1) is
    // a leftward swipe that brings in the right-hand neighbour.
    QString neighbourId(int direction) const;
    Q_INVOKABLE ScreenProxy *proxy(const QString &screen) const;
    QString action(int direction) const;
    const ScreenSettings *settings() const { return m_settings; }

    // Gesture side of a swipe; the frame animates, then calls transitionDone.
    bool beginSwipe(int direction);
    void setDragOffset(qreal offset);
    void settleSwipe(bool complete, int duration);

    void start(const QString &screen);
    bool doAction(const QString &action, int condition = -1);
    bool forward(const QString &screen, int history = 1);
    void showPopup(const QString &popup);
    void setWindowSize(const QSize &size);

    QQuickItem *view(const QString &screen) const { return m_views.value(screen); }
    QQuickItem *windowView(const QString &window) const { return m_windowViews.value(window); }
    QQuickItem *currentWindowView() const { return m_windowViews.value(windowID()); }

public slots:
    void DoAction(const QString &action) { doAction(action); }
    void Back();
    void BackToTop();
    void GoToPlayback();
    void ClosePopup(const QString &object_name);
    void LoadedView(const QString &id, QObject *view);
    void LoadedWindow(const QString &id, QObject *view);
    void LoadedPopup(const QString &id, QObject *view);
    void ReconfigureScreenByWindow(QObject *window = nullptr);
    void ReconfigureScreen() { ReconfigureScreenByWindow(); }
    void transitionDone(bool completed);

signals:
    void earlyLoadChanged();
    void pressed();
    void released();
    void updateScreen();
    void updateDeploy();
    void updatePopup();
    void updatedScreenArea();
    void reconfigureScreen();
    void busyChanged();
    void dragOffsetChanged();
    void locked();
    void unlocked();
    // Frame commands.
    void presentScreen(const QString &screen, const QString &window);
    void beginTransition(const QString &from, const QString &to, const QString &fromWindow,
                         const QString &toWindow, const QString &effect, int direction, bool interactive);
    void settleTransition(bool complete, int duration);
    void backgroundRequested();
    void screenChanged(const QString &screen);
    void viewLoaded(const QString &kind, const QString &id, QQuickItem *view);

private:
    struct Pending {
        QString to;
        QString action;
        int history = 1;
        int direction = None;
        bool interactive = false;
        bool isEmpty() const { return to.isEmpty(); }
    };
    QObject *neighbour(int direction) const;
    void recomputeNeighbours();
    bool navigate(const QString &to, int history, const QString &action);
    int programmaticDirection(const QString &to) const;
    void commit(const Pending &pending);
    int conditionFor(const QString &action) const;
    QString historyTarget(const QString &next) const;

    const ScreenSettings *m_settings;
    std::function<int(const QString &, const QString &)> m_condition;
    QHash<QString, ScreenProxy *> m_proxies;
    ScreenProxy *m_empty;
    QHash<QString, QPointer<QQuickItem>> m_views;
    QHash<QString, QPointer<QQuickItem>> m_windowViews;
    QHash<QString, QPointer<QQuickItem>> m_popupViews;
    QString m_screen;
    QStringList m_history;
    QHash<int, QString> m_neighbours;
    QStringList m_earlyLoad;
    QHash<int, int> m_neighbourHistory;
    Pending m_pending;
    QStringList m_popups;
    QSize m_windowSize{480, 800};
    QRect m_area{0, 44, 480, 700};
    int m_navigationY = 44;
    qreal m_dragOffset = 0;
    bool m_enabled = true;
};

#endif

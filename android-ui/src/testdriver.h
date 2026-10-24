#ifndef TESTDRIVER_H
#define TESTDRIVER_H

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QStringList>
#include <QtCore/QVector>

class QQuickItem;
class QQuickWindow;
class ScreenController;

// Drives the app from WALKMANUI_SCRIPT for host verification, then exits:
//
//   wait:MS                   let the UI run
//   tap:X,Y                   press and release at window pixel X,Y
//   hold:X,Y,MS               long press
//   drag:X1,Y1,X2,Y2,MS       press, move there over MS in 16 ms steps, release
//   press:X,Y / move:X,Y,MS / release   the same in parts, to look mid-gesture
//   key:back                  Android's back key
//   grab:FILE                 save the window
//   frames:start|stop         frame-interval statistics between the two, drawing
//                             continuously so idle time counts as frames
//   expect:SCREEN             fail unless SCREEN is the current screen
//   dump:OBJECTNAME[,DEPTH]   print that item's subtree geometry ("" = root)
//   go:SCREEN                 open a screen directly, as a history push
//   popup:POPUP               open a popup over the current screen
//
// Exit status 1 when a grab or an expectation fails.
class TestDriver : public QObject
{
    Q_OBJECT
public:
    TestDriver(QQuickWindow *window, ScreenController *controller, const QString &script, QObject *parent = nullptr);

private:
    void next();
    void mouse(int type, const QPointF &position);
    void report();
    void dump(QQuickItem *item, int depth, int indent) const;

    QQuickWindow *m_window;
    ScreenController *m_controller;
    QStringList m_steps;
    int m_step = 0;
    bool m_failed = false;
    // A drag in progress.
    QPointF m_from;
    QPointF m_to;
    int m_dragSteps = 0;
    int m_dragStep = -1;
    bool m_release = true;
    QPointF m_pointer;
    // Frame timing.
    QElapsedTimer m_clock;
    QVector<qint64> m_frames;
    bool m_recording = false;
};

#endif

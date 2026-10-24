#ifndef SWIPEGESTURE_H
#define SWIPEGESTURE_H

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QPointer>
#include <QtCore/QVector>

class QQuickItem;
class QQuickWindow;
class ScreenController;

// Screen-to-screen swipes, recognised ahead of item delivery the way the
// stock app's C++ did (its QML only reacts to startedSwipe/finishedSwipe).
// Thresholds and settle timing are W.music's ViewPager values, in the
// stage's 480-wide units (1 dp on the NW-A55's 160 dpi panel):
// paging slop 16, fling distance 25, fling velocity 400 per second.
class SwipeGesture : public QObject
{
    Q_OBJECT
public:
    SwipeGesture(QQuickWindow *window, ScreenController *controller, QObject *parent = nullptr);
    void setStageScale(qreal scale) { m_scale = scale > 0 ? scale : 1.0; }
    bool eventFilter(QObject *watched, QEvent *event) override;

    // The ViewPager settle: velocity-driven when flung, else distance-driven,
    // capped at 600 ms. Exposed for tests.
    static int settleDuration(qreal remaining, qreal pageSize, qreal velocity, bool horizontal);
    static bool completes(qreal travelled, qreal pageSize, qreal velocity);

private:
    struct Sample {
        QPointF position;
        qint64 time;
    };
    void press(const QPointF &position);
    void move(const QPointF &position);
    void release(const QPointF &position);
    void cancel();
    bool blocked(int direction) const;
    qreal velocity() const;
    qreal travel(const QPointF &position) const;

    QQuickWindow *m_window;
    ScreenController *m_controller;
    QElapsedTimer m_clock;
    QVector<Sample> m_samples;
    QVector<QPointer<QQuickItem>> m_chain;
    QPointF m_origin;
    qreal m_scale = 1.0;
    int m_direction = 0;
    bool m_tracking = false;
    bool m_swiping = false;
    bool m_rejected = false;
    bool m_touch = false;
};

#endif

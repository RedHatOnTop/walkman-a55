#include "swipegesture.h"

#include <QtCore/QtMath>
#include <QtGui/QMouseEvent>
#include <QtGui/QTouchEvent>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>

#include "screencontroller.h"

namespace {

constexpr qreal pagingSlop = 16;
constexpr qreal flingDistance = 25;
constexpr qreal flingVelocity = 400;
constexpr int maxSettle = 600;
constexpr qint64 velocityWindow = 100;

// The item a press at scenePos goes to: the topmost visible, enabled one
// that takes mouse presses, as QQuickWindow's delivery picks it. A disabled
// item on top (the frame's transition guard) must not hide a list below;
// a Qt 5 MouseArea's own "enabled" shadows the item's and needs reading too.
QQuickItem *deepestAt(QQuickItem *item, const QPointF &scenePos)
{
    QList<QQuickItem *> children = item->childItems();
    std::stable_sort(children.begin(), children.end(),
                     [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
    for (int i = children.size() - 1; i >= 0; --i) {
        QQuickItem *child = children.at(i);
        if (!child->isVisible() || !child->isEnabled() || !child->property("enabled").toBool()
            || child->opacity() <= 0)
            continue;
        if (child->contains(child->mapFromScene(scenePos)) || child->childItems().size() > 0) {
            QQuickItem *hit = deepestAt(child, scenePos);
            if (hit)
                return hit;
        }
    }
    return item->acceptedMouseButtons() && item->contains(item->mapFromScene(scenePos)) ? item : nullptr;
}

bool flickableBlocks(QQuickItem *item, int direction)
{
    if (!item->property("interactive").toBool())
        return false;
    const int flickDirection = item->property("flickableDirection").toInt();
    const bool vertical = qAbs(direction) == 2;
    const bool autoAxis = flickDirection == 0;
    if (vertical) {
        const bool scrolls = (flickDirection & 2) || (autoAxis && item->property("contentHeight").toReal() > item->height());
        if (!scrolls)
            return false;
        return direction == ScreenController::Down ? !item->property("atYEnd").toBool()
                                                   : !item->property("atYBeginning").toBool();
    }
    const bool scrolls = (flickDirection & 1) || (autoAxis && item->property("contentWidth").toReal() > item->width());
    if (!scrolls)
        return false;
    return direction == ScreenController::Right ? !item->property("atXEnd").toBool()
                                                : !item->property("atXBeginning").toBool();
}

}

SwipeGesture::SwipeGesture(QQuickWindow *window, ScreenController *controller, QObject *parent)
    : QObject(parent), m_window(window), m_controller(controller)
{
    m_clock.start();
    window->installEventFilter(this);
}

bool SwipeGesture::eventFilter(QObject *, QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd: {
        const auto *touch = static_cast<QTouchEvent *>(event);
        if (touch->touchPoints().isEmpty())
            break;
        const QPointF position = touch->touchPoints().first().scenePos() / m_scale;
        m_touch = true;
        if (event->type() == QEvent::TouchBegin)
            press(position);
        else if (event->type() == QEvent::TouchUpdate)
            move(position);
        else
            release(position);
        break;
    }
    case QEvent::TouchCancel:
        cancel();
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_touch && mouse->source() != Qt::MouseEventNotSynthesized)
            break;
        if (mouse->source() == Qt::MouseEventNotSynthesized)
            m_touch = false;
        const QPointF position = mouse->localPos() / m_scale;
        if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton)
            press(position);
        else if (event->type() == QEvent::MouseMove && (mouse->buttons() & Qt::LeftButton))
            move(position);
        else if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton)
            release(position);
        break;
    }
    default:
        break;
    }
    return false;
}

void SwipeGesture::press(const QPointF &position)
{
    m_tracking = true;
    m_swiping = false;
    m_rejected = m_controller->busy();
    m_direction = 0;
    m_origin = position;
    m_samples = {{position, m_clock.elapsed()}};
    m_chain.clear();
    for (QQuickItem *item = deepestAt(m_window->contentItem(), position * m_scale); item; item = item->parentItem())
        m_chain.append(item);
    emit m_controller->pressed();
}

qreal SwipeGesture::travel(const QPointF &position) const
{
    const QPointF delta = position - m_origin;
    switch (m_direction) {
    case ScreenController::Right: return -delta.x();
    case ScreenController::Left: return delta.x();
    case ScreenController::Down: return -delta.y();
    case ScreenController::Up: return delta.y();
    default: return 0;
    }
}

bool SwipeGesture::blocked(int direction) const
{
    for (const QPointer<QQuickItem> &item : m_chain) {
        if (!item)
            continue;
        if (item->inherits("QQuickFlickable") && flickableBlocks(item, direction))
            return true;
        if (item->inherits("QQuickMouseArea")) {
            if (item->property("preventStealing").toBool())
                return true;
            const QObject *drag = item->property("drag").value<QObject *>();
            if (drag && drag->property("active").toBool())
                return true;
        }
        // Sony's SCmnSlider becomes a seek/value drag after a 50 ms hold.
        if (item->property("operating").toBool() || item->property("sliderHoldingPressed").toBool())
            return true;
    }
    return false;
}

void SwipeGesture::move(const QPointF &position)
{
    if (!m_tracking || m_rejected)
        return;
    const qint64 now = m_clock.elapsed();
    m_samples.append({position, now});
    while (m_samples.size() > 2 && now - m_samples.first().time > velocityWindow)
        m_samples.removeFirst();
    if (!m_swiping) {
        const QPointF delta = position - m_origin;
        if (qMax(qAbs(delta.x()), qAbs(delta.y())) < pagingSlop)
            return;
        const int direction = qAbs(delta.x()) > qAbs(delta.y())
            ? (delta.x() < 0 ? ScreenController::Right : ScreenController::Left)
            : (delta.y() < 0 ? ScreenController::Down : ScreenController::Up);
        if (m_controller->neighbourId(direction).isEmpty() || blocked(direction)
            || !m_controller->beginSwipe(direction)) {
            m_rejected = true;
            return;
        }
        m_swiping = true;
        m_direction = direction;
        if (QQuickItem *grabber = m_window->mouseGrabberItem())
            grabber->ungrabMouse();
    }
    const qreal page = qAbs(m_direction) == 1 ? m_controller->width() : m_controller->height();
    m_controller->setDragOffset(qBound<qreal>(0, travel(position) - pagingSlop, page));
}

qreal SwipeGesture::velocity() const
{
    if (m_samples.size() < 2)
        return 0;
    const Sample &first = m_samples.first();
    const Sample &last = m_samples.last();
    const qint64 elapsed = last.time - first.time;
    if (elapsed <= 0)
        return 0;
    const QPointF delta = last.position - first.position;
    const qreal along = qAbs(m_direction) == 1 ? delta.x() : delta.y();
    // Positive is toward the incoming screen.
    return (m_direction > 0 ? -along : along) * 1000.0 / elapsed;
}

bool SwipeGesture::completes(qreal travelled, qreal pageSize, qreal velocity)
{
    if (travelled > flingDistance && qAbs(velocity) > flingVelocity)
        return velocity > 0;
    return travelled > pageSize * 0.6;
}

int SwipeGesture::settleDuration(qreal remaining, qreal pageSize, qreal velocity, bool horizontal)
{
    if (pageSize <= 0)
        return 0;
    const qreal half = pageSize / 2;
    const qreal ratio = qMin<qreal>(1.0, qAbs(remaining) / pageSize);
    const qreal distance = half + half * qSin((ratio - 0.5) * 0.4712389167638204);
    int duration;
    if (qAbs(velocity) > 1)
        duration = 4 * qRound(1000.0 * qAbs(distance / velocity));
    else
        duration = int((1.0 + qAbs(remaining) / pageSize) * (horizontal ? 100 : 150));
    return qMin(duration, maxSettle);
}

void SwipeGesture::release(const QPointF &position)
{
    if (!m_tracking)
        return;
    if (m_swiping) {
        m_samples.append({position, m_clock.elapsed()});
        const bool horizontal = qAbs(m_direction) == 1;
        const qreal page = horizontal ? m_controller->width() : m_controller->height();
        const qreal travelled = qBound<qreal>(0, travel(position) - pagingSlop, page);
        const qreal speed = velocity();
        const bool complete = completes(travelled, page, speed);
        const qreal remaining = complete ? page - travelled : travelled;
        m_controller->settleSwipe(complete, settleDuration(remaining, page, speed, horizontal));
    }
    m_tracking = false;
    m_swiping = false;
    emit m_controller->released();
}

void SwipeGesture::cancel()
{
    if (m_swiping)
        m_controller->settleSwipe(false, settleDuration(m_controller->dragOffset(), m_controller->width(), 0, true));
    m_tracking = false;
    m_swiping = false;
}

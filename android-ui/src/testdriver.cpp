#include "testdriver.h"

#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>

#include "screencontroller.h"

TestDriver::TestDriver(QQuickWindow *window, ScreenController *controller, const QString &script, QObject *parent)
    : QObject(parent), m_window(window), m_controller(controller), m_steps(script.split(';', Qt::SkipEmptyParts))
{
    m_clock.start();
    // While recording, every frame asks for the next one: the window keeps
    // drawing between gestures, so a long interval is a blocked GUI thread
    // rather than an idle window that had nothing to draw.
    connect(window, &QQuickWindow::frameSwapped, this, [this] {
        if (!m_recording)
            return;
        m_frames.append(m_clock.nsecsElapsed());
        QMetaObject::invokeMethod(m_window, "update", Qt::QueuedConnection);
    }, Qt::DirectConnection);
    QTimer::singleShot(0, this, &TestDriver::next);
}

void TestDriver::mouse(int type, const QPointF &position)
{
    const auto eventType = QEvent::Type(type);
    const Qt::MouseButtons buttons = eventType == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButton button = eventType == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(eventType, position, position, position, button, buttons, Qt::NoModifier);
    QGuiApplication::sendEvent(m_window, &event);
}

void TestDriver::report()
{
    if (m_frames.size() < 2) {
        qInfo("frames: %d swapped", m_frames.size());
        return;
    }
    QVector<double> gaps;
    for (int i = 1; i < m_frames.size(); ++i)
        gaps.append((m_frames.at(i) - m_frames.at(i - 1)) / 1e6);
    std::sort(gaps.begin(), gaps.end());
    const double worst = gaps.last();
    const double median = gaps.at(gaps.size() / 2);
    const int slow = int(std::count_if(gaps.begin(), gaps.end(), [](double gap) { return gap > 25.0; }));
    qInfo("frames: %d swapped, median %.1f ms, worst %.1f ms, %d over 25 ms",
            m_frames.size(), median, worst, slow);
}

static QQuickItem *findItem(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (QQuickItem *child : item->childItems()) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

void TestDriver::dump(QQuickItem *item, int depth, int indent) const
{
    const QPointF scene = item->mapToScene(QPointF());
    qInfo("%*s%s \"%s\" scene %.0f,%.0f size %.0fx%.0f%s%s", indent * 2, "",
          item->metaObject()->className(), qPrintable(item->objectName()), scene.x(), scene.y(), item->width(),
          item->height(), item->isVisible() ? "" : " hidden", item->opacity() < 1 ? " translucent" : "");
    if (depth > 0) {
        for (QQuickItem *child : item->childItems())
            dump(child, depth - 1, indent + 1);
    }
}

void TestDriver::next()
{
    if (m_dragStep >= 0) {
        ++m_dragStep;
        const qreal t = qreal(m_dragStep) / m_dragSteps;
        if (m_dragStep < m_dragSteps) {
            mouse(QEvent::MouseMove, m_from + (m_to - m_from) * t);
            QTimer::singleShot(16, this, &TestDriver::next);
        } else {
            mouse(QEvent::MouseMove, m_to);
            if (m_release)
                mouse(QEvent::MouseButtonRelease, m_to);
            m_pointer = m_to;
            m_dragStep = -1;
            QTimer::singleShot(16, this, &TestDriver::next);
        }
        return;
    }
    if (m_step >= m_steps.size()) {
        QGuiApplication::exit(m_failed ? 1 : 0);
        return;
    }
    const QString step = m_steps.at(m_step++).trimmed();
    const QString op = step.section(':', 0, 0);
    const QStringList args = step.section(':', 1).split(',');
    int delay = 0;
    if (op == "wait") {
        delay = args.value(0).toInt();
    } else if (op == "tap") {
        const QPointF point(args.value(0).toDouble(), args.value(1).toDouble());
        mouse(QEvent::MouseButtonPress, point);
        mouse(QEvent::MouseButtonRelease, point);
        delay = 50;
    } else if (op == "hold") {
        const QPointF point(args.value(0).toDouble(), args.value(1).toDouble());
        mouse(QEvent::MouseButtonPress, point);
        QTimer::singleShot(args.value(2).toInt(), this, [this, point] {
            mouse(QEvent::MouseButtonRelease, point);
            QTimer::singleShot(50, this, &TestDriver::next);
        });
        return;
    } else if (op == "drag") {
        m_from = QPointF(args.value(0).toDouble(), args.value(1).toDouble());
        m_to = QPointF(args.value(2).toDouble(), args.value(3).toDouble());
        m_dragSteps = qMax(2, args.value(4, "200").toInt() / 16);
        m_dragStep = 0;
        m_release = true;
        mouse(QEvent::MouseButtonPress, m_from);
        delay = 16;
    } else if (op == "press") {
        m_pointer = QPointF(args.value(0).toDouble(), args.value(1).toDouble());
        mouse(QEvent::MouseButtonPress, m_pointer);
        delay = 16;
    } else if (op == "move") {
        m_from = m_pointer;
        m_to = QPointF(args.value(0).toDouble(), args.value(1).toDouble());
        m_dragSteps = qMax(2, args.value(2, "200").toInt() / 16);
        m_dragStep = 0;
        m_release = false;
        delay = 16;
    } else if (op == "release") {
        mouse(QEvent::MouseButtonRelease, m_pointer);
    } else if (op == "key" && args.value(0) == "back") {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Back, Qt::NoModifier);
        QGuiApplication::sendEvent(m_window, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Back, Qt::NoModifier);
        QGuiApplication::sendEvent(m_window, &release);
        delay = 50;
    } else if (op == "grab") {
        // Layers leave zero alpha in the grab; the display is opaque.
        const bool ok = m_window->grabWindow().convertToFormat(QImage::Format_RGB32).save(args.value(0));
        qInfo("grab %s: %s", qPrintable(args.value(0)), ok ? "ok" : "FAILED");
        m_failed |= !ok;
    } else if (op == "frames") {
        if (args.value(0) == "start") {
            m_frames.clear();
            m_recording = true;
            m_window->update();
        } else {
            m_recording = false;
            report();
        }
    } else if (op == "dump") {
        QQuickItem *item = m_window->contentItem();
        if (!args.value(0).isEmpty())
            item = findItem(item, args.value(0));
        if (item)
            dump(item, args.value(1, "2").toInt(), 0);
        else
            qInfo("dump: no item %s", qPrintable(args.value(0)));
    } else if (op == "go") {
        if (!m_controller->forward(args.value(0)))
            qInfo("go %s: refused", qPrintable(args.value(0)));
        delay = 50;
    } else if (op == "popup") {
        m_controller->showPopup(args.value(0));
    } else if (op == "expect") {
        const bool ok = m_controller->screenID() == args.value(0);
        qInfo("expect %s: %s (current %s)", qPrintable(args.value(0)), ok ? "ok" : "FAILED",
                qPrintable(m_controller->screenID()));
        m_failed |= !ok;
    } else {
        qInfo("script: unknown step '%s'", qPrintable(step));
        m_failed = true;
    }
    QTimer::singleShot(delay, this, &TestDriver::next);
}

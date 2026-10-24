#include "probe.h"
#include "overlay.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QMap>
#include <QtCore/QMetaMethod>
#include <QtCore/QMetaProperty>
#include <QtCore/QPointer>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QWindow>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PLOG(fmt, ...) fprintf(stderr, "[wmprobe] " fmt "\n", ##__VA_ARGS__)

QT_BEGIN_NAMESPACE
// Private Qt Quick API, exported by the device's and the host's libQt5Quick.
class QQuickPixmap
{
public:
    static void purgeCache();
};
QT_END_NAMESPACE

namespace {

class Probe : public QObject
{
public:
    void listen();

private:
    void onConnection();
    void onReadyRead(QTcpSocket *sock);
    void reply(QTcpSocket *sock, QStringList out);
    QStringList run(const QString &line, QTcpSocket *sock);

    int idOf(QObject *o);
    QObject *lookup(const QString &id, QStringList &out);
    QString describe(QObject *o);
    QString valueText(const QVariant &v);

    void cmdWindows(QStringList &out);
    void cmdTree(const QStringList &args, QStringList &out);
    void walkItem(QQuickItem *item, int depth, int maxDepth, QStringList &out);
    void cmdObjects(const QStringList &args, QStringList &out);
    QList<QQmlEngine *> engines();
    void cmdCtx(const QStringList &args, QStringList &out);
    void walkObject(QObject *o, int depth, const QString &filter, QStringList &out);
    void cmdProps(const QStringList &args, QStringList &out);
    void cmdGet(const QStringList &args, QStringList &out);
    void cmdJson(const QStringList &args, QStringList &out);
    void cmdSet(const QStringList &args, QStringList &out);
    void cmdCall(const QStringList &args, QStringList &out);
    void cmdGrab(const QStringList &args, QStringList &out);
    void cmdShow(const QStringList &args, bool show, QStringList &out);
    void cmdGesture(const QStringList &args, bool drag, QStringList &out);
    void cmdEnv(QStringList &out);
    QList<QObject *> liveObjects();
    void cmdReload(const QStringList &args, QTcpSocket *sock, QStringList &out);

    QTcpServer *m_server = nullptr;
    QHash<int, QPointer<QObject>> m_byId;
    QHash<QObject *, int> m_ids;
    int m_nextId = 1;
    bool m_reloading = false;
};

// Set on a socket whose command answers later; its next lines wait.
const char HeldReply[] = "_wmprobe_held";

void Probe::listen()
{
    m_server = new QTcpServer(this);
    QObject::connect(m_server, &QTcpServer::newConnection, this, [this] { onConnection(); });
    if (m_server->listen(QHostAddress::LocalHost, PROBE_PORT))
        PLOG("listening on 127.0.0.1:%d, pid %d", PROBE_PORT, (int)getpid());
    else
        PLOG("listen failed: %s", qPrintable(m_server->errorString()));
}

void Probe::onConnection()
{
    while (QTcpSocket *sock = m_server->nextPendingConnection()) {
        QObject::connect(sock, &QTcpSocket::readyRead, this, [this, sock] { onReadyRead(sock); });
        QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        sock->write("wmprobe ready, `help` lists commands\n.\n");
    }
}

void Probe::onReadyRead(QTcpSocket *sock)
{
    while (!sock->property(HeldReply).toBool() && sock->canReadLine()) {
        const QString line = QString::fromUtf8(sock->readLine()).trimmed();
        if (line.isEmpty())
            continue;
        QStringList out = run(line, sock);
        if (!sock->property(HeldReply).toBool())
            reply(sock, out);
    }
}

void Probe::reply(QTcpSocket *sock, QStringList out)
{
    if (out.size() > PROBE_MAX_LINES) {
        const int dropped = out.size() - PROBE_MAX_LINES;
        out = out.mid(0, PROBE_MAX_LINES);
        out << QString("... %1 more lines dropped").arg(dropped);
    }
    sock->write(out.join('\n').toUtf8());
    sock->write("\n.\n");
}

QStringList Probe::run(const QString &line, QTcpSocket *sock)
{
    QStringList args = line.split(' ', QString::SkipEmptyParts);
    const QString cmd = args.takeFirst();
    QStringList out;
    if (cmd == "help") {
        out << "windows                      top-level windows"
            << "tree [maxdepth]              QQuickItem tree of every QQuickWindow"
            << "objects [classsubstr]        QObject tree from the app, windows and QML engines"
            << "ctx NAME                     look up a QML context property by name in every engine"
            << "props ID                     properties and methods of object ID"
            << "get ID PROP | set ID PROP VALUE"
            << "json ID PROP [KEY...]        a property as JSON, descending into map keys / list indexes"
            << "call ID METHOD [ARG...]      invoke a slot/invokable, args converted to its parameter types"
            << "grab [INDEX] PATH            save a window's frame as PNG"
            << "hide [INDEX] | show [INDEX]  hide or show a top-level window"
            << "tap X Y [MS]                 left-button press at X,Y of window 0, released after MS (80)"
            << "drag X1 Y1 X2 Y2 [MS]        press, move and release over MS (300), for swipes and sliders"
            << "env                          pid, uid, cwd and environment"
            << "overlay                      what the resource overlay did, at startup and since"
            << "reload PATH                  swap in the overlay at PATH and reload what holds " PROBE_RELOAD_TOKEN;
    } else if (cmd == "windows") {
        cmdWindows(out);
    } else if (cmd == "tree") {
        cmdTree(args, out);
    } else if (cmd == "objects") {
        cmdObjects(args, out);
    } else if (cmd == "ctx") {
        cmdCtx(args, out);
    } else if (cmd == "props") {
        cmdProps(args, out);
    } else if (cmd == "get") {
        cmdGet(args, out);
    } else if (cmd == "json") {
        cmdJson(args, out);
    } else if (cmd == "set") {
        cmdSet(args, out);
    } else if (cmd == "call") {
        cmdCall(args, out);
    } else if (cmd == "grab") {
        cmdGrab(args, out);
    } else if (cmd == "hide" || cmd == "show") {
        cmdShow(args, cmd == "show", out);
    } else if (cmd == "tap" || cmd == "drag") {
        cmdGesture(args, cmd == "drag", out);
    } else if (cmd == "env") {
        cmdEnv(out);
    } else if (cmd == "overlay") {
        out << overlayStatus();
    } else if (cmd == "reload") {
        cmdReload(args, sock, out);
    } else {
        out << "unknown command, try help";
    }
    return out;
}

int Probe::idOf(QObject *o)
{
    auto it = m_ids.constFind(o);
    if (it != m_ids.constEnd() && m_byId.value(*it) == o)
        return *it;
    const int id = m_nextId++;
    m_ids.insert(o, id);
    m_byId.insert(id, o);
    return id;
}

QObject *Probe::lookup(const QString &id, QStringList &out)
{
    QObject *o = m_byId.value(id.toInt()).data();
    if (!o)
        out << QString("no live object #%1 (list it again with tree/objects)").arg(id);
    return o;
}

QString Probe::describe(QObject *o)
{
    QString s = QString("#%1 %2").arg(idOf(o)).arg(o->metaObject()->className());
    if (!o->objectName().isEmpty())
        s += QString(" \"%1\"").arg(o->objectName());
    return s;
}

QString Probe::valueText(const QVariant &v)
{
    if (!v.isValid())
        return "<invalid>";
    if (QObject *o = v.value<QObject *>())
        return describe(o);
    if (v.type() == QVariant::List || v.type() == QVariant::StringList)
        return QString("<%1, %2 items>").arg(v.typeName()).arg(v.toList().size());
    if (v.type() == QVariant::Map)
        return QString("<map, %1 keys>").arg(v.toMap().size());
    if (v.canConvert<QString>())
        return v.toString().left(200);
    return QString("<%1>").arg(v.typeName());
}

void Probe::cmdWindows(QStringList &out)
{
    const QWindowList wl = QGuiApplication::allWindows();
    for (int i = 0; i < wl.size(); ++i) {
        QWindow *w = wl[i];
        out << QString("[%1] %2 %3x%4+%5+%6 visible=%7")
                   .arg(i).arg(describe(w))
                   .arg(w->width()).arg(w->height()).arg(w->x()).arg(w->y())
                   .arg(w->isVisible());
    }
}

void Probe::walkItem(QQuickItem *item, int depth, int maxDepth, QStringList &out)
{
    if (out.size() > PROBE_MAX_LINES)
        return;
    out << QString("%1%2 %3x%4+%5+%6%7")
               .arg(QString(depth * 2, ' ')).arg(describe(item))
               .arg(item->width()).arg(item->height()).arg(item->x()).arg(item->y())
               .arg(item->isVisible() ? "" : " hidden");
    if (depth < maxDepth)
        for (QQuickItem *c : item->childItems())
            walkItem(c, depth + 1, maxDepth, out);
}

void Probe::cmdTree(const QStringList &args, QStringList &out)
{
    const int maxDepth = args.isEmpty() ? 1000 : args[0].toInt();
    for (QWindow *w : QGuiApplication::allWindows())
        if (QQuickWindow *qw = qobject_cast<QQuickWindow *>(w)) {
            out << describe(qw);
            walkItem(qw->contentItem(), 1, maxDepth, out);
        }
}

void Probe::walkObject(QObject *o, int depth, const QString &filter, QStringList &out)
{
    if (out.size() > PROBE_MAX_LINES)
        return;
    if (filter.isEmpty() || QString(o->metaObject()->className()).contains(filter, Qt::CaseInsensitive))
        out << QString(depth * 2, ' ') + describe(o);
    for (QObject *c : o->children())
        walkObject(c, depth + 1, filter, out);
}

QList<QQmlEngine *> Probe::engines()
{
    QList<QQmlEngine *> found;
    for (QWindow *w : QGuiApplication::allWindows())
        if (QQuickWindow *qw = qobject_cast<QQuickWindow *>(w))
            for (QQuickItem *c : qw->contentItem()->childItems())
                if (QQmlEngine *e = qmlEngine(c))
                    if (!found.contains(e))
                        found << e;
    return found;
}

void Probe::cmdObjects(const QStringList &args, QStringList &out)
{
    const QString filter = args.value(0);
    walkObject(QCoreApplication::instance(), 0, filter, out);

    // QML engines, and with them the ViewModels exposed to QML, are usually
    // not children of the application object.
    for (QWindow *w : QGuiApplication::allWindows())
        if (!w->parent())
            walkObject(w, 0, filter, out);
    for (QQmlEngine *e : engines()) {
        out << "engine " + describe(e) + " root context " + describe(e->rootContext());
        if (!e->parent())
            walkObject(e, 1, filter, out);
    }
}

void Probe::cmdCtx(const QStringList &args, QStringList &out)
{
    // Context properties cannot be enumerated through public API; their names
    // come from the QML text embedded in the app binary.
    const QString name = args.value(0);
    for (QQmlEngine *e : engines()) {
        const QVariant v = e->rootContext()->contextProperty(name);
        out << QString("%1 in %2: %3").arg(name).arg(describe(e)).arg(valueText(v));
    }
    if (out.isEmpty())
        out << "no QML engine found";
}

void Probe::cmdProps(const QStringList &args, QStringList &out)
{
    QObject *o = lookup(args.value(0), out);
    if (!o)
        return;
    const QMetaObject *mo = o->metaObject();
    out << describe(o);
    for (const QMetaObject *m = mo; m; m = m->superClass())
        out << QString("  class %1").arg(m->className());
    for (int i = 0; i < mo->propertyCount(); ++i) {
        const QMetaProperty p = mo->property(i);
        out << QString("  prop %1 %2 = %3%4")
                   .arg(p.typeName()).arg(p.name()).arg(valueText(p.read(o)))
                   .arg(p.isWritable() ? "" : " (ro)");
    }
    for (const QByteArray &name : o->dynamicPropertyNames())
        out << QString("  dyn  %1 = %2").arg(QString(name)).arg(valueText(o->property(name)));
    static const char *kinds[] = {"method", "signal", "slot", "ctor"};
    for (int i = mo->methodOffset() > 0 ? QObject::staticMetaObject.methodCount() : 0;
         i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        out << QString("  %1 %2 %3").arg(kinds[m.methodType()])
                   .arg(m.typeName()).arg(QString(m.methodSignature()));
    }
}

void Probe::cmdGet(const QStringList &args, QStringList &out)
{
    if (QObject *o = lookup(args.value(0), out))
        out << valueText(o->property(args.value(1).toUtf8()));
}

void Probe::cmdJson(const QStringList &args, QStringList &out)
{
    QObject *o = lookup(args.value(0), out);
    if (!o)
        return;
    QVariant v = o->property(args.value(1).toUtf8());
    for (const QString &key : args.mid(2)) {
        if (v.type() == QVariant::Map)
            v = v.toMap().value(key);
        else if (v.canConvert<QVariantList>() && v.type() != QVariant::String)
            v = v.toList().value(key.toInt());
        else
            v = QVariant();
    }
    // QObject values inside maps and lists come out as null.
    const QJsonValue j = QJsonValue::fromVariant(v);
    if (j.isObject())
        out << QString::fromUtf8(QJsonDocument(j.toObject()).toJson()).trimmed().split('\n');
    else if (j.isArray())
        out << QString::fromUtf8(QJsonDocument(j.toArray()).toJson()).trimmed().split('\n');
    else
        out << valueText(v);
}

void Probe::cmdSet(const QStringList &args, QStringList &out)
{
    QObject *o = lookup(args.value(0), out);
    if (!o)
        return;
    const QByteArray name = args.value(1).toUtf8();
    const QString text = QStringList(args.mid(2)).join(' ');
    QVariant v(text);
    const int idx = o->metaObject()->indexOfProperty(name);
    if (idx >= 0)
        v.convert(o->metaObject()->property(idx).userType());
    out << (o->setProperty(name, v) ? "ok" : "failed") << valueText(o->property(name));
}

void Probe::cmdCall(const QStringList &args, QStringList &out)
{
    QObject *o = lookup(args.value(0), out);
    if (!o)
        return;
    const QByteArray name = args.value(1).toUtf8();
    const QStringList callArgs = args.mid(2);
    if (callArgs.size() > 10) {
        out << "at most 10 arguments";
        return;
    }
    const QMetaObject *mo = o->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.name() != name || m.parameterCount() != callArgs.size())
            continue;

        QVariant values[10];
        QGenericArgument gargs[10];
        for (int a = 0; a < callArgs.size(); ++a) {
            values[a] = QVariant(callArgs[a]);
            if (!values[a].convert(m.parameterType(a))) {
                out << QString("cannot convert '%1' to %2").arg(callArgs[a])
                           .arg(QString(m.parameterTypes().at(a)));
                return;
            }
            gargs[a] = QGenericArgument(m.parameterTypes().at(a).constData(), values[a].constData());
        }

        // An unregistered return type cannot be captured; the call still runs.
        const int rt = m.returnType();
        QVariant ret;
        QGenericReturnArgument gret;
        if (rt == QMetaType::QVariant) {
            gret = QGenericReturnArgument("QVariant", &ret);
        } else if (rt != QMetaType::Void && rt != QMetaType::UnknownType) {
            ret = QVariant(rt, nullptr);
            gret = QGenericReturnArgument(m.typeName(), ret.data());
        }
        const bool ok = m.invoke(o, Qt::DirectConnection, gret,
                                 gargs[0], gargs[1], gargs[2], gargs[3], gargs[4],
                                 gargs[5], gargs[6], gargs[7], gargs[8], gargs[9]);
        out << QString("%1 %2").arg(ok ? "ok" : "failed").arg(QString(m.methodSignature()));
        if (ok && rt != QMetaType::Void)
            out << "returned " + (rt == QMetaType::UnknownType ? QString("<unregistered %1>").arg(m.typeName())
                                                                 : valueText(ret));
        return;
    }
    out << QString("no method %1 taking %2 argument(s)").arg(QString(name)).arg(callArgs.size());
}

// Synthetic mouse events go straight to the window: the app grabs the touch
// panel's evdev node, so events written to it from outside never arrive.
static void sendMouse(QWindow *w, QEvent::Type type, const QPointF &pos, Qt::MouseButton button,
                      Qt::MouseButtons buttons)
{
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    QMouseEvent ev(type, pos, pos, w->mapToGlobal(pos.toPoint()), button, buttons, Qt::NoModifier);
    // Flickable and friends derive velocity from timestamps.
    ev.setTimestamp(clock.elapsed());
    QCoreApplication::sendEvent(w, &ev);
}

void Probe::cmdGesture(const QStringList &args, bool drag, QStringList &out)
{
    const int coords = drag ? 4 : 2;
    QList<double> v;
    bool ok = args.size() >= coords;
    for (int i = 0; ok && i < coords; ++i)
        v << args[i].toDouble(&ok);
    QWindow *w = QGuiApplication::allWindows().value(0);
    if (!ok || !w) {
        out << (drag ? "usage: drag X1 Y1 X2 Y2 [MS]" : "usage: tap X Y [MS]");
        return;
    }
    const QPointF from(v[0], v[1]);
    const QPointF to = drag ? QPointF(v[2], v[3]) : from;
    const int ms = args.value(coords, drag ? "300" : "80").toInt();
    const int steps = drag ? 12 : 1;

    sendMouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    // The rest comes from the event loop, so press-and-hold timers and
    // animations see the time pass.
    QPointer<QWindow> win(w);
    QTimer *timer = new QTimer(this);
    timer->setInterval(qMax(1, ms / steps));
    int step = 0;
    QObject::connect(timer, &QTimer::timeout, this, [=]() mutable {
        ++step;
        const QPointF pos = from + (to - from) * (double(step) / steps);
        if (win && drag)
            sendMouse(win, QEvent::MouseMove, pos, Qt::NoButton, Qt::LeftButton);
        if (step < steps)
            return;
        if (win)
            sendMouse(win, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
        timer->stop();
        timer->deleteLater();
    });
    timer->start();
    out << QString("%1 %2,%3 -> %4,%5 over %6 ms").arg(drag ? "drag" : "tap")
               .arg(from.x()).arg(from.y()).arg(to.x()).arg(to.y()).arg(ms);
}

void Probe::cmdGrab(const QStringList &args, QStringList &out)
{
    const int index = args.size() > 1 ? args[0].toInt() : 0;
    const QString path = args.value(args.size() > 1 ? 1 : 0);
    QQuickWindow *qw = qobject_cast<QQuickWindow *>(QGuiApplication::allWindows().value(index));
    if (!qw || path.isEmpty()) {
        out << "usage: grab [INDEX] PATH (INDEX must be a QQuickWindow)";
        return;
    }
    const QImage img = qw->grabWindow();
    out << (img.save(path, "PNG") ? QString("saved %1x%2 to %3").arg(img.width()).arg(img.height()).arg(path)
                                  : QString("save to %1 failed").arg(path));
}

void Probe::cmdShow(const QStringList &args, bool show, QStringList &out)
{
    QWindow *w = QGuiApplication::allWindows().value(args.value(0, "0").toInt());
    if (!w) {
        out << "no such window";
        return;
    }
    show ? w->show() : w->hide();
    out << describe(w) + QString(" visible=%1").arg(w->isVisible());
}

void Probe::cmdEnv(QStringList &out)
{
    out << QString("pid=%1 uid=%2 gid=%3 cwd=%4")
               .arg(getpid()).arg(getuid()).arg(getgid()).arg(QDir::currentPath());
    out << QCoreApplication::arguments().join(' ');
    out << QProcessEnvironment::systemEnvironment().toStringList();
}

static void walkAll(QObject *o, QSet<QObject *> &seen, QList<QObject *> &all)
{
    if (!o || seen.contains(o))
        return;
    seen.insert(o);
    all << o;
    for (QObject *c : o->children())
        walkAll(c, seen, all);
    if (QQuickItem *item = qobject_cast<QQuickItem *>(o))
        for (QQuickItem *c : item->childItems())
            walkAll(c, seen, all);
}

// Every object reachable from the windows, through QObject children and
// visual children alike.
QList<QObject *> Probe::liveObjects()
{
    QSet<QObject *> seen;
    QList<QObject *> all;
    for (QWindow *w : QGuiApplication::allWindows()) {
        walkAll(w, seen, all);
        if (QQuickWindow *qw = qobject_cast<QQuickWindow *>(w))
            walkAll(qw->contentItem(), seen, all);
    }
    return all;
}

static QString qrcPath(const QUrl &url)
{
    return url.scheme() == "qrc" ? url.path() : QString();
}

// Looking a name up with indexOfProperty(), as property() and
// setProperty() do, creates it on an open meta-object (PathView's attached
// objects, QQmlPropertyMap), so walks over the app's objects compare names
// instead, and only on the levels that QML declared.
static int qmlDeclaredProperty(QObject *o, const char *name)
{
    for (const QMetaObject *m = o->metaObject(); m && strstr(m->className(), "_QML"); m = m->superClass())
        for (int i = m->propertyOffset(); i < m->propertyCount(); ++i)
            if (!qstrcmp(m->property(i).name(), name))
                return i;
    return -1;
}

static QString sourceFile(QObject *o)
{
    if (!o->inherits("QQuickImageBase") && !o->inherits("QQuickLoader"))
        return QString();
    return qrcPath(o->property("source").toUrl());
}

static bool isLoading(QObject *o)
{
    if (!o->inherits("QQuickLoader"))
        return false;
    const QMetaObject *m = o->metaObject();
    const QMetaEnum status = m->enumerator(m->indexOfEnumerator("Status"));
    return o->property("status").toInt() == status.keyToValue("Loading");
}

// The qrc files an object shows: the QML document it was declared in, and
// what its source names.
static QStringList shownFiles(QObject *o)
{
    QStringList paths;
    if (QQmlContext *ctx = qmlContext(o))
        paths << qrcPath(ctx->baseUrl());
    paths << sourceFile(o);
    paths.removeAll(QString());
    return paths;
}

void Probe::cmdReload(const QStringList &args, QTcpSocket *sock, QStringList &out)
{
    if (args.size() != 1) {
        out << "usage: reload PATH";
        return;
    }
    if (m_reloading) {
        out << "a reload is in progress";
        return;
    }
    // A file still loading holds the files it uses outside the engine's
    // cache, which the trim below then drops from it: the next use compiles
    // them again, and Qt 5.3 mixes the two compiles up the same way it does
    // after a clear.
    for (QObject *o : liveObjects())
        if (isLoading(o)) {
            out << "not now, still loading: " + describe(o);
            return;
        }
    QStringList changed, added;
    if (!overlayReload(args[0], changed, added, out))
        return;
    for (const QString &path : changed)
        out << "changed " + path;
    for (const QString &path : added)
        out << "new " + path;
    if (!added.isEmpty())
        out << "new files: a URL finds them now, a type name may not until the app restarts";

    // Objects from a changed file that are still alive once the holders
    // have let go of theirs keep it compiled as it was. QML names an
    // instance's class after its file, which finds instances of leaf types,
    // but Qt 5.3 only does that when the file's root declares a property,
    // signal or function, so a leaf with none of those and no children goes
    // unreported (it is still kept, which is what matters for the crash).
    const QSet<QString> changedSet = QSet<QString>::fromList(changed);
    QHash<QString, QStringList> changedTypes;
    for (const QString &path : changed)
        if (path.endsWith(".qml"))
            changedTypes[QFileInfo(path).completeBaseName()] << path;
    QList<QPointer<QObject>> holders;
    QList<int> tokens;
    QList<QPair<QPointer<QObject>, QString>> shown;
    for (QObject *o : liveObjects()) {
        const int token = qmlDeclaredProperty(o, PROBE_RELOAD_TOKEN);
        if (token >= 0) {
            holders << o;
            tokens << o->metaObject()->property(token).read(o).toInt();
            continue;
        }
        QStringList paths = shownFiles(o);
        const QString className = o->metaObject()->className();
        const int type = className.indexOf("_QMLTYPE_");
        if (type > 0)
            paths << changedTypes.value(className.left(type));
        for (const QString &path : paths)
            if (changedSet.contains(path))
                shown << qMakePair(QPointer<QObject>(o), path);
    }
    for (const QPointer<QObject> &h : holders)
        if (h)
            h->setProperty(PROBE_RELOAD_TOKEN, -1);

    m_reloading = true;
    sock->setProperty(HeldReply, true);
    QPointer<QTcpSocket> client(sock);
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, this, [=]() mutable {
        timer->deleteLater();
        // Not clearComponentCache(): when a State with a binding applies,
        // Qt 5.3 looks the compiled file up again by URL
        // (QQmlBinding::createBinding), and after a clear that finds a new,
        // unlinked compile; the device's app crashed that way.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QQuickPixmap::purgeCache();
        for (QQmlEngine *e : engines())
            e->trimComponentCache();

        QStringList refreshed;
        for (int i = 0; i < holders.size(); ++i) {
            QObject *h = holders[i].data();
            if (!h)
                continue;
            h->setProperty(PROBE_RELOAD_TOKEN, qMax(tokens[i], 0) + 1);
            const QString source = sourceFile(h);
            refreshed << (source.isEmpty() ? describe(h) : source);
        }
        const QString summary = QString("refreshed %1%2").arg(refreshed.size())
                                    .arg(refreshed.isEmpty() ? QString() : ": " + refreshed.join(", "));
        out << summary;
        PLOG("%s", qPrintable(summary));

        QMap<QString, int> stale;
        for (const auto &s : shown)
            if (s.first)
                ++stale[s.second];
        for (auto it = stale.constBegin(); it != stale.constEnd(); ++it)
            out << QString("kept, still in use outside any %1: %2 (%3 objects)")
                       .arg(PROBE_RELOAD_TOKEN).arg(it.key()).arg(it.value());

        m_reloading = false;
        if (client) {
            client->setProperty(HeldReply, false);
            reply(client, out);
            onReadyRead(client);
        }
    });
    timer->start(PROBE_RELOAD_SETTLE_MS);
}

} // namespace

void probeStart()
{
    static bool started = false;
    if (started || !QCoreApplication::instance())
        return;
    started = true;
    // Off unless asked for, since it can call into the app.
    const QString flag = overlayDir() + "/probe";
    if (qgetenv("WMPROBE") != "1" && !QFile::exists(flag)) {
        PLOG("off; create %s or set WMPROBE=1 to start it", qPrintable(flag));
        return;
    }
    // Called from createNativeWindow() on the GUI thread, before the QML scene
    // exists. The server only answers queries later, from the event loop.
    Probe *probe = new Probe;
    probe->setParent(QCoreApplication::instance());
    probe->listen();
}

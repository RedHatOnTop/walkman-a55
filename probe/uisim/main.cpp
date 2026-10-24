// Host UI simulator: renders one of the app's screens from its own qrc
// resources, with QML stand-ins for the app's global context objects, and
// saves a PNG. Rendering is Qt 5.15 with Mesa, not the device's Qt 5.3 with
// Mali, so this is a layout preview, not a pixel-exact one.
//
//   uisim --rcc FILE [--rcc FILE ...] --stubs DIR --fonts DIR [--qm FILE] --spec JSON --out PNG
//
// The first --rcc registered wins for a path, so pass a mod's overlay
// before the stock resources. --spec is {"screen": url, "values": {...},
// "window": url, "windowValues": {...}, "cover": file in --covers}. The cover
// answers every image://coverart/ request, as the app's provider would for
// the track that is playing.

#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QResource>
#include <QtCore/QTimer>
#include <QtCore/QTranslator>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickImageProvider>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>

#include <stdio.h>

static int g_qmlWarnings = 0;

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type != QtDebugMsg && msg.contains("qrc:/"))
        ++g_qmlWarnings;
    fprintf(stderr, "%s\n", qPrintable(msg));
}

class CoverProvider : public QQuickImageProvider
{
public:
    explicit CoverProvider(const QString &path) : QQuickImageProvider(Image), m_image(path) {}

    QImage requestImage(const QString &, QSize *size, const QSize &requested) override
    {
        QImage image = m_image;
        if (!image.isNull() && requested.isValid())
            image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (size)
            *size = image.size();
        return image;
    }

private:
    QImage m_image;
};

int main(int argc, char **argv)
{
    qInstallMessageHandler(messageHandler);
    QGuiApplication app(argc, argv);

    QCommandLineParser args;
    args.addOptions({{"rcc", "resource file", "file"}, {"stubs", "stub dir", "dir"},
                     {"fonts", "font dir", "dir"}, {"qm", "translation", "file"},
                     {"spec", "screen spec", "json"}, {"covers", "cover art dir", "dir"},
                     {"out", "output png", "png"}, {"settle", "ms to wait", "ms", "1000"}});
    args.process(app);

    for (const QString &rcc : args.values("rcc"))
        if (!QResource::registerResource(rcc))
            qFatal("cannot register %s", qPrintable(rcc));
    for (const QFileInfo &f : QDir(args.value("fonts")).entryInfoList({"*.ttf", "*.otf"}))
        QFontDatabase::addApplicationFont(f.filePath());

    // The app's texts are message IDs (qsTrId) until a translation is loaded.
    QTranslator translator;
    if (args.isSet("qm")) {
        if (!translator.load(args.value("qm")))
            qFatal("cannot load %s", qPrintable(args.value("qm")));
        app.installTranslator(&translator);
    }

    QFile specFile(args.value("spec"));
    if (!specFile.open(QIODevice::ReadOnly))
        qFatal("cannot read %s", qPrintable(args.value("spec")));
    const QVariantMap spec = QJsonDocument::fromJson(specFile.readAll()).toVariant().toMap();

    QQuickView view;
    QQmlEngine *engine = view.engine();
    for (const QFileInfo &f : QDir(args.value("stubs")).entryInfoList({"*.qml"})) {
        QQmlComponent component(engine, QUrl::fromLocalFile(f.filePath()));
        QObject *stub = component.create();
        if (!stub)
            qFatal("stub %s: %s", qPrintable(f.fileName()), qPrintable(component.errorString()));
        stub->setParent(engine);
        engine->rootContext()->setContextProperty(f.baseName(), stub);
    }
    // The app sets its default font from the same family.
    if (QObject *font = engine->rootContext()->contextProperty("appFont").value<QObject *>())
        QGuiApplication::setFont(QFont(font->property("fontFamily").toString()));
    if (spec.contains("cover"))
        engine->addImageProvider("coverart", new CoverProvider(
            QDir(args.value("covers")).filePath(spec.value("cover").toString())));
    engine->rootContext()->setContextProperty("previewSpec", spec);
    view.setSource(QUrl("qrc:/uisim/Preview.qml"));
    if (view.status() != QQuickView::Ready)
        return 1;
    view.show();

    const QString out = args.value("out");
    QTimer::singleShot(args.value("settle").toInt(), [&] {
        QQuickItem *screen = view.rootObject()->findChild<QQuickItem *>("previewScreen");
        const bool loaded = screen && screen->property("status").toInt() == 1;
        const bool saved = view.grabWindow().save(out);
        printf("%s: screen %s, %d QML warnings, %s\n", qPrintable(spec.value("screen").toString()),
               loaded ? "loaded" : "NOT loaded", g_qmlWarnings, saved ? qPrintable(out) : "save failed");
        app.exit(loaded && saved ? 0 : 1);
    });
    return app.exec();
}

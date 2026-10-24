#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QLoggingCategory>
#include <QtCore/QResource>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QTranslator>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickWindow>

#include <memory>
#include <stdio.h>

#include "../library/musicdb.h"
#include "../library/sensme.h"
#include "../playback/platform.h"
#include "../playback/player.h"
#include "appcontext.h"
#include "coverart.h"
#include "inputcontextproxy.h"
#include "models/soundstate.h"
#include "screencontroller.h"
#include "soundeffects.h"
#include "swipegesture.h"
#include "testdriver.h"

namespace {

QString resourceDir()
{
#ifdef Q_OS_ANDROID
    // The install kit puts the Sony resources, made from the owner's own
    // firmware, into the system image; a development build carries them as
    // assets instead.
    const QString system = QStringLiteral("/system/etc/walkmanui/res");
    return QFileInfo::exists(system + "/stock.rcc") ? system : QStringLiteral("assets:/res");
#else
    const QByteArray configured = qgetenv("WALKMANUI_RES");
    return configured.isEmpty() ? QStringLiteral(WALKMANUI_RES_DIR) : QString::fromLocal8Bit(configured);
#endif
}

// QResource maps a file; an APK asset can only be read. Copy the bundle to
// the cache once per build so it is mapped instead of held in the heap.
QString mappableResource(const QString &path)
{
    if (!path.startsWith("assets:"))
        return path;
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty() || !QDir().mkpath(cache))
        return path;
    const QString target = cache + "/stock.rcc";
    QFile source(path);
    if (QFileInfo(target).size() == source.size())
        return target;
    QFile::remove(target);
    return source.copy(target) ? target : path;
}

QString loadResources(const QString &res, const QString &locale)
{
    if (!QResource::registerResource(mappableResource(res + "/stock.rcc")))
        return "Cannot register " + res + "/stock.rcc";
    const QDir fonts(res + "/fonts");
    for (const QString &name : fonts.entryList({"*.ttf", "*.otf"})) {
        if (QFontDatabase::addApplicationFont(fonts.filePath(name)) < 0)
            return "Cannot load font " + name;
    }
    // SST carries Latin only; the stock app substituted per language.
    QFont::insertSubstitutions("SST", {"Noto Sans KR", "SST UI"});
    auto *translator = new QTranslator(qApp);
    const QString tr = res + "/tr/HgrmMediaPlayerApp_%1.qm";
    if (!translator->load(tr.arg(locale)) && !translator->load(tr.arg("en_US")))
        return "Cannot load the Sony translations from " + res + "/tr";
    QCoreApplication::installTranslator(translator);
    return {};
}

// The language chosen in Settings > Language, else the system's, as one of
// Sony's translations (Sony names Portuguese pr_PR).
QString sonyLocale(const QSettings &prefs)
{
    const QByteArray forced = qgetenv("WALKMANUI_LANG");
    QString name = forced.isEmpty() ? prefs.value("settings/language").toString() : QString::fromLatin1(forced);
    if (name.isEmpty())
        name = QLocale::system().name();
    if (name.startsWith("pt"))
        name = "pr_PR";
    static const QStringList shipped{"de_DE", "en_US", "es_ES", "fr_FR", "it_IT", "ja_JP", "ko_KR",
                                     "pl_PL", "pr_PR", "ru_RU", "tr_TR", "zh_CN", "zh_HK"};
    if (shipped.contains(name))
        return name;
    for (const QString &candidate : shipped) {
        if (candidate.startsWith(name.left(2)))
            return candidate;
    }
    return "en_US";
}

// The stock storage list: the device's own storage, then a removable card.
QVector<Storage> musicStorages(const QString &folder)
{
    const char *context = "dmpapp::StorageTopViewModelList";
    const QString internal = sonyText(context, "020048");
    if (!folder.isEmpty())
        return {{internal, QDir(folder).absolutePath(), "qrc:/common/ic_music_library_list_storage_internal_memory.png"}};
#ifdef Q_OS_ANDROID
    QVector<Storage> storages{{internal, QStandardPaths::writableLocation(QStandardPaths::MusicLocation).section('/', 0, -2),
                               "qrc:/common/ic_music_library_list_storage_internal_memory.png"}};
    for (const QFileInfo &mount : QDir("/storage").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString name = mount.fileName();
        // FireOS's init links /storage/sdcard0 to the emulated storage, which
        // would list every internal track twice.
        if (name == "emulated" || name == "self" || mount.isSymLink() || !mount.isReadable()
            || QDir(mount.filePath()).isEmpty())
            continue;
        storages.append({sonyText(context, "020049"), mount.filePath(), "qrc:/ic_music_library_list_storage_sd_card.png"});
    }
    return storages;
#else
    return {{internal, QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
             "qrc:/common/ic_music_library_list_storage_internal_memory.png"}};
#endif
}

class BackKeyFilter : public QObject
{
public:
    explicit BackKeyFilter(AppContext *app) : QObject(app), m_app(app) {}
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::KeyRelease && event->type() != QEvent::KeyPress)
            return false;
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key != Qt::Key_Back && key != Qt::Key_Escape)
            return false;
        if (event->type() == QEvent::KeyRelease && !m_app->pressBack())
            backgroundPlaybackApp();
        return true;
    }

private:
    AppContext *m_app;
};

}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("WalkmanUI");
    QCoreApplication::setApplicationName("WalkmanUI");
    // Sony's views use the pre-5.15 Connections syntax throughout.
    QLoggingCategory::setFilterRules("qt.qml.connections.warning=false");

    QCommandLineParser arguments;
    arguments.addHelpOption();
    arguments.addOption({"music-folder", "Load a local music folder.", "path"});
    arguments.addOption({"prefs", "Use an explicit preferences file.", "path"});
    arguments.process(app);

    const std::unique_ptr<QSettings> prefs(arguments.isSet("prefs")
        ? new QSettings(arguments.value("prefs"), QSettings::IniFormat) : new QSettings);

    const QString res = resourceDir();
    const QString locale = sonyLocale(*prefs);
    QString error = loadResources(res, locale);
    ScreenSettings screens;
    if (error.isEmpty())
        error = screens.load(res + "/ScreenSettings.json", res + "/allViewLayouts.json");
    if (!error.isEmpty()) {
        fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    if (!SensMe::loadModel(res + "/sensme.bin"))
        fprintf(stderr, "No SensMe model in %s: every track stays in Shuffle All\n", qPrintable(res));

    const QString data = arguments.isSet("prefs") ? QFileInfo(arguments.value("prefs")).absolutePath() + "/data"
                                                  : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    MusicDb library(data);
    QMediaPlayer media;
    PlayerController player(&media, &library, prefs.get());
    AppContext context(&screens, prefs.get(), &player, &library, locale.left(2));

    qmlRegisterType<InputContextProxyHelper>("ext.lib.inputcontextproxy", 1, 0, "InputContextProxyHelper");
    QQmlApplicationEngine engine;
    context.install(&engine);
    engine.addImageProvider("coverart", new CoverArtProvider(&library));
    engine.addImageProvider("coverart-triple", new CoverArtProvider(&library));
    engine.load(QUrl("qrc:/app/AppFrame.qml"));
    auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) {
        fprintf(stderr, "Cannot load qrc:/app/AppFrame.qml\n");
        return 1;
    }

    ScreenController *controller = context.controller();
    auto *gesture = new SwipeGesture(window, controller, &context);
    const auto resize = [window, controller, gesture] {
        if (window->width() <= 0)
            return;
        const qreal scale = window->width() / 480.0;
        gesture->setStageScale(scale);
        controller->setWindowSize(QSize(480, qRound(window->height() / scale)));
    };
    QObject::connect(window, &QQuickWindow::widthChanged, &context, resize);
    QObject::connect(window, &QQuickWindow::heightChanged, &context, resize);
    window->installEventFilter(new BackKeyFilter(&context));
    QObject::connect(&context, &AppContext::backgroundRequested, &context, [] { backgroundPlaybackApp(); });
    context.setRoot(window->contentItem());

#ifdef Q_OS_ANDROID
    window->showMaximized();
#else
    window->resize(480, qEnvironmentVariableIntValue("WALKMANUI_HEIGHT") > 0
                            ? qEnvironmentVariableIntValue("WALKMANUI_HEIGHT") : 752);
    window->show();
#endif
    resize();
    attachPlaybackPlatform(&player);
    attachSoundEffects(SoundState::of(&context), &player);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &player, [&player] {
        player.stop();
        detachPlaybackPlatform();
    });

    const QString folder = arguments.value("music-folder");
    withMediaAccess(&library, [&library, folder] {
        const auto list = [&library, folder] { library.setStorages(musicStorages(folder)); };
        list();
        // MEDIA_MOUNTED comes once vold has mounted /mnt/media_rw/sdcard1; the sdcard
        // daemon's view of it in /storage is still empty for a while after that.
        onStoragesChanged(&library, [&library, list] {
            for (int ms : {0, 1000, 3000, 7000, 15000})
                QTimer::singleShot(ms, &library, list);
        });
    });
    context.start();

    QString script = QString::fromLocal8Bit(qgetenv("WALKMANUI_SCRIPT"));
    // An Android launch carries no environment: a script left in the app's
    // data (adb push or run-as) runs once instead.
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)) {
        QFile file(dir + "/walkmanui-script.txt");
        if (script.isEmpty() && file.open(QIODevice::ReadOnly)) {
            script = QString::fromUtf8(file.readAll()).trimmed();
            file.close();
            // Consumed even when an earlier run's .done is in the way: a
            // script left behind drives every later launch.
            QFile::remove(file.fileName() + ".done");
            if (!file.rename(file.fileName() + ".done"))
                file.remove();
        }
    }
    if (!script.isEmpty())
        new TestDriver(window, controller, script, &context);
    return app.exec();
}

#include "platform.h"

#include "player.h"

#ifdef Q_OS_ANDROID
#include <QtAndroidExtras/QtAndroid>
#include <QtAndroidExtras/QAndroidJniEnvironment>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

static const char serviceClass[] = "org/walkmanui/PlaybackService";
static QMutex playerMutex;
static QPointer<PlayerController> platformPlayer;
static bool platformFailed = false;
static QElapsedTimer publishedAt;
static qint64 publishedPosition = 0;
static bool publishedPlaying = false;
static QMutex storagesMutex;
static QPointer<QObject> storagesContext;
static std::function<void()> storagesChanged;

static bool androidCallSucceeded()
{
    QAndroidJniEnvironment environment;
    if (!environment->ExceptionCheck())
        return true;
    environment->ExceptionDescribe();
    environment->ExceptionClear();
    return false;
}

static void publishSession(PlayerController *player)
{
    if (platformFailed)
        return;
    QAndroidJniObject::callStaticMethod<void>(serviceClass, "update",
        "(Landroid/content/Context;ZZLjava/lang/String;Ljava/lang/String;JJ)V",
        QtAndroid::androidContext().object(), jboolean(player->sessionActive()),
        jboolean(player->playing()), QAndroidJniObject::fromString(player->title()).object<jstring>(),
        QAndroidJniObject::fromString(player->artist()).object<jstring>(),
        jlong(player->duration()), jlong(player->position()));
    if (!androidCallSucceeded()) {
        platformFailed = true;
        player->platformError("Android media controls could not start. Restart the app and retry.");
        return;
    }
    publishedAt.start();
    publishedPosition = player->position();
    publishedPlaying = player->playing();
}

// The session's controllers (SystemUI, the Bluetooth process's AVRCP) carry the
// position forward from the last state themselves. Republishing on every 250 ms
// position tick woke them four times a second; only a jump, a seek, needs it.
static bool positionJumped(PlayerController *player)
{
    const qint64 expected = publishedPosition + (publishedPlaying && publishedAt.isValid() ? publishedAt.elapsed() : 0);
    return qAbs(player->position() - expected) > 1000;
}

extern "C" JNIEXPORT void JNICALL
Java_org_walkmanui_PlaybackService_nativeCommand(JNIEnv *, jclass, jint command, jlong position)
{
    QMutexLocker lock(&playerMutex);
    const QPointer<PlayerController> player = platformPlayer;
    if (!player)
        return;
    QMetaObject::invokeMethod(player, [player, command, position] {
        if (!player)
            return;
        switch (command) {
        case 0: player->togglePlayback(); break;
        case 1: player->next(); break;
        case 2: player->previous(); break;
        case 3: player->stop(); break;
        case 4: player->pause(); break;
        case 5: if (!player->playing()) player->togglePlayback(); break;
        case 6: player->seek(position); break;
        default: qWarning("Unknown Android media command: %d", command); break;
        }
    }, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_org_walkmanui_PlaybackService_nativeFailure(JNIEnv *, jclass, jstring message)
{
    const QString error = QAndroidJniObject(message).toString();
    QMutexLocker lock(&playerMutex);
    const QPointer<PlayerController> player = platformPlayer;
    if (!player)
        return;
    QMetaObject::invokeMethod(player, [player, error] {
        platformFailed = true;
        if (player)
            player->platformError(error);
    }, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_org_walkmanui_WalkmanActivity_nativeStoragesChanged(JNIEnv *, jclass)
{
    QMutexLocker lock(&storagesMutex);
    if (storagesContext)
        QMetaObject::invokeMethod(storagesContext, storagesChanged, Qt::QueuedConnection);
}
#endif

void onStoragesChanged(QObject *context, std::function<void()> changed)
{
#ifdef Q_OS_ANDROID
    QMutexLocker lock(&storagesMutex);
    storagesContext = context;
    storagesChanged = std::move(changed);
#else
    Q_UNUSED(context)
    Q_UNUSED(changed)
#endif
}

void withMediaAccess(QObject *context, std::function<void()> ready)
{
#ifdef Q_OS_ANDROID
    const QString permission = "android.permission.READ_EXTERNAL_STORAGE";
    if (QtAndroid::androidSdkVersion() > 22
        && QtAndroid::checkPermission(permission) != QtAndroid::PermissionResult::Granted) {
        const QPointer<QObject> guard(context);
        QtAndroid::requestPermissions({permission}, [guard, ready](const QtAndroid::PermissionResultMap &) {
            QMetaObject::invokeMethod(guard, [guard, ready] { if (guard) ready(); }, Qt::QueuedConnection);
        });
        return;
    }
#else
    Q_UNUSED(context)
#endif
    ready();
}

bool requestPlaybackFocus()
{
#ifdef Q_OS_ANDROID
    const bool granted = QAndroidJniObject::callStaticMethod<jboolean>(serviceClass, "requestFocus",
        "(Landroid/content/Context;)Z", QtAndroid::androidContext().object());
    return androidCallSucceeded() && granted;
#else
    return true;
#endif
}

void releasePlaybackFocus()
{
#ifdef Q_OS_ANDROID
    QAndroidJniObject::callStaticMethod<void>(serviceClass, "releaseFocus", "()V");
    if (!androidCallSucceeded())
        qWarning("Android audio focus could not be released.");
#endif
}

void attachPlaybackPlatform(PlayerController *player)
{
#ifdef Q_OS_ANDROID
    {
        QMutexLocker lock(&playerMutex);
        platformPlayer = player;
    }
    QAndroidJniObject::callStaticMethod<void>(serviceClass, "setNativeReady", "(Z)V", jboolean(true));
    if (!androidCallSucceeded()) {
        platformFailed = true;
        player->platformError("Android playback service initialization failed.");
        return;
    }
    auto *timer = new QTimer(player);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, player, [player] { publishSession(player); });
    const auto schedule = [timer] { timer->start(0); };
    QObject::connect(player, &PlayerController::statusChanged, timer, schedule);
    QObject::connect(player, &PlayerController::trackChanged, timer, schedule);
    QObject::connect(player, &PlayerController::positionChanged, timer, [player, schedule] {
        if (positionJumped(player))
            schedule();
    });
#else
    Q_UNUSED(player)
#endif
}

void detachPlaybackPlatform()
{
#ifdef Q_OS_ANDROID
    QAndroidJniObject::callStaticMethod<void>(serviceClass, "shutdown",
        "(Landroid/content/Context;)V", QtAndroid::androidContext().object());
    if (!androidCallSucceeded())
        qWarning("Android playback service shutdown failed.");
    QMutexLocker lock(&playerMutex);
    platformPlayer.clear();
#endif
}

void backgroundPlaybackApp()
{
#ifdef Q_OS_ANDROID
    QtAndroid::androidActivity().callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true));
    if (!androidCallSucceeded())
        qWarning("Cannot move playback to the background.");
#endif
}

void keepScreenOn(bool on)
{
#ifdef Q_OS_ANDROID
    QtAndroid::runOnAndroidThread([on] {
        const jint keepScreenOnFlag = 0x80;  // WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        QAndroidJniObject window = QtAndroid::androidActivity().callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (window.isValid())
            window.callMethod<void>(on ? "addFlags" : "clearFlags", "(I)V", keepScreenOnFlag);
        if (!androidCallSucceeded())
            qWarning("Cannot change the keep-screen-on flag.");
    });
#else
    Q_UNUSED(on)
#endif
}

void applySoundEffects(const QByteArray &settings)
{
#ifdef Q_OS_ANDROID
    QAndroidJniEnvironment environment;
    const jbyteArray bytes = environment->NewByteArray(settings.size());
    environment->SetByteArrayRegion(bytes, 0, settings.size(), reinterpret_cast<const jbyte *>(settings.constData()));
    QAndroidJniObject::callStaticMethod<jboolean>("org/walkmanui/SoundEffects", "apply", "([B)Z", bytes);
    environment->DeleteLocalRef(bytes);
    if (!androidCallSucceeded())
        qWarning("Cannot apply the sound effects.");
#else
    Q_UNUSED(settings)
#endif
}

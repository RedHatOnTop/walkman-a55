// Host harness: a small QML app with a ViewModel-like context object and its
// QML in compiled-in resources, with the overlay and the probe attached at the
// points the device plugin attaches them (platformInit, first window).
// Exercised by hosttest.py; this checks their logic, not the device
// integration.

#include <QtCore/QObject>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickView>

#include "overlay.h"
#include "probe.h"

class FakeViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QVariantMap layouts READ layouts CONSTANT)

public:
    explicit FakeViewModel(QObject *parent = nullptr) : QObject(parent) { setObjectName("playerVM"); }
    int volume() const { return m_volume; }
    void setVolume(int v) { if (v != m_volume) { m_volume = v; emit volumeChanged(); } }
    QString title() const { return "Track One"; }
    QVariantMap layouts() const
    {
        QVariantMap nested;
        nested["k"] = QVariantList() << 1 << 2;
        QVariantMap m;
        m["MusicPlayer"] = "qrc:/sid_0201_player/MusicPlayerDefault.qml";
        m["nested"] = nested;
        return m;
    }

    Q_INVOKABLE int addVolume(int delta) { setVolume(m_volume + delta); return m_volume; }
    Q_INVOKABLE void reset() { setVolume(0); }
    Q_INVOKABLE QString describe(const QString &prefix, bool loud) const
    {
        return QString("%1:%2:%3").arg(prefix).arg(m_volume).arg(loud ? "loud" : "quiet");
    }

signals:
    void volumeChanged();

private:
    int m_volume = 10;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    overlayStart();
    probeStart();

    FakeViewModel vm(&app);
    QQuickView view;
    view.rootContext()->setContextProperty("playerVM", &vm);
    view.setSource(QUrl("qrc:/test.qml"));
    // Under qemu (run-arm.sh) there is no OpenGL to show it with.
    if (!qEnvironmentVariableIsSet("HOSTTEST_NOSHOW"))
        view.show();
    return app.exec();
}

#include "main.moc"

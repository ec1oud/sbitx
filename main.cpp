#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QWindow>

#include "gpio.h"

int main(int argc, char *argv[])
{
    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));
    encodersInit();

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(url);
    setEncoderWindow(qobject_cast<QWindow *>(engine.rootObjects().first()));

    return app.exec();
}

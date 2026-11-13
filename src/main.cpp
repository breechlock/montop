// vitals — see what the system is doing: CPU, memory, disks, network, and processes.

#include <QGuiApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "cli.h"
#include "monitor.h"
#include "sparkline.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    const QByteArray first = argc > 1 ? QByteArray(argv[1]) : QByteArray();
    if (first == "--version" || first == "version") {
        QTextStream(stdout) << "vitals 0.2.0\n";
        return 0;
    }

    // Bare vitals prints help, as a command line tool should; the launcher
    // entry says vitals open. Commands need no display, so they run without
    // a GUI application at all.
    if (first != "open") {
        QCoreApplication app(argc, argv);
        app.setApplicationName("vitals");
        app.setApplicationVersion("0.2.0");
        const int code = runCli(app.arguments());
        return isCliCommand(first) || first.isEmpty() ? code : 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("vitals");
    app.setApplicationVersion("0.2.0");

    // Matches the window to vitals.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("vitals");
    app.setWindowIcon(QIcon::fromTheme("vitals"));

    QQuickStyle::setStyle("Material");
    qmlRegisterType<Sparkline>("Vitals", 1, 0, "Sparkline");
    qmlRegisterUncreatableType<ProcessModel>("Vitals", 1, 0, "ProcessModel", "Owned by the monitor");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    // Numbers line up in columns only in a monospaced face, and Omarchy's
    // monospace font is the desktop's voice in every terminal and the bar.
    QFont font(theme.font());
    font.setPixelSize(13);
    app.setFont(font);

    Monitor monitor;
    monitor.start();

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("monitor", &monitor);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}

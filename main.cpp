#include <QGuiApplication>
#include <QQmlApplicationEngine>

// Number of live objects retained as GC ballast; set by the BALLAST_COUNT build
// option (see CMakeLists.txt). Larger values lengthen each incremental mark phase.
#ifndef BALLAST_COUNT
#define BALLAST_COUNT 200000
#endif

// Hosts Main.qml and nothing more. The crash is driven by real multi-touch on
// the window (a MacBook trackpad or a touchscreen) — see README.md for the
// recipe. Uses only public Qt modules.
int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);

  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.setInitialProperties({{QStringLiteral("ballastCount"), BALLAST_COUNT}});
  engine.load(QUrl(QStringLiteral("qrc:/QtBugApp/Main.qml")));
  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}

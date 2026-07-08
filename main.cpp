#include <QGuiApplication>
#include <QQmlApplicationEngine>

// Hosts Main.qml and nothing more. The crash is driven by real multi-touch on
// the window (a MacBook trackpad or a touchscreen) — see README.md for the
// recipe. Uses only public Qt modules.
int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);

  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.load(QUrl(QStringLiteral("qrc:/QtBugApp/Main.qml")));
  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}

#include <QDebug>
#include <QGuiApplication>
#include <QList>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTimer>

#include <qpa/qwindowsysteminterface.h>

// Injects synthetic multi-touch straight into the window's event path, exactly
// as a real touchscreen would, so the MultiPointTouchArea handlers churn the
// `property var` map with no human input.
//
// This models a real hand-slap faithfully, which matters for the crash:
//   * Every new finger gets a fresh, monotonically increasing id (like a real
//     touchscreen), so each press inserts a brand-new key into the map. The
//     crash is in QV4::Object::insertMember growing the object's member table,
//     so an ever-growing set of keys is what keeps that realloc path hot. A
//     small reused pool of ids would build the table once and never grow it.
//   * Each frame mixes Pressed / Updated / Released points, with fingers
//   landing
//     and lifting at staggered times, so the onUpdated array-grow path (the
//     heaviest allocator) fires alongside the inserts and deletes.
class TouchInjector : public QObject {
public:
  explicit TouchInjector(QWindow *window) : m_window(window) {
    m_device = new QPointingDevice(
        QStringLiteral("synthetic touchscreen"), 1,
        QInputDevice::DeviceType::TouchScreen,
        QPointingDevice::PointerType::Finger,
        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
        16, 0, QString(), QPointingDeviceUniqueId(), this);
    QWindowSystemInterface::registerInputDevice(m_device);

    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &TouchInjector::tick);
    m_timer.start();
  }

private:
  struct Finger {
    int id;
    qreal nx, ny;  // normalised position
    int movesLeft; // frames of movement before this finger lifts
  };

  static constexpr int kMaxFingers = 12;

  QWindowSystemInterface::TouchPoint makePoint(const Finger &f,
                                               const QSize &size,
                                               QEventPoint::State state) const {
    const qreal x = f.nx * size.width();
    const qreal y = f.ny * size.height();
    const QPointF global = m_window->mapToGlobal(QPointF(x, y));

    QWindowSystemInterface::TouchPoint tp;
    tp.id = f.id;
    tp.area = QRectF(global - QPointF(1, 1), QSizeF(2, 2));
    tp.normalPosition = QPointF(f.nx, f.ny);
    tp.pressure = (state == QEventPoint::State::Released) ? 0.0 : 1.0;
    tp.state = state;
    return tp;
  }

  void tick() {
    const QSize size = m_window->size();
    if (size.width() < 60 || size.height() < 60)
      return; // window not laid out yet

    auto *rng = QRandomGenerator::global();
    QList<QWindowSystemInterface::TouchPoint> points;

    // Advance existing fingers: each either moves (jittered) or lifts.
    QList<Finger> survivors;
    survivors.reserve(m_fingers.size());
    for (Finger f : m_fingers) {
      if (--f.movesLeft <= 0) {
        points.append(makePoint(f, size, QEventPoint::State::Released));
        ++m_slaps; // count a completed press/move/release cycle
      } else {
        f.nx = qBound(0.02, f.nx + (rng->generateDouble() - 0.5) * 0.05, 0.98);
        f.ny = qBound(0.02, f.ny + (rng->generateDouble() - 0.5) * 0.05, 0.98);
        points.append(makePoint(f, size, QEventPoint::State::Updated));
        survivors.append(f);
      }
    }
    m_fingers = survivors;

    // Land a fresh burst of new fingers, each with a brand-new id.
    const int room = kMaxFingers - int(m_fingers.size());
    const int newCount = room > 0 ? 1 + rng->bounded(room) : 0;
    for (int i = 0; i < newCount; ++i) {
      Finger f;
      f.id = m_nextId++;
      f.nx = 0.02 + rng->generateDouble() * 0.96;
      f.ny = 0.02 + rng->generateDouble() * 0.96;
      f.movesLeft = 1 + rng->bounded(3);
      m_fingers.append(f);
      points.append(makePoint(f, size, QEventPoint::State::Pressed));
    }

    if (points.isEmpty())
      return;

    QWindowSystemInterface::handleTouchEvent(m_window, m_device, points);
    QWindowSystemInterface::flushWindowSystemEvents();

    if (m_slaps / 1000 != m_lastReported) {
      m_lastReported = m_slaps / 1000;
      qInfo() << "survived" << m_slaps << "slaps";
    }
  }

  QWindow *m_window = nullptr;
  QPointingDevice *m_device = nullptr;
  QTimer m_timer;
  QList<Finger> m_fingers;
  int m_nextId = 1;
  quint64 m_slaps = 0;
  quint64 m_lastReported = 0;
};

int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);

  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.load(QUrl(QStringLiteral("qrc:/QtBugApp/Main.qml")));
  if (engine.rootObjects().isEmpty())
    return -1;

  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  if (!window) {
    qWarning() << "root object is not a window";
    return -1;
  }

  // MANUAL=1 disables synthetic injection so a human can drive real multi-touch
  // (e.g. a MacBook trackpad) without the injector's events fighting theirs.
  const bool manual = qEnvironmentVariableIsSet("MANUAL");
  TouchInjector *injector = manual ? nullptr : new TouchInjector(window);
  if (manual)
    qInfo() << "MANUAL mode: injector disabled — drive multi-touch by hand.";
  Q_UNUSED(injector);

  return app.exec();
}

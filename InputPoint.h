#pragma once

#include <QObject>
#include <QVector2D>
#include <QtQml>

// A minimal QML value type, present only to reproduce the allocation churn of
// the original report (each touch point produced a fresh value-type wrapper).
// Storing these into JS arrays held by a `property var` map is what the
// reproducer exercises.
struct InputPoint {
  int index = -1;
  qreal x = 0;
  qreal y = 0;
  QVector2D velocity = QVector2D();

  Q_GADGET
  QML_VALUE_TYPE(inputpoint)

public:
  Q_PROPERTY(int index MEMBER index FINAL)
  Q_PROPERTY(qreal x MEMBER x FINAL)
  Q_PROPERTY(qreal y MEMBER y FINAL)
  Q_PROPERTY(QVector2D velocity MEMBER velocity FINAL)
};

class InputPointFactory : public QObject {
  Q_OBJECT
  QML_ELEMENT
public:
  Q_INVOKABLE InputPoint make(int index, qreal x, qreal y) {
    return InputPoint{index, x, y, QVector2D()};
  }
};

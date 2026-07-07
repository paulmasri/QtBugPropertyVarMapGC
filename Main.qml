import QtQuick
import QtBugApp

// Minimal reproducer for a crash in QV4::Object::insertMember (EXC_BAD_ACCESS) under
// rapid multi-touch. A per-touch-point buffer is kept in a `property var` map and
// mutated in place from the MultiPointTouchArea signal handlers: a new key is inserted
// on finger-down, an array grown on move, the key deleted on finger-up. Every finger
// gets a fresh id (see the C++ injector), so the map inserts an ever-growing set of new
// keys — which keeps QV4::Object::insertMember growing the object's member table.
//
// Touches are injected automatically from C++ (see main.cpp) so no human or touchscreen
// is needed. A large, live "ballast" heap is retained so each incremental GC mark phase
// spans many touch events — reproducing the window in which a real app's big scene graph
// lets an insert land mid-mark and be collected out from under itself.
Window {
    id: win
    visible: true
    width: 1000
    height: 700
    title: "property var map GC crash"
    color: "#2c3e50"

    InputPointFactory { id: factory }

    // Retained live heap so the incremental collector's mark phase takes many steps.
    property var ballast

    Component.onCompleted: {
        let a = new Array(200000)
        for (let i = 0; i < a.length; ++i)
            a[i] = { i: i, x: i * 0.5, tag: "n" + (i & 1023), kids: [i, i + 1] }
        ballast = a
        console.log("ballast built:", a.length, "objects")
    }

    MultiPointTouchArea {
        id: touchArea
        anchors.fill: parent
        mouseEnabled: false
        minimumTouchPoints: 1

        onPressed:
            (points) => {
                for (var p in points)
                    my.insert(points[p].pointId, points[p].x, points[p].y)
            }

        onUpdated:
            (points) => {
                for (var p in points)
                    my.grow(points[p].pointId, points[p].x, points[p].y)
            }

        onReleased:
            (points) => {
                for (var p in points)
                    my.remove(points[p].pointId)
            }

        onCanceled:
            (points) => {
                for (var p in points)
                    my.remove(points[p].pointId)
            }
    }

    QtObject {
        id: my
        property var buffers: ({})   // the property-var map under test

        function insert(id: int, x: real, y: real): void {
            const key = "t-" + id
            buffers[key] = [ factory.make(id, x, y) ]   // new key -> QV4::Object::insertMember
        }

        function grow(id: int, x: real, y: real): void {
            const key = "t-" + id
            if (key in buffers)
                buffers[key].push(factory.make(id, x, y))
        }

        function remove(id: int): void {
            const key = "t-" + id
            if (key in buffers)
                delete buffers[key]
        }
    }

    Text {
        anchors.centerIn: parent
        color: "white"
        font.pixelSize: 20
        text: "Injecting synthetic multi-touch — see Application Output"
    }
}

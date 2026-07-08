# Crash in QV4::Object::insertMember under rapid multi-touch (GC frees a live property-var map's member data)

## Summary

A Qt Quick application crashes with `EXC_BAD_ACCESS` inside
`QV4::Object::insertMember` when many new keys are inserted into a JavaScript
object held by a QML `property var` during rapid multi-touch input. The fault is
a write through a null/freed `memberData` pointer: the collector appears to free
the backing store of a JS object that is still live and being mutated.

The same fault has been observed in three separate crashes, reached through two
completely different event-delivery paths (a touch handler and a hover handler) —
which points at the object's storage being collected rather than at anything in
the input handling. One of the three is **the minimal reproducer in this
repository, which crashes with the identical fault** under physical multi-touch
on the default collector.

## Environment

- Reproduced on macOS (ARM64), by trackpad multi-touch, across a wide range of Qt
  versions — **6.5.12, 6.8.6, 6.8.8, and 6.11.1** — usually crashing within 45
  seconds. The span of affected versions, still current in 6.11.1, indicates a
  long-standing issue rather than a recent regression.
- Also seen in production on iOS 26 (touchscreen), but this minimal project does
  not reproduce on an iPhone (see *Reproducer status*).
- Debug and release builds.
- Builds against **public Qt modules only** (Core, Gui, Qml, Quick).

## What happens

A `property var` holds a plain JS object used as a map (touch-point id → data).
On each finger-down a **new key** is inserted; on move the value array is grown;
on finger-up the key is deleted. Under a burst of many distinct touch ids the
map inserts new members continuously.

The crash is always the same write, reached through the QML `[]=` store:

```
QV4::Heap::Object::setProperty(...)                      // <-- EXC_BAD_ACCESS, addr 0x38
QV4::Object::setProperty(index, v) const
QV4::Object::insertMember(s, p, attributes)              qv4object.cpp:266
QV4::Object::insertMember(s, v, attributes)
QV4::Object::internalPut(id, value, receiver)            qv4object.cpp:561
QV4::Object::internalPut(...)
QV4::setElementFallback(engine, object, index, value)
QV4::Runtime::StoreElement::call(...)
QV4::Moth::VME::interpret(...)                            // obj[key] = value in QML
```

`insertMember` grows the object's internal class to make room for the new member
and then writes the value into `memberData` at the new index. The access faults
at a tiny address (`0x38` in this reproducer; `0x28` and `0x10` in the two
production traces), i.e. a write into a **null/near-null `memberData`**. The
`Object` itself is valid (a normal heap pointer); only its member backing store
is gone.

## Three crashes, one fault

The identical `insertMember` → `setProperty` write into freed member data has
been captured three times, including in the minimal reproducer in this
repository, and through unrelated event paths — the strongest evidence that the cause is the
object's storage being collected, not the input path. In every case the fault
address is a small offset into a null base (`0x38`, `0x28`, `0x10`), i.e. a write
into null `memberData`.

**1. This reproducer (macOS 26.5.1, Qt 6.8.6, default GC, trackpad)** — the
store happens inside the reproducer's `MultiPointTouchArea` `onPressed` handler:

```
QV4::Heap::Object::setProperty(...)                      // crash, addr 0x38
QV4::Object::setProperty(index, v) const
QV4::Object::insertMember(...)
QV4::Object::internalPut(...)
QV4::setElementFallback(...)
QV4::Runtime::StoreElement::call(...)                    // obj[key] = value
...
QQmlBoundSignal_callback(...)
QQuickMultiPointTouchArea::pressed(...)
QQuickMultiPointTouchArea::updateTouchData(...)
QQuickMultiPointTouchArea::touchEvent(...)
...
-[QNSView(Touch) touchesEndedWithEvent:]                 // MacBook trackpad
```

**2. Production app, touch path (macOS)** — same handler, same fault
(`insertMember`, addr `0x28`), also via `-[QNSView touchesBeganWithEvent:]`.

**3. Production app, hover path (iOS 26)** — the store happens instead inside a
`MouseArea` `onPositionChanged` handler delivered during frame-synchronous event
flushing (`insertMember`, addr `0x10`):

```
QV4::Object::insertMember(...)                           // crash
...
QQuickMouseArea::hoverMoveEvent(QHoverEvent*)
QQuickDeliveryAgentPrivate::deliverHoverEventToItem(...)
QQuickDeliveryAgentPrivate::flushFrameSynchronousEvents(QQuickWindow*)
QSGThreadedRenderLoop::polishAndSync(...)
```

The touch and hover paths share nothing above `insertMember`, yet bottom out in
the same write into freed member data.

## Likely cause

The JS object is reachable the whole time (it is the value of a live
`property var`), yet its `memberData` is freed while an insert is in progress.
This looks like a missing or ineffective write barrier during **incremental**
garbage collection: mutating the object in place (`obj[key] = value`,
`arr.push(...)`, `delete obj[key]`) between incremental mark steps does not keep
the freshly-referenced backing store marked, so it is swept, and the next
`insertMember` writes through the stale pointer.

Consistent with this:

- It only appears under sustained allocation churn (each touch frame allocates
  new value wrappers and arrays), which is what drives the collector.
- It would explain why the bug is timing-sensitive and rare: the insert has to
  land in the window between the object being marked and the sweep, which only a
  mark phase in progress at that instant provides.

## Reproducer

This repository is a minimal, self-contained Qt Quick project with no
application dependencies:

- `Main.qml` — a full-window `MultiPointTouchArea` whose `onPressed` /
  `onUpdated` / `onReleased` handlers insert / grow / delete entries in a
  `property var` map, exactly as above. Every finger uses a fresh id so the map
  keeps inserting new members (keeping `insertMember`'s table-growth path hot). A
  large retained "ballast" array is allocated at startup so each incremental mark
  phase spans many input events, approximating a real app's larger heap.
- `main.cpp` — just hosts the QML; **you drive the multi-touch by hand**. Uses
  only public Qt modules.

### Build and run

```
cmake -B build -DCMAKE_PREFIX_PATH=<path-to-Qt>
cmake --build build
./build/QtBugPropertyVarMapGC
```

Then **drum many fingers on the trackpad, over the window, repeatedly** — the
more fingers landing and lifting together the better. A recipe that reliably
crashes, usually **within 45 seconds**, on a MacBook trackpad:

- Cup one hand over the other so **~8 fingertips** reach the centre of the
  trackpad at once.
- Either tap all eight down/up together about **twice a second**, or just drum
  the fingers continuously. Both work.

(No trackpad? Any real multi-touch device works; a touchscreen was the original
trigger.)

The retained heap size is a CMake option, **`BALLAST_COUNT`** (default `200000`) —
the number of live objects held as GC ballast. Raise it where the default is too
small to keep the collector busy enough to reproduce on a given device:

```
cmake -B build -DBALLAST_COUNT=1000000 -DCMAKE_PREFIX_PATH=<path-to-Qt>
```

### Use the default collector

Run it with the **default** garbage collector — no GC environment variables.
That is how the included crash was produced.

Importantly, forcing **non-incremental** collection *hides* the bug, so do not
reach for these while trying to reproduce:

- `QV4_MM_AGGRESSIVE_GC=1` collects on (nearly) every allocation.
- `QV4_GC_TIMELIMIT=0` runs each collection to completion.

Both make GC non-incremental, and under either we could not reproduce the crash
at all — consistent with the cause being a missing barrier in the *incremental*
collector, which these settings bypass. This is a likely reason earlier
reproduction attempts came up empty: aggressive/synchronous GC is a natural first
thing to try, and it is exactly what masks this bug.

## Reproducer status

- **Confirmed and repeatable:** this project crashes with the exact fault above
  under **physical** multi-touch (a MacBook trackpad), on the **default**
  collector, with no environment variables set — just build, run, and drum fingers
  on the window. It usually crashes within **45 seconds** using the ~8-finger
  recipe above. The crash report is included
  (`CrashLog-Qt6_8_6-macOS26-trackpad.ips`).
- Physical multi-touch is the reliable trigger, matching the two production
  crashes (an iOS touchscreen and a macOS trackpad). Earlier, without the ballast
  heap, this was **rare** (on a device it took thousands of "slaps", and a previous
  reproduction attempt did not trigger it in ~5000); the retained ballast — which
  lengthens each incremental mark phase — is what makes it reliable.
- **iOS not reproduced by this project:** on an iPhone (Qt 6.8.8) it did not crash
  even after ~2 minutes of drumming with `BALLAST_COUNT` raised to 1,000,000 —
  despite the identical `insertMember` fault crashing the production app on iOS.
  So on iOS this minimal project is not a reliable reproducer; the production crash
  logs remain the evidence there. (The macOS reproducer and the iOS production
  crash hit the same fault, so it is the same bug.)

## What we think is worth checking

Whether an in-place mutation of a JS object held by a `property var`
(`obj[key] = value` that triggers `insertMember` → member-data growth) correctly
fires the write barrier during an in-progress incremental GC. Three crashes —
including a self-contained reproducer — hitting the identical `insertMember` →
`setProperty` write into freed `memberData`, via unrelated event paths, strongly
suggest the object's backing store is being collected while the object is still
live and being written to.

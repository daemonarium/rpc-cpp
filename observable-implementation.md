# Observable library — implementation (core slice)

Status: core slice. This document records implementation decisions
for `cpp-rpc/src/observable/` and the test suite. Requirements live
in `observable-requirements.md`.

## Decisions log

| Decision | Choice | Rejected | Why |
|---|---|---|---|
| Source hooks | Virtual `onStart` / `onStop` / `onPause` / `onResume` on `ObservableBase` | `std::function` hooks | Faster (vtable), easier to debug (overrides in stack traces). `RemoteObservable` will inherit `Observable` later. |
| Recovery | No `onRecover` hook | Separate recover API | Recovery is retransmit: `onReset` (`error → reloading`) or `onItem` (`error → loading`). |
| Consumer (external) | C++ concept, five wire-mirror signals + `sinceTimestamp()` | Virtual consumer base | Zero-overhead for app consumers; compile-time check. |
| Change | `{key, value, ts}`; empty value = tombstone | `ChangeKind` enum | Matches `stream_item` on the wire. |
| Materialized view | `std::map<binary, Entry>` | `unordered_map` | Sorted-by-binary-key is required; `lower_bound` for a later `FilteredObservable`. |
| Backpressure | Async wait on `onChange` promise; bounded FIFO; pause when full | Drop-oldest-N | Dropping breaks per-key consistency. No synchronous blocking. |
| ring_buffer | Own template on `std::vector` + head/tail/size; `push` returns false when full | Boost, `std::deque`, `io/circular_buffer.h` | No Boost runtime (WASM). Existing circular buffer is for serialized I/O bytes. |
| Retransmit | Clear-and-rebuild: fan out `onReset`, rebuild `data_`, no synthesized tombstones | Reconciliation loop inserting tombstones at `ts=maxTs` | Synthesized tombstones poison per-key monotonicity. |
| Replay watermark | Library-tracked per `ConsumerOutput`; seed from `sinceTimestamp()`; reset to 0 on reset | Read consumer `lastTs` at replay | Consumer `lastTs` grows during reloading and would skip the fresh full state. |
| Buffer on overflow | Mark paused after a successful fill; source must honor `onPause` | Overwrite / drop | Consistency. Pause when **all** outputs are paused (`onPause`); resume when any unpauses (`onResume`). |
| State machine | `idle`, `loading`, `ready`, `reloading`, `error`, `closed` | Terminal `closed` on last unsubscribe | Last unsubscribe → `idle` (view retained). `closed` is `kill()` / destruction only. |
| Error after last unsubscribe | State → `idle`; exception kept in `lastError` | Drop the exception | Diagnostics after `error → idle`. Re-subscribe starts fresh. |

## Architecture

```
  wire (stream_item / stream_done / stream_reset / stream_error)
          │
          ▼
  RemoteObservable  (later slice; inherits Observable)
          │  onItem / onDone / onReset / onError
          ▼
  Observable  (this slice)  ──► ConsumerOutput ──► Consumer
          │                         │
          │                         │ onChange / onReady / onReset
          │                         │ onError / onClosed
          ▼                         ▼
     (operators, later)        relay consumer ──► wire ──► next process
```

Signal symmetry: wire messages, Observable source API, and Consumer
callbacks are the same five events. A relay consumer can forward
verbatim.

This slice has no wire and no operators. Tests use `ManualSource`
(an `Observable` subclass that records hook calls and exposes
`push` / `done` / `reset` / `fail` / `recover`).

## Class responsibilities

**`ring_buffer<T>`** — bounded FIFO. `push` does not overwrite.
Used as the per-consumer queue of `Change`s waiting while
`onChange` is in-flight.

**`ConsumerOutput`** — one downstream subscription. Holds a
`weak_ptr<void>` to the consumer (GC), type-erased dispatch for
the five signals, `replayWatermark_`, in-flight promise, pause
flag. `emit` implements async backpressure.

**`ObservableBase`** — virtual destructor and source hooks
(`onStart`, `onStop`, `onPause`, `onResume`). Internal callbacks
from `ConsumerOutput`: `removeOutput`, `onConsumerPaused`,
`onConsumerResumed`.

**`Observable<EntryT>`** — materialized view, state machine,
refcount, fan-out, subscribe/replay, `readyPromise`, `lastError`.
Public source API: `onItem`, `onDone`, `onReset`, `onError`.

**`ManualSource`** (tests only) — overrides hooks; `recover()` is
`onReset` then fresh items + `onDone` (same path `RemoteObservable`
will use).

## Source hook contract

| Hook | When | Typical override (later RemoteObservable) |
|---|---|---|
| `onStart` | refcount 0 → 1 | `start_scan` with `since = maxTs` |
| `onStop` | refcount → 0 | `cancel_scan` |
| `onPause` | all consumers paused | stop reading the socket |
| `onResume` | at least one consumer unpaused | resume reading |

No `onRecover`. Data flow after error is `onItem` or `onReset`.

## Consumer signal contract

| Consumer | Wire |
|---|---|
| `onChange` (async `promise<void>`) | `stream_item` |
| `onReady` | `stream_done` |
| `onReset` | `stream_reset` |
| `onError` | `stream_error` |
| `onClosed` | terminal (`kill`) |

`sinceTimestamp()` seeds `replayWatermark` at subscribe only.

## GC model

`ConsumerOutput` holds `weak_ptr<void>` to the consumer. On `emit`,
if `lock()` fails, the output detaches and the Observable decrements
refcount (last one → `idle` + `onStop`). C++ has no
`FinalizationRegistry`; expiry is noticed on the next emit (or
explicit sweep). Matches the TS `WeakRef` pattern lazily.

## State machine

| From | Event | To | Actions |
|---|---|---|---|
| idle | subscribe (0→1) | loading | `onStart`, replay |
| loading | `onDone` | ready | resolve `readyPromise`, `onReady` |
| ready | `onReset` | reloading | `oldData = move(data)`, `data = {}`, fan out `onReset`, new pending `readyPromise` |
| error | `onReset` | reloading | same (recovery = retransmit) |
| loading | `onReset` | reloading | scan restarted |
| reloading | `onDone` | ready | drop `oldData`, resolve `readyPromise`, `onReady` |
| any active | `onError` | error | retain view, store `lastError`, `onError` |
| error | `onItem` | loading | merge + fan-out (delta resume) |
| any active | last unsubscribe | idle | `onStop`, view retained, `lastError` kept |
| any | `kill()` | closed | `onClosed` all, `onStop`, terminal |

Reads during `reloading` use `oldData`. Writes (`onItem`) go to
`data_`. Tombstones erase from `data_` and fan out empty value;
they are not stored.

## Test matrix

See `src/test/observable_ut.cpp`. Groups: state machine (1–9),
refcount (10–13), replay (14–16), merge/tombstone (17–20), GC (21),
ring_buffer (22–23), async backpressure (24–27), retransmit (28–29),
readyPromise (30), error→idle + `lastError` (31), recovery via
retransmit (32).

## Out of scope (later slices)

`FilteredObservable`, `MappedObservable`, `RemoteObservable`,
`RemoteRegistry` dedup, `DeserializedObservable`.

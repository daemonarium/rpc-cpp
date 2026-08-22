# Observable library — implementation (core slice)

Status: wire-bridge slice. This document records implementation
decisions for `cpp-rpc/src/observable/` and the test suite.
Requirements live in `observable-requirements.md`.

## Decisions log

| Decision | Choice | Rejected | Why |
|---|---|---|---|
| Source hooks | Virtual `onStart` / `onStop` / `onPause` / `onResume` on `ObservableBase` | `std::function` hooks | Faster (vtable), easier to debug (overrides in stack traces). `RemoteObservable` inherits `Observable`. |
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
| Muxer roles | `StreamMultiplexer<Starter, Channel>`: Starter=`call`, Channel=`on_message`/`send_message` | Hardcoded `kDbStartScan`/`kDbCancelScan`; kernel-shield client | Platform-agnostic. Same rpc_client = one-connection; distinct instances = split. |
| Cancel | `stream_cancel{stream_id}` message | `cancel_scan` RPC (fn 6) | Producer stream layer handles by id; fire-and-forget; no-op if unknown. |
| Socket ack vs stream handshake | Empty ack renamed `socket_opened`; per-stream `stream_opened{id}` appended last | Keep empty `stream_opened` as both bind ack and stream handshake | Type-index stability. Items must not precede `stream_opened{id}`. `start_scan` still does not emit per-scan opened. |
| Producer backpressure | `try_queue_message`; pending `onChange` until drain | Blocking send; drop on full | Observable already serializes the consumer and pauses when its ring fills. |
| Cancel before attach | `pendingCancels`; `attach` is a no-op | Ignore early cancel | Handshake race: client may cancel before the producer map insert. |
| Registry key | `(function_id, serialized args)` via `flat_bytes_writer` | Explicit string key; reflection of dataset identity | Same shape as `rpc_call_mixin::call`. |
| Registry ownership | Registry owns `RemoteObservable` across idle (no eviction in v1) | Destroy on last unsubscribe; weak_ptr | Resubscribe reuses `maxTs` / view → delta, not a full scan. |
| DB start | Unchanged `start_scan` / `ensure_channel_` reuse by `(pid, socket)` | Per-stream sockets; muxer-owned bind | One-connection = dedicated `open_socket` socket. |

## Architecture

```
  Observable  ──► StreamRelayConsumer ──► StreamPublisher ──► wire
          │         try_queue stream_item / done / reset / error
          │         pending onChange until onWritable
          ▼
  StreamPublisher.attach(id, obs, since)
          │  pendingCancels → no-op
          │  try_queue stream_opened{id} then subscribe
          ▼
  wire (stream_opened / stream_item / stream_done / stream_reset /
        stream_error / stream_cancel)
          │
          ▼
  StreamMultiplexer  ──► RemoteObservable ──► Observable ──► Consumer
```

Client stack (wire → Observable):

```
  RemoteRegistry  (key = function_id + serialized args)
          │  owns RemoteObservable across idle
          ▼
  RemoteObservable<Starter, Channel, Args...>
          │  onStart: mux.createStream(fn, maxTs, this, args...)
          │  onStop:  mux.cancelStream → stream_cancel
          ▼
  StreamMultiplexer<Starter, Channel>
          │  Starter.call<void>(fn, handle, args...)
          │  Channel.on_message<stream_*>  (not socket_opened)
          │  Channel.send_message<stream_cancel>
          ▼
  wire (stream_opened / stream_item / stream_done / stream_reset /
        stream_error / stream_cancel)
          │
          ▼
  Observable  ──► ConsumerOutput ──► Consumer
```

Signal symmetry: wire messages, Observable source API, and Consumer
callbacks are the same five events. A relay consumer can forward
verbatim. Cancel is a sixth message, consumer → producer only.

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
uses).

**`StreamMultiplexer<Starter, Channel>`** — demuxes `stream_*` by
`stream_id` (`stream_opened` included; `socket_opened` ignored).
`createStream(function_id, since, receiver, args...)`
builds `stream_handle` from muxer-owned return pid/socket and
calls `starter.call<void>`. `cancelStream` always sends
`stream_cancel`; if the id is not yet opened, opened later
triggers a second cancel.

**`StreamRelayConsumer<Channel>`** — one `stream_id`. `try_queue`
`stream_item` / control messages. Full socket → pending
`onChange`. Control never overtakes a blocked item; `onReset`
drops it.

**`StreamPublisher<Channel>`** — one Channel / socket.
`attach` / `detach` / `onCancel` / `onWritable`. Opened-before-
subscribe; `pendingCancels` for cancel-before-attach. Not used
by `start_scan`.

**`RemoteObservable<Starter, Channel, Args...>`** — wire →
`Observable<BinaryEntry>` bridge. Stores `function_id` +
`tuple<Args...>`. `onStart` / `onStop` go through the muxer.

**`RemoteRegistry<Starter, Channel>`** — `getOrCreate` keyed by
`(function_id, serialized args)`. Owns the observable across idle
so resumption is a delta.

## Source hook contract

| Hook | When | Typical override (`RemoteObservable`) |
|---|---|---|
| `onStart` | refcount 0 → 1 | `mux.createStream(function_id, maxTs, this, args...)` |
| `onStop` | refcount → 0 | `mux.cancelStream` (`stream_cancel`) |
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

`stream_multiplexer_ut`: sequential ids, demux, `stream_opened`
by id, cancel drops unknown ids, cancel-before-opened resend,
`stream_cancel` (not RPC), two-role vs one-client.

`stream_publisher_ut`: relay mapping, opened-before-items, since
replay, try_queue backpressure / pause-resume, done waits for
blocked item, reset-while-blocked, two ids one Observable,
cancel after/before attach, duplicate attach, detach/cancel
no-op, publisher dtor.

`remote_observable_ut`: subscribe/`onStart`, wire signals, last
unsubscribe cancel, resubscribe `since = maxTs`, loopback
start + `stream_*` + `stream_cancel`, registry resumption.

`remote_registry_ut`: key equality, dedup, alive-across-idle,
delta resume.

`db-native-observable-test`: native E2E (`RemoteRegistry` →
`RemoteObservable` → `StreamMultiplexer` → `db_native_process`).

## Out of scope (later slices)

`FilteredObservable`, `MappedObservable`, `DeserializedObservable`.
Registry GC (TTL / weak-ref eviction). One-connection DB refactor
if a dedicated `open_socket` socket is ever insufficient.
Export registry (server catalog of attachable Observables).
DB scan rewrite onto `StreamPublisher` (`start_scan` stays).

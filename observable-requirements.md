# Observable library — requirements

Status: draft v0.1. Target: a small FRP-style library for sorted
`key -> value` maps with per-entry timestamps, designed for one-way
`server -> client` synchronization over the existing flat RPC / stream
protocol. This document captures the contract and design constraints
agreed during the `observer_experiment` review. It is intentionally
verbose so that gaps and inconsistencies surface during reading.

## 1. Goal and scope

Build a small library that gives readable, composable abstractions for
**observing sorted `key -> value` datasets** with per-entry timestamps.
The mental model is "FRP on sorted maps with timestamps". The library
is the abstraction layer for manipulating observations; it is **not**
the DB scan API (`start_scan` stays as the concrete source for the DB
case).

In scope for v1:

- Core `Observable` operating on `binary` keys and `binary` values.
- A terminal `DeserializedObservable` helper that deserializes
  keys/values via templates.
- A `RemoteObservable` class that bridges an `Observable` to the wire
  (flat RPC + stream protocol).
- Operator node classes (`FilteredObservable`, `MappedObservable`, ...)
  that compose into trees.
- Lifecycle: lazy construction, cold subscribe with ts-based replay,
  refcounted upstream, GC-aware teardown.
- State machine with recoverable errors.
- Backpressure.
- Single-threaded execution model.

Out of scope for v1 (section 16):

- Upstream writes (sending observation changes back to the server).
- Server-side graph execution.
- `TableObservable` and other source specializations beyond
  `RemoteObservable`.
- Registry GC (TTL / weak-ref eviction of idle `RemoteObservable`s).
- Multi-threaded execution.

## 2. Core concepts

### Observable

An `Observable` is a live, materialized, sorted view of a dataset. It
holds a sorted map `binary_key -> (binary_value, uint64 ts)` and exposes:

- `subscribe(consumer)` / `unsubscribe(consumer)` — lifecycle.
- `getEntries()` — snapshot of the current materialized view.
- `get(key)` — point lookup (binary search, not linear).
- `readyPromise` — resolves when the view becomes `ready` (section 5).
- `maxTs()` — the highest timestamp seen so far (the watermark).

An `Observable` is **not** a query. It is an already-observed dataset.
Operators on an `Observable` produce new `Observable`s that are views
of their parents (section 9).

### Change

The unit of data flow. At the core (binary) level:

```
Change {
  key:   binary      // sorted by this, lexicographic
  value: binary      // empty == tombstone (delete)
  ts:    uint64       // per-entry version; per-key monotonic
}
```

- Empty `value` (length 0) means tombstone (delete). Unambiguous at
  the binary level and matches `stream_item` on the wire.
- `ts` is per-entry. Per-key, `ts` is monotonic (last-ts-wins).
  Cross-key, **no** ordering guarantee; `ts` is not globally monotonic.
- `maxTs` (the Observable watermark) is the max `ts` across all
  entries seen. Conservative watermark for reconnect.

### Consumer

```
Consumer {
  onChange(change: Change): Promise<void>   // stream_item; async; await = processed
  onReady(): void                            // stream_done (initial scan / reload complete)
  onReset(): void                            // stream_reset; clear/swap, retransmit incoming
  onError(error: Error): void               // stream_error; recoverable (section 11)
  onClosed(): void                           // terminal
  sinceTimestamp(): uint64                   // initial replay watermark at subscribe
}
```

The consumer interface mirrors the wire signal set (`stream_item`,
`stream_done`, `stream_reset`, `stream_error`) so a relay consumer
can forward each signal downstream verbatim (wire -> Observable
tree -> consumer -> wire -> next process). Without `onReset`,
retransmit cannot propagate through a relay chain.

`onChange` is async. The library awaits it before sending the next
change to that consumer (per-consumer serialization). This is the
backpressure seam (section 8).

`sinceTimestamp()` is the **initial** seed for the library's
per-output `replayWatermark` at subscribe (section 6). The library
owns the running watermark after that; the consumer may track its
own `lastTs` independently.

### Source

A `Source` feeds changes into an `Observable`. Concrete sources:

- `RemoteObservable` — receives `stream_item` / `stream_done` /
  `stream_reset` / `stream_error` from the wire and translates them to
  `Change` events (section 12).
- (Future) `TableObservable` — a `RemoteObservable` bound to a
  specific DB table, where `range(...)` may issue a new `start_scan`
  instead of a client-side filter.

The library does not assume a particular source. Anything that can
produce `Change` events and drive the state machine can be a source.

## 3. Data model

### Binary at the core

The core `Observable` operates on `binary` keys and `binary` values.
No deserialization happens in the core, the operators, or the wire
bridge. This keeps the hot path cheap and avoids coupling the library
to any specific value type.

### Sorted by binary key

Entries are sorted lexicographically by the raw `binary` key. This
holds everywhere, including inside `DeserializedObservable`: even when
keys are deserialized for access, the internal storage and iteration
order is by the serialized form. This guarantees that a
`DeserializedObservable` and its binary parent have identical
iteration order and identical range semantics.

### Tombstone

At the core level, tombstone = empty `binary` value (length 0). No
`kind` field, no `optional` at this level. Matches `stream_item` on
the wire exactly (empty value = delete).

At the `DeserializedObservable` level, where `V` is a typed value,
"empty" is ambiguous, so the helper uses `optional<V>` (`nullopt` =
delete). The wire adapter maps `nullopt` to empty `binary` and back.

### Timestamps

`ts` is `uint64`. Per-key monotonic (a newer entry for the same key
has `ts` >= the previous). Cross-key, no monotonicity guarantee.
`maxTs` is the max `ts` observed across all entries and is used as
the reconnect watermark (section 7).

## 4. Lifecycle: lazy construction, refcounted upstream, GC

### Lazy

Constructing an `Observable`, applying operators, and wiring nodes
into a tree **must not start any observation**. The tree is built
without side effects. Hard requirement: the user must be able to
construct and compose freely without triggering I/O.

### Subscribe starts the chain

The chain starts only when a terminal consumer subscribes
(`observable.subscribe(consumer)`). Subscribe walks the tree
upstream incrementing refcounts; the source starts (e.g.
`RemoteObservable` issues the start RPC via the muxer) when its
refcount goes from 0 to 1.

### Unsubscribe stops the chain (to idle, not closed)

`unsubscribe(consumer)` decrements refcounts downstream-upstream.
When a source's refcount reaches 0, it stops (e.g. sends
`stream_cancel`). The Observable enters `idle`
state — **not** `closed`. `idle` is non-terminal: the view is
retained and re-subscribe is possible (section 5). `closed` is
reserved for explicit `kill()` / destruction (section 5).

### Multiple consumers share one view

An `Observable` has **one** materialized view shared by all
consumers (refcounted fan-out). A new consumer does not create a new
upstream subscription; it replays from the local view (section 6)
and then receives live changes from the shared upstream.

### GC

- **TypeScript**: `WeakRef<Consumer>` + `FinalizationRegistry` on
  the per-consumer output. When the consumer is GC'd, the output is
  disposed, which decrements the refcount. The last disposed output
  stops the upstream.
- **C++**: `shared_ptr<Consumer>` / `weak_ptr<Consumer>` mirror.
  The output holds a `weak_ptr` back to the consumer; on consumer
  destruction the output is removed and the refcount decremented.
  `shared_ptr<Observable>` keeps the view alive while any consumer
  holds it; `weak_ptr` back-references prevent cycles.

The GC mechanics differ per language but the pattern is identical:
consumer lifetime drives output lifetime drives upstream refcount.

## 5. State machine

An `Observable` is always in exactly one state:

```
                subscribe()  (since = maxTs; 0 if fresh)
   idle ─────────────────► loading
    ▲                        │
    │                        │ stream_done
    │                        ▼
    │                      ready ◄────────────┐
    │                        │                 │
    │                        │ stream_reset     │ stream_done
    │                        ▼                 │
    │                     reloading ───────────┘
    │                        │
    │                        │ stream_error
    │                        ▼
    │                      error
    │                        │
    │                        │ source reconnects: stream_reset
    │                        │   (recovery = retransmit)
    │                        │ or onItem (delta resume)
    │                        ▼
    │                     reloading / loading
    │
    │  last unsubscribe (refcount 0): stream_cancel, view retained
    └────────────────────────┘

   any state ──► closed   (explicit kill / destruction only)
```

States:

- `idle` — 0 consumers, upstream stopped (`stream_cancel` sent),
  view retained (may be empty if never loaded; `maxTs` may be 0).
  Non-terminal. Re-subscribe goes to `loading` with
  `since = maxTs` (delta if `maxTs > 0`, full scan if `maxTs == 0`).
  This replaces the earlier `uninitialized` state — `idle` with
  empty view is the fresh case; no need to distinguish "never
  subscribed" from "subscribed then unsubscribed".
- `loading` — ≥1 consumer, receiving initial scan (or delta
  resume from `idle`). View is being populated.
- `ready` — ≥1 consumer, initial scan complete (`stream_done`).
  View is coherent; subsequent changes are live updates.
  `readyPromise` resolves here.
- `reloading` — ≥1 consumer, was `ready`, got `stream_reset`
  (full retransmit, e.g. server buffer overflow). `oldData` is
  retained (section 10); reads serve from `oldData`; `data` is
  rebuilt in parallel.
- `error` — `stream_error` received. Recoverable (section 11).
  View is retained across error.
- `closed` — **explicit `kill()` or destruction/GC of the
  Observable itself**. Terminal. Last consumer unsubscribe does
  **not** enter `closed`; it enters `idle`. This is the fix: the
  earlier draft made `closed` terminal on last unsubscribe, which
  made re-subscribe impossible and contradicted the GC model
  (consumer GC'd → refcount 0 → `idle`, Observable still held by
  the app → re-subscribe should work).

Transitions:

- `idle → loading` on subscribe (`since = maxTs`; 0 if fresh).
- `loading → ready` on `stream_done`.
- `ready → reloading` on `stream_reset`.
- `error → reloading` on `stream_reset` (recovery = retransmit).
- `loading → reloading` on `stream_reset` (scan restarted).
- `reloading → ready` on `stream_done` (after atomic swap of
  `data` and `oldData`, section 10).
- `any → error` on `stream_error`.
- `error → loading` on `onItem` (delta resume; data flow
  resumes, error cleared). `error → reloading` on `stream_reset`
  (recovery is retransmit; same clear-and-rebuild as a ready
  retransmit). The exact trigger lives in `RemoteObservable`;
  the Observable only reacts to `onItem` / `onReset`.
- `loading/ready/reloading/error → idle` on **last unsubscribe**
  (refcount 0). Upstream stops (`stream_cancel`); view retained.
  `error → idle` on last unsubscribe means the error **state** is
  abandoned; the exception is kept in `lastError` for diagnostics.
  Re-subscribe from `idle` starts fresh with `since = maxTs`.
- `any → closed` on **explicit `kill()` / destruction** only.
  `kill()` forces `closed` even if consumers remain (it
  unsubscribes all consumers first, then marks terminal).

`ready` does **not** mean "no more changes". It means "initial scan
complete, now observing live". Further changes keep arriving while
in `ready`. `ready` can be lost (`stream_reset → reloading`). The
name `ready` (not "stable") avoids the implication of "static".

`idle` is the natural "parked" state: no consumers, no upstream,
view kept for cheap re-subscribe. The app may hold an Observable in
`idle` indefinitely; it only enters `closed` when the app drops it
(GC) or calls `kill()`.

## 6. Subscribe semantics: cold replay with ts optimization

### Cold, not hot

A new consumer must receive the **current state**, not just
"changes from now". Pure hot subscribe causes races (the consumer
misses the initial state and must fetch it separately, leading to
inconsistencies). This is a hard requirement and the reason
`since_timestamp` was introduced.

### Local replay

On `subscribe(consumer)`:

1. If the view is non-empty, replay the current materialized entries
   to the new consumer as a sequence of `Change` events (live entries
   only — empty value is a tombstone and is not stored in the view,
   so replay is sets only, each with that entry's `ts`).
2. Then deliver live changes from the shared upstream as they arrive.

Replay is **local** (no upstream re-query). The upstream stream is
shared by all consumers via refcount; a new consumer does not trigger
a new upstream subscription.

### Re-subscribe from idle

When the Observable is in `idle` (0 consumers, upstream stopped,
view retained) and a new consumer subscribes, the upstream must
restart. The restart uses `stream_handle.since_timestamp = maxTs`
(section 7) — a delta request that catches up cheaply if the
server can satisfy it. If `maxTs == 0` (never loaded), the restart
is a full scan. The Observable transitions `idle → loading`. The
new consumer gets the local replay (per ts-optimization below)
plus the upstream delta catch-up, then live changes.

This is the primary use case for `since_timestamp`: re-opening a
parked Observable without paying for a full rescan.

### ts-based replay optimization

Replay is ts-aware. The **library** tracks a per-output
`replayWatermark` (distinct from any `lastTs` the consumer keeps
for its own use). On subscribe it is initialized from
`consumer.sinceTimestamp()`. On each delivered change it becomes
`max(replayWatermark, change.ts)`. On `stream_reset` it is reset
to 0.

On (re)subscribe, replay only entries with `ts > replayWatermark`:

- New consumer: `sinceTimestamp() = 0` → `replayWatermark = 0` →
  full replay.
- Returning consumer (e.g. after a transient unsubscribe):
  `sinceTimestamp() = X` → replay only entries with `ts > X`,
  typically a small delta.

The consumer's own `lastTs` must **not** be used as the replay
filter. After `stream_reset`, the consumer receives retransmit
items via `onChange` during `reloading` and its `lastTs` grows;
if the library then filtered by `consumer.lastTs`, post-reload
replay would skip the fresh full state. `sinceTimestamp()` is
only the initial seed; the library owns the running watermark.

This makes replay cheap for returning consumers and uses
`since_timestamp` semantics locally. The shared view is
unaffected.

### readyPromise

`observable.readyPromise: Promise<void>` resolves when the
Observable enters `ready` (initial scan done). It re-resolves (new
promise) on each `ready` re-entry after `reloading`. Consumers that
only want a snapshot can `await observable.readyPromise` then read
`getEntries()` then `unsubscribe`. There is no separate `fetch` API
returning a bare `Promise` — `Observable` is the single abstraction,
and `readyPromise` is the bridge to promise-based code.

## 7. Timestamps and reconnect

### Watermark

`observable.maxTs()` is the highest `ts` across all entries seen. It
is the reconnect watermark.

### Upstream resume

When the upstream (transport) reconnects, `RemoteObservable`
resumes the stream with
`stream_handle.since_timestamp = maxTs`. This is a delta request:
"give me entries with `ts > maxTs`". If the server can satisfy it
(its elision floor and retention floor are below `maxTs`), the
client gets a small delta and catches up cheaply.

### Full rescan fallback

If the server cannot satisfy the delta (its
`max_elided_tombstone_ts` or `min_retention_ts` rose above `maxTs`),
it sends `stream_reset` and restarts as a full scan. The Observable
transitions `ready → reloading` (section 10). This is the
`since_timestamp` safety contract already implemented in the DB; the
Observable library consumes it.

### Per-key monotonicity, not global

`ts` is per-key monotonic. Cross-key, no ordering guarantee. `maxTs`
is a conservative watermark (max of all seen). Reconnect with
`since = maxTs` is correct because any key's newer entry has `ts` >
that key's previous `ts` <= `maxTs`.

## 8. Backpressure

Each consumer has a bounded buffer of pending `Change` events. The
library awaits `consumer.onChange(change)` before sending the next
change to that consumer; this serializes per-consumer and naturally
backpressures when the consumer is slow.

When a consumer's buffer is full, the library stops delivering to
that consumer (the `onChange` promise is pending). This must
propagate upstream: operators pause their parents when all their
downstream buffers are full, and ultimately the source pauses (e.g.
`RemoteObservable` stops reading from the socket, or applies the
existing `scan_writable_floor`).

When the consumer drains (its `onChange` resolves), delivery resumes
and the upstream unpauses. There is no unbounded buffering anywhere
in the tree.

The exact buffer size is configurable per consumer; the default is
small (single-flight). The `drain()` callback pattern from the
experiment is the reference shape.

## 9. Operators as separate node classes

Operators are **not** methods on `Observable` that internally filter.
They are separate classes, each a node in the tree.
`observable.range(...)` is sugar for constructing the corresponding
node. This keeps each operator's state and semantics isolated and
the tree readable.

### v1 operators

- `FilteredObservable(parent, lower?, upper?, lower_mode,
  upper_mode)` — filter by key range. `observable.range(lower,
  upper)` returns a `FilteredObservable`. Filtering applies to
  **both** the materialized data and the incoming changes (a change
  outside the range is dropped, not propagated). This is a pure
  client-side filter; it does not issue a new upstream query.
- `MappedObservable(parent, keyFn?, valueFn?)` — transform key
  and/or value. Rare in pure sync (usually identity), but useful for
  derived views. The output sort order remains by the **parent's**
  binary key (mapping the key does not reorder; if reordering is
  needed, it is a separate operator outside v1).

### Composition

Operators return `Observable`, so they compose:

```
obs
  .range(lower, upper)        // FilteredObservable
  .map(valueFn)               // MappedObservable
  .subscribe(consumer)        // starts the chain
```

Each node has its own refcount and propagates subscribe/unsubscribe
upstream. Each node has its own backpressure (pauses parent when its
downstream is full).

### Future operators (not v1)

`JoinObservable(a, b)`, `DiffObservable(a, b)`,
`MergeObservable(a, b)`, `LimitObservable(parent, n)`,
`ReverseObservable(parent)`. These can be built on top of two
materialized views in user code in v1; adding them to the library is
deferred until the need is concrete.

### TableObservable (future)

A `TableObservable` is a source specialization where `range(...)`
may return a new `RemoteObservable` for a specific table (i.e.
issue a new `start_scan` with bounds) instead of a client-side
`FilteredObservable`. This is out of scope for v1 but the operator
structure (separate classes, sugar on `Observable`) is designed to
allow it later without breaking the core.

## 10. Retain old on retransmit

When `stream_reset` arrives (full retransmit triggered by
server-side buffer overflow or elision-floor invalidation), the
Observable must **not** clear its local data immediately.
Requirement 9.

Behavior:

```
on stream_reset:
  oldData = std::move(data)
  data = {}                  // new, empty
  state = reloading
  fan out onReset() to all consumers   // consumers clear / swap
  reset each output replayWatermark to 0
  // reads during reloading serve from oldData (stale but coherent)

on stream_item (during reloading):
  data.insert({key, value, ts})   // live entries only
  fan out onChange to consumers   // they already cleared

on stream_done (after reloading):
  // NO reconciliation loop. data_ holds the full rescan.
  // Synthesizing tombstones with ts=maxTs would poison per-key
  // monotonicity (a later live set with ts between the real
  // delete and maxTs would be dropped).
  oldData = {}
  state = ready
  notify onReady / resolve readyPromise
```

During `reloading`, `get(k)` and `getEntries()` serve from
`oldData` (the previous coherent snapshot). After `stream_done`,
the new `data` is complete and atomically swaps in (oldData
dropped). This gives snapshot readers a consistent view
throughout the retransmit (no partial state visible) and
satisfies requirement 9.

Consumers follow the same clear-and-rebuild: `onReset` is the
clear/swap signal; subsequent `onChange` events are the new
dataset (sets only; empty value = tombstone as usual). Keys
absent from the rescan are gone because the consumer cleared,
not because the library synthesized deletes. A relay consumer
forwards `onReset` as `stream_reset` and `onChange` as
`stream_item`, so the same protocol continues downstream.

## 11. Error handling: recoverable

Errors are **recoverable**. A dead Observable locks the
frontend and consumer logic forever, which has hurt in production
repeatedly. So `error` is not terminal.

Behavior:

- On `stream_error`, the Observable enters `error`, retains its
  view, and notifies consumers via `onError(error)`.
- The recovery trigger lives in `RemoteObservable` (the source /
  transport layer), not in the core Observable. There is **no**
  `onRecover` hook. Recovery **is** retransmit: the source comes
  back and either sends `stream_reset` (`error → reloading`) or
  resumes with a delta (`onItem` → `error → loading`).
- When the source reconnects, it resumes with
  `stream_handle.since_timestamp = maxTs` (section 7). The
  Observable transitions `error → loading` on `onItem`, or
  `error → reloading` on `stream_reset`.
- Consumers see `onError` then either `onReset` + `onChange` +
  `onReady` (retransmit) or `onChange` + `onReady` (delta
  catch-up) — never a terminal signal.
- On **last unsubscribe** while in `error`: the Observable goes
  `error → idle` (not `closed`). The error **state** is abandoned;
  the exception is kept in `lastError` for diagnostics. The view
  is retained. Re-subscribe from `idle` starts fresh with
  `since = maxTs` (section 6). This avoids a sticky error
  state with no consumers.

`closed` (explicit `kill()` / destruction only) is the only
terminal state. Last unsubscribe never enters `closed`; it enters
`idle` (section 5).

## 12. RemoteObservable: wire bridge

`RemoteObservable` is the class that connects a core `Observable`
to the wire (flat RPC + stream protocol). It does not hardcode DB
function ids. Start is `function_id` + args (the same shape as
`rpc_call_mixin::call`). Cancel is a `stream_cancel{stream_id}`
message, not an RPC.

`StreamMultiplexer<Starter, Channel>` is the platform-agnostic
demux: `Starter` issues `call<void>(function_id, handle, args...)`;
`Channel` receives `stream_*` and sends `stream_cancel`. No
kernel-shield dependency. An `rpc_client` can fill both roles
(one-connection). Distinct instances = split start vs stream
sockets. One-connection is a dedicated socket the app opens with
`open_socket`; `start_scan` / `ensure_channel_` already reuse by
`(pid, socket)`, so several streams share that socket.

The muxer owns the return channel (`pid` / `socket` on
`stream_handle`). `RemoteObservable` stores `function_id` and a
`tuple<Args...>`.

Responsibilities:

- On first subscribe (from `idle`): `createStream(function_id,
  since = maxTs, this, args...)`. For DB this is `start_scan`
  (fn 5) with `db_scan_query`.
- On `stream_item`: translate to `Change { key, value, ts }` and
  feed the Observable.
- On `stream_done`: Observable `loading → ready` (or
  `reloading → ready`).
- On `stream_reset`: Observable `ready → reloading` (section 10).
- On `stream_error`: Observable `→ error` (section 11).
- On last unsubscribe: send `stream_cancel`. Observable `→ idle`
  (view retained, upstream stopped). This is **not** `closed`;
  re-subscribe is possible (section 5).
- On transport reconnect (while `idle`): no-op — upstream is
  already stopped; nothing to resume. Re-subscribe will start a
  fresh stream with `since = maxTs`.
- On transport reconnect (while active `loading`/`ready`/
  `reloading`): resume with
  `stream_handle.since_timestamp = maxTs` (section 7).
- On transport reconnect after `error`: drive recovery as
  retransmit (`stream_reset` → `error → reloading`) or as a
  delta (`onItem` → `error → loading`).

The core `Observable` and the operator nodes never see wire types.
This keeps the library testable without a socket and portable
across transports.

## 13. DeserializedObservable: terminal helper

`DeserializedObservable<K, V>` is a terminal helper that wraps a
core `Observable` and deserializes keys and values on access. It is
the only place where deserialization happens.

- Storage and iteration order remain by the **serialized** binary
  key (section 3). Deserialization is for access only.
- The deserialization method is chosen via templates (e.g.
  `DeserializedObservable<K, V>` with `K` and `V` providing
  `deserialize(reader) -> K/V`).
- Tombstone: at this level, `optional<V>` (`nullopt` = delete).
  The adapter maps core empty `binary` to `nullopt` and back.
- `getEntries()` returns `Array<{ key: K, value: V | none, ts }>`.

`DeserializedObservable` is a consumer of the core Observable
(subscribes to it). It is itself an `Observable`-like (consumers
can subscribe to it) but it is terminal — operators are not applied
on the deserialized side in v1 (apply operators on the binary
parent, then deserialize at the end).

## 14. Dedup of remote observables

When multiple `Observable`s over the **same** remote dataset are
created, they must share one upstream stream (one start RPC, one
wire stream). This is an essential optimization: without it, N
views of the same table produce N scans.

**Mechanism:** `RemoteRegistry` keyed by `ObservationKey
{function_id, serialized args}`. Args are serialized with
`flat_bytes_writer` (deterministic; same bytes as a tuple on the
wire). `getOrCreate` returns a `shared_ptr<RemoteObservable>` that
the registry **owns** for the connection lifetime, so the object
survives idle (0 consumers). Resubscribe reuses the same
observable and therefore `maxTs` / the retained view — the start
RPC is a delta (`since = maxTs`), not a full scan.

One muxer per connection. v1 does not evict idle entries (TTL /
weak-ref GC is a follow-up).

This dedup lives in the **source / `RemoteObservable` layer**,
not in the core `Observable`. The core library is agnostic.

## 15. Threading model

- **Single-threaded**. Exactly one thread operates on the
  Observable tree.
- **TypeScript / WASM**: the JS event loop / the single WASM
  thread. No synchronization needed.
- **C++ native**: the I/O thread (the same thread that runs
  `handle_data` / `handle_drain`). Store worker threads only post
  completions to the I/O thread; they never touch the Observable
  tree directly.
- Consumer callbacks run on this single thread. Consumers must
  not block; heavy work must be offloaded to a queue.
- No locks in the Observable tree. If a C++ host ever needs to
  touch the tree from another thread, it must post to the I/O
  thread first.

This is a hard requirement: the library is single-threaded by
design. Multi-threaded execution is out of scope (section 16).

## 16. Out of scope / future

- **Upstream writes** (sending observation changes back to the
  server). Solvable in the future by re-observing different ranges,
  consistent with the wire protocol, but complicates the logic
  and may never be needed in practice. Out of v1.
- **Server-side graph execution** (the server runs the operator
  tree). v1 is client-side composition only.
- **`TableObservable`** and other source specializations where
  `range(...)` issues a new upstream query (section 9).
- **Registry GC** (TTL / weak-ref eviction of idle
  `RemoteObservable`s). v1 keeps every entry for the connection
  lifetime (section 14).
- **One-connection DB refactor** if a dedicated `open_socket`
  socket is ever insufficient (`ensure_channel_` already reuses
  by `(pid, socket)`).
- **Multi-threaded execution** (section 15).
- **Reordering operators** (`sortBy`, `groupBy`). v1 keeps the
  parent's binary key order.
- **Join / diff / merge / limit / reverse** as library operators
  (section 9). Buildable in user code in v1.
- **Replay with ts filtering beyond the per-output `replayWatermark`**
  (e.g. windowed replay, snapshot isolation). v1 uses the
  library-tracked `replayWatermark` only.
- **Explicit `retry()` API on Observable**. v1 puts retry in
  `RemoteObservable`.

## 17. Open questions / to specify later

- **Recovery trigger mechanism** in `RemoteObservable`
  (auto-reconnect timer vs transport-level reconnect vs explicit
  `retry()`). Section 11.
- **Default per-consumer buffer size** for backpressure. Section 8.
- **Whether `MappedObservable` may change the key** (reorder) or
  is restricted to value-only maps in v1. Section 9 currently
  restricts to value-only to preserve sort order.
- **Whether `readyPromise` returns a fresh promise on each
  `ready` re-entry or reuses a resolved one**. Section 6 says
  fresh; confirm.
- **Storage layout for the materialized view** in C++ (sorted
  array vs `std::map` vs skip-list from `db/`). The choice
  affects `get` and iteration performance; v1 can start with
  `std::map<binary, Entry>` and refine later.

## 18. Summary checklist

The v1 library must satisfy:

1. Timestamps on every entry; `maxTs` watermark; `since=maxTs`
   on reconnect. (req 1, 7)
2. Only sorted `key -> value`; sort by binary key everywhere. (req 2, 3)
3. One-way `server -> client`; no upstream writes in v1. (req 3, 16)
4. C++ readability with templates; reflection only for wire types
   (`Change` is a `[[=serialization::message()]]`). (req 4, 7)
5. Lazy construction; subscribe starts the chain; unsubscribe
   stops it. (req 5, 4)
6. Async `onChange`; per-consumer serialization. (req 6, 8)
7. GC: `WeakRef`/`FinalizationRegistry` (TS), `shared/weak_ptr`
   (C++). (req 7)
8. `ready` state + `readyPromise`; `ready` ≠ "no more changes". (req 8)
9. Retain `oldData` on `stream_reset`; clear-and-rebuild; fan out
   `onReset` to consumers. (req 9)
10. Operators as separate node classes; `range` returns
    `FilteredObservable`. (req 9, 10)
11. Recoverable errors; `closed` (explicit `kill()`/destruction)
    is the only terminal state; last unsubscribe → `idle`
    (re-subscribable). (req 11)
12. Backpressure: bounded per-consumer buffer; propagates upstream. (req 8)
13. Single-threaded; I/O thread in C++. (req 15)
14. Core on `binary`; `DeserializedObservable` is terminal helper. (req 2, 7)
15. `RemoteObservable` + `StreamMultiplexer` + `RemoteRegistry`
    are the wire-aware source layer. (req 7, 12, 14)
16. Dedup of same-remote observables in the source layer. (req 14)

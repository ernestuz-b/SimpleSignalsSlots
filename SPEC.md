# SimpleSignalsSlots — Specification

## 1. Scope

SimpleSignalsSlots is a small C++20 signal/slot facility intended for engine, UI, embedded-style, and application code where synchronous multicast callbacks are useful but raw callback lifetime management is undesirable.

The first version provides:

- typed multicast signals;
- ordinary member functions as slots;
- compact connection syntax;
- ordered synchronous emission;
- automatic disconnection when either endpoint dies;
- intentional duplicate connections;
- exact signal/slot membership queries;
- reverse lookup from a slot to its connected signals;
- an optional signal spy/logger with source locations;
- safe connection removal during emission.

It does **not** provide thread-safe emission, queued delivery, RPC, serialization, ownership of connected objects, reflection, or an event system.

## 2. Vocabulary

### Object

An `sss::Object` is a lifetime-tracked participant in signal/slot connections. It owns a signal manager and a coarse set of currently connected peer objects.

Objects are neither copyable nor movable. Connections do not own objects.

### Signal

`sss::Signal<Args...>` is an ordered multicast source. A signal belongs to one `sss::Object` and has a developer-supplied debug name.

Example:

```cpp
sss::Signal<int, Damage> hit{*this, "hit"};
```

A signal has no result type. Connected slot return values, if any, are ignored.

### Slot

A slot is an ordinary member function. `sss::slot(object, method)` creates a small `SlotRef` containing the target object, member-function pointer, and the source location at which the `SlotRef` was created.

```cpp
actor.hit += sss::slot(enemy, &Enemy::damage);
```

The compiler checks that the selected member function can be invoked using the signal arguments.

### Connection

Each `+=` creates one independent connection. Two connections may point from the same signal to the same slot.

When diagnostics are enabled, the spy assigns an internal numeric ID to each connection so duplicate edges can be distinguished in traces. That ID is diagnostic-only and is not part of the core connection API or stored in the stripped build.

Detailed dynamic connection records are stored only by the signal. Static connections are constexpr descriptors owned by the type's `static_connections` declaration. Reverse lifetime tracking for dynamic wiring is object-granular, not connection-granular.

## 3. Static connections

Stable object wiring may be declared as compile-time structure:

```cpp
struct Actor : sss::Object {
    Enemy enemy;
    UI ui;
    sss::Signal<int, Damage> hit{*this, "hit"};

    inline static constexpr auto static_connections = sss::static_connections(
        sss::connect(&Actor::hit, &Actor::enemy, &Enemy::damage),
        sss::connect(&Actor::hit, &Actor::ui,    &UI::show_damage)
    );
};
```

`sss::connect()` creates a constexpr descriptor containing the source signal member, target object member, and target member function. `sss::static_connections()` preserves descriptor order and provides compile-time counting:

```cpp
static_assert(Actor::static_connections.count<&Actor::hit>() == 2);
```

Version 1 requires the source signal and target object to be direct members of the same owner type. This makes a static edge a structural lifetime relationship rather than a runtime subscription.

Static edges are immutable. They are not inserted into the dynamic connection container and cannot be removed by `-=`, `remove_one()`, `remove_all()`, or `clear()`. Those operations affect dynamic edges only. Queries such as `connected()`, `connection_count()`, and `size()` include both static and dynamic edges; `dynamic_size()` reports only dynamic edges.

For each emission, matching static descriptors are invoked in declaration order before any dynamic connection. Dynamic connections then run in their existing insertion order. The dynamic connection-count boundary is captured before static dispatch begins, so a dynamic listener added by a static slot waits until the next outer emission.

When diagnostics are compiled out, static descriptors add no per-instance connection storage and a static-only object performs no library heap allocation. Signal registration is intrusive; dynamic containers remain empty until dynamic wiring is used.

## 4. Public connection operations

Given:

```cpp
auto endpoint = sss::slot(enemy, &Enemy::damage);
```

these operations are defined:

```cpp
signal += endpoint;                 // add one connection
signal.add(endpoint);               // named equivalent of +=
signal.add_unique(endpoint);        // add only when no identical connection exists

signal -= endpoint;                 // remove one: most recently added matching connection
signal.remove_one(endpoint);        // same semantic, returns whether one was removed
signal.remove_all(endpoint);        // remove every exact match, returns count
signal.clear();                     // remove every connection from this signal

signal.connected(endpoint);         // exact signal/slot membership
signal.connection_count(endpoint);  // exact multiplicity
signal.has_duplicate(endpoint);     // connection_count > 1
signal.size();                      // all active connections
```

A slot identity is the pair `(target object, member-function value)`. Member-function pointer equality is used while the original concrete member-pointer type is preserved internally. No RTTI is required for slot identity.

## 5. Object-level relationship graph

For dynamic lifetime safety, every `sss::Object` maintains a list of dynamic peer objects. There is one peer entry per object pair, with a reference count equal to the number of live dynamic signal connections between the pair in either direction. Static member-to-member edges require no peer entry because both endpoints share the containing object's structural lifetime.

Example:

```text
A.hit   -> B.damage
A.seen  -> B.notice
A.done  -> B.wake
```

The detailed edges exist in A's signals. At object level, A and B each need only one peer record whose reference count is three.

This intentionally avoids a second detailed connection graph on the slot side.

Self-connections do not require a peer record because signal and target have the same lifetime owner. They remain fully queryable through the owning object's signals.

## 6. Automatic disconnection

A signal destructor removes all of its outgoing connections.

An object destructor asks every connected peer's signal manager to remove every connection targeting the dying object. Consequently, after destruction completes, no live connection may point at the destroyed object.

`Object::disconnect_all()` performs the same two-sided cleanup while the object remains alive and can be used when an object is logically retired before destruction.

### Destruction/re-entrancy boundary

The library is single-threaded. Under ordinary destruction there is no concurrent emitter, so cleanup is automatic.

C++ provides no generic hook at the *start* of an arbitrary most-derived destructor. If a destructor itself performs re-entrant signal emissions that could target the object currently being torn down, it should call `disconnect_all()` before performing those emissions. This is the exceptional case; normal destruction requires no manual disconnect.

## 7. Emission

A signal is emitted by calling it:

```cpp
actor.hit(10, Damage::Fire);
```

Slots are invoked synchronously and sequentially. Static connections run first in `static_connections(...)` declaration order. Dynamic connections follow in `+=` insertion order.

The current outer emission captures the dynamic connection-count boundary before static dispatch starts. Therefore:

- a dynamic slot connected during either static or dynamic dispatch is not invoked by that outer emission;
- a slot disconnected before its turn is skipped;
- a slot may disconnect itself safely;
- nested emission takes a new boundary and therefore sees the topology that exists when the nested emission begins.

Inactive records are compacted when the outermost active emission finishes.

If a slot throws, the exception propagates. Emission-depth and connection-compaction bookkeeping are restored before propagation.

By-value signal argument types must be copy-constructible because one emitted value can be delivered to multiple slots. Lvalue-reference parameters are supported. Rvalue-reference signal parameters are intentionally rejected.

## 8. Inspector / spy

`sss::Inspector` is optional. Only one inspector is globally active in the current single-threaded version.

```cpp
sss::Inspector spy;
spy.activate();
spy.set_enabled(false); // runtime pause
spy.set_enabled(true);
spy.deactivate();
```

Destroying an active inspector automatically deactivates it.

Tracing and inspection can be compiled out with `SIMPLE_SIGNALS_DISABLE_TRACE`. This is a hard compile-time cut, not merely a runtime disable: the compiler selects the stripped core implementation and does not include or store `Inspector`, trace records, debug names, source locations, signal/object/connection diagnostic IDs, or their ancillary standard-library machinery. The `Object("name")` and `Signal{*this, "name"}` spellings remain accepted so application source does not need conditional compilation; those labels are ignored in the stripped build.

Lifetime safety does not depend on diagnostic metadata. The coarse peer-object graph, signal-owned connection records, exact slot matching, duplicate counts, and automatic destruction cleanup remain present because they are core semantics.

### Trace kinds

The inspector records:

- `CONNECT` — a signal acquired a slot;
- `DISCONNECT` — a concrete connection was removed;
- `EMIT` — a signal was fired;
- `INVOKE` — one connected slot was about to be called.

A trace record contains signal ID/name, source-object ID/name, target-object ID/name where applicable, connection kind, dynamic connection ID or static declaration index where applicable, the action source location, the connection/declaration source location, duplicate multiplicity on dynamic connect, and a disconnection reason. Static edges do not emit runtime CONNECT records because no runtime connection operation occurs; they appear in topology queries and produce STATIC INVOKE records when fired.

Disconnection reasons distinguish explicit removal, target destruction, signal destruction, and `Object::disconnect_all()`.

### Source locations

`std::source_location` is captured without macros.

Emission source location is the line where `signal(...)` is written.

Connection source location is the line where `slot(...)` is created. In the normal idiom:

```cpp
actor.hit += sss::slot(enemy, &Enemy::damage);
```

this is also the connection line.

If a `SlotRef` is stored and connected later, the inspector intentionally reports the earlier `slot(...)` creation line. This is a documented edge case.

C++ source locations expose the caller function name but do not expose the runtime `this` pointer of an unrelated object whose method contains the connection statement. The signal owner and slot target are known exactly.

## 9. Topology queries

The signal itself answers exact membership and multiplicity queries.

The inspector can return the peer objects connected to an object:

```cpp
spy.connected_objects(object);
```

It can also answer the reverse question, "which signals are connected to this exact slot?":

```cpp
const auto uses = spy.signals_for(sss::slot(enemy, &Enemy::damage));
```

Each result identifies the source object, signal, and exact static, dynamic, and combined connection counts for that slot.

Dynamic reverse lookup uses the coarse peer graph. With diagnostics enabled, static structure is also discoverable through the inspector's diagnostic object registry so compile-time edges appear alongside dynamic ones. The registry itself is intrusive and is absent from the stripped build.

## 10. Threading and future Message transport

Version 1 is deliberately single-threaded. There are no locks and no hidden scheduling.

Cross-thread delivery and networking are deferred. The intended future seam is to package a signal invocation into an owned, by-value `Message` and hand that message to a thread relay or RPC transport. That future mechanism must not change the meaning of local direct signals.

## 11. Required implementation invariants

1. A live connection never survives completed destruction of either endpoint.
2. Dynamic connection state has one runtime owner: the signal; static connection state is constexpr type structure.
3. Dynamic object reverse state is coarse: peer object plus reference count, not duplicated edge records.
4. Static connections are immutable and dispatch in declaration order before dynamic connections.
5. `+=` always adds exactly one dynamic connection.
6. `-=` removes at most one dynamic connection, choosing the most recently added exact match.
7. Duplicate connections are legal and observable across the combined static + dynamic topology.
8. Dynamic connection order determines the dynamic portion of synchronous invocation order.
9. Removal during emission is immediately respected by later dynamic slots in that emission.
10. Dynamic additions during an outer emission wait for the next outer emission, even when added by a static slot.
11. Inspection must never be required for correct signal behaviour.
12. A stripped static-only object must not require library heap allocation.

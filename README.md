# SimpleSignalsSlots

A small C++20 signal/slot library built around three ideas:

1. **The call site should stay obvious.**
2. **Destroying an object must not leave dangling signal connections.**
3. **The wiring should be inspectable instead of becoming invisible callback state.**

The normal syntax is deliberately small:

```cpp
struct Enemy : sss::Object {
    Enemy() : Object("enemy") {}

    void damage(int amount, Damage kind) {
        // ...
    }
};

struct Actor : sss::Object {
    Actor() : Object("actor") {}

    sss::Signal<int, Damage> hit{*this, "hit"};
};

Actor actor;
Enemy enemy;

actor.hit += sss::slot(enemy, &Enemy::damage);
actor.hit(10, Damage::Fire);
```

`Signal<Args...>` has no return type: a signal is multicast and returns nothing. Slots remain ordinary member functions. `slot(object, &Type::method)` is only the small binding expression C++ still requires for a bound member function.

## Static topology

Stable wiring can be declared as part of the object's type rather than executed as constructor bookkeeping:

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

static_assert(Actor::static_connections.count<&Actor::hit>() == 2);
```

These are compile-time connection descriptors, not runtime connection records. The compiler knows the number and declaration order of static edges. Emission invokes them directly in the order written, then invokes any runtime `+=` connections in their insertion order.

Static wiring is immutable. Runtime `-=`, `remove_one()`, and `remove_all()` operate only on dynamic connections, while `connected()`, `connection_count()`, and `size()` report the combined static + dynamic topology. `dynamic_size()` reports only the runtime tail.

In version 1, the signal and target of a static connection are direct members of the same owning object. This is intentional: static wiring describes stable object structure. Connections to objects discovered at runtime remain ordinary dynamic `+=` connections.

With `SIMPLE_SIGNALS_DISABLE_TRACE`, a static-only object performs no library heap allocation: the static edges exist only as constexpr descriptors, signal registration uses an intrusive list, and the dynamic containers remain empty until a dynamic connection is actually added.

## Connections and lifetime

Connections are synchronous and ordered. `+=` always adds one connection, including an intentional duplicate. `-=` removes the most recently added matching connection.

```cpp
actor.hit += sss::slot(enemy, &Enemy::damage);
actor.hit += sss::slot(enemy, &Enemy::damage); // legal: called twice

assert(actor.hit.connection_count(sss::slot(enemy, &Enemy::damage)) == 2);
assert(actor.hit.has_duplicate(sss::slot(enemy, &Enemy::damage)));

actor.hit -= sss::slot(enemy, &Enemy::damage); // one remains
actor.hit.remove_all(sss::slot(enemy, &Enemy::damage));
```

Every participating object derives from `sss::Object`. Detailed **dynamic** connections live only at the signal owner. Objects maintain only a coarse list of other objects to which they are dynamically connected, with a reference count per object pair. That object-level graph is used for automatic destruction cleanup. Static edges need no runtime lifetime bookkeeping because source and target are members of the same owning object.

Destroying either side therefore removes live connections automatically. Signal connections do **not** impose ownership and do not require `shared_ptr`/`weak_ptr`.

## Inspector / spy

`sss::Inspector` observes the same wiring used by the runtime. It can be enabled or disabled without changing signal behaviour.

```cpp
sss::Inspector spy;
spy.activate();

actor.hit += sss::slot(enemy, &Enemy::damage);
actor.hit(10, Damage::Fire);

spy.dump(std::cout);
```

Typical output:

```text
CONNECT actor.hit -> enemy [connection 1] @ game.cpp:41 in void wire_game()
EMIT actor.hit @ combat.cpp:87 in void Sword::strike()
INVOKE actor.hit -> enemy [connection 1] @ combat.cpp:87 in void Sword::strike() [connected @ game.cpp:41]
```

The inspector records `CONNECT`, `DISCONNECT`, `EMIT`, and `INVOKE`. `std::source_location` supplies file, line, column, and function without macros.

It can also query the live topology. In particular, a slot can ask which signals point to it:

```cpp
const auto matches = spy.signals_for(sss::slot(enemy, &Enemy::damage));

for (const auto& match : matches) {
    // match.source
    // match.signal_name
    // match.count  -- includes duplicate connections
}
```

`connected_objects(object)` exposes the coarse object graph, while each signal exposes `connections()`, `connected(slot)`, `connection_count(slot)`, and `has_duplicate(slot)`.

### Source-location edge case

The location attached to a connection is the location where the `SlotRef` is created. In the intended one-line form these are the same place:

```cpp
actor.hit += sss::slot(enemy, &Enemy::damage);
```

If a slot reference is deliberately created earlier and connected later:

```cpp
auto damage = sss::slot(enemy, &Enemy::damage); // recorded location
actor.hit += damage;                            // not recorded as the connect location
```

The inspector reports the first line. This is documented rather than hidden behind macros.

## Emission semantics

Emission is synchronous and sequential: static connections first in declaration order, then dynamic connections in `+=` order. Disconnecting a slot during an emission prevents it from running later in that same emission. A connection added during an emission is first considered by the next outer emission. A nested emission sees the topology as it exists when the nested emission begins.

Exceptions thrown by a slot propagate to the emitter; internal emission bookkeeping is still restored.

## Threading

This first version is intentionally **not thread-safe**. Connections, disconnections, destruction, inspection, and emission belong to one execution context.

The earlier design that motivated this library also used per-thread relays and later packaged value arguments as RPC messages. That work is deliberately deferred here. A future transport layer can package an invocation as a by-value `Message` without changing the local signal/slot vocabulary.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The library itself is header-only: include `include/simple_signals_slots.hpp` and compile as C++20 or newer.

Define `SIMPLE_SIGNALS_DISABLE_TRACE` for a zero-diagnostics build. In that configuration the public signal/slot syntax and lifetime behaviour are unchanged, but the compiler takes a stripped implementation path: `Inspector`, trace records, debug names, source locations, diagnostic IDs, and their associated headers/storage are absent. The optional string literals in `Object("name")` and `Signal{*this, "name"}` are accepted for source compatibility and ignored.

The stripped configuration is built as a separate test target so it cannot silently rot. Exact signal operations such as `connected()`, `connection_count()`, duplicate handling, emission, and automatic destruction cleanup remain part of the core and are still available. A dedicated allocation-counting test also verifies that stripped static-only wiring performs no library heap allocation.

See [`SPEC.md`](SPEC.md) for the precise behavioural contract.

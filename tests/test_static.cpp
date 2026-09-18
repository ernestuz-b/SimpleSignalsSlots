#include "simple_signals_slots.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

using namespace sss;

struct Enemy : Object {
    using Object::Object;
    std::vector<int>* order = nullptr;
    void damage(int) { if (order) order->push_back(1); }
};

struct UI : Object {
    using Object::Object;
    std::vector<int>* order = nullptr;
    void show(int) { if (order) order->push_back(2); }
};

struct Mod : Object {
    using Object::Object;
    std::vector<int>* order = nullptr;
    void react(int) { if (order) order->push_back(3); }
};

struct Actor : Object {
    explicit Actor(std::vector<int>& order)
        : Object("actor"), enemy("enemy"), ui("ui") {
        enemy.order = &order;
        ui.order = &order;
    }

    Enemy enemy;
    UI ui;
    Signal<int> hit{*this, "hit"};

    inline static constexpr auto static_connections = sss::static_connections(
        connect(&Actor::hit, &Actor::enemy, &Enemy::damage),
        connect(&Actor::hit, &Actor::ui, &UI::show)
    );
};

static_assert(Actor::static_connections.count<&Actor::hit>() == 2);

static void static_then_dynamic_order() {
    std::vector<int> order;
    Actor actor(order);
    Mod mod("mod");
    mod.order = &order;

    actor.hit += slot(mod, &Mod::react);
    actor.hit(7);

    assert((order == std::vector<int>{1, 2, 3}));
    assert(actor.hit.size() == 3);
    assert(actor.hit.dynamic_size() == 1);
}

struct MultiTarget : Object {
    using Object::Object;
    int integer_total = 0;
    int zero_calls = 0;
    void integer(int value) { integer_total += value; }
    void zero() { ++zero_calls; }
};

struct MultiOwner : Object {
    MultiOwner() : Object("multi"), target("target") {}

    MultiTarget target;
    Signal<int> integer_signal{*this, "integer"};
    Signal<> zero_signal{*this, "zero"};

    inline static constexpr auto static_connections = sss::static_connections(
        connect(&MultiOwner::integer_signal, &MultiOwner::target, &MultiTarget::integer),
        connect(&MultiOwner::zero_signal, &MultiOwner::target, &MultiTarget::zero)
    );
};

static_assert(MultiOwner::static_connections.count<&MultiOwner::integer_signal>() == 1);
static_assert(MultiOwner::static_connections.count<&MultiOwner::zero_signal>() == 1);

static void differently_typed_static_signals_coexist() {
    MultiOwner owner;
    assert(owner.integer_signal.size() == 1);
    assert(owner.zero_signal.size() == 1);
    owner.integer_signal(4);
    owner.zero_signal();
    assert(owner.target.integer_total == 4);
    assert(owner.target.zero_calls == 1);
}

struct MutationOwner;

struct DynamicTail : Object {
    using Object::Object;
    std::vector<int>* order = nullptr;
    void call() { order->push_back(3); }
};

struct StaticFirst : Object {
    using Object::Object;
    MutationOwner* owner = nullptr;
    std::vector<int>* order = nullptr;
    void call();
};

struct StaticSecond : Object {
    using Object::Object;
    std::vector<int>* order = nullptr;
    void call() { order->push_back(2); }
};

struct MutationOwner : Object {
    explicit MutationOwner(std::vector<int>& order)
        : Object("mutation"), first("first"), second("second"), tail("tail") {
        first.owner = this;
        first.order = &order;
        second.order = &order;
        tail.order = &order;
    }

    StaticFirst first;
    StaticSecond second;
    DynamicTail tail;
    Signal<> signal{*this, "signal"};

    inline static constexpr auto static_connections = sss::static_connections(
        connect(&MutationOwner::signal, &MutationOwner::first, &StaticFirst::call),
        connect(&MutationOwner::signal, &MutationOwner::second, &StaticSecond::call)
    );
};

void StaticFirst::call() {
    order->push_back(1);
    owner->signal += slot(owner->tail, &DynamicTail::call);
}

static void dynamic_add_during_static_dispatch_waits() {
    std::vector<int> order;
    MutationOwner owner(order);

    owner.signal();
    assert((order == std::vector<int>{1, 2}));

    order.clear();
    owner.signal();
    assert((order == std::vector<int>{1, 2, 3}));
}

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
static void inspector_reports_static_structure() {
    std::vector<int> order;
    Actor actor(order);
    Inspector spy;

    assert(actor.hit.connected(slot(actor.enemy, &Enemy::damage)));
    assert(actor.hit.connection_count(slot(actor.enemy, &Enemy::damage)) == 1);
    assert(!actor.hit.add_unique(slot(actor.enemy, &Enemy::damage)));
    assert(!actor.hit.remove_one(slot(actor.enemy, &Enemy::damage))); // static edges are immutable

    const auto uses = spy.signals_for(slot(actor.enemy, &Enemy::damage));
    assert(uses.size() == 1);
    assert(uses[0].static_count == 1);
    assert(uses[0].dynamic_count == 0);

    const auto connections = actor.hit.connections();
    assert(connections.size() == 2);
    assert(connections[0].kind == ConnectionKind::static_connection);
    assert(connections[1].kind == ConnectionKind::static_connection);

    const auto peers = spy.connected_objects(actor);
    assert(peers.size() == 2);

    spy.activate();
    const auto emit_line = static_cast<std::uint_least32_t>(__LINE__ + 1);
    actor.hit(9);
    spy.deactivate();

    assert(spy.records().size() == 3); // EMIT + two static INVOKEs
    assert(spy.records()[0].kind == TraceKind::emit);
    assert(spy.records()[0].site.line == emit_line);
    assert(spy.records()[1].kind == TraceKind::invoke);
    assert(spy.records()[1].connection_kind == ConnectionKind::static_connection);
    assert(spy.records()[1].connected_at.valid());
}
#endif

int main() {
    static_then_dynamic_order();
    differently_typed_static_signals_coexist();
    dynamic_add_during_static_dispatch_waits();
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    inspector_reports_static_structure();
#endif
}

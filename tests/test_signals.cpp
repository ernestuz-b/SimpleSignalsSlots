#include "simple_signals_slots.hpp"

#include <cassert>
#include <sstream>
#include <string>
#include <vector>

using namespace sss;

enum class Damage { Fire, Ice };

struct Receiver final : Object {
    explicit Receiver(std::string name) : Object(std::move(name)) {}

    void damage(int amount, Damage kind) {
        calls.push_back(amount);
        kinds.push_back(kind);
    }

    void ping() { ++pings; }

    std::vector<int> calls;
    std::vector<Damage> kinds;
    int pings = 0;
};

struct Source final : Object {
    explicit Source(std::string name) : Object(std::move(name)) {}

    Signal<int, Damage> hit{*this, "hit"};
    Signal<> pulse{*this, "pulse"};
};

struct SelfRemoving final : Object {
    explicit SelfRemoving(Source& source)
        : Object("self-removing"), source(&source) {}

    void on_pulse() {
        ++calls;
        source->pulse -= slot(*this, &SelfRemoving::on_pulse);
    }

    Source* source;
    int calls = 0;
};

struct RemovesOther final : Object {
    RemovesOther(Source& source, Receiver& other)
        : Object("remover"), source(&source), other(&other) {}

    void on_pulse() {
        ++calls;
        source->pulse.remove_all(slot(*other, &Receiver::ping));
    }

    Source* source;
    Receiver* other;
    int calls = 0;
};

static void basic_connect_emit_and_duplicates() {
    Source actor("actor");
    Receiver enemy("enemy");

    actor.hit += slot(enemy, &Receiver::damage);
    assert(actor.hit.connected(slot(enemy, &Receiver::damage)));
    assert(actor.hit.connection_count(slot(enemy, &Receiver::damage)) == 1);

    actor.hit(10, Damage::Fire);
    assert(enemy.calls == std::vector<int>{10});
    assert(enemy.kinds == std::vector<Damage>{Damage::Fire});

    actor.hit += slot(enemy, &Receiver::damage);
    assert(actor.hit.connection_count(slot(enemy, &Receiver::damage)) == 2);
    assert(actor.hit.has_duplicate(slot(enemy, &Receiver::damage)));

    actor.hit(7, Damage::Ice);
    assert(enemy.calls == (std::vector<int>{10, 7, 7}));

    actor.hit -= slot(enemy, &Receiver::damage);
    assert(actor.hit.connection_count(slot(enemy, &Receiver::damage)) == 1);

    assert(!actor.hit.add_unique(slot(enemy, &Receiver::damage)));
    assert(actor.hit.remove_all(slot(enemy, &Receiver::damage)) == 1);
    assert(!actor.hit.connected(slot(enemy, &Receiver::damage)));
    assert(actor.hit.size() == 0);
}

static void destruction_disconnects_incoming_connections() {
    Source source("source");
    Inspector inspector;

    {
        auto target = std::make_unique<Receiver>("temporary");
        source.hit += slot(*target, &Receiver::damage);
        assert(source.hit.size() == 1);
        assert(inspector.connected_objects(source).size() == 1);
        target.reset();
    }

    assert(source.hit.size() == 0);
    assert(inspector.connected_objects(source).empty());

    Receiver target("target");
    {
        auto temporary_source = std::make_unique<Source>("temporary-source");
        temporary_source->hit += slot(target, &Receiver::damage);
        assert(inspector.connected_objects(target).size() == 1);
        temporary_source.reset();
    }
    assert(inspector.connected_objects(target).empty());
}

static void reverse_slot_query_uses_object_links() {
    Source a("a");
    Source b("b");
    Receiver receiver("receiver");
    Inspector inspector;

    a.hit += slot(receiver, &Receiver::damage);
    a.hit += slot(receiver, &Receiver::damage);
    b.hit += slot(receiver, &Receiver::damage);

    const auto matches = inspector.signals_for(slot(receiver, &Receiver::damage));
    assert(matches.size() == 2);

    std::size_t a_count = 0;
    std::size_t b_count = 0;
    for (const auto& match : matches) {
        if (match.source.name == "a" && match.signal_name == "hit") {
            a_count = match.count;
        }
        if (match.source.name == "b" && match.signal_name == "hit") {
            b_count = match.count;
        }
    }

    assert(a_count == 2);
    assert(b_count == 1);
}

static void modifications_during_emit_are_safe_and_ordered() {
    Source source("source");
    SelfRemoving self(source);
    Receiver later("later");

    source.pulse += slot(self, &SelfRemoving::on_pulse);
    source.pulse += slot(later, &Receiver::ping);

    source.pulse();
    assert(self.calls == 1);
    assert(later.pings == 1);
    assert(source.pulse.size() == 1);

    source.pulse();
    assert(self.calls == 1);
    assert(later.pings == 2);

    Source source2("source2");
    Receiver victim("victim");
    RemovesOther remover(source2, victim);

    source2.pulse += slot(remover, &RemovesOther::on_pulse);
    source2.pulse += slot(victim, &Receiver::ping);

    source2.pulse();
    assert(remover.calls == 1);
    assert(victim.pings == 0); // disconnected before its turn
}

static void inspector_traces_connect_emit_invoke_disconnect_and_locations() {
    Source source("source");
    Receiver target("target");
    Inspector inspector;
    inspector.activate();

    const auto connect_line = static_cast<std::uint_least32_t>(__LINE__ + 1);
    source.hit += slot(target, &Receiver::damage);

    const auto emit_line = static_cast<std::uint_least32_t>(__LINE__ + 1);
    source.hit(42, Damage::Fire);

    const auto remove_line = static_cast<std::uint_least32_t>(__LINE__ + 1);
    source.hit -= slot(target, &Receiver::damage);

    inspector.deactivate();

    assert(inspector.records().size() == 4);
    assert(inspector.records()[0].kind == TraceKind::connect);
    assert(inspector.records()[0].site.line == connect_line);
    assert(inspector.records()[0].source_object_name == "source");
    assert(inspector.records()[0].target_object_name == "target");
    assert(inspector.records()[0].signal_name == "hit");

    assert(inspector.records()[1].kind == TraceKind::emit);
    assert(inspector.records()[1].site.line == emit_line);

    assert(inspector.records()[2].kind == TraceKind::invoke);
    assert(inspector.records()[2].site.line == emit_line);
    assert(inspector.records()[2].connected_at.line == connect_line);

    assert(inspector.records()[3].kind == TraceKind::disconnect);
    assert(inspector.records()[3].site.line == remove_line);

    std::ostringstream out;
    inspector.dump(out);
    const auto text = out.str();
    assert(text.find("CONNECT source.hit -> target") != std::string::npos);
    assert(text.find("EMIT source.hit") != std::string::npos);
    assert(text.find("INVOKE source.hit -> target") != std::string::npos);
}

static void peer_refcounts_and_self_connections_are_correct() {
    Source source("source");
    Receiver target("target");
    Inspector inspector;

    source.hit += slot(target, &Receiver::damage);
    source.pulse += slot(target, &Receiver::ping);
    assert(inspector.connected_objects(source).size() == 1);
    assert(inspector.connected_objects(target).size() == 1);

    source.hit.remove_all(slot(target, &Receiver::damage));
    assert(inspector.connected_objects(source).size() == 1);

    source.pulse.remove_all(slot(target, &Receiver::ping));
    assert(inspector.connected_objects(source).empty());
    assert(inspector.connected_objects(target).empty());

    struct Self final : Object {
        Self() : Object("self") {}
        void on_pulse() { ++calls; }
        Signal<> pulse{*this, "pulse"};
        int calls = 0;
    } self;

    self.pulse += slot(self, &Self::on_pulse);
    self.pulse();
    assert(self.calls == 1);
    assert(inspector.connected_objects(self).empty());
    const auto matches = inspector.signals_for(slot(self, &Self::on_pulse));
    assert(matches.size() == 1);
    assert(matches.front().count == 1);
}

static void inspector_deactivates_on_destruction() {
    Source source("source");
    Receiver target("target");

    {
        Inspector inspector;
        inspector.activate();
        source.hit += slot(target, &Receiver::damage);
        assert(active_inspector() == &inspector);
    }

    assert(active_inspector() == nullptr);
    source.hit(3, Damage::Fire);
    assert(target.calls == std::vector<int>{3});
}

static void inspector_can_be_disabled_without_changing_semantics() {
    Source source("source");
    Receiver target("target");
    Inspector inspector;
    inspector.activate();
    inspector.set_enabled(false);

    source.hit += slot(target, &Receiver::damage);
    source.hit(1, Damage::Ice);

    assert(target.calls.size() == 1);
    assert(inspector.records().empty());

    inspector.set_enabled(true);
    source.hit(2, Damage::Fire);
    assert(target.calls.size() == 2);
    assert(!inspector.records().empty());

    inspector.deactivate();
}

int main() {
    basic_connect_emit_and_duplicates();
    destruction_disconnects_incoming_connections();
    reverse_slot_query_uses_object_links();
    modifications_during_emit_are_safe_and_ordered();
    peer_refcounts_and_self_connections_are_correct();
    inspector_traces_connect_emit_invoke_disconnect_and_locations();
    inspector_deactivates_on_destruction();
    inspector_can_be_disabled_without_changing_semantics();
}

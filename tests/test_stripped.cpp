#include "simple_signals_slots.hpp"

#include <cassert>
#include <memory>
#include <vector>

using namespace sss;

enum class Damage { Fire, Ice };

struct Receiver final : Object {
    Receiver() : Object("receiver") {}

    void damage(int amount, Damage) { calls.push_back(amount); }
    void ping() { ++pings; }

    std::vector<int> calls;
    int pings = 0;
};

struct Source final : Object {
    Source() : Object("source") {}

    Signal<int, Damage> hit{*this, "hit"};
    Signal<> pulse{*this, "pulse"};
};

int main() {
    Source source;
    Receiver receiver;

    source.hit += slot(receiver, &Receiver::damage);
    source.hit += slot(receiver, &Receiver::damage);
    assert(source.hit.connection_count(slot(receiver, &Receiver::damage)) == 2);

    source.hit(7, Damage::Fire);
    assert((receiver.calls == std::vector<int>{7, 7}));

    source.hit -= slot(receiver, &Receiver::damage);
    assert(source.hit.connection_count(slot(receiver, &Receiver::damage)) == 1);

    {
        auto temporary = std::make_unique<Receiver>();
        source.hit += slot(*temporary, &Receiver::damage);
        assert(source.hit.size() == 2);
        temporary.reset();
        assert(source.hit.size() == 1);
    }

    assert(source.hit.remove_all(slot(receiver, &Receiver::damage)) == 1);
    assert(source.hit.size() == 0);
}

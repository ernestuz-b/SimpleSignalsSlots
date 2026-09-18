#include "simple_signals_slots.hpp"

#include <iostream>

using namespace sss;

enum class Damage { Physical, Fire };

struct Enemy final : Object {
    Enemy() : Object("enemy") {}

    void damage(int amount, Damage kind) {
        hp -= amount;
        std::cout << "enemy takes " << amount
                  << (kind == Damage::Fire ? " fire" : " physical")
                  << " damage; hp=" << hp << '\n';
    }

    int hp = 100;
};

struct Actor final : Object {
    Actor() : Object("actor") {}

    Signal<int, Damage> hit{*this, "hit"};
};

int main() {
    Actor actor;
    Enemy enemy;

    Inspector spy;
    spy.activate();

    actor.hit += slot(enemy, &Enemy::damage);
    actor.hit(10, Damage::Fire);

    std::cout << "connected: "
              << actor.hit.connection_count(slot(enemy, &Enemy::damage))
              << "\n\ntrace:\n";
    spy.dump(std::cout);
}

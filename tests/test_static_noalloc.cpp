#define SIMPLE_SIGNALS_DISABLE_TRACE
#include "simple_signals_slots.hpp"
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>

static std::size_t allocations = 0;
void* operator new(std::size_t n) {
    ++allocations;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace sss;
struct Sink : Object { Sink():Object("sink"){} int total=0; void add(int x){ total += x; } };
struct StaticOnly : Object {
    StaticOnly():Object("owner"){}
    Sink sink;
    Signal<int> signal{*this,"signal"};
    inline static constexpr auto static_connections = sss::static_connections(
        connect(&StaticOnly::signal, &StaticOnly::sink, &Sink::add)
    );
};
static_assert(StaticOnly::static_connections.count<&StaticOnly::signal>() == 1);

int main(){
    const auto before = allocations;
    {
        StaticOnly value;
        value.signal(4);
        value.signal(5);
        assert(value.sink.total == 9);
    }
    assert(allocations == before);
}

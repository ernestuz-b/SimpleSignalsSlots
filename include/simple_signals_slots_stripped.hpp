#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace sss {

class Object;
class SignalBase;
template <class... Args> class Signal;
template <class T, class Method> class SlotRef;

namespace detail {

template <class T>
const void* type_tag() noexcept {
    static const int tag = 0;
    return &tag;
}

struct SlotQuery {
    Object* target = nullptr;
    const void* method_type = nullptr;
    const void* method_value = nullptr;
};

} // namespace detail

class SignalManager {
public:
    SignalManager() = default;
    SignalManager(const SignalManager&) = delete;
    SignalManager& operator=(const SignalManager&) = delete;
    SignalManager(SignalManager&&) = delete;
    SignalManager& operator=(SignalManager&&) = delete;

private:
    friend class Object;
    friend class SignalBase;
    template <class...> friend class Signal;

    void register_signal(SignalBase* signal) { signals_.push_back(signal); }

    void unregister_signal(SignalBase* signal) noexcept {
        const auto it = std::find(signals_.begin(), signals_.end(), signal);
        if (it != signals_.end()) signals_.erase(it);
    }

    void disconnect_target(Object* target) noexcept;
    void disconnect_all_outgoing() noexcept;

    std::vector<SignalBase*> signals_;
};

class Object {
public:
    explicit Object(const char* = nullptr) {}
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    void disconnect_all() noexcept;

private:
    friend class SignalManager;
    friend class SignalBase;
    template <class...> friend class Signal;

    struct Peer {
        Object* object = nullptr;
        std::size_t refs = 0;
    };

    static auto find_peer(std::vector<Peer>& peers, Object* object) {
        return std::find_if(peers.begin(), peers.end(), [object](const Peer& peer) {
            return peer.object == object;
        });
    }

    static void link_connection(Object& a, Object& b) {
        if (&a == &b) return;

        auto a_it = find_peer(a.peers_, &b);
        auto b_it = find_peer(b.peers_, &a);
        if (a_it == a.peers_.end()) a.peers_.reserve(a.peers_.size() + 1);
        if (b_it == b.peers_.end()) b.peers_.reserve(b.peers_.size() + 1);

        a_it = find_peer(a.peers_, &b);
        b_it = find_peer(b.peers_, &a);

        if (a_it == a.peers_.end()) a.peers_.push_back({&b, 1});
        else ++a_it->refs;
        if (b_it == b.peers_.end()) b.peers_.push_back({&a, 1});
        else ++b_it->refs;
    }

    static void unlink_connection(Object& a, Object& b) noexcept {
        if (&a == &b) return;

        auto erase_one = [](Object& owner, Object* other) {
            const auto it = find_peer(owner.peers_, other);
            if (it == owner.peers_.end()) return;
            if (it->refs > 1) --it->refs;
            else owner.peers_.erase(it);
        };

        erase_one(a, &b);
        erase_one(b, &a);
    }

    void disconnect_all_impl() noexcept;

    SignalManager signal_manager_;
    std::vector<Peer> peers_;
    bool disconnecting_ = false;
};

class SignalBase {
public:
    virtual ~SignalBase() {
        if (owner_ != nullptr) owner_->signal_manager_.unregister_signal(this);
    }

    SignalBase(const SignalBase&) = delete;
    SignalBase& operator=(const SignalBase&) = delete;
    SignalBase(SignalBase&&) = delete;
    SignalBase& operator=(SignalBase&&) = delete;

    [[nodiscard]] Object& owner() const noexcept { return *owner_; }

protected:
    explicit SignalBase(Object& owner, const char* = nullptr) : owner_(&owner) {
        owner_->signal_manager_.register_signal(this);
    }

private:
    friend class SignalManager;
    template <class...> friend class Signal;

    virtual void disconnect_target(Object* target) noexcept = 0;
    virtual void disconnect_all_connections() noexcept = 0;

    Object* owner_ = nullptr;
};

inline void SignalManager::disconnect_target(Object* target) noexcept {
    const auto signals = signals_;
    for (auto* signal : signals) {
        if (std::find(signals_.begin(), signals_.end(), signal) != signals_.end()) {
            signal->disconnect_target(target);
        }
    }
}

inline void SignalManager::disconnect_all_outgoing() noexcept {
    const auto signals = signals_;
    for (auto* signal : signals) {
        if (std::find(signals_.begin(), signals_.end(), signal) != signals_.end()) {
            signal->disconnect_all_connections();
        }
    }
}

inline void Object::disconnect_all_impl() noexcept {
    if (disconnecting_) return;
    disconnecting_ = true;

    signal_manager_.disconnect_all_outgoing();

    while (!peers_.empty()) {
        Object* peer = peers_.back().object;
        const std::size_t before = peers_.size();
        peer->signal_manager_.disconnect_target(this);

        // Defensive recovery if a custom SignalBase violates the unlink contract.
        if (peers_.size() == before && !peers_.empty() && peers_.back().object == peer) {
            const auto it = find_peer(peers_, peer);
            if (it != peers_.end()) peers_.erase(it);
            const auto other = find_peer(peer->peers_, this);
            if (other != peer->peers_.end()) peer->peers_.erase(other);
        }
    }

    disconnecting_ = false;
}

inline void Object::disconnect_all() noexcept { disconnect_all_impl(); }
inline Object::~Object() { disconnect_all_impl(); }

template <class T, class Method>
class SlotRef {
public:
    using object_type = T;
    using method_type = Method;

    SlotRef(T& object, Method method) : object_(&object), method_(method) {}

    [[nodiscard]] T& object() const noexcept { return *object_; }
    [[nodiscard]] Method method() const noexcept { return method_; }

private:
    template <class...> friend class Signal;
    T* object_ = nullptr;
    Method method_{};
};

template <class T, class Method>
    requires std::derived_from<T, Object> && std::is_member_function_pointer_v<Method>
auto slot(T& object, Method method) -> SlotRef<T, Method> {
    return SlotRef<T, Method>(object, method);
}

template <class... Args>
class Signal final : public SignalBase {
    static_assert((!std::is_rvalue_reference_v<Args> && ...),
                  "Signal does not support rvalue-reference parameters");
    static_assert(((std::is_reference_v<Args> || std::copy_constructible<Args>) && ...),
                  "By-value Signal parameters must be copy constructible because a signal is multicast");

    struct Connection {
        Object* target = nullptr;
        std::function<void(Args...)> invoke;
        std::function<bool(const detail::SlotQuery&)> matches;
        bool active = true;
    };

    struct State {
        explicit State(Object& source) : source(&source) {}
        Object* source = nullptr;
        bool alive = true;
        std::size_t emit_depth = 0;
        std::size_t active_count = 0;
        std::vector<Connection> connections;
    };

public:
    explicit Signal(Object& owner, const char* name = nullptr)
        : SignalBase(owner, name), state_(std::make_shared<State>(owner)) {}

    ~Signal() override {
        auto state = state_;
        if (state) {
            disconnect_all_impl(state);
            state->alive = false;
            compact_if_idle(state);
        }
    }

    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    Signal& operator+=(const SlotRef<T, Method>& endpoint) {
        add(endpoint);
        return *this;
    }

    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    Signal& operator-=(const SlotRef<T, Method>& endpoint) noexcept {
        remove_one(endpoint);
        return *this;
    }

    // Connection IDs are diagnostic-only. The stripped build preserves the
    // source-compatible return type but returns 0 and stores no ID per edge.
    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    std::uint64_t add(const SlotRef<T, Method>& endpoint) {
        auto state = state_;
        Connection connection;
        connection.target = &endpoint.object();

        T* target = &endpoint.object();
        Method method = endpoint.method();
        connection.invoke = [target, method](Args... args) {
            (void)std::invoke(method, *target, args...);
        };
        connection.matches = [target, method](const detail::SlotQuery& query) {
            if (query.target != target || query.method_type != detail::type_tag<Method>()) return false;
            return method == *static_cast<const Method*>(query.method_value);
        };

        state->connections.push_back(std::move(connection));
        try {
            Object::link_connection(*state->source, *target);
        } catch (...) {
            state->connections.pop_back();
            throw;
        }
        ++state->active_count;
        return 0;
    }

    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    bool add_unique(const SlotRef<T, Method>& endpoint) {
        if (connected(endpoint)) return false;
        add(endpoint);
        return true;
    }

    template <class T, class Method>
    [[nodiscard]] bool connected(const SlotRef<T, Method>& endpoint) const noexcept {
        return connection_count(endpoint) != 0;
    }

    template <class T, class Method>
    [[nodiscard]] std::size_t connection_count(const SlotRef<T, Method>& endpoint) const noexcept {
        return connection_count_impl(make_query(endpoint));
    }

    template <class T, class Method>
    [[nodiscard]] bool has_duplicate(const SlotRef<T, Method>& endpoint) const noexcept {
        return connection_count(endpoint) > 1;
    }

    template <class T, class Method>
    bool remove_one(const SlotRef<T, Method>& endpoint) noexcept {
        auto state = state_;
        const auto query = make_query(endpoint);
        for (std::size_t i = state->connections.size(); i > 0; --i) {
            auto& connection = state->connections[i - 1];
            if (connection.active && connection.matches(query)) {
                deactivate(state, connection);
                compact_if_idle(state);
                return true;
            }
        }
        return false;
    }

    template <class T, class Method>
    std::size_t remove_all(const SlotRef<T, Method>& endpoint) noexcept {
        auto state = state_;
        const auto query = make_query(endpoint);
        std::size_t removed = 0;
        for (auto& connection : state->connections) {
            if (connection.active && connection.matches(query)) {
                deactivate(state, connection);
                ++removed;
            }
        }
        compact_if_idle(state);
        return removed;
    }

    void clear() noexcept {
        auto state = state_;
        disconnect_all_impl(state);
        compact_if_idle(state);
    }

    void operator()(Args... args) {
        auto state = state_;
        if (!state || !state->alive) return;

        ++state->emit_depth;
        const std::size_t limit = state->connections.size();
        try {
            for (std::size_t i = 0; i < limit; ++i) {
                if (!state->alive || i >= state->connections.size()) break;
                auto& connection = state->connections[i];
                if (!connection.active) continue;

                // Copy before invoking: a nested connection may reallocate the vector.
                const auto invoke = connection.invoke;
                invoke(args...);
            }
        } catch (...) {
            --state->emit_depth;
            compact_if_idle(state);
            throw;
        }

        --state->emit_depth;
        compact_if_idle(state);
    }

    [[nodiscard]] std::size_t size() const noexcept { return state_->active_count; }

private:
    template <class T, class Method>
    static detail::SlotQuery make_query(const SlotRef<T, Method>& endpoint) noexcept {
        return {&endpoint.object(), detail::type_tag<Method>(), &endpoint.method_};
    }

    [[nodiscard]] std::size_t connection_count_impl(const detail::SlotQuery& query) const noexcept {
        std::size_t count = 0;
        for (const auto& connection : state_->connections) {
            if (connection.active && connection.matches(query)) ++count;
        }
        return count;
    }

    void disconnect_target(Object* target) noexcept override {
        auto state = state_;
        for (auto& connection : state->connections) {
            if (connection.active && connection.target == target) deactivate(state, connection);
        }
        compact_if_idle(state);
    }

    void disconnect_all_connections() noexcept override {
        auto state = state_;
        disconnect_all_impl(state);
        compact_if_idle(state);
    }

    static void disconnect_all_impl(const std::shared_ptr<State>& state) noexcept {
        for (auto& connection : state->connections) {
            if (connection.active) deactivate(state, connection);
        }
    }

    static void deactivate(const std::shared_ptr<State>& state, Connection& connection) noexcept {
        if (!connection.active) return;
        Object* source = state->source;
        Object* target = connection.target;
        connection.active = false;
        if (state->active_count > 0) --state->active_count;
        Object::unlink_connection(*source, *target);
    }

    static void compact_if_idle(const std::shared_ptr<State>& state) noexcept {
        if (state->emit_depth != 0) return;
        std::erase_if(state->connections, [](const Connection& connection) {
            return !connection.active;
        });
    }

    std::shared_ptr<State> state_;
};

} // namespace sss

#pragma once

#ifdef SIMPLE_SIGNALS_DISABLE_TRACE
#include "simple_signals_slots_stripped.hpp"
#else

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sss {

class Object;
class SignalBase;
class Inspector;
template <class... Args>
class Signal;
template <class T, class Method>
class SlotRef;

struct SourcePoint {
    std::string file;
    std::string function;
    std::uint_least32_t line = 0;
    std::uint_least32_t column = 0;

    static SourcePoint from(std::source_location where) {
        if (where.line() == 0) {
            return {};
        }
        return {
            where.file_name(),
            where.function_name(),
            where.line(),
            where.column(),
        };
    }

    [[nodiscard]] bool valid() const noexcept { return line != 0; }
};

enum class TraceKind {
    connect,
    disconnect,
    emit,
    invoke,
};

enum class DisconnectReason {
    explicit_remove,
    target_destroyed,
    signal_destroyed,
    object_disconnect_all,
};

struct TraceRecord {
    TraceKind kind = TraceKind::emit;
    DisconnectReason disconnect_reason = DisconnectReason::explicit_remove;

    std::uint64_t signal_id = 0;
    std::uint64_t connection_id = 0;
    std::uint64_t source_object_id = 0;
    std::uint64_t target_object_id = 0;

    std::string signal_name;
    std::string source_object_name;
    std::string target_object_name;

    SourcePoint site;
    SourcePoint connected_at;

    // Number of connections for this exact signal/slot pair after CONNECT,
    // or remaining after an explicit REMOVE when that information is known.
    std::size_t multiplicity = 0;
};

struct ObjectInfo {
    std::uint64_t id = 0;
    std::string name;
};

struct ConnectionInfo {
    std::uint64_t connection_id = 0;
    ObjectInfo target;
    SourcePoint connected_at;
};

struct SignalMatch {
    std::uint64_t signal_id = 0;
    ObjectInfo source;
    std::string signal_name;
    std::size_t count = 0;
};

namespace detail {

inline std::uint64_t next_object_id() noexcept {
    static std::uint64_t value = 1;
    return value++;
}

inline std::uint64_t next_signal_id() noexcept {
    static std::uint64_t value = 1;
    return value++;
}

inline std::uint64_t next_connection_id() noexcept {
    static std::uint64_t value = 1;
    return value++;
}

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

Inspector*& active_inspector() noexcept;
void submit_trace(TraceRecord record);

} // namespace detail

class SignalManager {
public:
    explicit SignalManager(Object& owner) noexcept : owner_(&owner) {}

    SignalManager(const SignalManager&) = delete;
    SignalManager& operator=(const SignalManager&) = delete;
    SignalManager(SignalManager&&) = delete;
    SignalManager& operator=(SignalManager&&) = delete;

private:
    friend class Object;
    friend class SignalBase;
    friend class Inspector;
    template <class...>
    friend class Signal;

    void register_signal(SignalBase* signal);
    void unregister_signal(SignalBase* signal) noexcept;
    void disconnect_target(Object* target, DisconnectReason reason) noexcept;
    void disconnect_all_outgoing(DisconnectReason reason) noexcept;

    Object* owner_ = nullptr;
    std::vector<SignalBase*> signals_;
};

class Object {
public:
    explicit Object(std::string debug_name = {})
        : id_(detail::next_object_id()), debug_name_(std::move(debug_name)), signal_manager_(*this) {}

    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] std::string_view debug_name() const noexcept { return debug_name_; }

    void set_debug_name(std::string name) { debug_name_ = std::move(name); }

    // Disconnect every outgoing and incoming connection involving this object.
    // This is automatic during destruction, but is also useful when an object
    // is logically retired before its storage is reclaimed.
    void disconnect_all() noexcept;

private:
    void disconnect_all_impl(DisconnectReason reason) noexcept;
    friend class SignalManager;
    friend class SignalBase;
    friend class Inspector;
    template <class...>
    friend class Signal;

    struct Peer {
        Object* object = nullptr;
        std::size_t refs = 0;
    };

    static void link_connection(Object& a, Object& b);
    static void unlink_connection(Object& a, Object& b) noexcept;

    static auto find_peer(std::vector<Peer>& peers, Object* object) {
        return std::find_if(peers.begin(), peers.end(), [object](const Peer& peer) {
            return peer.object == object;
        });
    }

    static auto find_peer(const std::vector<Peer>& peers, const Object* object) {
        return std::find_if(peers.begin(), peers.end(), [object](const Peer& peer) {
            return peer.object == object;
        });
    }

    [[nodiscard]] std::string display_name() const {
        if (!debug_name_.empty()) {
            return debug_name_;
        }
        return "object#" + std::to_string(id_);
    }

    std::uint64_t id_ = 0;
    std::string debug_name_;
    SignalManager signal_manager_;
    std::vector<Peer> peers_;
    bool disconnecting_ = false;
};

class SignalBase {
public:
    virtual ~SignalBase();

    SignalBase(const SignalBase&) = delete;
    SignalBase& operator=(const SignalBase&) = delete;
    SignalBase(SignalBase&&) = delete;
    SignalBase& operator=(SignalBase&&) = delete;

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] Object& owner() const noexcept { return *owner_; }
    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] SourcePoint declared_at() const { return SourcePoint::from(declared_at_); }

    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t connection_count_to(const Object* target) const noexcept = 0;
    [[nodiscard]] virtual std::vector<ConnectionInfo> connections() const = 0;

protected:
    SignalBase(Object& owner,
               std::string name,
               std::source_location declared_at = std::source_location::current())
        : owner_(&owner),
          id_(detail::next_signal_id()),
          name_(std::move(name)),
          declared_at_(declared_at) {
        owner_->signal_manager_.register_signal(this);
    }

private:
    friend class SignalManager;
    friend class Inspector;
    template <class...>
    friend class Signal;

    [[nodiscard]] virtual std::size_t connection_count(const detail::SlotQuery& query) const noexcept = 0;
    virtual void disconnect_target(Object* target, DisconnectReason reason) noexcept = 0;
    virtual void disconnect_all(DisconnectReason reason) noexcept = 0;

    Object* owner_ = nullptr;
    std::uint64_t id_ = 0;
    std::string name_;
    std::source_location declared_at_;
};

inline void SignalManager::register_signal(SignalBase* signal) {
    signals_.push_back(signal);
}

inline void SignalManager::unregister_signal(SignalBase* signal) noexcept {
    const auto it = std::find(signals_.begin(), signals_.end(), signal);
    if (it != signals_.end()) {
        signals_.erase(it);
    }
}

inline void SignalManager::disconnect_target(Object* target, DisconnectReason reason) noexcept {
    // A signal can unregister itself while disconnecting if user code destroys
    // its owner. Work on a copy of the pointers to keep traversal stable.
    const auto signals = signals_;
    for (auto* signal : signals) {
        if (std::find(signals_.begin(), signals_.end(), signal) != signals_.end()) {
            signal->disconnect_target(target, reason);
        }
    }
}

inline void SignalManager::disconnect_all_outgoing(DisconnectReason reason) noexcept {
    const auto signals = signals_;
    for (auto* signal : signals) {
        if (std::find(signals_.begin(), signals_.end(), signal) != signals_.end()) {
            signal->disconnect_all(reason);
        }
    }
}

inline SignalBase::~SignalBase() {
    if (owner_ != nullptr) {
        owner_->signal_manager_.unregister_signal(this);
    }
}

inline void Object::link_connection(Object& a, Object& b) {
    if (&a == &b) {
        return;
    }

    auto a_it = find_peer(a.peers_, &b);
    auto b_it = find_peer(b.peers_, &a);

    if (a_it == a.peers_.end()) {
        a.peers_.reserve(a.peers_.size() + 1);
    }
    if (b_it == b.peers_.end()) {
        b.peers_.reserve(b.peers_.size() + 1);
    }

    // Re-find after reserve, which may invalidate iterators.
    a_it = find_peer(a.peers_, &b);
    b_it = find_peer(b.peers_, &a);

    if (a_it == a.peers_.end()) {
        a.peers_.push_back({&b, 1});
    } else {
        ++a_it->refs;
    }

    if (b_it == b.peers_.end()) {
        b.peers_.push_back({&a, 1});
    } else {
        ++b_it->refs;
    }
}

inline void Object::unlink_connection(Object& a, Object& b) noexcept {
    if (&a == &b) {
        return;
    }

    auto erase_one = [](Object& owner, Object* other) {
        const auto it = find_peer(owner.peers_, other);
        if (it == owner.peers_.end()) {
            return;
        }
        if (it->refs > 1) {
            --it->refs;
        } else {
            owner.peers_.erase(it);
        }
    };

    erase_one(a, &b);
    erase_one(b, &a);
}

inline void Object::disconnect_all_impl(DisconnectReason reason) noexcept {
    if (disconnecting_) {
        return;
    }
    disconnecting_ = true;

    signal_manager_.disconnect_all_outgoing(reason);

    // After removing outgoing connections, any remaining peer represents at
    // least one incoming connection. Ask that peer's signal manager to remove
    // every connection targeting this object. The peer list shrinks as those
    // connections are removed.
    while (!peers_.empty()) {
        Object* peer = peers_.back().object;
        const std::size_t before = peers_.size();
        peer->signal_manager_.disconnect_target(this, reason);

        // Defensive escape hatch for an inconsistent custom SignalBase.
        if (peers_.size() == before && !peers_.empty() && peers_.back().object == peer) {
            const auto it = find_peer(peers_, peer);
            if (it != peers_.end()) {
                peers_.erase(it);
            }
            const auto other = find_peer(peer->peers_, this);
            if (other != peer->peers_.end()) {
                peer->peers_.erase(other);
            }
        }
    }

    disconnecting_ = false;
}

inline void Object::disconnect_all() noexcept {
    disconnect_all_impl(DisconnectReason::object_disconnect_all);
}

inline Object::~Object() {
    disconnect_all_impl(DisconnectReason::target_destroyed);
}

class Inspector {
public:
    Inspector() = default;
    ~Inspector() { deactivate(); }

    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;
    Inspector(Inspector&&) = delete;
    Inspector& operator=(Inspector&&) = delete;

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    void clear() { records_.clear(); }
    [[nodiscard]] const std::vector<TraceRecord>& records() const noexcept { return records_; }

    // Make this inspector receive subsequent trace records. Passing nullptr to
    // set_active_inspector() disables collection globally without affecting
    // signal behaviour or topology queries.
    void activate() noexcept;
    void deactivate() noexcept;

    [[nodiscard]] std::vector<ObjectInfo> connected_objects(const Object& object) const {
        std::vector<ObjectInfo> result;
        result.reserve(object.peers_.size());
        for (const auto& peer : object.peers_) {
            result.push_back({peer.object->id(), peer.object->display_name()});
        }
        return result;
    }

    template <class T, class Method>
    [[nodiscard]] std::vector<SignalMatch> signals_for(const SlotRef<T, Method>& slot) const;

    void dump(std::ostream& out) const;

private:
    friend void detail::submit_trace(TraceRecord record);

    void record(TraceRecord record) {
        if (enabled_) {
            records_.push_back(std::move(record));
        }
    }

    bool enabled_ = true;
    std::vector<TraceRecord> records_;
};

namespace detail {

inline Inspector*& active_inspector() noexcept {
    static Inspector* value = nullptr;
    return value;
}

inline void submit_trace(TraceRecord record) {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    if (auto* inspector = active_inspector(); inspector != nullptr && inspector->enabled()) {
        inspector->record(std::move(record));
    }
#else
    (void)record;
#endif
}

inline const char* trace_kind_name(TraceKind kind) noexcept {
    switch (kind) {
        case TraceKind::connect: return "CONNECT";
        case TraceKind::disconnect: return "DISCONNECT";
        case TraceKind::emit: return "EMIT";
        case TraceKind::invoke: return "INVOKE";
    }
    return "UNKNOWN";
}

inline const char* disconnect_reason_name(DisconnectReason reason) noexcept {
    switch (reason) {
        case DisconnectReason::explicit_remove: return "explicit";
        case DisconnectReason::target_destroyed: return "target-destroyed";
        case DisconnectReason::signal_destroyed: return "signal-destroyed";
        case DisconnectReason::object_disconnect_all: return "object-disconnect-all";
    }
    return "unknown";
}

} // namespace detail

inline void Inspector::activate() noexcept {
    detail::active_inspector() = this;
}

inline void Inspector::deactivate() noexcept {
    if (detail::active_inspector() == this) {
        detail::active_inspector() = nullptr;
    }
}

inline void set_active_inspector(Inspector* inspector) noexcept {
    detail::active_inspector() = inspector;
}

inline Inspector* active_inspector() noexcept {
    return detail::active_inspector();
}

inline void Inspector::dump(std::ostream& out) const {
    for (const auto& record : records_) {
        out << detail::trace_kind_name(record.kind) << ' '
            << record.source_object_name << '.' << record.signal_name;

        if (record.target_object_id != 0) {
            out << " -> " << record.target_object_name;
        }

        if (record.connection_id != 0) {
            out << " [connection " << record.connection_id << ']';
        }

        if (record.kind == TraceKind::connect && record.multiplicity > 1) {
            out << " [multiplicity " << record.multiplicity << ']';
        }

        if (record.kind == TraceKind::disconnect) {
            out << " [" << detail::disconnect_reason_name(record.disconnect_reason) << ']';
        }

        if (record.site.valid()) {
            out << " @ " << record.site.file << ':' << record.site.line;
            if (!record.site.function.empty()) {
                out << " in " << record.site.function;
            }
        }

        if (record.kind == TraceKind::invoke && record.connected_at.valid()) {
            out << " [connected @ " << record.connected_at.file << ':'
                << record.connected_at.line << ']';
        }

        out << '\n';
    }
}

template <class T, class Method>
class SlotRef {
public:
    using object_type = T;
    using method_type = Method;

    [[nodiscard]] T& object() const noexcept { return *object_; }
    [[nodiscard]] Method method() const noexcept { return method_; }
    [[nodiscard]] std::source_location location() const noexcept { return location_; }

public:
    // Normally created through slot(object, &Type::method). The constructor is
    // public only so the tiny value type stays straightforward; user code does
    // not need to name SlotRef or its template arguments.
    SlotRef(T& object, Method method, std::source_location location)
        : object_(&object), method_(method), location_(location) {}

private:
    template <class...>
    friend class Signal;
    friend class Inspector;

    T* object_ = nullptr;
    Method method_{};
    std::source_location location_;
};

template <class T, class Method>
    requires std::derived_from<T, Object> && std::is_member_function_pointer_v<Method>
auto slot(T& object,
          Method method,
          std::source_location location = std::source_location::current()) -> SlotRef<T, Method> {
    return SlotRef<T, Method>(object, method, location);
}

template <class... Args>
class Signal final : public SignalBase {
    static_assert((!std::is_rvalue_reference_v<Args> && ...),
                  "Signal does not support rvalue-reference parameters");
    static_assert(((std::is_reference_v<Args> || std::copy_constructible<Args>) && ...),
                  "By-value Signal parameters must be copy constructible because a signal is multicast");

    struct Connection {
        std::uint64_t id = 0;
        Object* target = nullptr;
        std::function<void(Args...)> invoke;
        std::function<bool(const detail::SlotQuery&)> matches;
        std::source_location connected_at;
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
    explicit Signal(Object& owner,
                    std::string name,
                    std::source_location declared_at = std::source_location::current())
        : SignalBase(owner, std::move(name), declared_at),
          state_(std::make_shared<State>(owner)) {}

    ~Signal() override {
        auto state = state_;
        if (state) {
            disconnect_all_impl(state, DisconnectReason::signal_destroyed, {});
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

    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    std::uint64_t add(const SlotRef<T, Method>& endpoint) {
        auto state = state_;
        Connection connection;
        connection.id = detail::next_connection_id();
        connection.target = &endpoint.object();
        connection.connected_at = endpoint.location();

        T* target = &endpoint.object();
        Method method = endpoint.method();

        connection.invoke = [target, method](Args... args) {
            (void)std::invoke(method, *target, args...);
        };

        connection.matches = [target, method](const detail::SlotQuery& query) {
            if (query.target != target || query.method_type != detail::type_tag<Method>()) {
                return false;
            }
            return method == *static_cast<const Method*>(query.method_value);
        };

        const auto connection_id = connection.id;
        state->connections.push_back(std::move(connection));

        try {
            Object::link_connection(*state->source, *target);
        } catch (...) {
            state->connections.pop_back();
            throw;
        }

        ++state->active_count;

        const auto query = make_query(endpoint);
        const auto multiplicity = connection_count(query);
        trace(TraceKind::connect,
              connection_id,
              target,
              endpoint.location(),
              endpoint.location(),
              DisconnectReason::explicit_remove,
              multiplicity);

        return connection_id;
    }

    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    bool add_unique(const SlotRef<T, Method>& endpoint) {
        if (connected(endpoint)) {
            return false;
        }
        add(endpoint);
        return true;
    }

    template <class T, class Method>
    [[nodiscard]] bool connected(const SlotRef<T, Method>& endpoint) const noexcept {
        return connection_count(endpoint) != 0;
    }

    template <class T, class Method>
    [[nodiscard]] std::size_t connection_count(const SlotRef<T, Method>& endpoint) const noexcept {
        return connection_count(make_query(endpoint));
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
                deactivate(state,
                           connection,
                           DisconnectReason::explicit_remove,
                           endpoint.location(),
                           0);
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
                deactivate(state,
                           connection,
                           DisconnectReason::explicit_remove,
                           endpoint.location(),
                           0);
                ++removed;
            }
        }
        compact_if_idle(state);
        return removed;
    }

    void clear() noexcept {
        auto state = state_;
        disconnect_all_impl(state, DisconnectReason::object_disconnect_all, {});
        compact_if_idle(state);
    }

    void operator()(Args... args,
                    std::source_location where = std::source_location::current()) {
        auto state = state_;
        if (!state || !state->alive) {
            return;
        }

        trace(TraceKind::emit, 0, nullptr, where, {}, DisconnectReason::explicit_remove, 0);

        ++state->emit_depth;
        const std::size_t limit = state->connections.size();

        try {
            for (std::size_t i = 0; i < limit; ++i) {
                if (!state->alive) {
                    break;
                }

                if (i >= state->connections.size()) {
                    break;
                }

                auto& connection = state->connections[i];
                if (!connection.active) {
                    continue;
                }

                // Copy what is needed before invoking. A nested connection can
                // grow the vector and invalidate references, and a slot can
                // disconnect itself or another slot.
                const auto invoke = connection.invoke;
                Object* target = connection.target;
                const auto connection_id = connection.id;
                const auto connected_at = connection.connected_at;

                trace(TraceKind::invoke,
                      connection_id,
                      target,
                      where,
                      connected_at,
                      DisconnectReason::explicit_remove,
                      0);

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

    [[nodiscard]] std::size_t size() const noexcept override {
        return state_->active_count;
    }

    [[nodiscard]] std::size_t connection_count_to(const Object* target) const noexcept override {
        std::size_t count = 0;
        for (const auto& connection : state_->connections) {
            if (connection.active && connection.target == target) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::vector<ConnectionInfo> connections() const override {
        std::vector<ConnectionInfo> result;
        result.reserve(state_->active_count);
        for (const auto& connection : state_->connections) {
            if (!connection.active) {
                continue;
            }
            result.push_back({
                connection.id,
                {connection.target->id(), connection.target->display_name()},
                SourcePoint::from(connection.connected_at),
            });
        }
        return result;
    }

private:
    template <class T, class Method>
    static detail::SlotQuery make_query(const SlotRef<T, Method>& endpoint) noexcept {
        return {
            &endpoint.object(),
            detail::type_tag<Method>(),
            &endpoint.method_,
        };
    }

    [[nodiscard]] std::size_t connection_count(const detail::SlotQuery& query) const noexcept override {
        std::size_t count = 0;
        for (const auto& connection : state_->connections) {
            if (connection.active && connection.matches(query)) {
                ++count;
            }
        }
        return count;
    }

    void disconnect_target(Object* target, DisconnectReason reason) noexcept override {
        auto state = state_;
        for (auto& connection : state->connections) {
            if (connection.active && connection.target == target) {
                deactivate(state, connection, reason, {}, 0);
            }
        }
        compact_if_idle(state);
    }

    void disconnect_all(DisconnectReason reason) noexcept override {
        auto state = state_;
        disconnect_all_impl(state, reason, {});
        compact_if_idle(state);
    }

    void disconnect_all_impl(const std::shared_ptr<State>& state,
                             DisconnectReason reason,
                             std::source_location where) noexcept {
        for (auto& connection : state->connections) {
            if (connection.active) {
                deactivate(state, connection, reason, where, 0);
            }
        }
    }

    void deactivate(const std::shared_ptr<State>& state,
                    Connection& connection,
                    DisconnectReason reason,
                    std::source_location where,
                    std::size_t multiplicity) noexcept {
        if (!connection.active) {
            return;
        }

        Object* source = state->source;
        Object* target = connection.target;
        const auto connection_id = connection.id;
        const auto connected_at = connection.connected_at;

        connection.active = false;
        if (state->active_count > 0) {
            --state->active_count;
        }

        trace(TraceKind::disconnect,
              connection_id,
              target,
              where,
              connected_at,
              reason,
              multiplicity);

        Object::unlink_connection(*source, *target);
    }

    static void compact_if_idle(const std::shared_ptr<State>& state) noexcept {
        if (state->emit_depth != 0) {
            return;
        }
        std::erase_if(state->connections, [](const Connection& connection) {
            return !connection.active;
        });
    }

    void trace(TraceKind kind,
               std::uint64_t connection_id,
               Object* target,
               std::source_location site,
               std::source_location connected_at,
               DisconnectReason reason,
               std::size_t multiplicity) const {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        if (detail::active_inspector() == nullptr || !detail::active_inspector()->enabled()) {
            return;
        }

        TraceRecord record;
        record.kind = kind;
        record.disconnect_reason = reason;
        record.signal_id = id();
        record.connection_id = connection_id;
        record.source_object_id = owner().id();
        record.source_object_name = owner().display_name();
        record.signal_name = std::string(name());
        record.site = SourcePoint::from(site);
        record.connected_at = SourcePoint::from(connected_at);
        record.multiplicity = multiplicity;

        if (target != nullptr) {
            record.target_object_id = target->id();
            record.target_object_name = target->display_name();
        }

        detail::submit_trace(std::move(record));
#else
        (void)kind;
        (void)connection_id;
        (void)target;
        (void)site;
        (void)connected_at;
        (void)reason;
        (void)multiplicity;
#endif
    }

    std::shared_ptr<State> state_;
};

template <class T, class Method>
std::vector<SignalMatch> Inspector::signals_for(const SlotRef<T, Method>& endpoint) const {
    const detail::SlotQuery query{
        &endpoint.object(),
        detail::type_tag<Method>(),
        &endpoint.method_,
    };

    std::vector<SignalMatch> result;
    Object& target = endpoint.object();

    auto inspect_source = [&](Object& source) {
        for (auto* signal : source.signal_manager_.signals_) {
            const auto count = signal->connection_count(query);
            if (count != 0) {
                result.push_back({
                    signal->id(),
                    {source.id(), source.display_name()},
                    std::string(signal->name()),
                    count,
                });
            }
        }
    };

    // Self-connections are intentionally not represented in the peer list.
    inspect_source(target);
    for (const auto& peer : target.peers_) {
        inspect_source(*peer.object);
    }

    return result;
}

} // namespace sss

#endif // SIMPLE_SIGNALS_DISABLE_TRACE

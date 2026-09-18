#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
#include <cstdint>
#include <ostream>
#include <source_location>
#include <string>
#include <string_view>
#endif

namespace sss {

class Object;
class SignalBase;
template <class... Args> class Signal;
template <class T, class Method> class SlotRef;
template <class SignalMemberPtr, class TargetMemberPtr, class Method> struct StaticConnection;
template <class... Connections> class StaticConnections;

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
class Inspector;

enum class TraceKind { connect, disconnect, emit, invoke };
enum class DisconnectReason {
    explicit_remove,
    target_destroyed,
    signal_destroyed,
    object_disconnect_all,
};
enum class ConnectionKind { static_connection, dynamic_connection };

struct SourcePoint {
    std::string file;
    std::string function;
    std::uint_least32_t line = 0;
    std::uint_least32_t column = 0;

    static SourcePoint from(std::source_location where) {
        if (where.line() == 0) return {};
        return {where.file_name(), where.function_name(), where.line(), where.column()};
    }
    [[nodiscard]] bool valid() const noexcept { return line != 0; }
};

struct TraceRecord {
    TraceKind kind = TraceKind::emit;
    DisconnectReason disconnect_reason = DisconnectReason::explicit_remove;
    ConnectionKind connection_kind = ConnectionKind::dynamic_connection;
    std::uint64_t signal_id = 0;
    std::uint64_t connection_id = 0; // dynamic only; static uses static_index
    std::size_t static_index = 0;
    std::uint64_t source_object_id = 0;
    std::uint64_t target_object_id = 0;
    std::string signal_name;
    std::string source_object_name;
    std::string target_object_name;
    SourcePoint site;
    SourcePoint connected_at;
    std::size_t multiplicity = 0;
};

struct ObjectInfo {
    std::uint64_t id = 0;
    std::string name;
};
struct ConnectionInfo {
    ConnectionKind kind = ConnectionKind::dynamic_connection;
    std::uint64_t connection_id = 0;
    std::size_t static_index = 0;
    ObjectInfo target;
    SourcePoint connected_at;
};
struct SignalMatch {
    std::uint64_t signal_id = 0;
    ObjectInfo source;
    std::string signal_name;
    std::size_t count = 0;
    std::size_t static_count = 0;
    std::size_t dynamic_count = 0;
};
#endif

namespace detail {

template <class T> struct member_pointer_class;
template <class M, class C> struct member_pointer_class<M C::*> { using type = C; };
template <class T> using member_pointer_class_t = typename member_pointer_class<T>::type;

template <class T> struct member_pointer_value;
template <class M, class C> struct member_pointer_value<M C::*> { using type = M; };
template <class T> using member_pointer_value_t = typename member_pointer_value<T>::type;

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

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
inline std::uint64_t next_object_id() noexcept { static std::uint64_t v = 1; return v++; }
inline std::uint64_t next_signal_id() noexcept { static std::uint64_t v = 1; return v++; }
inline std::uint64_t next_connection_id() noexcept { static std::uint64_t v = 1; return v++; }
Inspector*& active_inspector() noexcept;
void submit_trace(TraceRecord record);
Object*& object_list_head() noexcept;
#endif

template <class T>
concept HasStaticConnections = requires { T::static_connections; };


} // namespace detail

class SignalManager {
public:
    SignalManager() = default;
    SignalManager(const SignalManager&) = delete;
    SignalManager& operator=(const SignalManager&) = delete;

private:
    friend class Object;
    friend class SignalBase;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    friend class Inspector;
#endif
    template <class...> friend class Signal;

    void register_signal(SignalBase* signal) noexcept;
    void unregister_signal(SignalBase* signal) noexcept;
    void disconnect_target(Object* target
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                           , DisconnectReason reason
#endif
    ) noexcept;
    void disconnect_all_outgoing(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        DisconnectReason reason
#endif
    ) noexcept;

    SignalBase* head_ = nullptr;
};

class Object {
public:
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    explicit Object(std::string debug_name = {});
#else
    explicit Object(const char* = nullptr) noexcept {}
#endif
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    void disconnect_all() noexcept;

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] std::string_view debug_name() const noexcept { return debug_name_; }
    void set_debug_name(std::string name) { debug_name_ = std::move(name); }
#endif

private:
    friend class SignalManager;
    friend class SignalBase;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    friend class Inspector;
#endif
    template <class...> friend class Signal;
    template <class, class, class> friend struct StaticConnection;

    struct Peer { Object* object = nullptr; std::size_t refs = 0; };

    static auto find_peer(std::vector<Peer>& peers, Object* object) {
        return std::find_if(peers.begin(), peers.end(), [object](const Peer& p) { return p.object == object; });
    }
    static void link_dynamic(Object& a, Object& b);
    static void unlink_dynamic(Object& a, Object& b) noexcept;

    void disconnect_all_impl(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        DisconnectReason reason
#endif
    ) noexcept;

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    [[nodiscard]] std::string display_name() const {
        return debug_name_.empty() ? "object#" + std::to_string(id_) : debug_name_;
    }
    std::uint64_t id_ = 0;
    std::string debug_name_;
    Object* inspector_prev_ = nullptr;
    Object* inspector_next_ = nullptr;
#endif
    SignalManager signal_manager_;
    std::vector<Peer> dynamic_peers_; // remains empty/allocation-free for static-only wiring
    bool disconnecting_ = false;
};

class SignalBase {
public:
    virtual ~SignalBase();
    SignalBase(const SignalBase&) = delete;
    SignalBase& operator=(const SignalBase&) = delete;
    SignalBase(SignalBase&&) = delete;
    SignalBase& operator=(SignalBase&&) = delete;

    [[nodiscard]] Object& owner() const noexcept { return *owner_; }

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] virtual std::size_t connection_count(const detail::SlotQuery& query) const noexcept = 0;
    [[nodiscard]] virtual std::size_t dynamic_connection_count(const detail::SlotQuery& query) const noexcept = 0;
    [[nodiscard]] virtual std::vector<ConnectionInfo> connections() const = 0;
#endif

protected:
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    SignalBase(Object& owner, std::string name)
        : owner_(&owner), id_(detail::next_signal_id()), name_(std::move(name)) {
        owner_->signal_manager_.register_signal(this);
    }
#else
    SignalBase(Object& owner, const char* = nullptr) : owner_(&owner) {
        owner_->signal_manager_.register_signal(this);
    }
#endif

private:
    friend class SignalManager;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    friend class Inspector;
#endif
    template <class...> friend class Signal;

    virtual void disconnect_dynamic_target(Object* target
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                           , DisconnectReason reason
#endif
    ) noexcept = 0;
    virtual void disconnect_all_dynamic(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        DisconnectReason reason
#endif
    ) noexcept = 0;

    Object* owner_ = nullptr;
    SignalBase* manager_prev_ = nullptr;
    SignalBase* manager_next_ = nullptr;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    std::uint64_t id_ = 0;
    std::string name_;
#endif
};

inline void SignalManager::register_signal(SignalBase* signal) noexcept {
    signal->manager_prev_ = nullptr;
    signal->manager_next_ = head_;
    if (head_) head_->manager_prev_ = signal;
    head_ = signal;
}
inline void SignalManager::unregister_signal(SignalBase* signal) noexcept {
    if (signal->manager_prev_) signal->manager_prev_->manager_next_ = signal->manager_next_;
    else if (head_ == signal) head_ = signal->manager_next_;
    if (signal->manager_next_) signal->manager_next_->manager_prev_ = signal->manager_prev_;
    signal->manager_prev_ = signal->manager_next_ = nullptr;
}
inline void SignalManager::disconnect_target(Object* target
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                             , DisconnectReason reason
#endif
) noexcept {
    for (auto* signal = head_; signal != nullptr;) {
        auto* next = signal->manager_next_;
        signal->disconnect_dynamic_target(target
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                          , reason
#endif
        );
        signal = next;
    }
}
inline void SignalManager::disconnect_all_outgoing(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    DisconnectReason reason
#endif
) noexcept {
    for (auto* signal = head_; signal != nullptr;) {
        auto* next = signal->manager_next_;
        signal->disconnect_all_dynamic(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
            reason
#endif
        );
        signal = next;
    }
}

inline SignalBase::~SignalBase() {
    if (owner_) owner_->signal_manager_.unregister_signal(this);
}

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
inline Object*& detail::object_list_head() noexcept { static Object* head = nullptr; return head; }
inline Object::Object(std::string debug_name)
    : id_(detail::next_object_id()), debug_name_(std::move(debug_name)) {
    inspector_next_ = detail::object_list_head();
    if (inspector_next_) inspector_next_->inspector_prev_ = this;
    detail::object_list_head() = this;
}
#endif

inline void Object::link_dynamic(Object& a, Object& b) {
    if (&a == &b) return;
    auto ai = find_peer(a.dynamic_peers_, &b);
    auto bi = find_peer(b.dynamic_peers_, &a);
    if (ai == a.dynamic_peers_.end()) a.dynamic_peers_.reserve(a.dynamic_peers_.size() + 1);
    if (bi == b.dynamic_peers_.end()) b.dynamic_peers_.reserve(b.dynamic_peers_.size() + 1);
    ai = find_peer(a.dynamic_peers_, &b);
    bi = find_peer(b.dynamic_peers_, &a);
    if (ai == a.dynamic_peers_.end()) a.dynamic_peers_.push_back({&b, 1}); else ++ai->refs;
    if (bi == b.dynamic_peers_.end()) b.dynamic_peers_.push_back({&a, 1}); else ++bi->refs;
}
inline void Object::unlink_dynamic(Object& a, Object& b) noexcept {
    if (&a == &b) return;
    auto erase_one = [](Object& owner, Object* other) {
        auto it = find_peer(owner.dynamic_peers_, other);
        if (it == owner.dynamic_peers_.end()) return;
        if (it->refs > 1) --it->refs; else owner.dynamic_peers_.erase(it);
    };
    erase_one(a, &b); erase_one(b, &a);
}
inline void Object::disconnect_all_impl(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    DisconnectReason reason
#endif
) noexcept {
    if (disconnecting_) return;
    disconnecting_ = true;
    signal_manager_.disconnect_all_outgoing(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        reason
#endif
    );
    while (!dynamic_peers_.empty()) {
        Object* peer = dynamic_peers_.back().object;
        const auto before = dynamic_peers_.size();
        peer->signal_manager_.disconnect_target(this
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                                , reason
#endif
        );
        if (dynamic_peers_.size() == before && !dynamic_peers_.empty() && dynamic_peers_.back().object == peer) {
            auto it = find_peer(dynamic_peers_, peer);
            if (it != dynamic_peers_.end()) dynamic_peers_.erase(it);
            auto other = find_peer(peer->dynamic_peers_, this);
            if (other != peer->dynamic_peers_.end()) peer->dynamic_peers_.erase(other);
        }
    }
    disconnecting_ = false;
}
inline void Object::disconnect_all() noexcept {
    disconnect_all_impl(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        DisconnectReason::object_disconnect_all
#endif
    );
}
inline Object::~Object() {
    disconnect_all_impl(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        DisconnectReason::target_destroyed
#endif
    );
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    if (inspector_prev_) inspector_prev_->inspector_next_ = inspector_next_;
    else if (detail::object_list_head() == this) detail::object_list_head() = inspector_next_;
    if (inspector_next_) inspector_next_->inspector_prev_ = inspector_prev_;
#endif
}

// Runtime slot endpoint -------------------------------------------------------
template <class T, class Method>
class SlotRef {
public:
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    SlotRef(T& object, Method method, std::source_location where)
        : object_(&object), method_(method), where_(where) {}
#else
    SlotRef(T& object, Method method) : object_(&object), method_(method) {}
#endif
    [[nodiscard]] T& object() const noexcept { return *object_; }
    [[nodiscard]] Method method() const noexcept { return method_; }
private:
    template <class...> friend class Signal;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    friend class Inspector;
#endif
    T* object_ = nullptr;
    Method method_{};
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    std::source_location where_;
#endif
};

template <class T, class Method>
    requires std::derived_from<T, Object> && std::is_member_function_pointer_v<Method>
auto slot(T& object, Method method
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
          , std::source_location where = std::source_location::current()
#endif
) -> SlotRef<T, Method> {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    return {object, method, where};
#else
    return {object, method};
#endif
}

// Compile-time topology ------------------------------------------------------
template <class SignalMemberPtr, class TargetMemberPtr, class Method>
struct StaticConnection {
    SignalMemberPtr signal_member{};
    TargetMemberPtr target_member{};
    Method method{};
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    std::source_location where{};
#endif

    using owner_type = detail::member_pointer_class_t<SignalMemberPtr>;
    using target_owner_type = detail::member_pointer_class_t<TargetMemberPtr>;
    using target_type = std::remove_cv_t<detail::member_pointer_value_t<TargetMemberPtr>>;

    static_assert(std::is_same_v<owner_type, target_owner_type>,
                  "static signal and target members must belong to the same owner type");
    static_assert(std::derived_from<target_type, Object>,
                  "static connection target must be an Object member");
    static_assert(std::is_member_function_pointer_v<Method>,
                  "static connection endpoint must be a member function");

    template <class... Args>
    void emit(owner_type& owner, Signal<Args...>& signal, std::size_t static_index
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
              , std::source_location emitted_at
#endif
              , Args... args) const {
        using source_signal_type = std::remove_cv_t<detail::member_pointer_value_t<SignalMemberPtr>>;
        if constexpr (std::is_same_v<source_signal_type, Signal<Args...>>) {
            auto& selected = owner.*signal_member;
            if (static_cast<void*>(&selected) != static_cast<void*>(&signal)) return;
            static_assert(std::invocable<Method, target_type&, std::add_lvalue_reference_t<Args>...>,
                          "static slot cannot be invoked with this signal's arguments");
            auto& target = owner.*target_member;
            signal.template invoke_static<target_type>(target, method, static_index
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                                       , where, emitted_at
#endif
                                                       , args...);
        }
    }

    template <class... Args>
    std::size_t count_slot(const owner_type& owner, const Signal<Args...>& signal,
                           const detail::SlotQuery& query) const noexcept {
        using source_signal_type = std::remove_cv_t<detail::member_pointer_value_t<SignalMemberPtr>>;
        if constexpr (!std::is_same_v<source_signal_type, Signal<Args...>>) {
            return 0;
        } else {
            const auto& selected = owner.*signal_member;
            if (static_cast<const void*>(&selected) != static_cast<const void*>(&signal)) return 0;
            const auto& target = owner.*target_member;
            if (query.target != &target || query.method_type != detail::type_tag<Method>()) return 0;
            return method == *static_cast<const Method*>(query.method_value) ? 1u : 0u;
        }
    }

    template <class... Args>
    std::size_t count_for_signal(const owner_type& owner, const Signal<Args...>& signal) const noexcept {
        using source_signal_type = std::remove_cv_t<detail::member_pointer_value_t<SignalMemberPtr>>;
        if constexpr (!std::is_same_v<source_signal_type, Signal<Args...>>) {
            return 0;
        } else {
            const auto& selected = owner.*signal_member;
            return static_cast<const void*>(&selected) == static_cast<const void*>(&signal) ? 1u : 0u;
        }
    }

    template <class... Args>
    std::size_t count_target(const owner_type& owner, const Signal<Args...>& signal,
                             const Object* target_object) const noexcept {
        using source_signal_type = std::remove_cv_t<detail::member_pointer_value_t<SignalMemberPtr>>;
        if constexpr (!std::is_same_v<source_signal_type, Signal<Args...>>) {
            return 0;
        } else {
            const auto& selected = owner.*signal_member;
            if (static_cast<const void*>(&selected) != static_cast<const void*>(&signal)) return 0;
            const auto& target = owner.*target_member;
            return &target == target_object ? 1u : 0u;
        }
    }

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    template <class... Args>
    void append_connection(const owner_type& owner, const Signal<Args...>& signal,
                           std::size_t static_index, std::vector<ConnectionInfo>& out) const {
        using source_signal_type = std::remove_cv_t<detail::member_pointer_value_t<SignalMemberPtr>>;
        if constexpr (std::is_same_v<source_signal_type, Signal<Args...>>) {
            const auto& selected = owner.*signal_member;
            if (static_cast<const void*>(&selected) != static_cast<const void*>(&signal)) return;
            const auto& target = owner.*target_member;
            out.push_back({ConnectionKind::static_connection, 0, static_index,
                           {target.id(), target.display_name()}, SourcePoint::from(where)});
        }
    }
#endif
};

template <class SignalMemberPtr, class TargetMemberPtr, class Method>
    requires std::is_member_object_pointer_v<SignalMemberPtr> &&
             std::is_member_object_pointer_v<TargetMemberPtr> &&
             std::is_member_function_pointer_v<Method>
consteval auto connect(SignalMemberPtr signal_member, TargetMemberPtr target_member, Method method
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                       , std::source_location where = std::source_location::current()
#endif
) {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    return StaticConnection<SignalMemberPtr, TargetMemberPtr, Method>{signal_member, target_member, method, where};
#else
    return StaticConnection<SignalMemberPtr, TargetMemberPtr, Method>{signal_member, target_member, method};
#endif
}

template <class... Connections>
class StaticConnections {
public:
    constexpr explicit StaticConnections(Connections... connections)
        : connections_(std::move(connections)...) {}

    template <auto SignalMember>
    [[nodiscard]] consteval std::size_t count() const {
        std::size_t result = 0;
        std::apply([&](const auto&... connection) {
            (([&] {
                using member_type = std::remove_cv_t<decltype(connection.signal_member)>;
                using query_type = std::remove_cv_t<decltype(SignalMember)>;
                if constexpr (std::is_same_v<member_type, query_type>) {
                    if (connection.signal_member == SignalMember) ++result;
                }
            }()), ...);
        }, connections_);
        return result;
    }

    template <class Owner, class... Args>
    void emit(Owner& owner, Signal<Args...>& signal
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
              , std::source_location emitted_at
#endif
              , Args... args) const {
        std::size_t index = 0;
        std::apply([&](const auto&... connection) {
            ((connection.emit(owner, signal, index++
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                              , emitted_at
#endif
                              , args...)), ...);
        }, connections_);
    }

    template <class Owner, class... Args>
    [[nodiscard]] std::size_t count_slot(const Owner& owner, const Signal<Args...>& signal,
                                         const detail::SlotQuery& query) const noexcept {
        std::size_t count = 0;
        std::apply([&](const auto&... connection) {
            ((count += connection.count_slot(owner, signal, query)), ...);
        }, connections_);
        return count;
    }

    template <class Owner, class... Args>
    [[nodiscard]] std::size_t count_for_signal(const Owner& owner, const Signal<Args...>& signal) const noexcept {
        std::size_t count = 0;
        std::apply([&](const auto&... connection) {
            ((count += connection.count_for_signal(owner, signal)), ...);
        }, connections_);
        return count;
    }

    template <class Owner, class... Args>
    [[nodiscard]] std::size_t count_target(const Owner& owner, const Signal<Args...>& signal,
                                           const Object* target) const noexcept {
        std::size_t count = 0;
        std::apply([&](const auto&... connection) {
            ((count += connection.count_target(owner, signal, target)), ...);
        }, connections_);
        return count;
    }

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    template <class Owner, class... Args>
    void append_connections(const Owner& owner, const Signal<Args...>& signal,
                            std::vector<ConnectionInfo>& out) const {
        std::size_t index = 0;
        std::apply([&](const auto&... connection) {
            ((connection.append_connection(owner, signal, index++, out)), ...);
        }, connections_);
    }
#endif

private:
    std::tuple<Connections...> connections_;
};

template <class... Connections>
constexpr auto static_connections(Connections... connections) {
    return StaticConnections<Connections...>(std::move(connections)...);
}

// Signal ---------------------------------------------------------------------
template <class... Args>
class Signal final : public SignalBase {
    static_assert((!std::is_rvalue_reference_v<Args> && ...),
                  "Signal does not support rvalue-reference parameters");
    static_assert(((std::is_reference_v<Args> || std::copy_constructible<Args>) && ...),
                  "By-value Signal parameters must be copy constructible because a signal is multicast");

    struct DynamicConnection {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        std::uint64_t id = 0;
        std::source_location connected_at{};
#endif
        Object* target = nullptr;
        std::function<void(Args...)> invoke;
        std::function<bool(const detail::SlotQuery&)> matches;
        bool active = true;
    };

    using StaticEmitFn = void(*)(Object&, Signal&, Args...
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                 , std::source_location
#endif
                                 );
    using StaticCountFn = std::size_t(*)(const Object&, const Signal&, const detail::SlotQuery&);
    using StaticSizeFn = std::size_t(*)(const Object&, const Signal&);
    using StaticTargetCountFn = std::size_t(*)(const Object&, const Signal&, const Object*);
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    using StaticListFn = void(*)(const Object&, const Signal&, std::vector<ConnectionInfo>&);
#endif

public:
    template <class Owner>
        requires std::derived_from<Owner, Object>
    explicit Signal(Owner& owner
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                    , std::string name = {}
#else
                    , const char* name = nullptr
#endif
    )
        : SignalBase(owner, std::move(name)) {
        static_emit_ = [](Object& base, Signal& signal, Args... args
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                          , std::source_location emitted_at
#endif
        ) {
            if constexpr (detail::HasStaticConnections<Owner>) {
                Owner::static_connections.emit(static_cast<Owner&>(base), signal
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                            , emitted_at
#endif
                                            , args...);
            }
        };
        static_count_ = [](const Object& base, const Signal& signal, const detail::SlotQuery& query) -> std::size_t {
            if constexpr (detail::HasStaticConnections<Owner>) {
                return Owner::static_connections.count_slot(static_cast<const Owner&>(base), signal, query);
            }
            return 0;
        };
        static_size_ = [](const Object& base, const Signal& signal) -> std::size_t {
            if constexpr (detail::HasStaticConnections<Owner>) {
                return Owner::static_connections.count_for_signal(static_cast<const Owner&>(base), signal);
            }
            return 0;
        };
        static_target_count_ = [](const Object& base, const Signal& signal, const Object* target) -> std::size_t {
            if constexpr (detail::HasStaticConnections<Owner>) {
                return Owner::static_connections.count_target(static_cast<const Owner&>(base), signal, target);
            }
            return 0;
        };
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        static_list_ = [](const Object& base, const Signal& signal, std::vector<ConnectionInfo>& out) {
            if constexpr (detail::HasStaticConnections<Owner>) {
                Owner::static_connections.append_connections(static_cast<const Owner&>(base), signal, out);
            }
        };
#endif
    }

    ~Signal() override {
        disconnect_all_dynamic(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
            DisconnectReason::signal_destroyed
#endif
        );
    }

    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    Signal& operator+=(const SlotRef<T, Method>& endpoint) { add(endpoint); return *this; }

    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    Signal& operator-=(const SlotRef<T, Method>& endpoint) noexcept { remove_one(endpoint); return *this; }

    template <class T, class Method>
        requires std::derived_from<T, Object> &&
                 std::invocable<Method, T&, std::add_lvalue_reference_t<Args>...>
    void add(const SlotRef<T, Method>& endpoint) {
        DynamicConnection connection;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        connection.id = detail::next_connection_id();
        connection.connected_at = endpoint.where_;
#endif
        connection.target = &endpoint.object();
        T* target = &endpoint.object();
        Method method = endpoint.method();
        connection.invoke = [target, method](Args... args) { (void)std::invoke(method, *target, args...); };
        connection.matches = [target, method](const detail::SlotQuery& query) {
            if (query.target != target || query.method_type != detail::type_tag<Method>()) return false;
            return method == *static_cast<const Method*>(query.method_value);
        };
        dynamic_.push_back(std::move(connection));
        try { Object::link_dynamic(owner(), *target); }
        catch (...) { dynamic_.pop_back(); throw; }
        ++dynamic_active_;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        const auto q = make_query(endpoint);
        trace_dynamic(TraceKind::connect, dynamic_.back(), endpoint.where_, connection_count_dynamic(q));
#endif
    }

    template <class T, class Method>
    bool add_unique(const SlotRef<T, Method>& endpoint) {
        if (connected(endpoint)) return false;
        add(endpoint); return true;
    }
    template <class T, class Method>
    [[nodiscard]] bool connected(const SlotRef<T, Method>& endpoint) const noexcept {
        return connection_count(endpoint) != 0;
    }
    template <class T, class Method>
    [[nodiscard]] std::size_t connection_count(const SlotRef<T, Method>& endpoint) const noexcept {
        const auto q = make_query(endpoint);
        return connection_count_dynamic(q) + (static_count_ ? static_count_(owner(), *this, q) : 0);
    }
    template <class T, class Method>
    [[nodiscard]] bool has_duplicate(const SlotRef<T, Method>& endpoint) const noexcept {
        return connection_count(endpoint) > 1;
    }
    template <class T, class Method>
    bool remove_one(const SlotRef<T, Method>& endpoint) noexcept {
        const auto q = make_query(endpoint);
        for (std::size_t i = dynamic_.size(); i > 0; --i) {
            auto& c = dynamic_[i - 1];
            if (c.active && c.matches(q)) {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                const auto where = endpoint.where_;
                deactivate_dynamic(c, DisconnectReason::explicit_remove, where);
#else
                deactivate_dynamic(c);
#endif
                compact_if_idle(); return true;
            }
        }
        return false;
    }
    template <class T, class Method>
    std::size_t remove_all(const SlotRef<T, Method>& endpoint) noexcept {
        const auto q = make_query(endpoint); std::size_t removed = 0;
        for (auto& c : dynamic_) if (c.active && c.matches(q)) {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
            deactivate_dynamic(c, DisconnectReason::explicit_remove, endpoint.where_);
#else
            deactivate_dynamic(c);
#endif
            ++removed;
        }
        compact_if_idle(); return removed;
    }
    void clear() noexcept {
        disconnect_all_dynamic(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
            DisconnectReason::object_disconnect_all
#endif
        );
    }

    void operator()(Args... args
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                    , std::source_location where = std::source_location::current()
#endif
    ) {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        trace_emit(where);
#endif
        ++emit_depth_;
        const std::size_t dynamic_limit = dynamic_.size(); // additions anywhere in this emission wait
        try {
            static_emit_(owner(), *this, args...
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                         , where
#endif
            );
            for (std::size_t i = 0; i < dynamic_limit && i < dynamic_.size(); ++i) {
                auto& c = dynamic_[i];
                if (!c.active) continue;
                const auto invoke = c.invoke;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                trace_dynamic(TraceKind::invoke, c, where, 0);
#endif
                invoke(args...);
            }
        } catch (...) {
            --emit_depth_; compact_if_idle(); throw;
        }
        --emit_depth_; compact_if_idle();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return dynamic_active_ + (static_size_ ? static_size_(owner(), *this) : 0);
    }
    [[nodiscard]] std::size_t dynamic_size() const noexcept { return dynamic_active_; }
    [[nodiscard]] std::size_t connection_count_to(const Object* target) const noexcept {
        std::size_t count = 0;
        for (const auto& c : dynamic_) if (c.active && c.target == target) ++count;
        if (static_target_count_) count += static_target_count_(owner(), *this, target);
        return count;
    }

    template <class Target, class Method>
    void invoke_static(Target& target, Method method, std::size_t static_index
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                       , std::source_location connected_at, std::source_location emitted_at
#endif
                       , Args... args) {
#ifdef SIMPLE_SIGNALS_DISABLE_TRACE
        (void)static_index;
#else
        trace_static_invoke(target, static_index, connected_at, emitted_at);
#endif
        (void)std::invoke(method, target, args...);
    }

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    [[nodiscard]] std::size_t connection_count(const detail::SlotQuery& query) const noexcept override {
        return connection_count_dynamic(query) + (static_count_ ? static_count_(owner(), *this, query) : 0);
    }
    [[nodiscard]] std::size_t dynamic_connection_count(const detail::SlotQuery& query) const noexcept override {
        return connection_count_dynamic(query);
    }
    [[nodiscard]] std::vector<ConnectionInfo> connections() const override {
        std::vector<ConnectionInfo> out;
        if (static_list_) static_list_(owner(), *this, out);
        for (const auto& c : dynamic_) if (c.active) {
            out.push_back({ConnectionKind::dynamic_connection, c.id, 0,
                           {c.target->id(), c.target->display_name()}, SourcePoint::from(c.connected_at)});
        }
        return out;
    }
#endif

private:
    template <class T, class Method>
    static detail::SlotQuery make_query(const SlotRef<T, Method>& endpoint) noexcept {
        return {&endpoint.object(), detail::type_tag<Method>(), &endpoint.method_};
    }
    [[nodiscard]] std::size_t connection_count_dynamic(const detail::SlotQuery& query) const noexcept {
        std::size_t n = 0;
        for (const auto& c : dynamic_) if (c.active && c.matches(query)) ++n;
        return n;
    }
    void disconnect_dynamic_target(Object* target
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                   , DisconnectReason reason
#endif
    ) noexcept override {
        for (auto& c : dynamic_) if (c.active && c.target == target) {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
            deactivate_dynamic(c, reason, {});
#else
            deactivate_dynamic(c);
#endif
        }
        compact_if_idle();
    }
    void disconnect_all_dynamic(
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        DisconnectReason reason
#endif
    ) noexcept override {
        for (auto& c : dynamic_) if (c.active) {
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
            deactivate_dynamic(c, reason, {});
#else
            deactivate_dynamic(c);
#endif
        }
        compact_if_idle();
    }
    void deactivate_dynamic(DynamicConnection& c
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                            , DisconnectReason reason, std::source_location where
#endif
    ) noexcept {
        if (!c.active) return;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
        trace_dynamic(TraceKind::disconnect, c, where, 0, reason);
#endif
        c.active = false;
        if (dynamic_active_) --dynamic_active_;
        Object::unlink_dynamic(owner(), *c.target);
    }
    void compact_if_idle() noexcept {
        if (emit_depth_ != 0) return;
        std::erase_if(dynamic_, [](const auto& c) { return !c.active; });
    }

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    void trace_emit(std::source_location where) {
        TraceRecord r;
        r.kind = TraceKind::emit; r.signal_id = id(); r.source_object_id = owner().id();
        r.signal_name = std::string(name()); r.source_object_name = owner().display_name();
        r.site = SourcePoint::from(where);
        detail::submit_trace(std::move(r));
    }
    void trace_dynamic(TraceKind kind, const DynamicConnection& c, std::source_location where,
                       std::size_t multiplicity,
                       DisconnectReason reason = DisconnectReason::explicit_remove) const {
        TraceRecord r;
        r.kind = kind; r.disconnect_reason = reason; r.connection_kind = ConnectionKind::dynamic_connection;
        r.signal_id = id(); r.connection_id = c.id; r.source_object_id = owner().id();
        r.target_object_id = c.target->id(); r.signal_name = std::string(name());
        r.source_object_name = owner().display_name(); r.target_object_name = c.target->display_name();
        r.site = SourcePoint::from(where); r.connected_at = SourcePoint::from(c.connected_at); r.multiplicity = multiplicity;
        detail::submit_trace(std::move(r));
    }
    template <class Target>
    void trace_static_invoke(Target& target, std::size_t static_index,
                             std::source_location connected_at, std::source_location emitted_at) const {
        TraceRecord r;
        r.kind = TraceKind::invoke; r.connection_kind = ConnectionKind::static_connection;
        r.signal_id = id(); r.static_index = static_index; r.source_object_id = owner().id();
        r.target_object_id = target.id(); r.signal_name = std::string(name());
        r.source_object_name = owner().display_name(); r.target_object_name = target.display_name();
        r.site = SourcePoint::from(emitted_at); r.connected_at = SourcePoint::from(connected_at);
        detail::submit_trace(std::move(r));
    }
#endif

    std::vector<DynamicConnection> dynamic_;
    std::size_t dynamic_active_ = 0;
    std::size_t emit_depth_ = 0;
    StaticEmitFn static_emit_ = [](Object&, Signal&, Args...
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
                                    , std::source_location
#endif
                                    ) {};
    StaticCountFn static_count_ = nullptr;
    StaticSizeFn static_size_ = nullptr;
    StaticTargetCountFn static_target_count_ = nullptr;
#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
    StaticListFn static_list_ = nullptr;
#endif
};

#ifndef SIMPLE_SIGNALS_DISABLE_TRACE
class Inspector {
public:
    Inspector() = default;
    ~Inspector() { deactivate(); }
    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void clear() { records_.clear(); }
    [[nodiscard]] const std::vector<TraceRecord>& records() const noexcept { return records_; }
    void activate() noexcept;
    void deactivate() noexcept;

    template <class T, class Method>
    [[nodiscard]] std::vector<SignalMatch> signals_for(const SlotRef<T, Method>& endpoint) const {
        const detail::SlotQuery q{&endpoint.object(), detail::type_tag<Method>(), &endpoint.method_};
        std::vector<SignalMatch> result;
        for (Object* object = detail::object_list_head(); object != nullptr; object = object->inspector_next_) {
            for (SignalBase* signal = object->signal_manager_.head_; signal != nullptr; signal = signal->manager_next_) {
                const auto total = signal->connection_count(q);
                if (!total) continue;
                const std::size_t dyn = signal->dynamic_connection_count(q);
                const std::size_t stat = total - dyn;
                result.push_back({signal->id(), {object->id(), object->display_name()},
                                  std::string(signal->name()), total, stat, dyn});
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<ObjectInfo> connected_objects(const Object& object) const {
        std::vector<ObjectInfo> out;
        auto add = [&](const Object& candidate) {
            if (&candidate == &object) return;
            if (std::none_of(out.begin(), out.end(), [&](const ObjectInfo& i) { return i.id == candidate.id(); }))
                out.push_back({candidate.id(), candidate.display_name()});
        };
        // Dynamic peers are exact and cheap.
        for (const auto& peer : object.dynamic_peers_) add(*peer.object);
        // Static edges are compile-time metadata, so discover them through the diagnostic graph.
        for (Object* source = detail::object_list_head(); source != nullptr; source = source->inspector_next_) {
            for (SignalBase* signal = source->signal_manager_.head_; signal != nullptr; signal = signal->manager_next_) {
                for (const auto& c : signal->connections()) {
                    if (source == &object) {
                        for (Object* candidate = detail::object_list_head(); candidate != nullptr; candidate = candidate->inspector_next_)
                            if (candidate->id() == c.target.id) add(*candidate);
                    } else if (c.target.id == object.id()) add(*source);
                }
            }
        }
        return out;
    }

    void dump(std::ostream& out) const;

private:
    friend void detail::submit_trace(TraceRecord record);
    void record(TraceRecord record) { if (enabled_) records_.push_back(std::move(record)); }
    bool enabled_ = true;
    std::vector<TraceRecord> records_;
};

namespace detail {
inline Inspector*& active_inspector() noexcept { static Inspector* value = nullptr; return value; }
inline void submit_trace(TraceRecord record) {
    if (auto* i = active_inspector(); i && i->enabled()) i->record(std::move(record));
}
}
inline void Inspector::activate() noexcept { detail::active_inspector() = this; }
inline void Inspector::deactivate() noexcept { if (detail::active_inspector() == this) detail::active_inspector() = nullptr; }
inline void set_active_inspector(Inspector* inspector) noexcept { detail::active_inspector() = inspector; }
inline Inspector* active_inspector() noexcept { return detail::active_inspector(); }
inline void Inspector::dump(std::ostream& out) const {
    auto kind = [](TraceKind k) { switch(k) { case TraceKind::connect:return "CONNECT"; case TraceKind::disconnect:return "DISCONNECT"; case TraceKind::emit:return "EMIT"; case TraceKind::invoke:return "INVOKE"; } return "?"; };
    for (const auto& r : records_) {
        out << kind(r.kind) << ' ' << r.source_object_name << '.' << r.signal_name;
        if (r.target_object_id) out << " -> " << r.target_object_name;
        if (r.connection_kind == ConnectionKind::static_connection && r.kind == TraceKind::invoke) out << " [static " << r.static_index << ']';
        else if (r.connection_id) out << " [connection " << r.connection_id << ']';
        if (r.site.valid()) out << " @ " << r.site.file << ':' << r.site.line;
        if (r.connected_at.valid() && r.kind == TraceKind::invoke) out << " [connected @ " << r.connected_at.file << ':' << r.connected_at.line << ']';
        out << '\n';
    }
}
#endif

} // namespace sss

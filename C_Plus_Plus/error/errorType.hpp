#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>


/**
* @author Bryce Hart @date Sept 21
* Requires C++17 or newer. 
* Basically tries to mirror the rust Result<> type


*/
namespace bstd {
namespace error {

/* A simple, general purpose error type: just a message.
 * Used as the default error type of Result<T>.                              */
class Error {
private:
    std::string _message;

public:
    Error() = default;
    Error(std::string message) : _message(std::move(message)) {}
    Error(const char* message) : _message(message) {}

    const std::string& message() const noexcept { return _message; }

    operator std::string_view() const noexcept { return _message; }
};

/* Thrown by unwrap()/expect()/unwrapErr() when called on the wrong variant. */
class BadResultAccess : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

namespace detail {

/* Tag wrappers so that Ok(x) and Err(x) are unambiguous, even when the
 * success type and the error type are the same (e.g. Result<string,string>). */
template <typename T>
struct OkValue {
    T value;
};
template <>
struct OkValue<void> {};

template <typename E>
struct ErrValue {
    E value;
};

template <typename E, typename = void>
struct HasMessage : std::false_type {};
template <typename E>
struct HasMessage<E, std::void_t<decltype(std::declval<const E&>().message())>>
    : std::true_type {};

/* Turn an error value into text for exception messages. */
template <typename E>
std::string describe(const E& e) {
    if constexpr (std::is_convertible_v<const E&, std::string_view>) {
        return std::string(std::string_view(e));
    } else if constexpr (HasMessage<E>::value) {
        return std::string(e.message());
    } else {
        return "(error value with no message)";
    }
}

}  // namespace detail

/* Construction helpers, mirroring Rust's Ok(..) / Err(..).
 *
 *   return Ok(42);        // Result<int>
 *   return Ok();          // Result<void>
 *   return Err("oops");   // Result<anything>
 */
template <typename T>
detail::OkValue<std::decay_t<T>> Ok(T&& value) {
    return {std::forward<T>(value)};
}
inline detail::OkValue<void> Ok() { return {}; }

template <typename E>
detail::ErrValue<std::decay_t<E>> Err(E&& error) {
    return {std::forward<E>(error)};
}

/* Result<T, E>: holds either a success value (T) or an error (E).
 * E defaults to bstd::error::Error. Use Result<void, E> for "succeeded or
 * failed with an error, but there is no value" (see specialization below).
 *
 * Marked [[nodiscard]] so silently ignoring a returned Result is a warning. */
template <typename T, typename E = Error>
class [[nodiscard]] Result {
    static_assert(!std::is_void_v<E>, "Result's error type cannot be void");
    static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>,
                  "Result cannot hold references");

private:
        //type and error
    std::variant<T, E> _data;

    template <typename Self>
    static decltype(auto) valueOf(Self&& self) {
        return std::get<0>(std::forward<Self>(self)._data);
    }
    template <typename Self>
    static decltype(auto) errorOf(Self&& self) {
        return std::get<1>(std::forward<Self>(self)._data);
    }

    [[noreturn]] void fail(std::string_view msg) const {
        throw BadResultAccess(std::string(msg) + ": " +
        detail::describe(std::get<1>(_data)));
    }

    template <typename Self, typename F>
    static auto mapImpl(Self&& self, F&& f) {
        using Arg = decltype(valueOf(std::forward<Self>(self)));
        using U = std::decay_t<std::invoke_result_t<F, Arg>>;
        if (self.isErr()) {
            return Result<U, E>(Err(errorOf(std::forward<Self>(self))));
        }
        if constexpr (std::is_void_v<U>) {
            std::invoke(std::forward<F>(f), valueOf(std::forward<Self>(self)));
            return Result<U, E>(Ok());
        } else {
            return Result<U, E>(
                Ok(std::invoke(std::forward<F>(f), valueOf(std::forward<Self>(self)))));
        }
    }

    template <typename Self, typename F>
    static auto mapErrImpl(Self&& self, F&& f) {
        using Arg = decltype(errorOf(std::forward<Self>(self)));
        using G = std::decay_t<std::invoke_result_t<F, Arg>>;
        if (self.isOk()) {
            return Result<T, G>(Ok(valueOf(std::forward<Self>(self))));
        }
        return Result<T, G>(
            Err(std::invoke(std::forward<F>(f), errorOf(std::forward<Self>(self)))));
    }

    template <typename Self, typename F>
    static auto andThenImpl(Self&& self, F&& f) {
        using Arg = decltype(valueOf(std::forward<Self>(self)));
        using R = std::decay_t<std::invoke_result_t<F, Arg>>;
        if (self.isErr()) {
            return R(Err(errorOf(std::forward<Self>(self))));
        }
        return std::invoke(std::forward<F>(f), valueOf(std::forward<Self>(self)));
    }

public:
    using value_type = T;
    using error_type = E;

    /* Implicit, so `return Ok(5);` / `return Err("x");` just work.
     * Ok(u) converts if T is constructible from u, Err(f) if E is from f.   */
    template <typename U, typename = std::enable_if_t<std::is_constructible_v<T, U&&>>>
    Result(detail::OkValue<U> ok) : _data(std::in_place_index<0>, std::move(ok.value)) {}

    template <typename F, typename = std::enable_if_t<std::is_constructible_v<E, F&&>>>
    Result(detail::ErrValue<F> err) : _data(std::in_place_index<1>, std::move(err.value)) {}

    /* ---- state ---------------------------------------------------------- */

    bool isOk() const noexcept { return _data.index() == 0; }
    bool isErr() const noexcept { return _data.index() == 1; }
    explicit operator bool() const noexcept { return isOk(); }

    /* ---- extracting the value ------------------------------------------- */

    /* Returns the value, or throws BadResultAccess("<msg>: <error text>").  */
    T& expect(std::string_view msg) & {
        if (isErr()) fail(msg);
        return *std::get_if<0>(&_data);
    }
    const T& expect(std::string_view msg) const& {
        if (isErr()) fail(msg);
        return *std::get_if<0>(&_data);
    }
    T&& expect(std::string_view msg) && {
        if (isErr()) fail(msg);
        return std::move(*std::get_if<0>(&_data));
    }

    T& unwrap() & { return expect("called unwrap() on an Err value"); }
    const T& unwrap() const& { return expect("called unwrap() on an Err value"); }
    T&& unwrap() && { return std::move(*this).expect("called unwrap() on an Err value"); }

    /* The value if Ok, otherwise the fallback. */
    template <typename U>
    T unwrapOr(U&& fallback) const& {
        if (isOk()) return std::get<0>(_data);
        return static_cast<T>(std::forward<U>(fallback));
    }
    template <typename U>
    T unwrapOr(U&& fallback) && {
        if (isOk()) return std::get<0>(std::move(_data));
        return static_cast<T>(std::forward<U>(fallback));
    }

    /* The value if Ok, otherwise f(error). */
    template <typename F>
    T unwrapOrElse(F&& f) const& {
        if (isOk()) return std::get<0>(_data);
        return std::invoke(std::forward<F>(f), std::get<1>(_data));
    }
    template <typename F>
    T unwrapOrElse(F&& f) && {
        if (isOk()) return std::get<0>(std::move(_data));
        return std::invoke(std::forward<F>(f), std::get<1>(std::move(_data)));
    }

    
    /* ---- extracting the error ------------------------------------------- */

    /* Returns the error, or throws BadResultAccess if this is Ok. */
    E& unwrapErr() & {
        if (isOk()) throw BadResultAccess("called unwrapErr() on an Ok value");
        return *std::get_if<1>(&_data);
    }
    const E& unwrapErr() const& {
        if (isOk()) throw BadResultAccess("called unwrapErr() on an Ok value");
        return *std::get_if<1>(&_data);
    }
    E&& unwrapErr() && {
        if (isOk()) throw BadResultAccess("called unwrapErr() on an Ok value");
        return std::move(*std::get_if<1>(&_data));
    }

    /* Error text as a view; empty if Ok. Only usable when E converts to
     * std::string_view (true for the default Error type).                   */
    std::string_view whatIsError() const noexcept {
        static_assert(std::is_convertible_v<const E&, std::string_view>,
        "whatIsError() needs an error type convertible to std::string_view");

        if (isOk()) return {}; //no error
        return std::get<1>(_data);
    }

    /* Throws std::runtime_error(<error text>) if this is an error; otherwise
     * does nothing.                                                          */
    void throwAsRuntime() const {
        if (isErr()) throw std::runtime_error(detail::describe(std::get<1>(_data)));
    }

    /* ---- combinators ---------------------------------------------------- */

    /* Ok(v) -> Ok(f(v)); errors pass through untouched. */
    template <typename F>
    auto map(F&& f) const& { return mapImpl(*this, std::forward<F>(f)); }
    template <typename F>
    auto map(F&& f) && { return mapImpl(std::move(*this), std::forward<F>(f)); }

    /* Err(e) -> Err(f(e)); values pass through untouched. */
    template <typename F>
    auto mapErr(F&& f) const& { return mapErrImpl(*this, std::forward<F>(f)); }
    template <typename F>
    auto mapErr(F&& f) && { return mapErrImpl(std::move(*this), std::forward<F>(f)); }

    /* Ok(v) -> f(v), where f itself returns a Result. Chain fallible steps. */
    template <typename F>
    auto andThen(F&& f) const& { return andThenImpl(*this, std::forward<F>(f)); }
    template <typename F>
    auto andThen(F&& f) && { return andThenImpl(std::move(*this), std::forward<F>(f)); }
};

/* Result<void, E>: success carries no value (Rust's Result<(), E>). This is
 * the "did it work, and if not why" case your original class covered.       */
template <typename E>
class [[nodiscard]] Result<void, E> {
    static_assert(!std::is_void_v<E>, "Result's error type cannot be void");
    static_assert(!std::is_reference_v<E>, "Result cannot hold references");

private:
    std::optional<E> _error;

    template <typename Self>
    static decltype(auto) errorOf(Self&& self) {
        return *std::forward<Self>(self)._error;
    }

    template <typename Self, typename F>
    static auto mapImpl(Self&& self, F&& f) {
        using U = std::decay_t<std::invoke_result_t<F>>;
        if (self.isErr()) {
            return Result<U, E>(Err(errorOf(std::forward<Self>(self))));
        }
        if constexpr (std::is_void_v<U>) {
            std::invoke(std::forward<F>(f));
            return Result<U, E>(Ok());
        } else {
            return Result<U, E>(Ok(std::invoke(std::forward<F>(f))));
        }
    }

    template <typename Self, typename F>
    static auto mapErrImpl(Self&& self, F&& f) {
        using Arg = decltype(errorOf(std::forward<Self>(self)));
        using G = std::decay_t<std::invoke_result_t<F, Arg>>;
        if (self.isOk()) {
            return Result<void, G>(Ok());
        }
        return Result<void, G>(
            Err(std::invoke(std::forward<F>(f), errorOf(std::forward<Self>(self)))));
    }

    template <typename Self, typename F>
    static auto andThenImpl(Self&& self, F&& f) {
        using R = std::decay_t<std::invoke_result_t<F>>;
        if (self.isErr()) {
            return R(Err(errorOf(std::forward<Self>(self))));
        }
        return std::invoke(std::forward<F>(f));
    }

public:
    using value_type = void;
    using error_type = E;

    Result(detail::OkValue<void>) noexcept {}

    template <typename F, typename = std::enable_if_t<std::is_constructible_v<E, F&&>>>
    Result(detail::ErrValue<F> err) : _error(std::in_place, std::move(err.value)) {}

    bool isOk() const noexcept { return !_error.has_value(); }
    bool isErr() const noexcept { return _error.has_value(); }
    explicit operator bool() const noexcept { return isOk(); }

    /* Throws BadResultAccess("<msg>: <error text>") if this is an error. */
    void expect(std::string_view msg) const {
        if (isErr()) {
            throw BadResultAccess(std::string(msg) + ": " + detail::describe(*_error));
        }
    }
    void unwrap() const { expect("called unwrap() on an Err value"); }

    E& unwrapErr() & {
        if (isOk()) throw BadResultAccess("called unwrapErr() on an Ok value");
        return *_error;
    }
    const E& unwrapErr() const& {
        if (isOk()) throw BadResultAccess("called unwrapErr() on an Ok value");
        return *_error;
    }
    E&& unwrapErr() && {
        if (isOk()) throw BadResultAccess("called unwrapErr() on an Ok value");
        return std::move(*_error);
    }

    std::string_view whatIsError() const noexcept {
        static_assert(std::is_convertible_v<const E&, std::string_view>,
                      "whatIsError() needs an error type convertible to std::string_view");
        if (isOk()) return {};
        return *_error;
    }

    void throwAsRuntime() const {
        if (isErr()) throw std::runtime_error(detail::describe(*_error));
    }

    /* Ok -> Ok(f()) */
    template <typename F>
    auto map(F&& f) const& { return mapImpl(*this, std::forward<F>(f)); }
    template <typename F>
    auto map(F&& f) && { return mapImpl(std::move(*this), std::forward<F>(f)); }

    template <typename F>
    auto mapErr(F&& f) const& { return mapErrImpl(*this, std::forward<F>(f)); }
    template <typename F>
    auto mapErr(F&& f) && { return mapErrImpl(std::move(*this), std::forward<F>(f)); }

    /* Ok -> f(), where f itself returns a Result */
    template <typename F>
    auto andThen(F&& f) const& { return andThenImpl(*this, std::forward<F>(f)); }
    template <typename F>
    auto andThen(F&& f) && { return andThenImpl(std::move(*this), std::forward<F>(f)); }
};

}  // namespace error
}  // namespace bstd
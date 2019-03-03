
// +--------------+
// | Try operator |
// +--------------+

// Defines a 'try()' macro to automatically propagate errors.

#pragma once

#include <tuple>
#include <optional>
#include <system_error>

namespace tryop {

// ____________
// error_traits

// When using 'try(T&&)', T and the return type of the current function
// must implement 'error_traits' to satisfy the predicates 'is_error_v'.
template <class T, class SFINAE = void>
struct error_traits; /*
{
    static bool indicates_error(T const&) noexcept;
    static T make_error(OPT error_type&&) noexcept;
    OPT static auto extract_value(T&&) noexcept;
    OPT static auto extract_error(T&&) noexcept;
};
*/

namespace detail {
    template <class T, class F>
    constexpr bool is_detected(F) noexcept {
        return std::is_invocable_v<F, T>;
    }
}

// A detailed error is when a value is associated with an error, like 'std::error_code'.
template <class T>
constexpr bool is_detailed_error_v = detail::is_detected<T>([] (auto x) -> decltype(
    error_traits<decltype(x)>::indicates_error(x),
    error_traits<decltype(x)>::make_error(
        error_traits<decltype(x)>::extract_error(std::move(x))
    )
) {});

// An error with value is when a value is embedded in case of success, like 'std::optional<T>'.
template <class T>
constexpr bool is_error_with_value_v = detail::is_detected<T>([] (auto x) -> decltype(
    error_traits<decltype(x)>::indicates_error(x),
    error_traits<decltype(x)>::make_error,
    error_traits<decltype(x)>::extract_value(std::move(x))
) {});

template <class T>
constexpr bool is_error_v = is_detailed_error_v<T> || is_error_with_value_v<T>;

// ___________
// error_proxy

// Used to move a value in an error state to create another value in an error state.
// Can downgrade a detailed error to a not detailed one, but not the opposite :
// For example, a 'std::error_code' and be converted to an empty 'std::optional<float>'.
template <class Src>
class error_proxy {
    Src&& value;
public:
    constexpr error_proxy(Src&& value) noexcept :
        value{std::move(value)}
    {
        static_assert(is_error_v<Src>);
    }

    template <class Dst, class = std::enable_if_t<is_error_v<Dst>>>
    constexpr operator Dst() const noexcept {
        if constexpr (is_detailed_error_v<Dst>) {
            static_assert(is_detailed_error_v<Src>);
            auto error = error_traits<Src>::extract_error(std::move(value));
            return error_traits<Dst>::make_error(std::move(error));
        }
        else {
            return error_traits<Dst>::make_error();
        }
    }
};
template <class T>
error_proxy(T&&) -> error_proxy<T>;

// ___________
// try() macro

namespace detail {
    template <class T>
    constexpr auto maybe_extract_value(T&& value) noexcept {
        if constexpr (is_error_with_value_v<T>) {
            return error_traits<T>::extract_value(std::move(value));
        }
    }
}

// try(T&& value) will checks if 'value' is in an error state :
// If it's the case, it will return it's error from the current function.
// Otherwise, it will return it's embedded result (if there is one).
#if defined(try)
#error The 'try()' macro is already defined.
#endif
#define try(expr) ({ \
    auto _value_ = (expr); \
    static_assert(::tryop::is_error_v<decltype(_value_)>); \
    if (::tryop::error_traits<decltype(_value_)>::indicates_error(_value_)) { \
        return ::tryop::error_proxy{std::move(_value_)}; \
    } \
    ::tryop::detail::maybe_extract_value(std::move(_value_)); \
})

// __________________________
// optional-like error_traits

// This boolean can be specialized for optional-like types to implement 'error_traits<T>' :
template <class T>
constexpr bool has_optional_semantics_v = false;

template <class T>
constexpr bool has_optional_semantics_v<std::optional<T>> = true;

template <class T>
struct error_traits<T, std::enable_if_t<
    has_optional_semantics_v<T>
>> {
    static_assert(detail::is_detected<T>([] (auto x) -> decltype(
        decltype(x){},
        static_cast<bool>(x),
        *x
    ) {}));

    static constexpr bool indicates_error(T const& opt) noexcept {
        return !static_cast<bool>(opt);
    }
    static constexpr T make_error() noexcept {
        return {};
    }
    static constexpr auto extract_value(T&& opt) noexcept {
        return std::move(*opt);
    }
};

static_assert(is_error_with_value_v<std::optional<int>>);
static_assert(!is_detailed_error_v<std::optional<int>>);

// ____________________________
// error code-like error_traits

// This function can to be implemented for error code-like types to implement 'error_traits<T>' :
// bool indicates_error(T const& error) noexcept;

inline bool indicates_error(std::error_code const& ec) noexcept {
    return static_cast<bool>(ec);
}
inline bool indicates_error(std::error_condition const& ec) noexcept {
    return static_cast<bool>(ec);
}

template <class T>
constexpr bool has_indicates_error_v = detail::is_detected<T>([] (auto x) -> decltype(
    indicates_error(x)
) {});

namespace detail {
    template <class T>
    constexpr bool call_indicates_error(T const& error) noexcept {
        return indicates_error(error);
    }
}

template <class T>
struct error_traits<T, std::enable_if_t<
    has_indicates_error_v<T>
>> {
    static constexpr bool indicates_error(T const& err) noexcept {
        return detail::call_indicates_error(err);
    }
    static constexpr T make_error(T&& err) noexcept {
        return std::move(err);
    }
    static constexpr T extract_error(T&& err) noexcept {
        return std::move(err);
    }
};

static_assert(is_detailed_error_v<std::error_code>);
static_assert(!is_error_with_value_v<std::error_code>);

// ______________________
// pair-like error_traits

template <class T>
constexpr bool is_pair_v = false;

template <template <class...> class Pair, class T1, class T2>
constexpr bool is_pair_v<Pair<T1, T2>> = detail::is_detected<Pair<T1, T2>>([] (auto x) -> decltype(
    std::get<0>(x),
    std::get<1>(x)
) {});

// Implements 'error_traits<T>' if T is a pair and it's second value
// is a detailed error type without value (like an error code).
template <template <class...> class Pair, class Error, class T>
struct error_traits<Pair<T, Error>, std::enable_if_t<
    is_pair_v<Pair<T, Error>> &&
    is_detailed_error_v<Error> &&
    !is_error_with_value_v<Error>
>> {
    static constexpr bool indicates_error(Pair<T, Error> const& val) noexcept {
        return detail::call_indicates_error(std::get<1>(val));
    }
    static constexpr Pair<T, Error> make_error(Error&& err) noexcept {
        auto val = Pair<T, Error>{};
        std::get<1>(val) = std::move(err);
        return val;
    }
    static constexpr T extract_value(Pair<T, Error>&& val) noexcept {
        return std::move(std::get<0>(val));
    }
    static constexpr Error extract_error(Pair<T, Error>&& val) noexcept {
        return std::move(std::get<1>(val));
    }
};

static_assert(is_detailed_error_v<std::tuple<int, std::error_code>>);
static_assert(is_error_with_value_v<std::tuple<int, std::error_code>>);

} // ::tryop

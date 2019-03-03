
# Try operator

This C++17 header defines a `try(rvalue)` macro that automatically propagates errors.
It uses the 'compound statements' extension which is available on GCC and Clang.

Example :

```cpp
#include <try_operator.hpp>

enum class error { ok, fail };

bool indicates_error(error e) { return e != error::ok; }

[[nodiscard]]
error initialize_something();

std::pair<int, error> make_number(int n);

std::tuple<std::string, error> ugly_add_and_print(int x, int y) {
    const auto err = initialize_something();
    if (err != error::ok) return { {}, err };

    const auto [x_res, x_err] = make_number(x);
    if (x_err != error::ok) return { {}, x_err };

    const auto [y_res, y_err] = make_number(y);
    if (y_err != error::ok) return { {}, y_err };

    return { std::to_string(x_res + y_res), error::ok };
}

std::tuple<std::string, error> beautiful_add_and_print(int x, int y) {
    try(initialize_something());
    auto const num = try(make_number(x)) + try(make_number(y));
    return { std::to_string(num), error::ok };
}
```

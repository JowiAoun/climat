// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0
//
// A value, or an Error. Never both, never neither.
//
// This is the return type of everything in libclimat that can fail, and the
// reason it exists rather than the three obvious alternatives:
//
//   * A bool plus an out-parameter lets a caller ignore the bool and read a
//     half-filled struct. That is precisely the "partial success silently"
//     that docs/04-architecture.md §4.4 rules out.
//   * An exception does not cross a QFuture boundary usefully, and the whole
//     network layer is asynchronous.
//   * std::optional says something is missing without saying what happened,
//     and "no forecast" and "no forecast because our User-Agent is banned" are
//     the two cases the UI most needs to tell apart.
//
// std::expected would be the right type and is C++23; the floor here is C++17
// (see libclimat/CMakeLists.txt), so this is a small hand-rolled stand-in with
// the same shape. If the floor ever moves, the migration is mechanical.
//
// ---- the default constructor is a failure -----------------------------------
//
// Result<T> is default-constructible only because QFuture's result storage
// wants it to be, and a default-constructed Result is an *error* - Cancelled,
// "result never produced". A future that was destroyed before it was fulfilled
// therefore reads as a failure rather than as an empty success, which is the
// only default that is safe when the whole point of the type is that success
// must be stated.

#pragma once

#include "error.h"

#include <utility>
#include <variant>

namespace climat {

template <typename T>
class Result
{
public:
    Result()
        : m_state(Error(ErrorKind::Cancelled, QStringLiteral("result was never produced")))
    {
    }

    Result(T value)                                   // NOLINT(google-explicit-constructor)
        : m_state(std::move(value))
    {
    }

    Result(Error error)                               // NOLINT(google-explicit-constructor)
        : m_state(std::move(error))
    {
    }

    [[nodiscard]] bool hasValue() const { return std::holds_alternative<T>(m_state); }
    explicit           operator bool() const { return hasValue(); }

    // Undefined if !hasValue(). Callers check first; there is one shape for
    // that and it is `if (!result) return result.error();`.
    [[nodiscard]] const T &value() const & { return std::get<T>(m_state); }
    [[nodiscard]] T       &value() & { return std::get<T>(m_state); }

    // On a temporary, the value itself rather than a reference into it. A
    // reference outlives the Result it points into: `for (x : f().value())`
    // walked freed memory, and on macOS read back three blank places where
    // Linux happened to still hold the old bytes. By value, a loop or a
    // `const T &` keeps it alive the way it keeps any temporary.
    [[nodiscard]] T        value() && { return std::move(std::get<T>(m_state)); }
    [[nodiscard]] T        takeValue() { return std::move(std::get<T>(m_state)); }

    // Safe to call in either state: a successful Result reports a default
    // Error, whose kind is Network and whose message is empty. That is a
    // deliberate choice against a hard assert, because the commonest use is a
    // log line built before the branch is taken.
    [[nodiscard]] Error error() const
    {
        if (const Error *e = std::get_if<Error>(&m_state))
            return *e;
        return {};
    }

    [[nodiscard]] ErrorKind errorKind() const { return error().kind(); }

private:
    std::variant<T, Error> m_state;
};

// The value type of an operation that either worked or did not, and has
// nothing to hand back when it worked. `Status` reads better at a call site
// than `Result<void>` and avoids a template specialisation whose only job
// would be to have no value() member.
struct Nothing {
};

using Status = Result<Nothing>;

inline Status ok()
{
    return Status(Nothing{});
}

} // namespace climat

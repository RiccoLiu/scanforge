#pragma once

// Explicit error channel. NEVER throw across a queued Qt connection: worker
// slots must return Result/Status instead.
// Reference: llvm/include/llvm/Support/Error.h (Apache-2.0) for the concept;
// C++23 std::expected for the interface shape; tl::expected (CC0) as a
// drop-in replacement once the project moves to C++23.

#include <cstdint>
#include <string>
#include <variant>
#include <utility>

namespace sf {

enum class ErrorCode : std::uint8_t {
    None = 0,
    InvalidArgument,
    IoError,
    UnsupportedFormat,
    DeviceNotFound,
    DeviceDisconnected,
    Timeout,
    Cancelled,
    EndOfStream,
    CalibrationInvalid,
    ProcessingFailed,
    InvalidState
};

struct Error {
    ErrorCode   code = ErrorCode::None;
    std::string message;
    std::string context;   // module / stage that produced the error

    [[nodiscard]] std::string toString() const {
        return "[" + std::to_string(static_cast<int>(code)) + "] " + message +
               (context.empty() ? "" : " @" + context);
    }
};

[[nodiscard]] inline Error makeError(ErrorCode code, std::string message,
                                     std::string context = {}) {
    return Error{code, std::move(message), std::move(context)};
}

template <class T>
class Result {
public:
    Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}   // NOLINT
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {} // NOLINT

    [[nodiscard]] static Result success(T value) { return Result(std::move(value)); }
    [[nodiscard]] static Result failure(Error e)  { return Result(std::move(e)); }

    [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] const T& value() const { return std::get<0>(storage_); }
    [[nodiscard]] T&       value()       { return std::get<0>(storage_); }
    [[nodiscard]] const Error& error() const { return std::get<1>(storage_); }

    // Monadic helper for chaining processing stages without exception handling.
    template <class Fn>
    [[nodiscard]] auto andThen(Fn&& fn) const
        -> decltype(fn(std::declval<const T&>())) {
        using Next = decltype(fn(std::declval<const T&>()));
        if (!hasValue()) return Next::failure(error());
        return fn(value());
    }

private:
    std::variant<T, Error> storage_;
};

using Status = Result<std::monostate>;

[[nodiscard]] inline Status ok() { return Status::success(std::monostate{}); }

} // namespace sf

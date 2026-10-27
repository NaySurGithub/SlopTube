#pragma once

#include <expected>
#include <string>
#include <utility>

namespace sloptube {

struct Error {
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(std::string message) {
    return std::unexpected(Error{std::move(message)});
}

} // namespace sloptube

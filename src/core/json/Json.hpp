#pragma once

#include "core/util/Result.hpp"

#include <simdjson.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace sloptube::json {

using Element = simdjson::dom::element;

// Parses `document` with a per-thread parser. The returned element stays valid until the next
// call to parse() on the same thread. `document` may be grown to hold simdjson's padding.
[[nodiscard]] Result<Element> parse(std::string& document);

// JSON-pointer accessors returning a neutral value when the path is absent or mistyped.
[[nodiscard]] std::string_view string(Element element, std::string_view pointer);

// Accepts both JSON numbers and decimal strings (InnerTube encodes 64-bit values as strings).
[[nodiscard]] std::int64_t integer(Element element, std::string_view pointer);

[[nodiscard]] bool boolean(Element element, std::string_view pointer);

[[nodiscard]] bool has(Element element, std::string_view pointer);

// Invokes `visit` for every value stored under `key`, at any depth, in document order. Matched
// values are not descended into, nor are values stored under any of the `pruned` keys.
template <typename Visit>
void forEachKey(Element element, std::string_view key, Visit&& visit,
                std::initializer_list<std::string_view> pruned = {}) {
    using Type = simdjson::dom::element_type;
    switch (element.type()) {
    case Type::OBJECT:
        for (const auto field : simdjson::dom::object(element)) {
            if (field.key == key) {
                visit(field.value);
            } else if (std::ranges::find(pruned, field.key) == pruned.end()) {
                forEachKey(field.value, key, visit, pruned);
            }
        }
        break;
    case Type::ARRAY:
        for (const auto child : simdjson::dom::array(element)) {
            forEachKey(child, key, visit, pruned);
        }
        break;
    default:
        break;
    }
}

// Minimal streaming writer for request bodies and small state files.
class Writer {
public:
    Writer& beginObject();
    Writer& beginObject(std::string_view key);
    Writer& endObject();
    Writer& field(std::string_view key, std::string_view value);
    Writer& field(std::string_view key, std::int64_t value);
    Writer& beginArray(std::string_view key);
    Writer& endArray();
    // Array element; objects inside arrays use the key-less beginObject().
    Writer& value(std::string_view value);

    Writer& singletonArray(std::string_view key, std::string_view element) {
        return beginArray(key).value(element).endArray();
    }

    // Constrained so string literals and integers never silently convert to bool.
    template <std::same_as<bool> Bool>
    Writer& field(std::string_view key, Bool value) {
        return boolField(key, value);
    }

    [[nodiscard]] std::string take() { return std::move(m_out); }

private:
    Writer& boolField(std::string_view key, bool value);
    void key(std::string_view key);
    void quoted(std::string_view text);

    std::string m_out;
    bool m_needsComma = false;
};

} // namespace sloptube::json

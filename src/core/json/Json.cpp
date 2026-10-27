#include "core/json/Json.hpp"

#include <array>
#include <charconv>

namespace sloptube::json {

Result<Element> parse(std::string& document) {
    thread_local simdjson::dom::parser parser;
    document.reserve(document.size() + simdjson::SIMDJSON_PADDING);
    Element root;
    if (const auto error = parser.parse(document.data(), document.size(), false).get(root)) {
        return fail(std::string("json: ") + simdjson::error_message(error));
    }
    return root;
}

std::string_view string(Element element, std::string_view pointer) {
    std::string_view value;
    if (element.at_pointer(pointer).get_string().get(value)) {
        return {};
    }
    return value;
}

std::int64_t integer(Element element, std::string_view pointer) {
    Element value;
    if (element.at_pointer(pointer).get(value)) {
        return 0;
    }
    if (std::int64_t number = 0; !value.get_int64().get(number)) {
        return number;
    }
    std::string_view text;
    if (value.get_string().get(text)) {
        return 0;
    }
    std::int64_t number = 0;
    std::from_chars(text.data(), text.data() + text.size(), number);
    return number;
}

bool boolean(Element element, std::string_view pointer) {
    bool value = false;
    if (element.at_pointer(pointer).get_bool().get(value)) {
        return false;
    }
    return value;
}

bool has(Element element, std::string_view pointer) {
    return element.at_pointer(pointer).error() == simdjson::SUCCESS;
}

Writer& Writer::beginObject() {
    if (m_needsComma) {
        m_out += ',';
    }
    m_out += '{';
    m_needsComma = false;
    return *this;
}

Writer& Writer::beginObject(std::string_view name) {
    key(name);
    m_out += '{';
    m_needsComma = false;
    return *this;
}

Writer& Writer::endObject() {
    m_out += '}';
    m_needsComma = true;
    return *this;
}

Writer& Writer::field(std::string_view name, std::string_view value) {
    key(name);
    quoted(value);
    m_needsComma = true;
    return *this;
}

Writer& Writer::field(std::string_view name, std::int64_t value) {
    key(name);
    std::array<char, 24> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    m_out.append(buffer.data(), end);
    m_needsComma = true;
    return *this;
}

Writer& Writer::beginArray(std::string_view name) {
    key(name);
    m_out += '[';
    m_needsComma = false;
    return *this;
}

Writer& Writer::endArray() {
    m_out += ']';
    m_needsComma = true;
    return *this;
}

Writer& Writer::value(std::string_view text) {
    if (m_needsComma) {
        m_out += ',';
    }
    quoted(text);
    m_needsComma = true;
    return *this;
}

Writer& Writer::boolField(std::string_view name, bool value) {
    key(name);
    m_out += value ? "true" : "false";
    m_needsComma = true;
    return *this;
}

void Writer::key(std::string_view name) {
    if (m_needsComma) {
        m_out += ',';
    }
    quoted(name);
    m_out += ':';
}

void Writer::quoted(std::string_view text) {
    static constexpr std::string_view kHex = "0123456789abcdef";
    m_out += '"';
    for (const char c : text) {
        switch (c) {
        case '"':
            m_out += "\\\"";
            break;
        case '\\':
            m_out += "\\\\";
            break;
        case '\n':
            m_out += "\\n";
            break;
        case '\r':
            m_out += "\\r";
            break;
        case '\t':
            m_out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                m_out += "\\u00";
                m_out += kHex[(c >> 4) & 0xF];
                m_out += kHex[c & 0xF];
            } else {
                m_out += c;
            }
        }
    }
    m_out += '"';
}

} // namespace sloptube::json

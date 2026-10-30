// volt2.hpp: write a website in C++, see it at F5, deploy it with Emscripten.
// https://github.com/vdcoder/volt2 (MIT licence)
//
// Development (any native compiler): volt2::run serves http://127.0.0.1:8080/,
// opens your browser, and your C++ talks to the page through a synchronous bridge.
// Deployment (em++): the same code talks to the page directly from WebAssembly.
//
// Development environment variables:
//   VOLT2_PORT=8123     use this port (default: the first free one from 8080)
//   VOLT2_NO_BROWSER=1  do not open the browser
#pragma once

#if defined(_MSVC_LANG)
#  if _MSVC_LANG < 202002L
#    error "volt2 needs C++20 or newer. In Visual Studio: Project Properties > C/C++ > Language > C++ Language Standard > ISO C++20."
#  endif
#elif __cplusplus < 202002L
#  error "volt2 needs C++20 or newer, for example -std=c++20."
#endif

#include <charconv>
#include <chrono>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#  if !defined(__cpp_exceptions)
#    error "volt2 needs C++ exceptions: add -fexceptions to your em++ command."
#  endif
#  include <emscripten/bind.h>
#  include <emscripten/val.h>
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")
#  endif
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace volt2 {

// ============================================================================
// JSON: the message format between C++ and the page (both builds)
// ============================================================================
namespace detail {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json boolean(bool value) { Json j; j.type_ = Type::Bool; j.bool_ = value; return j; }
    static Json number(double value) { Json j; j.type_ = Type::Number; j.number_ = value; return j; }
    static Json string(std::string value) { Json j; j.type_ = Type::String; j.string_ = std::move(value); return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool asBool() const { expect(Type::Bool); return bool_; }
    double asNumber() const { expect(Type::Number); return number_; }
    const std::string& asString() const { expect(Type::String); return string_; }
    const std::vector<Json>& items() const { expect(Type::Array); return items_; }

    Json& push(Json value) { expect(Type::Array); items_.push_back(std::move(value)); return *this; }
    Json& set(const std::string& key, Json value);
    const Json* find(const std::string& key) const;

    std::string dump() const { std::string out; write(out); return out; }
    static Json parse(const std::string& text);

private:
    struct Parser;
    void expect(Type type) const {
        if (type_ != type) throw std::runtime_error("volt2: unexpected JSON value type");
    }
    void write(std::string& out) const;
    static void writeString(std::string& out, const std::string& text);

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<std::string> keys_;  // object keys, parallel to items_
    std::vector<Json> items_;        // array items or object values
};

inline Json& Json::set(const std::string& key, Json value) {
    expect(Type::Object);
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) { items_[i] = std::move(value); return *this; }
    }
    keys_.push_back(key);
    items_.push_back(std::move(value));
    return *this;
}

inline const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) return &items_[i];
    }
    return nullptr;
}

inline void Json::writeString(std::string& out, const std::string& text) {
    static const char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

inline void Json::write(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            // Locale-independent and round-trip exact. Non-finite numbers never get here.
            char buffer[32];
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
            auto result = std::to_chars(buffer, buffer + sizeof buffer, number_);
            out.append(buffer, result.ptr);
#else
            std::snprintf(buffer, sizeof buffer, "%.17g", number_);
            for (char* p = buffer; *p; ++p) if (*p == ',') *p = '.';
            out += buffer;
#endif
            break;
        }
        case Type::String: writeString(out, string_); break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < items_.size(); ++i) {
                if (i) out += ',';
                items_[i].write(out);
            }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < items_.size(); ++i) {
                if (i) out += ',';
                writeString(out, keys_[i]);
                out += ':';
                items_[i].write(out);
            }
            out += '}';
            break;
    }
}

struct Json::Parser {
    const std::string& text;
    size_t pos = 0;

    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("volt2: malformed JSON (") + what + ")");
    }
    void skipSpace() {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' || text[pos] == '\t')) ++pos;
    }
    bool consume(char c) {
        skipSpace();
        if (pos < text.size() && text[pos] == c) { ++pos; return true; }
        return false;
    }
    void word(const char* w) {
        size_t n = std::strlen(w);
        if (text.compare(pos, n, w) != 0) fail("unknown word");
        pos += n;
    }
    unsigned hex4() {
        if (pos + 4 > text.size()) fail("short \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text[pos++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') value |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= unsigned(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return value;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    std::string str() {
        ++pos;  // opening quote
        std::string out;
        for (;;) {
            if (pos >= text.size()) fail("unterminated string");
            char c = text[pos++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos >= text.size()) fail("unterminated escape");
            char e = text[pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && text.compare(pos, 2, "\\u") == 0) {
                        size_t mark = pos;
                        pos += 2;
                        unsigned low = hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        else pos = mark;  // lone high surrogate; the next escape is read on its own
                    }
                    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;  // lone surrogate
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }
    Json num() {
        size_t start = pos;
        while (pos < text.size() && std::strchr("+-0123456789.eE", text[pos]) != nullptr) ++pos;
        if (start == pos) fail("unexpected character");
        double value = 0;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        auto result = std::from_chars(text.data() + start, text.data() + pos, value);
        if (result.ec != std::errc() || result.ptr != text.data() + pos) fail("bad number");
#else
        std::string digits = text.substr(start, pos - start);
        char* end = nullptr;
        value = std::strtod(digits.c_str(), &end);
        if (end != digits.c_str() + digits.size()) fail("bad number");
#endif
        return Json::number(value);
    }
    Json value(int depth = 0) {
        if (depth > 256) fail("nested too deeply");
        skipSpace();
        if (pos >= text.size()) fail("unexpected end");
        char c = text[pos];
        if (c == '{') {
            ++pos;
            Json object = Json::object();
            if (consume('}')) return object;
            do {
                skipSpace();
                if (pos >= text.size() || text[pos] != '"') fail("expected a key");
                std::string key = str();
                if (!consume(':')) fail("expected ':'");
                object.set(key, value(depth + 1));
            } while (consume(','));
            if (!consume('}')) fail("expected '}'");
            return object;
        }
        if (c == '[') {
            ++pos;
            Json array = Json::array();
            if (consume(']')) return array;
            do {
                array.push(value(depth + 1));
            } while (consume(','));
            if (!consume(']')) fail("expected ']'");
            return array;
        }
        if (c == '"') return Json::string(str());
        if (c == 't') { word("true"); return Json::boolean(true); }
        if (c == 'f') { word("false"); return Json::boolean(false); }
        if (c == 'n') { word("null"); return Json(); }
        return num();
    }
};

inline Json Json::parse(const std::string& text) {
    Parser parser{text};
    Json result = parser.value();
    parser.skipSpace();
    if (parser.pos != text.size()) parser.fail("trailing characters");
    return result;
}

}  // namespace detail

// ============================================================================
// js::Value: a JavaScript value seen from C++ (both builds)
// ============================================================================
namespace js {

// A JavaScript exception, or a value that could not be converted.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Value;
using Args = std::vector<Value>;

// Types that can be passed to JavaScript: Value, nullptr, bool, numbers, strings.
template <class T>
concept ToJs = std::same_as<std::remove_cvref_t<T>, Value> || std::same_as<std::remove_cvref_t<T>, std::nullptr_t> ||
               std::is_arithmetic_v<std::remove_cvref_t<T>> || std::convertible_to<T, std::string>;

// Types a JavaScript value can be converted to with Value::as.
template <class T>
concept FromJs = std::same_as<T, Value> || std::is_arithmetic_v<T> || std::same_as<T, std::string>;

// Result types for call and operator(): FromJs, or void to ignore the result.
template <class T>
concept CallResult = std::is_void_v<T> || FromJs<T>;

}  // namespace js

namespace detail {

// A JavaScript object, function or other non-primitive that C++ refers to.
// The page keeps it alive until the last Value copy referring to it is gone.
struct Handle {
    long long id;
    unsigned long long generation;  // which page load the handle belongs to
    Handle(long long i, unsigned long long g) : id(i), generation(g) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle();
};

Json toWire(const js::Value& value);
js::Value fromWire(const Json& wire);
Json request(Json op);

}  // namespace detail

namespace js {

// Mirrors the shape of emscripten::val. Every operation is synchronous: in the
// WebAssembly build it is a direct call, in development a round trip to the page.
class Value {
public:
    Value() = default;  // undefined
    explicit Value(std::nullptr_t) : kind_(Kind::Null) {}
    explicit Value(bool value) : kind_(Kind::Bool), bool_(value) {}
    template <class T>
        requires(std::is_arithmetic_v<T> && !std::same_as<T, bool>)
    explicit Value(T value) : kind_(Kind::Number), number_(static_cast<double>(value)) {}
    explicit Value(const char* text) : kind_(Kind::String), string_(text ? text : "") {}
    explicit Value(std::string text) : kind_(Kind::String), string_(std::move(text)) {}

    static Value undefined() { return Value(); }
    static Value null() { return Value(nullptr); }
    // globalThis[name], or globalThis itself when name is empty.
    static Value global(const std::string& name = std::string());
    static Value object();
    static Value array();

    Value operator[](const std::string& key) const { return get(Value(key)); }
    Value operator[](const char* key) const { return get(Value(key)); }
    Value operator[](int index) const { return get(Value(index)); }

    template <ToJs T>
    void set(const std::string& key, T&& value) const;
    template <ToJs T>
    void set(int index, T&& value) const;

    // this[method](args...), converted to R (Value, void, bool, a number type or std::string).
    template <CallResult R = Value, ToJs... A>
    R call(const std::string& method, A&&... args) const;
    // this(args...) for a function value.
    template <CallResult R = Value, ToJs... A>
    R operator()(A&&... args) const;
    // new this(args...)
    template <ToJs... A>
    Value new_(A&&... args) const;

    // bool: JavaScript truthiness. Numbers: must be a number (or boolean); integer types
    // truncate toward zero and reject out-of-range values. std::string: must be a string.
    template <FromJs T>
    T as() const;

    bool isUndefined() const { return kind_ == Kind::Undefined; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool isBool() const { return kind_ == Kind::Bool; }
    bool isNumber() const { return kind_ == Kind::Number; }
    bool isString() const { return kind_ == Kind::String; }
    std::string typeOf() const;
    bool strictlyEquals(const Value& other) const;

private:
    enum class Kind { Undefined, Null, Bool, Number, String, Handle };

    Value get(const Value& key) const;
    double numberValue() const;
    bool truthy() const;

    friend detail::Json detail::toWire(const Value&);
    friend Value detail::fromWire(const detail::Json&);

    Kind kind_ = Kind::Undefined;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::shared_ptr<detail::Handle> handle_;
};

}  // namespace js

// ============================================================================
// Runtime state and the call dispatch shared by both builds
// ============================================================================
class Session {
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    // Per-page state lives here in later versions.
};

using Site = std::function<void(Session&)>;

// For example: volt2::run(site, {.port = 8123, .openBrowser = false});
struct Options {
    int port = 0;             // 0: VOLT2_PORT, else the first free port from 8080
    bool openBrowser = true;  // development only; VOLT2_NO_BROWSER=1 also disables it
};

namespace detail {

using Callback = std::function<js::Value(const js::Args&)>;

// How operations reach the page: synchronous HTTP in development, a direct call in WebAssembly.
struct Transport {
    virtual ~Transport() = default;
    virtual Json request(const Json& op) = 0;
};

// Thrown through C++ frames when a newer page load replaces the current one
// (development only). Deliberately not a std::exception.
struct PageReplaced {};

struct Runtime {
    Transport* transport = nullptr;
    unsigned long long generation = 1;  // bumped on every page load
    std::vector<long long> releases;    // handles to release, sent with the next message
    long long nextCallback = 1;
    std::unordered_map<long long, std::shared_ptr<Callback>> callbacks;
};

// Never destroyed, so Values that outlive main cannot touch a dead runtime.
inline Runtime& runtime() {
    static Runtime* instance = new Runtime();
    return *instance;
}

inline Handle::~Handle() {
    Runtime& r = runtime();
    if (generation == r.generation) r.releases.push_back(id);
}

inline void attachReleases(Json& message) {
    Runtime& r = runtime();
    if (r.releases.empty()) return;
    Json list = Json::array();
    for (long long id : r.releases) list.push(Json::number(static_cast<double>(id)));
    r.releases.clear();
    message.set("rel", std::move(list));
}

inline Json op(const char* name) {
    Json j = Json::object();
    j.set("op", Json::string(name));
    return j;
}

// Sends one operation to the page and returns its result value, or throws js::Error.
inline Json request(Json op) {
    Runtime& r = runtime();
    if (!r.transport) throw js::Error("volt2: no page is connected. JavaScript is only reachable inside volt2::run.");
    attachReleases(op);
    Json reply = r.transport->request(op);
    if (const Json* error = reply.find("error")) throw js::Error(error->asString());
    const Json* value = reply.find("v");
    if (!value) throw js::Error("volt2: malformed reply from the page");
    return *value;
}

inline Json toWire(const js::Value& value) {
    using Kind = js::Value::Kind;
    switch (value.kind_) {
        case Kind::Undefined: { Json j = Json::object(); j.set("u", Json::number(1)); return j; }
        case Kind::Null: return Json();
        case Kind::Bool: return Json::boolean(value.bool_);
        case Kind::Number: {
            if (std::isfinite(value.number_)) return Json::number(value.number_);
            Json j = Json::object();
            j.set("f", Json::string(std::isnan(value.number_) ? "NaN" : value.number_ > 0 ? "Infinity" : "-Infinity"));
            return j;
        }
        case Kind::String: return Json::string(value.string_);
        case Kind::Handle: {
            if (value.handle_->generation != runtime().generation) {
                throw js::Error("volt2: this JavaScript value belongs to a previous page load");
            }
            Json j = Json::object();
            j.set("h", Json::number(static_cast<double>(value.handle_->id)));
            return j;
        }
    }
    return Json();
}

inline js::Value fromWire(const Json& wire) {
    using Kind = js::Value::Kind;
    js::Value value;
    switch (wire.type()) {
        case Json::Type::Null: value.kind_ = Kind::Null; break;
        case Json::Type::Bool: value.kind_ = Kind::Bool; value.bool_ = wire.asBool(); break;
        case Json::Type::Number: value.kind_ = Kind::Number; value.number_ = wire.asNumber(); break;
        case Json::Type::String: value.kind_ = Kind::String; value.string_ = wire.asString(); break;
        case Json::Type::Object:
            if (const Json* h = wire.find("h")) {
                value.kind_ = Kind::Handle;
                value.handle_ = std::make_shared<Handle>(static_cast<long long>(h->asNumber()), runtime().generation);
            } else if (const Json* f = wire.find("f")) {
                const std::string& name = f->asString();
                value.kind_ = Kind::Number;
                value.number_ = name == "Infinity" ? HUGE_VAL : name == "-Infinity" ? -HUGE_VAL : std::nan("");
            }  // {"u":1}: undefined, already the default
            break;
        case Json::Type::Array: throw js::Error("volt2: malformed value from the page");
    }
    return value;
}

template <js::ToJs T>
js::Value toValue(T&& x) {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::same_as<D, js::Value>) return std::forward<T>(x);
    else if constexpr (std::same_as<D, std::nullptr_t>) return js::Value(nullptr);
    else if constexpr (std::same_as<D, bool>) return js::Value(static_cast<bool>(x));
    else if constexpr (std::is_arithmetic_v<D>) return js::Value(x);
    else return js::Value(std::string(std::forward<T>(x)));
}

template <js::ToJs... A>
Json argList(A&&... args) {
    Json list = Json::array();
    (list.push(toWire(toValue(std::forward<A>(args)))), ...);
    return list;
}

template <js::CallResult R>
R convertResult(const Json& wire) {
    js::Value result = fromWire(wire);
    if constexpr (std::is_void_v<R>) {
        (void)result;
    } else {
        return result.template as<R>();
    }
}

inline Json returnReply() {
    Json reply = Json::object();
    reply.set("t", Json::string("return"));
    return reply;
}

inline Json errorReply(const std::string& message) {
    Json reply = returnReply();
    reply.set("error", Json::string(message));
    return reply;
}

// Runs the C++ function a page asked for. Exceptions become the reply's error, which
// the page rethrows as a JavaScript Error in the caller, so nothing unwinds across the bridge.
inline Json serveCall(const Json& message) {
    Runtime& r = runtime();
    Json reply = returnReply();
    try {
        const Json* id = message.find("id");
        auto it = id ? r.callbacks.find(static_cast<long long>(id->asNumber())) : r.callbacks.end();
        if (it == r.callbacks.end()) throw js::Error("volt2: this C++ function no longer exists");
        std::shared_ptr<Callback> callback = it->second;  // keeps it alive if the table changes
        js::Args args;
        if (const Json* list = message.find("a")) {
            for (const Json& wire : list->items()) args.push_back(fromWire(wire));
        }
        reply.set("v", toWire((*callback)(args)));
    } catch (const PageReplaced&) {
        throw;
    } catch (const std::exception& e) {
        reply.set("error", Json::string(e.what()));
    } catch (...) {
        reply.set("error", Json::string("volt2: unknown C++ exception"));
    }
    attachReleases(reply);
    return reply;
}

inline js::Value makeFunction(Callback callback) {
    Runtime& r = runtime();
    long long id = r.nextCallback++;
    r.callbacks[id] = std::make_shared<Callback>(std::move(callback));
    Json o = op("fn");
    o.set("k", Json::number(static_cast<double>(id)));
    return fromWire(request(std::move(o)));
}

template <class G>
js::Value resultToValue(G&& invoke) {
    using R = decltype(invoke());
    static_assert(std::is_void_v<R> || js::ToJs<R>, "volt2: a js::function must return void or a type that can be passed to JavaScript");
    if constexpr (std::is_void_v<R>) {
        invoke();
        return js::Value();
    } else {
        return toValue(invoke());
    }
}

inline void beginPage() {
    Runtime& r = runtime();
    ++r.generation;  // first, so values dropped below do not queue releases
    r.callbacks.clear();
    r.releases.clear();
}

inline void logError(const std::string& message) {
    std::fprintf(stderr, "volt2: %s\n", message.c_str());
    std::fflush(stderr);
}

}  // namespace detail

namespace js {

inline Value Value::global(const std::string& name) {
    detail::Json o = detail::op("global");
    if (!name.empty()) o.set("k", detail::Json::string(name));
    return detail::fromWire(detail::request(std::move(o)));
}

inline Value Value::object() { return global("Object").new_(); }
inline Value Value::array() { return global("Array").new_(); }

inline Value Value::get(const Value& key) const {
    detail::Json o = detail::op("get");
    o.set("o", detail::toWire(*this));
    o.set("k", detail::toWire(key));
    return detail::fromWire(detail::request(std::move(o)));
}

template <ToJs T>
void Value::set(const std::string& key, T&& value) const {
    detail::Json o = detail::op("set");
    o.set("o", detail::toWire(*this));
    o.set("k", detail::Json::string(key));
    o.set("v", detail::toWire(detail::toValue(std::forward<T>(value))));
    detail::request(std::move(o));
}

template <ToJs T>
void Value::set(int index, T&& value) const {
    detail::Json o = detail::op("set");
    o.set("o", detail::toWire(*this));
    o.set("k", detail::Json::number(index));
    o.set("v", detail::toWire(detail::toValue(std::forward<T>(value))));
    detail::request(std::move(o));
}

template <CallResult R, ToJs... A>
R Value::call(const std::string& method, A&&... args) const {
    detail::Json o = detail::op("call");
    o.set("o", detail::toWire(*this));
    o.set("k", detail::Json::string(method));
    o.set("a", detail::argList(std::forward<A>(args)...));
    return detail::convertResult<R>(detail::request(std::move(o)));
}

template <CallResult R, ToJs... A>
R Value::operator()(A&&... args) const {
    detail::Json o = detail::op("invoke");
    o.set("o", detail::toWire(*this));
    o.set("a", detail::argList(std::forward<A>(args)...));
    return detail::convertResult<R>(detail::request(std::move(o)));
}

template <ToJs... A>
Value Value::new_(A&&... args) const {
    detail::Json o = detail::op("new");
    o.set("o", detail::toWire(*this));
    o.set("a", detail::argList(std::forward<A>(args)...));
    return detail::fromWire(detail::request(std::move(o)));
}

inline std::string Value::typeOf() const {
    switch (kind_) {
        case Kind::Undefined: return "undefined";
        case Kind::Null: return "object";
        case Kind::Bool: return "boolean";
        case Kind::Number: return "number";
        case Kind::String: return "string";
        case Kind::Handle: break;
    }
    detail::Json o = detail::op("typeof");
    o.set("o", detail::toWire(*this));
    return detail::fromWire(detail::request(std::move(o))).string_;
}

inline bool Value::strictlyEquals(const Value& other) const {
    if (kind_ != Kind::Handle && other.kind_ != Kind::Handle) {
        if (kind_ != other.kind_) return false;
        switch (kind_) {
            case Kind::Bool: return bool_ == other.bool_;
            case Kind::Number: return number_ == other.number_;  // NaN !== NaN, as in JavaScript
            case Kind::String: return string_ == other.string_;
            default: return true;  // undefined, null
        }
    }
    detail::Json o = detail::op("eq");
    o.set("o", detail::toWire(*this));
    o.set("v", detail::toWire(other));
    return detail::fromWire(detail::request(std::move(o))).bool_;
}

inline double Value::numberValue() const {
    if (kind_ == Kind::Number) return number_;
    if (kind_ == Kind::Bool) return bool_ ? 1 : 0;
    throw Error("volt2: expected a number, got " + typeOf());
}

inline bool Value::truthy() const {
    switch (kind_) {
        case Kind::Undefined:
        case Kind::Null: return false;
        case Kind::Bool: return bool_;
        case Kind::Number: return number_ != 0 && !std::isnan(number_);
        case Kind::String: return !string_.empty();
        case Kind::Handle: return true;
    }
    return false;
}

template <FromJs T>
T Value::as() const {
    if constexpr (std::same_as<T, Value>) {
        return *this;
    } else if constexpr (std::same_as<T, bool>) {
        return truthy();
    } else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(numberValue());
    } else if constexpr (std::is_integral_v<T>) {
        double n = std::trunc(numberValue());
        double limit = std::ldexp(1.0, std::numeric_limits<T>::digits);  // exclusive upper bound
        double lowest = std::is_signed_v<T> ? -limit : 0.0;
        if (!(n >= lowest && n < limit)) throw Error("volt2: number out of range for this integer type");
        return static_cast<T>(n);
    } else {
        if (kind_ != Kind::String) throw Error("volt2: expected a string, got " + typeOf());
        return string_;
    }
}

// Callables js::function accepts: taking all arguments, the first one, or none.
template <class F>
concept TakesArgs = std::invocable<F&, const Args&>;
template <class F>
concept TakesFirst = !TakesArgs<F> && std::invocable<F&, const Value&>;
template <class F>
concept TakesNothing = !TakesArgs<F> && !TakesFirst<F> && std::invocable<F&>;
template <class F>
concept Callable = TakesArgs<std::decay_t<F>> || TakesFirst<std::decay_t<F>> || TakesNothing<std::decay_t<F>>;

// Wraps a C++ function so JavaScript can call it. It may take (const js::Args&),
// (const js::Value& first) or nothing, and return void or anything ToJs.
// The function lives until the page is reloaded.
template <Callable F>
Value function(F&& f) {
    using Fn = std::decay_t<F>;
    auto fn = std::make_shared<Fn>(std::forward<F>(f));
    if constexpr (TakesArgs<Fn>) {
        return detail::makeFunction([fn](const Args& args) {
            return detail::resultToValue([&]() -> decltype(auto) { return (*fn)(args); });
        });
    } else if constexpr (TakesFirst<Fn>) {
        return detail::makeFunction([fn](const Args& args) {
            Value first = args.empty() ? Value() : args[0];
            return detail::resultToValue([&]() -> decltype(auto) { return (*fn)(first); });
        });
    } else {
        return detail::makeFunction([fn](const Args&) {
            return detail::resultToValue([&]() -> decltype(auto) { return (*fn)(); });
        });
    }
}

}  // namespace js

// ============================================================================
// The page client, embedded from web/volt2.js (generated, do not edit by hand)
// ============================================================================
namespace detail {
// <volt2-js>
inline const char* const kClientJsParts[] = {
R"VOLT2JS(// volt2 page client. The same file serves both builds:
//   development: volt2.startDev() talks to your native program over synchronous HTTP;
//   WebAssembly: the module calls volt2.attachWasm() and talks to it directly.
//
// C++ asks for operations (get, set, call, ...) and this file runs them; JavaScript
// calls C++ functions through callNative. Values cross as JSON: booleans, finite
// numbers, strings and null as themselves; undefined as {u:1}; NaN and the
// infinities as {f:"NaN"}; anything else as {h:id}, a handle that C++ releases
// when it no longer needs it.
//
// volt2.hpp embeds a copy of this file. After editing it, run tools/embed_js.py.
(function () {
  'use strict';

  const handles = new Map();
  let nextHandle = 1;
  let transport = null; // (message) => reply; set by startDev or attachWasm

  function toWire(value) {
    switch (typeof value) {
      case 'undefined':
        return { u: 1 };
      case 'boolean':
      case 'string':
        return value;
      case 'number':
        return Number.isFinite(value) ? value : { f: String(value) };
      default: {
        if (value === null) return null;
        const id = nextHandle++;
        handles.set(id, value);
        return { h: id };
      }
    }
  }

  function fromWire(wire) {
    if (wire === null || typeof wire !== 'object') return wire;
    if ('h' in wire) {
      if (!handles.has(wire.h)) throw new Error('volt2: JavaScript value #' + wire.h + ' was already released');
      return handles.get(wire.h);
    }
    if ('u' in wire) return undefined;
    if ('f' in wire) return Number(wire.f);
    throw new Error('volt2: malformed value ' + JSON.stringify(wire));
  }

  function release(ids) {
    if (ids) for (const id of ids) handles.delete(id);
  }

  function kind(value) {
    return value === null ? 'null' : typeof value;
  }

  function perform(op) {
    const args = op.a ? op.a.map(fromWire) : [];
    switch (op.op) {
      case 'global':
        return op.k === undefined ? globalThis : globalThis[op.k];
      case 'get':
        return fromWire(op.o)[fromWire(op.k)];
      case 'set':
        fromWire(op.o)[fromWire(op.k)] = fromWire(op.v);
        return undefined;
      case 'call': {
        const target = fromWire(op.o);
        const method = target[op.k];
        if (typeof method !== 'function') {
          throw new TypeError("'" + op.k + "' is not a function (it is " + kind(method) + ')');
        }
        return Reflect.apply(method, target, args);
      }
      case 'invoke': {
        const fn = fromWire(op.o);
        if (typeof fn !== 'function') throw new TypeError(kind(fn) + ' is not a function');
        return Reflect.apply(fn, undefined, args);
      }
      case 'new':
        return Reflect.construct(fromWire(op.o), args);
      case 'typeof':
        return typeof fromWire(op.o);
      case 'eq':
        return fromWire(op.o) === fromWire(op.v);
      case 'fn': {
        const id = op.k;
        return function () {
          return callNative(id, Array.prototype.slice.call(arguments));
        };
      }
      default:
        throw new Error('volt2: unknown operation ' + op.op);
    }
  }

  // Runs one operation for C++. Errors are returned, never thrown across the bridge.
  // Released handles are dropped afterwards, so a reply may still refer to them.
  function exec(op) {
    try {
      return { v: toWire(perform(op)) };
    } catch (e) {
      return { error: e instanceof Error ? e.name + ': ' + e.message : String(e) };
    } finally {
      release(op.rel);
    }
  }

  // Sends a message to C++ and waits for its return value. In development the
  // native side may first answer with operations; we run them and keep waiting,
  // which is what lets calls nest in both directions.
  function converse(message) {
    for (;;) {
      const reply = transport(message);
      if (reply.t === 'op') {
        message = Object.assign({ t: 'result' }, exec(reply));
        continue;
      }
      const failed = 'error' in reply;
      const value = failed ? undefined : fromWire(reply.v);
      release(reply.rel);
      if (failed) throw new Error(reply.error);
      return value;
    }
  }

  function callNative(id, args) {
    if (!transport) throw new Error('volt2: not connected to C++');
    return converse({ t: 'call', id: id, a: args.map(toWire) });
  }

  function showProblem(error) {
    const box = document.createElement('pre');
    box.style.cssText = 'color:#b00020;background:#fff0f0;border:1px solid #b00020;padding:12px;white-space:pre-wrap';
    box.textContent = String((error && error.message) || error);
    document.body.appendChild(box);
  }

  function startDev() {
    const page = Date.now().toString(36) + Math.random().toString(36).slice(2);
    transport = function (message) {
      message.s = page;
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/volt2/bridge', false); // synchronous on purpose, see the README
      xhr.setRequestHeader('Content-Type', 'application/json');
      try {
        xhr.send(JSON.stringify(message));
      } catch (e) {
        throw new Error('volt2: cannot reach your program. Is it still running?');
      }
      if (xhr.status !== 200) throw new Error('volt2: your program answered HTTP ' + xhr.status);
      return JSON.parse(xhr.responseText);
    };
    try {
      converse({ t: 'start' });
    } catch (e) {
      showProblem(e);
      throw e;
    }
  }

  function attachWasm(dispatch) {
    transport = function (message) {
      return JSON.parse(dispatch(JSON.stringify(message)));
    };
  }

  function execJson(text) {
    return JSON.stringify(exec(JSON.parse(text)));
  }

  globalThis.volt2 = {
    startDev: startDev,
    attachWasm: attachWasm,
    execJson: execJson,
    handleCount: function () {
      return handles.size;
    },
  };
})();
)VOLT2JS",
};
// </volt2-js>

inline const std::string& clientJs() {
    static const std::string text = [] {
        std::string all;
        for (const char* part : kClientJsParts) all += part;
        return all;
    }();
    return text;
}
}  // namespace detail

#ifndef __EMSCRIPTEN__
// ============================================================================
// Development backend: a local HTTP server and the synchronous bridge
// ============================================================================
namespace detail {

namespace net {
#ifdef _WIN32
using Socket = SOCKET;
inline const Socket kInvalid = INVALID_SOCKET;
inline void closeSocket(Socket s) { ::closesocket(s); }
inline bool startup() {
    static const bool ok = [] {
        WSADATA data;
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
#else
using Socket = int;
inline const Socket kInvalid = -1;
inline void closeSocket(Socket s) { ::close(s); }
inline bool startup() { return true; }
#endif

inline void tune(Socket s) {
    int yes = 1;
    // Without TCP_NODELAY, small replies on a kept-alive connection stall for ~40 ms.
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof yes);
#ifdef SO_NOSIGPIPE
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&yes), sizeof yes);
#endif
}

inline bool sendAll(Socket s, const std::string& data) {
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    size_t sent = 0;
    while (sent < data.size()) {
        auto n = ::send(s, data.data() + sent, static_cast<int>(data.size() - sent), flags);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

inline Socket listenOn(int port) {
    Socket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid) return kInvalid;
#ifndef _WIN32
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || ::listen(s, 64) != 0) {
        closeSocket(s);
        return kInvalid;
    }
    return s;
}
}  // namespace net

// One HTTP request from the page, waiting for its one reply.
struct Pending {
    Json body;
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    std::string reply;
};

// The page and C++ take turns: every message the page sends gets exactly one reply.
// A reply is either the final return value or an operation to run first; the page
// answers an operation with a new message, so calls nest in both directions.
class Bridge final : public Transport {
public:
    // Connection threads: queue a message and wait for its reply.
    std::string deliver(Json body) {
        const Json* type = body.find("t");
        const Json* page = body.find("s");
        if (!type || type->type() != Json::Type::String || !page || page->type() != Json::Type::String) {
            return errorReply("volt2: malformed message").dump();
        }
        auto pending = std::make_shared<Pending>();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (type->asString() == "start") {
                page_ = page->asString();  // the newest page load wins
            } else if (page->asString() != page_) {
                return errorReply("volt2: a newer page replaced this one. Reload to continue.").dump();
            }
            pending->body = std::move(body);
            inbox_.push_back(pending);
        }
        arrived_.notify_one();
        std::unique_lock<std::mutex> lock(pending->mutex);
        pending->ready.wait(lock, [&] { return pending->done; });
        return pending->reply;
    }

    // Session thread from here on.
    std::shared_ptr<Pending> next() {
        std::unique_lock<std::mutex> lock(mutex_);
        arrived_.wait(lock, [&] { return !inbox_.empty(); });
        std::shared_ptr<Pending> pending = inbox_.front();
        inbox_.pop_front();
        return pending;
    }

    bool isCurrentPage(const Json& body) {
        std::lock_guard<std::mutex> lock(mutex_);
        const Json* page = body.find("s");
        return page && page->asString() == page_;
    }

    // Replies to the message currently being served.
    void answer(Json reply) {
        attachReleases(reply);
        std::shared_ptr<Pending> pending = std::move(current);
        current.reset();
        if (!pending) return;
        {
            std::lock_guard<std::mutex> lock(pending->mutex);
            pending->reply = reply.dump();
            pending->done = true;
        }
        pending->ready.notify_one();
    }

    Json request(const Json& operation) override {
        if (replacement) throw PageReplaced{};
        Json out = operation;
        out.set("t", Json::string("op"));
        answer(std::move(out));
        for (;;) {
            current = next();
            const Json& body = current->body;
            const std::string& type = body.find("t")->asString();
            if (type == "result") return body;  // {"v":...} or {"error":...}
            if (type == "call") {               // the page called C++ while running our operation
                answer(serveCall(body));
                continue;
            }
            if (type == "start") {  // the page was reloaded or another tab opened
                replacement = std::move(current);
                throw PageReplaced{};
            }
            answer(errorReply("volt2: unexpected message"));
        }
    }

    std::shared_ptr<Pending> current;      // the message being served
    std::shared_ptr<Pending> replacement;  // a page load that interrupted the current run

private:
    std::mutex mutex_;
    std::condition_variable arrived_;
    std::deque<std::shared_ptr<Pending>> inbox_;
    std::string page_;
};

inline const char* devPage() {
    return "<!doctype html>\n"
           "<html>\n"
           "<head>\n"
           "<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "<title>volt2</title>\n"
           "</head>\n"
           "<body>\n"
           "<script src=\"/volt2/volt2.js\"></script>\n"
           "<script>volt2.startDev();</script>\n"
           "</body>\n"
           "</html>\n";
}

inline std::string httpResponse(int status, const char* reason, const char* type, const std::string& body) {
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    out += std::string("Content-Type: ") + type + "\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n";
    out += body;
    return out;
}

inline std::string handleHttp(Bridge& bridge, const std::string& method, const std::string& path, const std::string& body) {
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        return httpResponse(200, "OK", "text/html; charset=utf-8", devPage());
    }
    if (method == "GET" && path == "/volt2/volt2.js") {
        return httpResponse(200, "OK", "text/javascript; charset=utf-8", clientJs());
    }
    if (method == "POST" && path == "/volt2/bridge") {
        Json message;
        try {
            message = Json::parse(body);
        } catch (const std::exception& e) {
            return httpResponse(400, "Bad Request", "text/plain; charset=utf-8", e.what());
        }
        return httpResponse(200, "OK", "application/json; charset=utf-8", bridge.deliver(std::move(message)));
    }
    if (path == "/favicon.ico") return httpResponse(204, "No Content", "text/plain", "");
    return httpResponse(404, "Not Found", "text/plain; charset=utf-8", "Not found");
}

inline std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

// Serves one kept-alive connection until the browser closes it.
inline void serveConnection(net::Socket socket, Bridge* bridge) {
    std::string buffer;
    char chunk[16384];
    auto fill = [&]() {
        auto n = ::recv(socket, chunk, static_cast<int>(sizeof chunk), 0);
        if (n <= 0) return false;
        buffer.append(chunk, static_cast<size_t>(n));
        return true;
    };
    for (;;) {
        size_t end;
        bool ok = true;
        while ((end = buffer.find("\r\n\r\n")) == std::string::npos) {
            if (buffer.size() > 65536 || !fill()) { ok = false; break; }
        }
        if (!ok) break;
        const std::string head = buffer.substr(0, end);
        size_t first = head.find(' ');
        size_t second = first == std::string::npos ? first : head.find(' ', first + 1);
        if (second == std::string::npos) break;
        std::string method = head.substr(0, first);
        std::string path = head.substr(first + 1, second - first - 1);
        path = path.substr(0, path.find('?'));

        size_t length = 0;
        bool close = false;
        for (size_t pos = head.find("\r\n"); pos != std::string::npos;) {
            size_t next = head.find("\r\n", pos + 2);
            std::string line = head.substr(pos + 2, (next == std::string::npos ? head.size() : next) - pos - 2);
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = lower(line.substr(0, colon));
                std::string value = line.substr(colon + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                if (name == "content-length") length = static_cast<size_t>(std::strtoull(value.c_str(), nullptr, 10));
                if (name == "connection" && lower(value).find("close") != std::string::npos) close = true;
            }
            pos = next;
        }
        if (length > (64u << 20)) break;
        const size_t total = end + 4 + length;
        while (ok && buffer.size() < total) ok = fill();
        if (!ok) break;
        std::string body = buffer.substr(end + 4, length);
        buffer.erase(0, total);
        if (!net::sendAll(socket, handleHttp(*bridge, method, path, body)) || close) break;
    }
    net::closeSocket(socket);
}

inline std::string environment(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    size_t size = 0;
    std::string result;
    if (_dupenv_s(&value, &size, name) == 0 && value) result = value;
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

inline void openBrowser(const std::string& url) {
#if defined(_WIN32)
    std::string command = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    std::string command = "open \"" + url + "\"";
#else
    std::string command = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
#endif
    int ignored = std::system(command.c_str());
    (void)ignored;
}

// The session loop. It runs on the thread that called volt2::run, so your site
// and every handler run there too, one at a time, like JavaScript.
[[noreturn]] inline void serveForever(Bridge& bridge, const Site& site) {
    for (;;) {
        std::shared_ptr<Pending> message = bridge.replacement ? std::move(bridge.replacement) : bridge.next();
        bridge.replacement.reset();
        bridge.current = message;
        const std::string type = message->body.find("t")->asString();
        try {
            if (type == "start") {
                if (!bridge.isCurrentPage(message->body)) {  // an even newer load is queued
                    bridge.answer(errorReply("volt2: a newer page replaced this one. Reload to continue."));
                    continue;
                }
                beginPage();
                Json reply = returnReply();
                try {
                    Session session;
                    site(session);
                    reply.set("v", toWire(js::Value()));
                } catch (const PageReplaced&) {
                    throw;
                } catch (const std::exception& e) {
                    logError(std::string("your site threw an exception: ") + e.what());
                    reply.set("error", Json::string(std::string("C++ exception: ") + e.what()));
                }
                bridge.answer(std::move(reply));
            } else if (type == "call") {
                bridge.answer(serveCall(message->body));
            } else {
                bridge.answer(errorReply("volt2: unexpected message"));
            }
        } catch (const PageReplaced&) {
            // A newer page load interrupted this run; it is served next.
        }
    }
}

}  // namespace detail

// Development: serves the site and runs it for each page load. Does not return
// unless the server cannot start (then it returns 1).
inline int run(Site site, Options options = Options()) {
    using namespace detail;
    if (!net::startup()) {
        logError("could not start networking");
        return 1;
    }
    std::string fixed = environment("VOLT2_PORT");
    int port = options.port ? options.port : std::atoi(fixed.c_str());
    net::Socket listener = net::kInvalid;
    if (port) {
        listener = net::listenOn(port);
    } else {
        for (int candidate = 8080; candidate < 8100 && listener == net::kInvalid; ++candidate) {
            listener = net::listenOn(candidate);
            port = candidate;
        }
    }
    if (listener == net::kInvalid) {
        logError("could not listen on 127.0.0.1:" + std::to_string(port) + ". Set VOLT2_PORT to a free port.");
        return 1;
    }

    static Bridge bridge;  // outlives the detached connection threads
    runtime().transport = &bridge;
    std::thread([listener] {
        for (;;) {
            net::Socket connection = ::accept(listener, nullptr, nullptr);
            if (connection == net::kInvalid) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            net::tune(connection);
            std::thread(serveConnection, connection, &bridge).detach();
        }
    }).detach();

    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::printf("volt2: your site is running at %s (press Ctrl+C to stop)\n", url.c_str());
    std::fflush(stdout);
    if (options.openBrowser && environment("VOLT2_NO_BROWSER").empty()) openBrowser(url);
    serveForever(bridge, site);
}

#else
// ============================================================================
// WebAssembly backend: direct calls into the page
// ============================================================================
namespace detail {

struct WasmTransport final : Transport {
    Json request(const Json& operation) override {
        static const emscripten::val client = emscripten::val::global("volt2");
        return Json::parse(client.call<std::string>("execJson", operation.dump()));
    }
};

// Called by the page when JavaScript calls a C++ function.
inline std::string wasmDispatch(std::string text) {
    return serveCall(Json::parse(text)).dump();
}

}  // namespace detail

// WebAssembly: runs the site once, then returns; the page keeps calling into the module.
inline int run(Site site, Options = Options()) {
    using namespace detail;
    static bool registered = false;
    if (!registered) {
        emscripten::function("volt2_dispatch", &wasmDispatch);
        registered = true;
    }
    emscripten::val client = emscripten::val::global("volt2");
    if (client.isUndefined()) {
        logError("volt2.js is not loaded. Add <script src=\"volt2.js\"></script> before app.js.");
        return 1;
    }
    static WasmTransport transport;
    runtime().transport = &transport;
    client.call<void>("attachWasm", emscripten::val::module_property("volt2_dispatch"));
    beginPage();
    try {
        Session session;
        site(session);
    } catch (const std::exception& e) {
        logError(std::string("your site threw an exception: ") + e.what());
        return 1;
    }
    return 0;
}
#endif

}  // namespace volt2

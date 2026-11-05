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
//   VOLT2_WARN_AT=500   warn once C++ holds this many JavaScript values or functions
//                       (default 10000; the threshold doubles after each warning)
//
// Contents. Each part starts with a banner of "// ====" lines and the title below; the
// names next to it are what to search for inside it.
//
//   JSON                   the messages between C++ and the page: class Json
//   js::Value              a JavaScript value seen from C++: class Value, class Function
//   Runtime state          class Session, struct Options, struct Runtime, request (C++ calls
//                          the page), serveCall (the page calls C++), makeFunction, intern
//                          and releaseHandle (lifetimes), noticeGrowth (leak warnings)
//   Markup                 identity at creation: mixId, struct CallSite, callScope
//                          state: struct Circle (a component call), struct Circles (a
//                          page's), ComponentScope and ItemScope, local
//                          the language: class VirtualNode, None, struct Children (what a
//                          child can be), FirstChild and FirstProp (call sites), class
//                          Element, class Tag, class Component, Map, Loop
//                          names: namespace tag, class BasicAttribute, namespace attr
//                          (KeyName is attr::Key), namespace event
//   Rendering              class Renderer: renderNow (one render, and the patch format),
//                          diff, identify and find (matching each parent's children),
//                          children, update and create (the diff), dispatch (events);
//                          startPage; run
//   The page client        web/volt2.js, embedded by tools/embed_js.py: do not edit here
//   Development backend    the HTTP server and the synchronous bridge: class Bridge,
//                          serveConnection, openBrowser, runSite
//   WebAssembly backend    the module's side of the page client: wasmDispatch, runSite
#pragma once

// volt2 needs C++23: its markup, tag::Div[attr::Id = "x", attr::Class = "y"](...), uses
// C++23's multidimensional subscript operator, so that is the feature checked here.
#if !defined(__cpp_multidimensional_subscript)
#  if defined(_MSC_VER)
#    error "volt2 needs C++23. In Visual Studio: Project Properties > C/C++ > Language > C++ Language Standard > Preview - ISO C++23 (/std:c++23preview)."
#  else
#    error "volt2 needs C++23, for example -std=c++23 (g++ 12+, clang 15+, em++)."
#  endif
#endif

#include <algorithm>
#include <atomic>
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
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
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
    enum class Type { Null, Bool, Number, String, Array, Object, Raw };

    Json() = default;
    static Json boolean(bool value) { Json j; j.type_ = Type::Bool; j.bool_ = value; return j; }
    static Json number(double value) { Json j; j.type_ = Type::Number; j.number_ = value; return j; }
    static Json string(std::string value) { Json j; j.type_ = Type::String; j.string_ = std::move(value); return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }
    // JSON text written as it is, for parts written directly (a render's patch).
    static Json raw(std::string text) { Json j; j.type_ = Type::Raw; j.string_ = std::move(text); return j; }

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
    static void writeString(std::string& out, std::string_view text);

private:
    struct Parser;
    void expect(Type type) const {
        if (type_ != type) throw std::runtime_error("volt2: unexpected JSON value type");
    }
    void write(std::string& out) const;

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

inline void Json::writeString(std::string& out, std::string_view text) {
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
        case Type::Raw: out += string_; break;
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
class Function;
using Args = std::vector<Value>;

// Types that can be passed to JavaScript: Value, Function (by name), nullptr, bool, numbers, strings.
template <class T>
concept ToJs = std::same_as<std::remove_cvref_t<T>, Value> || std::same_as<std::remove_cvref_t<T>, Function> ||
               std::same_as<std::remove_cvref_t<T>, std::nullptr_t> || std::is_arithmetic_v<std::remove_cvref_t<T>> ||
               std::convertible_to<T, std::string>;

// Types a JavaScript value can be converted to with Value::as.
template <class T>
concept FromJs = std::same_as<T, Value> || std::is_arithmetic_v<T> || std::same_as<T, std::string>;

// Result types for call and operator(): FromJs, or void to ignore the result.
template <class T>
concept CallResult = std::is_void_v<T> || FromJs<T>;

}  // namespace js

namespace detail {

// A JavaScript object, function or other non-primitive that C++ refers to. There is
// one Handle per object, shared by every Value referring to it; the page keeps the
// object alive until the last of those Values is gone.
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

using Callback = std::function<js::Value(const js::Args&)>;
js::Function makeFunction(Callback callback);

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
    // JavaScript ===. Objects compare by identity, locally: each page object has one handle.
    bool strictlyEquals(const Value& other) const;

    // == and std::hash follow JavaScript Map keys (SameValueZero): like ===, except that
    // NaN equals NaN. So a Value can key an unordered_map, for example by element.
    friend bool operator==(const Value& a, const Value& b) noexcept;
    std::size_t hash() const noexcept;

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

// A C++ function that JavaScript can call, made by js::function. It works for as long
// as you hold it: copies share it, and when the last copy is destroyed the C++ function
// is released (calling it from JavaScript then throws). Pass it to JavaScript by name,
// from a variable that lives as long as JavaScript may call it:
//
//     onClick = volt2::js::function([] { ... });   // a member, say
//     button.call("addEventListener", "click", onClick);
class Function {
public:
    Function() = default;  // empty
    // The JavaScript function, or undefined when empty.
    const Value& value() const;
    explicit operator bool() const { return owner_ != nullptr; }

private:
    struct Owner;
    explicit Function(std::shared_ptr<Owner> owner) : owner_(std::move(owner)) {}
    friend Function detail::makeFunction(detail::Callback);

    std::shared_ptr<Owner> owner_;
};

struct Function::Owner {
    long long id;
    unsigned long long generation;
    Value wrapper;
    Owner(long long i, unsigned long long g, Value w) : id(i), generation(g), wrapper(std::move(w)) {}
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    ~Owner();
};

}  // namespace js

// What C++ currently holds of the page. Useful for finding leaks.
struct Stats {
    std::size_t values = 0;     // distinct JavaScript objects held through js::Values
    std::size_t functions = 0;  // live js::Functions (C++ functions JavaScript can call)
};

// ============================================================================
// Runtime state and the call dispatch shared by both builds
// ============================================================================
namespace detail {
class Renderer;
struct Sessions;
}  // namespace detail

// Your session type derives from this. volt2 creates one per page load and destroys it
// when that page goes away (reloaded, or replaced by another tab): its constructor is the
// one-time setup, its members are the page's state, its destructor the cleanup.
class Session {
public:
    Session() = default;
    virtual ~Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Renders the page again. Event handlers do this by themselves; call it from C++ that
    // changes the state some other way, such as a js::function the page calls.
    void invalidate();

private:
    friend struct detail::Sessions;
    detail::Renderer* renderer_ = nullptr;
};

template <class S>
concept SessionType = std::derived_from<S, Session> && std::default_initializable<S>;

using js::Value;

// What every page of the site loads in its <head>: stylesheets, fonts, CSS of your own and
// scripts, given to run:
//
//     volt2::run(site, {.head = {volt2::head::Pico, volt2::head::GoogleFont("Inter:wght@400;600"),
//                                volt2::head::Css("body { font-family: Inter, sans-serif; }")}});
//
// While developing they are written into the page's <head>, so the first render is already
// styled. In WebAssembly they are added when the site starts, and the page stays hidden until
// the stylesheets have loaded (3 seconds at most).
namespace head {
struct Item {
    enum class Kind { Stylesheet, Css, Script };
    Kind kind;
    std::string text;  // the URL, or the CSS itself
};
inline Item Stylesheet(std::string url) { return {Item::Kind::Stylesheet, std::move(url)}; }
inline Item Css(std::string css) { return {Item::Kind::Css, std::move(css)}; }
inline Item Script(std::string url) { return {Item::Kind::Script, std::move(url)}; }

// A Google Fonts family, written the way Google Fonts writes it: "Inter",
// "Inter:wght@400;700", "Roboto Mono". Then use it in your CSS: font-family: Inter, sans-serif.
inline Item GoogleFont(std::string_view family) {
    std::string name(family);
    for (char& c : name) {
        if (c == ' ') c = '+';
    }
    return Stylesheet("https://fonts.googleapis.com/css2?family=" + name + "&display=swap");
}

// CSS libraries that make plain HTML look good, pinned, from the jsDelivr CDN.
// Pico (picocss.com): styles semantic HTML (header, article, button, input...), light and
// dark; its classes (container, grid, secondary, outline...) are optional.
inline const Item Pico = Stylesheet("https://cdn.jsdelivr.net/npm/@picocss/pico@2.1.1/css/pico.min.css");
inline const Item PicoClassless =
    Stylesheet("https://cdn.jsdelivr.net/npm/@picocss/pico@2.1.1/css/pico.classless.min.css");
// Water.css (watercss.kognise.dev): no classes at all, light and dark.
inline const Item WaterCss = Stylesheet("https://cdn.jsdelivr.net/npm/water.css@2.1.1/out/water.min.css");
// Chota (jenil.github.io/chota): tiny, with a grid and utility classes.
inline const Item Chota = Stylesheet("https://cdn.jsdelivr.net/npm/chota@0.9.2/dist/chota.min.css");
// Tailwind CSS, compiled in the browser as the page changes: good for trying things out;
// Tailwind's authors advise building the CSS ahead of time for a real site.
inline const Item TailwindBrowser = Script("https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4.3.4");
}  // namespace head

// For example: volt2::run(site, {.port = 8123, .openBrowser = false, .head = {volt2::head::Pico}});
struct Options {
    int port = 0;             // 0: VOLT2_PORT, else the first free port from 8080
    bool openBrowser = true;  // development only; VOLT2_NO_BROWSER=1 also disables it
    std::vector<head::Item> head;
};

namespace detail {

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
    std::thread::id thread;             // the thread running volt2::run; only it may touch the page
    unsigned long long generation = 1;  // bumped on every page load
    std::vector<long long> releases;    // handles to release, sent with the next message
    std::unordered_map<long long, std::weak_ptr<Handle>> live;  // the one Handle per page object
    long long nextCallback = 1;
    std::unordered_map<long long, std::shared_ptr<Callback>> callbacks;
    bool tearingDown = false;    // destroying a gone page's session: JavaScript calls do nothing
    std::size_t warnAt = 10000;  // development leak warnings, see VOLT2_WARN_AT
    std::size_t warnValuesAt = 10000;
    std::size_t warnFunctionsAt = 10000;

    // Values and Functions destroyed on other threads: queued here (id, generation) and
    // applied on volt2's thread before its next operation or call.
    std::mutex elsewhereMutex;
    std::atomic<bool> elsewherePending{false};
    std::vector<std::pair<long long, unsigned long long>> releasedElsewhere;
    std::vector<std::pair<long long, unsigned long long>> functionsDroppedElsewhere;
};

// Never destroyed, so Values that outlive main cannot touch a dead runtime.
inline Runtime& runtime() {
    static Runtime* instance = new Runtime();
    return *instance;
}

inline void logError(const std::string& message) {
    std::fprintf(stderr, "volt2: %s\n", message.c_str());
    std::fflush(stderr);
}

inline bool onVoltThread() { return std::this_thread::get_id() == runtime().thread; }

inline void requireVoltThread() {
    if (!onVoltThread()) {
        throw js::Error("volt2: JavaScript values can only be used on the thread that called volt2::run");
    }
}

// The runtime is not thread-safe, so a Value or Function destroyed on another thread only
// queues its release; volt2's thread applies it (see settleElsewhere).
inline void queueElsewhere(std::vector<std::pair<long long, unsigned long long>> Runtime::*queue, long long id,
                           unsigned long long generation) {
    Runtime& r = runtime();
    {
        std::lock_guard<std::mutex> lock(r.elsewhereMutex);
        (r.*queue).emplace_back(id, generation);
    }
    r.elsewherePending.store(true, std::memory_order_release);
}

inline void releaseHandle(long long id, unsigned long long generation) {
    Runtime& r = runtime();
    if (generation != r.generation) return;  // that page is gone, and its table with it
    r.releases.push_back(id);
    auto slot = r.live.find(id);
    if (slot != r.live.end() && slot->second.expired()) r.live.erase(slot);
}

inline Handle::~Handle() {
    if (onVoltThread()) releaseHandle(id, generation);
    else queueElsewhere(&Runtime::releasedElsewhere, id, generation);
}

// On volt2's thread: apply releases queued by other threads. Functions first, so a
// function destroyed elsewhere can never be called again, even if its lambda pointed
// at memory that is gone now.
inline void settleElsewhere() {
    Runtime& r = runtime();
    if (!r.elsewherePending.exchange(false, std::memory_order_acquire)) return;
    std::vector<std::pair<long long, unsigned long long>> handles, functions;
    {
        std::lock_guard<std::mutex> lock(r.elsewhereMutex);
        handles.swap(r.releasedElsewhere);
        functions.swap(r.functionsDroppedElsewhere);
    }
    for (auto [id, generation] : functions) {
        if (generation == r.generation) r.callbacks.erase(id);
    }
    for (auto [id, generation] : handles) releaseHandle(id, generation);
}

// The Handle for a page object, shared by every Value that refers to it.
inline std::shared_ptr<Handle> intern(long long id) {
    Runtime& r = runtime();
    std::weak_ptr<Handle>& slot = r.live[id];
    if (std::shared_ptr<Handle> existing = slot.lock()) return existing;
    auto handle = std::make_shared<Handle>(id, r.generation);
    slot = handle;
    // The page may hand back an object whose release we queued but have not sent yet.
    // It still has the same id there, so cancel the release.
    std::erase(r.releases, id);
    return handle;
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
    requireVoltThread();
    if (r.tearingDown) {  // never throw here: we are inside a destructor
        static bool warned = false;
        if (!warned) {
            warned = true;
            logError("a session destructor used JavaScript after its page was gone; such calls do nothing.");
        }
        Json undefined = Json::object();
        undefined.set("u", Json::number(1));
        return undefined;
    }
    settleElsewhere();
    attachReleases(op);
    Json reply = r.transport->request(op);
    if (const Json* error = reply.find("error")) throw js::Error(error->asString());
    const Json* value = reply.find("v");
    if (!value) throw js::Error("volt2: malformed reply from the page");
    return *value;
}

inline Json toWire(const js::Value& value) {
    using Kind = js::Value::Kind;
    requireVoltThread();  // before touching any shared state
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
    requireVoltThread();
    js::Value value;
    switch (wire.type()) {
        case Json::Type::Null: value.kind_ = Kind::Null; break;
        case Json::Type::Bool: value.kind_ = Kind::Bool; value.bool_ = wire.asBool(); break;
        case Json::Type::Number: value.kind_ = Kind::Number; value.number_ = wire.asNumber(); break;
        case Json::Type::String: value.kind_ = Kind::String; value.string_ = wire.asString(); break;
        case Json::Type::Object:
            if (const Json* h = wire.find("h")) {
                value.kind_ = Kind::Handle;
                value.handle_ = intern(static_cast<long long>(h->asNumber()));
            } else if (const Json* f = wire.find("f")) {
                const std::string& name = f->asString();
                value.kind_ = Kind::Number;
                value.number_ = name == "Infinity" ? HUGE_VAL : name == "-Infinity" ? -HUGE_VAL : std::nan("");
            }  // {"u":1}: undefined, already the default
            break;
        case Json::Type::Array:
        case Json::Type::Raw: throw js::Error("volt2: malformed value from the page");
    }
    return value;
}

template <js::ToJs T>
js::Value toValue(T&& x) {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::same_as<D, js::Value>) {
        // A copy, never a move: a temporary argument must keep its handle alive until the
        // operation using it has reached the page.
        return js::Value(x);
    } else if constexpr (std::same_as<D, js::Function>) {
        static_assert(std::is_lvalue_reference_v<T>,
                      "volt2: pass a js::Function by name, from a variable that lives as long as JavaScript may "
                      "call it. A temporary js::Function is released at the end of the statement.");
        if (!x) throw js::Error("volt2: this js::Function is empty");
        return x.value();
    } else if constexpr (std::same_as<D, std::nullptr_t>) return js::Value(nullptr);
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

// Development only: warn when C++ keeps accumulating page values or functions. Each
// warning doubles the next threshold, so a steady leak keeps reporting but rarely.
inline void noticeGrowth() {
#ifndef __EMSCRIPTEN__
    Runtime& r = runtime();
    auto check = [&](std::size_t count, std::size_t& next, const char* what) {
        if (count < next) return;
        while (next <= count) next *= 2;
        std::string message = "C++ now holds " + std::to_string(count) + " " + what +
                              ". If this keeps growing, something keeps js::Values or js::Functions alive longer than intended.";
        logError(message);
        try {  // also in the browser console, where web developers look
            Json find = op("global");
            find.set("k", Json::string("console"));
            js::Value console = fromWire(request(std::move(find)));
            Json warn = op("call");
            warn.set("o", toWire(console));
            warn.set("k", Json::string("warn"));
            Json args = Json::array();
            args.push(Json::string("volt2: " + message));
            warn.set("a", std::move(args));
            request(std::move(warn));
        } catch (const js::Error&) {
        }
    };
    check(r.live.size(), r.warnValuesAt, "JavaScript values");
    check(r.callbacks.size(), r.warnFunctionsAt, "C++ functions callable from JavaScript");
#endif
}

// Runs the C++ function a page asked for. Exceptions become the reply's error, which
// the page rethrows as a JavaScript Error in the caller, so nothing unwinds across the bridge.
inline Json serveCall(const Json& message) {
    Runtime& r = runtime();
    settleElsewhere();  // before the lookup: a function destroyed elsewhere must not run
    Json reply = returnReply();
    try {
        // Arguments first: they hold page objects that must be released even if the call fails.
        js::Args args;
        if (const Json* list = message.find("a")) {
            for (const Json& wire : list->items()) args.push_back(fromWire(wire));
        }
        const Json* id = message.find("id");
        auto it = id ? r.callbacks.find(static_cast<long long>(id->asNumber())) : r.callbacks.end();
        if (it == r.callbacks.end()) throw js::Error("volt2: this C++ function was released (its js::Function is gone)");
        std::shared_ptr<Callback> callback = it->second;  // keeps it alive if the table changes
        js::Value result = (*callback)(args);
        noticeGrowth();  // may talk to the page, so `result` must still hold its object
        reply.set("v", toWire(result));
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

inline js::Function makeFunction(Callback callback) {
    requireVoltThread();
    Runtime& r = runtime();
    long long id = r.nextCallback++;
    r.callbacks[id] = std::make_shared<Callback>(std::move(callback));
    Json o = op("fn");
    o.set("k", Json::number(static_cast<double>(id)));
    js::Value wrapper;
    try {
        wrapper = fromWire(request(std::move(o)));
    } catch (...) {
        r.callbacks.erase(id);
        throw;
    }
    return js::Function(std::make_shared<js::Function::Owner>(id, r.generation, std::move(wrapper)));
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
    ++r.generation;  // first, so values and functions dropped below do nothing
    r.callbacks.clear();
    r.releases.clear();
    r.live.clear();
    r.warnValuesAt = r.warnFunctionsAt = r.warnAt;
    std::lock_guard<std::mutex> lock(r.elsewhereMutex);  // queued releases belong to the old page
    r.releasedElsewhere.clear();
    r.functionsDroppedElsewhere.clear();
}

}  // namespace detail

// Only on the thread that called volt2::run.
inline Stats stats() {
    detail::requireVoltThread();
    detail::settleElsewhere();
    detail::Runtime& r = detail::runtime();
    std::size_t values = 0;
    for (const auto& entry : r.live) values += entry.second.expired() ? 0 : 1;
    return Stats{values, r.callbacks.size()};
}

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
    if (kind_ != other.kind_) return false;
    switch (kind_) {
        case Kind::Bool: return bool_ == other.bool_;
        case Kind::Number: return number_ == other.number_;  // NaN !== NaN, as in JavaScript
        case Kind::String: return string_ == other.string_;
        case Kind::Handle: return handle_ == other.handle_;  // one Handle per page object
        default: return true;                                 // undefined, null
    }
}

inline bool operator==(const Value& a, const Value& b) noexcept {
    if (a.kind_ == Value::Kind::Number && b.kind_ == Value::Kind::Number && std::isnan(a.number_) && std::isnan(b.number_)) {
        return true;
    }
    return a.strictlyEquals(b);
}

inline std::size_t Value::hash() const noexcept {
    switch (kind_) {
        case Kind::Undefined: return 0x51ed270b;
        case Kind::Null: return 0x2c1b3c6d;
        case Kind::Bool: return bool_ ? 0x297a2d39 : 0x4cf5ad43;
        case Kind::Number:
            if (std::isnan(number_)) return 0x7ff80000;
            return std::hash<double>{}(number_ == 0 ? 0.0 : number_);  // +0 and -0 are equal
        case Kind::String: return std::hash<std::string>{}(string_);
        case Kind::Handle: return std::hash<const void*>{}(handle_.get());
    }
    return 0;
}

inline const Value& Function::value() const {
    static const Value undefined;
    return owner_ ? owner_->wrapper : undefined;
}

inline Function::Owner::~Owner() {
    if (!detail::onVoltThread()) {
        detail::queueElsewhere(&detail::Runtime::functionsDroppedElsewhere, id, generation);
        return;
    }
    detail::Runtime& r = detail::runtime();
    if (generation == r.generation) r.callbacks.erase(id);
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
// (const js::Value& first) or nothing, and return void or anything ToJs. It works for
// as long as you hold the returned js::Function.
template <Callable F>
[[nodiscard]] Function function(F&& f) {
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
}  // namespace volt2

template <>
struct std::hash<volt2::js::Value> {
    std::size_t operator()(const volt2::js::Value& value) const noexcept { return value.hash(); }
};

namespace volt2 {

// ============================================================================
// Markup: tag::Div[attr::Id = "x", event::OnClick(handler)](children...)  (both builds)
// ============================================================================
class VirtualNode;
class Element;
class Tag;

namespace detail {
class Renderer;
struct Children;
std::string outline(const VirtualNode& node);

// An attribute, an event handler or a key, as made by attr:: and event::.
struct Prop {
    enum class Kind { Absent, Attribute, Event, Key };
    Kind kind = Kind::Absent;  // Absent: a boolean attribute set to false
    std::string name;          // attribute name, or event type
    std::string value;
    std::function<void(const js::Value&)> handler;
};

// Integers are written exactly; other numbers as JavaScript would show them.
template <class N>
std::string numberText(N number) {
    if constexpr (std::is_integral_v<N>) {
        return std::to_string(number);
    } else {
        double value = static_cast<double>(number);
        if (std::isfinite(value) && std::trunc(value) == value && std::fabs(value) < 1e15) {
            return std::to_string(static_cast<long long>(value));
        }
        if (!std::isfinite(value)) return std::isnan(value) ? "NaN" : value > 0 ? "Infinity" : "-Infinity";
        return Json::number(value).dump();
    }
}

template <class...>
inline constexpr bool dependentFalse = false;

// Identity. Every element remembers where in your code it was made: the call site of its
// tag, and the component calls it was made in. Renders use it to keep each element's DOM
// node, so focus, typed text and scroll positions stay.
constexpr std::uint64_t mixBits(std::uint64_t h) {  // splitmix64's finalizer
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebull;
    return h ^ (h >> 31);
}
constexpr std::uint64_t mixId(std::uint64_t seed, std::uint64_t value) { return mixBits(seed ^ mixBits(value + 0x9e3779b97f4a7c15ull)); }
inline std::uint64_t hashText(std::string_view text) {
    std::uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a
    for (unsigned char c : text) h = (h ^ c) * 0x100000001b3ull;
    return mixBits(h);
}
// A call site: where in the code, and a hash of its file name, line and column worked out
// by the compiler, so it costs nothing when the code runs and is the same on every run.
// Made from std::source_location::current() in a default argument:
// `detail::CallSite at = detail::CallSite(std::source_location::current())`. Small on
// purpose (16 bytes with g++ and clang): it is passed along with every element.
struct CallSite {
    std::source_location where;
    std::uint64_t hash = 0;

    constexpr CallSite() = default;
    consteval explicit CallSite(std::source_location at) : where(at), hash(0xcbf29ce484222325ull) {
        for (const char* c = at.file_name(); *c; ++c) hash = (hash ^ static_cast<unsigned char>(*c)) * 0x100000001b3ull;
        hash = mixId(hash, (static_cast<std::uint64_t>(at.line()) << 32) | at.column());
    }
    // The same place in the code. File names compare as text: each translation unit may have
    // its own copy of a name.
    friend bool operator==(const CallSite& a, const CallSite& b) {
        return a.hash == b.hash && a.where.line() == b.where.line() && a.where.column() == b.where.column() &&
               (a.where.file_name() == b.where.file_name() || std::strcmp(a.where.file_name(), b.where.file_name()) == 0);
    }
};

// The component calls in progress, as a chain: chain * base + call, for each call, and
// how many. Kept this way, the calls of one chain below another can be taken out exactly
// (callsBetween), so an element's identity counts only the component calls between its
// parent element and itself, and a component keeps its insides wherever it is called.
struct CallScope {
    std::uint64_t chain = 0;
    std::uint32_t depth = 0;
};
inline constexpr std::uint64_t kChainBase = 0x9e3779b97f4a7c15ull;  // odd, so it can be divided out
inline CallScope& callScope() {
    thread_local CallScope scope;
    return scope;
}

// Where an element was made: its tag's call site, inside these component calls.
struct Site {
    CallSite at;
    CallScope scope;
};
inline Site siteOf(const CallSite& at) { return {at, callScope()}; }

// The calls in `inner` below `outer`: for an element made in its parent's arguments, the
// component calls between the two. An element made elsewhere and passed in (a node given to
// a component) gets a number of its own, the same on every render.
inline std::uint64_t callsBetween(const CallScope& outer, const CallScope& inner) {
    std::uint64_t power = 1;
    if (inner.depth >= outer.depth) {
        for (std::uint32_t i = outer.depth; i < inner.depth; ++i) power *= kChainBase;
        return mixId(inner.depth - outer.depth, inner.chain - outer.chain * power);
    }
    for (std::uint32_t i = inner.depth; i < outer.depth; ++i) power *= kChainBase;
    return mixId(0x8000000000000000ull | (outer.depth - inner.depth), inner.chain * power - outer.chain);
}

// ---- State: the circles that local() values live in

// A local() value, of any type.
struct LocalValue {
    virtual ~LocalValue() = default;
};
template <class T>
struct LocalOf final : LocalValue {
    explicit LocalOf(T initial) : value(std::move(initial)) {}
    T value;
};

// A component call (or an item of Map or Loop), kept from one render to the next for as long
// as it is called: its local() values live here. A call is known by where it is made and
// inside which call, exactly, so its state is found while it runs, before its elements
// exist. Calls that a render does not make are dropped after it, with their state.
struct Circle {
    struct Local {
        CallSite at;
        unsigned seen = 0;
        const std::type_info* type = nullptr;
        std::unique_ptr<LocalValue> value;
    };

    Circle* parent = nullptr;
    CallSite at;                      // the call, or an item's Map or Loop
    std::optional<std::string> key;   // an item's key
    std::size_t index = 0;            // an item's position, when it has no key
    std::uint64_t hash = 0;           // of the above, to find it among its siblings
    unsigned seen = 0;                // the last render that called it
    bool twice = false;               // called more than once in that render (reported)
    std::size_t nextChild = 0;        // where the next call is looked for first: right after the last
    std::size_t nextLocal = 0;
    std::vector<std::unique_ptr<Circle>> children;
    std::vector<Local> locals;
    std::vector<std::pair<std::uint64_t, std::size_t>> lookup;  // children by hash, when out of order
    unsigned lookupBuilt = 0;

    bool is(const CallSite& site, const std::string* itemKey, std::size_t itemIndex) const {
        if (!(at == site)) return false;
        if (itemKey) return key && *key == *itemKey;
        return !key && index == itemIndex;
    }
    // Where this call is: site > main.cpp:40 > main.cpp:12 [key "a"].
    std::string path() const {
        std::string out = parent ? parent->path() : "site";
        if (!parent) return out;
        const char* file = at.where.file_name();
        const char* name = std::strrchr(file, '/');
        if (const char* back = std::strrchr(file, '\\'); back && (!name || back > name)) name = back;
        out += " > " + std::string(name ? name + 1 : file) + ":" + std::to_string(at.where.line());
        if (key) out += " [key \"" + *key + "\"]";
        return out;
    }
};

// The component calls in progress while the page renders (and Map and Loop items): only
// their places, cheap to keep. Their circles are found when a local() needs them.
struct Frame {
    const CallSite* at;
    const std::string* key;
    std::size_t index;
    Circle* circle;  // once found
};

// The circles of one page, and the render in progress.
struct Circles {
    Circle root;
    unsigned generation = 0;
    std::vector<Frame> frames;
    std::vector<std::string> warnings;  // for the console, after the render
    std::unordered_set<std::string> warned;

    void warn(std::string message) {
        if (warned.insert(message).second) warnings.push_back(std::move(message));
    }

    // The circle of the innermost call in progress, found (or made) along with the ones
    // around it that are not found yet.
    Circle* current() {
        std::size_t i = frames.size();
        while (i > 0 && !frames[i - 1].circle) --i;
        Circle* circle = i ? frames[i - 1].circle : &root;
        for (; i < frames.size(); ++i) circle = frames[i].circle = enter(*circle, frames[i]);
        return circle;
    }

    // After a render: what it did not reach is gone.
    void sweep(Circle& circle) {
        std::erase_if(circle.locals, [&](const Circle::Local& local) { return local.seen != generation; });
        std::erase_if(circle.children, [&](const std::unique_ptr<Circle>& child) { return child->seen != generation; });
        circle.lookupBuilt = 0;
        for (auto& child : circle.children) sweep(*child);
    }

private:
    // The circle of a call made inside `parent`, found or new.
    Circle* enter(Circle& parent, const Frame& frame) {
        const CallSite& at = *frame.at;
        const std::string* key = frame.key;
        const std::size_t index = frame.index;
        auto& children = parent.children;
        const std::uint64_t hash = mixId(at.hash, key ? hashText(*key) : index);
        Circle* found = nullptr;
        std::size_t position = parent.nextChild;
        if (position < children.size() && children[position]->hash == hash && children[position]->is(at, key, index)) {
            found = children[position].get();
        } else {
            if (parent.lookupBuilt != generation) {  // out of order: look them up, built once per render
                parent.lookup.clear();
                for (std::size_t i = 0; i < children.size(); ++i) parent.lookup.emplace_back(children[i]->hash, i);
                std::ranges::sort(parent.lookup);
                parent.lookupBuilt = generation;
            }
            auto it = std::ranges::lower_bound(parent.lookup, std::pair<std::uint64_t, std::size_t>(hash, 0));
            for (; it != parent.lookup.end() && it->first == hash; ++it) {
                if (children[it->second]->is(at, key, index)) {
                    found = children[it->second].get();
                    position = it->second;
                    break;
                }
            }
        }
        if (!found) {
            auto circle = std::make_unique<Circle>();
            circle->parent = &parent;
            circle->at = at;
            if (key) circle->key = *key;
            circle->index = index;
            circle->hash = hash;
            found = circle.get();
            position = children.size();
            children.push_back(std::move(circle));
            if (parent.lookupBuilt == generation) {  // already looked up this render: it must find this one too
                const std::pair<std::uint64_t, std::size_t> entry(hash, position);
                parent.lookup.insert(std::ranges::lower_bound(parent.lookup, entry), entry);
            }
        } else if (found->seen == generation) {
            found->twice = true;
            warn("the same component call ran twice in one render, at " + found->path() +
                 "; its local() values are shared. For lists, use Map");
        }
        parent.nextChild = position + 1;
        if (found->seen != generation) {
            found->seen = generation;
            found->twice = false;
            found->nextChild = 0;
            found->nextLocal = 0;
        }
        return found;
    }
};
inline Circles*& currentCircles() {
    thread_local Circles* circles = nullptr;
    return circles;
}

// While one is alive, elements are made inside that component call, and local() finds that
// call's state.
class ComponentScope {
public:
    explicit ComponentScope(const CallSite& at) : outer_(callScope()), circles_(currentCircles()) {
        callScope() = {outer_.chain * kChainBase + at.hash, outer_.depth + 1};
        if (circles_) circles_->frames.push_back({&at, nullptr, 0, nullptr});
    }
    ~ComponentScope() {
        callScope() = outer_;
        if (circles_) circles_->frames.pop_back();
    }
    ComponentScope(const ComponentScope&) = delete;
    ComponentScope& operator=(const ComponentScope&) = delete;

private:
    CallScope outer_;
    Circles* circles_;
};

// One item of Map or Loop: a call of its own, named by its key or position.
class ItemScope {
public:
    ItemScope(const CallSite& at, const std::string* key, std::size_t index) : circles_(currentCircles()) {
        if (circles_) circles_->frames.push_back({&at, key, index, nullptr});
    }
    ~ItemScope() {
        if (circles_) circles_->frames.pop_back();
    }
    ItemScope(const ItemScope&) = delete;
    ItemScope& operator=(const ItemScope&) = delete;

private:
    Circles* circles_;
};

}  // namespace detail

// A piece of the page: an element, some text, or nothing. VirtualNode() renders nothing,
// which is handy in conditionals: `open ? tag::P("details") : VirtualNode()`.
class VirtualNode {
public:
    VirtualNode() = default;
    bool isNothing() const { return kind_ == Kind::Nothing; }

private:
    friend class Element;
    friend class Tag;
    friend class detail::Renderer;
    friend struct detail::Children;
    friend std::string detail::outline(const VirtualNode& node);

    static constexpr std::size_t kNoItem = static_cast<std::size_t>(-1);

    enum class Kind { Nothing, Text, Element };
    Kind kind_ = Kind::Nothing;
    std::string name_;  // the tag name, or the text
    std::vector<detail::Prop> props_;
    std::vector<VirtualNode> children_;
    // Who it is: where it was made, its attr::Key, its position in a range of children.
    detail::Site site_;
    std::optional<std::string> key_;
    std::size_t item_ = kNoItem;
    // Set by the renderer: who it is among its siblings, the number of its DOM node, and
    // (once it is the previous tree) whether the next render kept it.
    enum class Use : std::uint8_t { Unused, Kept, Replaced };
    std::uint64_t own_ = 0;
    std::uint64_t calls_ = 0;
    std::uint64_t number_ = 0;
    Use use_ = Use::Unused;
};

// Nothing, wherever a prop or a node can go, so conditionals type-check:
//     tag::Input[s.locked ? attr::ReadOnly = true : None]()
//     tag::Span[s.tip.empty() ? None : attr::Title = s.tip]()
//     s.open ? tag::P("details") : None
struct NoneType {
    explicit constexpr NoneType(int) {}  // like std::nullopt_t: only volt2::None
    operator detail::Prop() const { return {}; }
    operator VirtualNode() const { return {}; }
};
inline constexpr NoneType None{0};

namespace detail {

template <class T>
concept NodeChild = std::convertible_to<T, VirtualNode>;
template <class T>
concept TextChild = std::convertible_to<T, std::string_view>;
template <class T>
concept NumberChild = std::is_arithmetic_v<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool> &&
                      !std::same_as<std::remove_cvref_t<T>, char>;
template <class T>
concept OptionalChild = std::same_as<std::remove_cvref_t<T>, std::optional<VirtualNode>>;
template <class T>
concept RangeChild = std::ranges::input_range<T> && !TextChild<T> &&
                     std::convertible_to<std::ranges::range_reference_t<T>, VirtualNode>;

struct Children {
    static VirtualNode text(std::string text) {
        VirtualNode node;
        node.kind_ = VirtualNode::Kind::Text;
        node.name_ = std::move(text);
        return node;
    }
    static VirtualNode element(std::string_view name, Site site, std::vector<Prop> props) {
        VirtualNode node;
        node.kind_ = VirtualNode::Kind::Element;
        node.name_ = std::string(name);
        node.site_ = site;
        node.props_.reserve(props.size());
        for (Prop& prop : props) {
            if (prop.kind == Prop::Kind::Key) {
                node.key_ = std::move(prop.value);
            } else {
                node.props_.push_back(std::move(prop));
            }
        }
        return node;
    }
    template <class T>
    static void append(std::vector<VirtualNode>& out, T&& child) {
        if constexpr (NodeChild<T>) {
            VirtualNode node(std::forward<T>(child));
            if (!node.isNothing()) out.push_back(std::move(node));
        } else if constexpr (TextChild<T>) {
            if constexpr (std::is_pointer_v<std::remove_cvref_t<T>>) {  // a pointer, not a string literal
                if (child == nullptr) return;                                 // a null const char* is nothing, like None
            }
            out.push_back(text(std::string(std::string_view(child))));
        } else if constexpr (NumberChild<T>) {
            out.push_back(text(numberText(std::remove_cvref_t<T>(child))));
        } else if constexpr (OptionalChild<T>) {
            if (child) append(out, *std::forward<T>(child));
        } else {
            // A range's items are told apart by their position in it (or by attr::Key), as
            // they usually come from the same line of code.
            std::size_t index = 0;
            for (auto&& item : child) {
                VirtualNode node(std::forward<decltype(item)>(item));
                if (!node.isNothing()) {
                    node.item_ = index;
                    out.push_back(std::move(node));
                }
                ++index;
            }
        }
    }
    static std::vector<VirtualNode>& children(VirtualNode& node) { return node.children_; }
    static void giveKey(VirtualNode& node, const std::string& key) {
        if (node.kind_ == VirtualNode::Kind::Element && !node.key_) node.key_ = key;
    }
};

}  // namespace detail

namespace detail {
// A tree as text, attributes sorted by name, for tests that compare it with the page:
// <div class="x">text<b>bold</b></div>.
inline std::string outline(const VirtualNode& node) {
    auto escape = [](std::string_view text, bool quote) {
        std::string out;
        for (char c : text) {
            if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else if (quote && c == '"') out += "&quot;";
            else out += c;
        }
        return out;
    };
    if (node.kind_ == VirtualNode::Kind::Nothing) return "";
    if (node.kind_ == VirtualNode::Kind::Text) return escape(node.name_, false);
    std::vector<std::pair<std::string_view, std::string_view>> attributes;
    for (const Prop& prop : node.props_) {
        if (prop.kind != Prop::Kind::Attribute) continue;
        std::erase_if(attributes, [&](const auto& a) { return a.first == prop.name; });  // the last one stays
        attributes.emplace_back(prop.name, prop.value);
    }
    std::ranges::sort(attributes);
    std::string out = "<" + node.name_;
    for (const auto& [name, value] : attributes) out += " " + std::string(name) + "=\"" + escape(value, true) + "\"";
    out += ">";
    for (const VirtualNode& child : node.children_) out += outline(child);
    return out + "</" + node.name_ + ">";
}
}  // namespace detail

// What can go between the parentheses: nodes, text, numbers, std::optional<VirtualNode>,
// and ranges of nodes (a std::vector, or a view such as std::views::transform).
template <class T>
concept Child = detail::NodeChild<T> || detail::TextChild<T> || detail::NumberChild<T> || detail::OptionalChild<T> ||
                detail::RangeChild<T>;

// What can go between the brackets: attr:: and event:: props, or None.
template <class T>
concept PropLike = std::same_as<std::remove_cvref_t<T>, detail::Prop> || std::same_as<std::remove_cvref_t<T>, NoneType>;

namespace detail {

// The first child of tag::Name(...) or the first prop of tag::Name[...], which bring the
// call site along. They convert on the spot and cannot be copied or moved, so they cannot
// be kept and shared between calls.
class FirstChild {
public:
    template <Child T>
    FirstChild(T&& child, CallSite at = CallSite(std::source_location::current())) : site_(siteOf(at)) {
        Children::append(nodes_, std::forward<T>(child));
    }
    FirstChild(const FirstChild&) = delete;
    FirstChild(FirstChild&&) = delete;
    FirstChild& operator=(const FirstChild&) = delete;
    FirstChild& operator=(FirstChild&&) = delete;

private:
    friend class volt2::Tag;
    Site site_;
    std::vector<VirtualNode> nodes_;
};

class FirstProp {
public:
    template <PropLike T>
    FirstProp(T&& prop, CallSite at = CallSite(std::source_location::current()))
        : site_(siteOf(at)), prop_(std::forward<T>(prop)) {}
    FirstProp(const FirstProp&) = delete;
    FirstProp(FirstProp&&) = delete;
    FirstProp& operator=(const FirstProp&) = delete;
    FirstProp& operator=(FirstProp&&) = delete;

private:
    friend class volt2::Tag;
    Site site_;
    Prop prop_;
};

}  // namespace detail

// A tag with its props, waiting for its children: what tag::Div[...] returns. Use it in the
// same expression, tag::Div[props](children): a stored one would give every use the same
// identity.
class Element {
public:
    template <Child... C>
    VirtualNode operator()(C&&... children) && {
        VirtualNode node = detail::Children::element(name_, site_, std::move(props_));
        auto& list = detail::Children::children(node);
        (detail::Children::append(list, std::forward<C>(children)), ...);
        return node;
    }
    template <Child... C>
    VirtualNode operator()(C&&...) const& {
        static_assert(detail::dependentFalse<C...>,
                      "volt2: write tag::Name[props](children) in one expression; a stored tag::Name[props] would give "
                      "every element made from it the same identity");
        return {};
    }
    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

private:
    friend class Tag;
    Element(std::string_view name, detail::Site site, std::vector<detail::Prop> props)
        : name_(name), site_(site), props_(std::move(props)) {}

    std::string_view name_;
    detail::Site site_;
    std::vector<detail::Prop> props_;
};

// An HTML element: tag::P("text") or tag::P[props...](children...). Define your own with
// `inline constexpr volt2::Tag MyWidget{"my-widget"};`. Each call site gives its elements
// their identity.
class Tag {
public:
    constexpr explicit Tag(std::string_view name) : name_(name) {}

    Element operator[](detail::CallSite at = detail::CallSite(std::source_location::current())) const {
        return Element(name_, detail::siteOf(at), {});
    }
    template <PropLike... P>
    Element operator[](detail::FirstProp first, P&&... rest) const {
        std::vector<detail::Prop> list;
        list.reserve(1 + sizeof...(P));
        auto keep = [&](detail::Prop prop) {
            if (prop.kind != detail::Prop::Kind::Absent) list.push_back(std::move(prop));
        };
        keep(std::move(first.prop_));
        (keep(std::forward<P>(rest)), ...);
        return Element(name_, first.site_, std::move(list));
    }

    VirtualNode operator()(detail::CallSite at = detail::CallSite(std::source_location::current())) const {
        return detail::Children::element(name_, detail::siteOf(at), {});
    }
    template <Child... C>
    VirtualNode operator()(detail::FirstChild first, C&&... rest) const {
        VirtualNode node = detail::Children::element(name_, first.site_, {});
        auto& list = detail::Children::children(node);
        list = std::move(first.nodes_);
        (detail::Children::append(list, std::forward<C>(rest)), ...);
        return node;
    }

    constexpr std::string_view name() const { return name_; }

private:
    std::string_view name_;
};

// A component: a function that builds part of the page, wrapped where you define it so
// each place that calls it gets its own identity, and so do the elements it makes:
//
//     inline constexpr volt2::Component Card = [](std::string title) {
//         return tag::Div[attr::Class = "card"](tag::H2(title));
//     };
//     ... tag::Main(Card("one"), Card("two")) ...
//
// It takes a lambda with fixed parameter types or a function. A lambda that captures
// cannot be constexpr: make it a local (`const volt2::Component Row = [&s](...) {...};`),
// or a session member with the signature written out:
// `volt2::Component<VirtualNode(std::string)> Banner = [this](std::string text) {...};`.
template <class F>
struct ComponentCall {
    using type = void;
};
template <class F>
    requires requires { &F::operator(); }
struct ComponentCall<F> {
    using type = decltype(&F::operator());
};

template <class F, class Signature = typename ComponentCall<F>::type>
class Component {
    static_assert(detail::dependentFalse<F>,
                  "volt2::Component needs a lambda with fixed parameter types (not auto, not mutable) or a function: "
                  "it adds the call site after the parameters");
};

// A lambda, noexcept or not.
template <class F, class R, bool N, class... P>
class Component<F, R (F::*)(P...) const noexcept(N)> {
public:
    constexpr Component(F body) : body_(std::move(body)) {}
    R operator()(P... args, detail::CallSite at = detail::CallSite(std::source_location::current())) const noexcept(N) {
        detail::ComponentScope scope(at);
        return body_(std::forward<P>(args)...);
    }

private:
    F body_;
};

// A static lambda (C++23).
template <class F, class R, bool N, class... P>
class Component<F, R (*)(P...) noexcept(N)> {
public:
    constexpr Component(F) {}
    R operator()(P... args, detail::CallSite at = detail::CallSite(std::source_location::current())) const noexcept(N) {
        detail::ComponentScope scope(at);
        return F::operator()(std::forward<P>(args)...);
    }
};

// A function.
template <class R, bool N, class... P>
class Component<R (*)(P...) noexcept(N), void> {
public:
    constexpr Component(R (*body)(P...) noexcept(N)) : body_(body) {}
    R operator()(P... args, detail::CallSite at = detail::CallSite(std::source_location::current())) const noexcept(N) {
        detail::ComponentScope scope(at);
        return body_(std::forward<P>(args)...);
    }

private:
    R (*body_)(P...) noexcept(N);
};

// The signature written out, for components that capture, such as session members.
template <class R, class... P>
class Component<R(P...), void> {
public:
    template <class G>
        requires std::is_invocable_r_v<R, G&, P...>
    Component(G body) : body_(std::move(body)) {}
    R operator()(P... args, detail::CallSite at = detail::CallSite(std::source_location::current())) const {
        detail::ComponentScope scope(at);
        return body_(std::forward<P>(args)...);
    }

private:
    std::function<R(P...)> body_;
};

template <class F>
Component(F) -> Component<F>;

namespace detail {
template <class R, class F>
concept ItemMaker = std::invocable<F&, std::ranges::range_reference_t<R>> ||
                    std::invocable<F&, std::ranges::range_reference_t<R>, std::size_t>;

template <class F, class Item>
VirtualNode makeItem(F& make, Item&& item, std::size_t index) {
    if constexpr (std::invocable<F&, Item, std::size_t>) {
        return make(std::forward<Item>(item), index);
    } else {
        return make(std::forward<Item>(item));
    }
}

template <class K>
std::string keyText(const K& key) {
    if constexpr (std::convertible_to<const K&, std::string_view>) {
        return std::string(std::string_view(key));
    } else {
        static_assert(std::integral<K> && !std::same_as<K, bool>, "volt2::Map: a key is a string or an integer");
        return std::to_string(key);
    }
}
}  // namespace detail

// One node per item, like Volt's map: Map(s.fruits, [&](const std::string& fruit) {...}).
// The function may also take the item's index: [&](const std::string& fruit, std::size_t i).
// Each item is a call of its own: the components it calls keep their own local() state.
// Items are told apart by position; give them a key when the list can reorder (below).
template <std::ranges::input_range R, class F>
    requires detail::ItemMaker<R, F>
std::vector<VirtualNode> Map(R&& items, F&& make, detail::CallSite at = detail::CallSite(std::source_location::current())) {
    std::vector<VirtualNode> nodes;
    if constexpr (std::ranges::sized_range<R>) nodes.reserve(std::ranges::size(items));
    std::size_t index = 0;
    for (auto&& item : items) {
        detail::ItemScope scope(at, nullptr, index);
        nodes.push_back(detail::makeItem(make, std::forward<decltype(item)>(item), index));
        ++index;
    }
    return nodes;
}

// The same with a key for each item, a string or an integer, unique in the list:
// Map(s.todos, &Todo::id, [&](const Todo& todo) {...}), or any function of the item for the
// key. Each item's element gets it as its attr::Key, and its components' local() state goes
// with the item when the list reorders.
template <std::ranges::input_range R, class K, class F>
    requires std::invocable<K&, std::ranges::range_reference_t<R>> && detail::ItemMaker<R, F>
std::vector<VirtualNode> Map(R&& items, K&& key, F&& make,
                             detail::CallSite at = detail::CallSite(std::source_location::current())) {
    std::vector<VirtualNode> nodes;
    if constexpr (std::ranges::sized_range<R>) nodes.reserve(std::ranges::size(items));
    std::size_t index = 0;
    for (auto&& item : items) {
        const std::string name = detail::keyText(std::invoke(key, item));
        detail::ItemScope scope(at, &name, index);
        nodes.push_back(detail::makeItem(make, std::forward<decltype(item)>(item), index));
        detail::Children::giveKey(nodes.back(), name);
        ++index;
    }
    return nodes;
}

// `count` nodes, like Volt's loop: Loop(3, [](std::size_t i) { return tag::Li(i); }).
template <class F>
    requires std::invocable<F&, std::size_t>
std::vector<VirtualNode> Loop(std::size_t count, F&& make,
                              detail::CallSite at = detail::CallSite(std::source_location::current())) {
    std::vector<VirtualNode> nodes;
    nodes.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        detail::ItemScope scope(at, nullptr, index);
        nodes.push_back(make(index));
    }
    return nodes;
}

// State of a component call, kept from one render to the next for as long as the component
// is called (shown or not), and dropped once a render does not call it:
//
//     inline constexpr volt2::Component Dropdown = [](std::string label) {
//         bool& open = volt2::local(false);
//         return tag::Div(tag::Button[event::OnClick([&open] { open = !open; })](label),
//                         open ? tag::Ul(...) : None);
//     };
//
// Each call site of the component has its own; inside Map and Loop, each item. The initial
// value is used the first time: volt2::local(0), volt2::local<std::string>("text"),
// volt2::local<std::vector<int>>(). Handlers may keep the reference. Only while the page
// renders, inside site() or a component it calls.
template <class T>
T& local(T initial = T{}, detail::CallSite at = detail::CallSite(std::source_location::current())) {
    detail::Circles* circles = detail::currentCircles();
    if (!circles) throw std::logic_error("volt2::local works while the page renders, in site() or a component it calls");
    detail::Circle* circle = circles->current();
    auto& locals = circle->locals;
    detail::Circle::Local* found = nullptr;
    if (circle->nextLocal < locals.size() && locals[circle->nextLocal].at == at) {
        found = &locals[circle->nextLocal];
    } else {
        for (auto& local : locals) {
            if (local.at == at) {
                found = &local;
                break;
            }
        }
    }
    if (!found) {
        locals.push_back({at, 0, &typeid(T), std::make_unique<detail::LocalOf<T>>(std::move(initial))});
        found = &locals.back();
    } else if (found->seen == circles->generation && !circle->twice) {  // a repeated call is reported already
        circles->warn("the same local() ran twice for one component call, at " + circle->path() +
                      "; it is shared. For lists, use Map");
    }
    found->seen = circles->generation;
    circle->nextLocal = static_cast<std::size_t>(found - locals.data()) + 1;
    if (*found->type != typeid(T)) throw std::logic_error("volt2::local: one call site, two types");
    return static_cast<detail::LocalOf<T>&>(*found->value).value;
}

// How an attribute writes `= true` and `= false`.
enum class Booleans {
    None,       // not a boolean: `= true` is a compile error
    Presence,   // HTML boolean attributes: true writes it (as ""), false leaves it out
    TrueFalse,  // draggable, spellcheck, contenteditable, aria-*, data-*: "true" or "false"
};

// An attribute: attr::Id = "main", attr::Disabled = true, attr::TabIndex = 3.
template <class Name, Booleans Bools = Booleans::None>
class BasicAttribute {
public:
    constexpr explicit BasicAttribute(Name name) : name_(std::move(name)) {}

    detail::Prop operator=(std::string_view value) const {
        return {detail::Prop::Kind::Attribute, std::string(name_), std::string(value), {}};
    }
    // A null const char* leaves the attribute out, like None.
    detail::Prop operator=(const char* value) const {
        if (value == nullptr) return {};
        return *this = std::string_view(value);
    }
    // An empty std::optional leaves the attribute out: attr::Title = s.tooltip.
    template <class T>
        requires(std::convertible_to<const T&, std::string_view> || std::is_arithmetic_v<T>)
    detail::Prop operator=(const std::optional<T>& value) const {
        if (!value) return {};
        return *this = *value;
    }
    template <std::same_as<bool> B>
    detail::Prop operator=(B value) const {
        if constexpr (Bools == Booleans::Presence) {
            if (!value) return {};
            return {detail::Prop::Kind::Attribute, std::string(name_), std::string(), {}};
        } else if constexpr (Bools == Booleans::TrueFalse) {
            return {detail::Prop::Kind::Attribute, std::string(name_), value ? "true" : "false", {}};
        } else {
            static_assert(detail::dependentFalse<B>,
                          "volt2: this attribute does not take true/false. Write the value as a string: \"\" for a "
                          "present boolean attribute, or \"true\"/\"false\" if that is what the attribute expects.");
            return {};
        }
    }
    template <class N>
        requires(std::is_arithmetic_v<N> && !std::same_as<N, bool> && !std::same_as<N, char>)
    detail::Prop operator=(N number) const {
        return {detail::Prop::Kind::Attribute, std::string(name_), detail::numberText(number), {}};
    }

private:
    Name name_;
};

using AttributeName = BasicAttribute<std::string_view>;
using BooleanAttribute = BasicAttribute<std::string_view, Booleans::Presence>;
using TrueFalseAttribute = BasicAttribute<std::string_view, Booleans::TrueFalse>;

// Element names, PascalCase so names like Template and Object need no underscores.
namespace tag {
inline constexpr Tag A{"a"}, Abbr{"abbr"}, Address{"address"}, Area{"area"}, Article{"article"}, Aside{"aside"},
    Audio{"audio"}, B{"b"}, Bdi{"bdi"}, Bdo{"bdo"}, Blockquote{"blockquote"}, Br{"br"}, Button{"button"},
    Canvas{"canvas"}, Caption{"caption"}, Cite{"cite"}, Code{"code"}, Col{"col"}, Colgroup{"colgroup"}, Data{"data"},
    Datalist{"datalist"}, Dd{"dd"}, Del{"del"}, Details{"details"}, Dfn{"dfn"}, Dialog{"dialog"}, Div{"div"}, Dl{"dl"},
    Dt{"dt"}, Em{"em"}, Embed{"embed"}, Fieldset{"fieldset"}, Figcaption{"figcaption"}, Figure{"figure"},
    Footer{"footer"}, Form{"form"}, H1{"h1"}, H2{"h2"}, H3{"h3"}, H4{"h4"}, H5{"h5"}, H6{"h6"}, Header{"header"},
    Hgroup{"hgroup"}, Hr{"hr"}, I{"i"}, Iframe{"iframe"}, Img{"img"}, Input{"input"}, Ins{"ins"}, Kbd{"kbd"},
    Label{"label"}, Legend{"legend"}, Li{"li"}, Main{"main"}, Map{"map"}, Mark{"mark"}, Menu{"menu"}, Meter{"meter"},
    Nav{"nav"}, Object{"object"}, Ol{"ol"}, Optgroup{"optgroup"}, Option{"option"}, Output{"output"}, P{"p"},
    Picture{"picture"}, Pre{"pre"}, Progress{"progress"}, Q{"q"}, Rp{"rp"}, Rt{"rt"}, Ruby{"ruby"}, S{"s"},
    Samp{"samp"}, Search{"search"}, Section{"section"}, Select{"select"}, Slot{"slot"}, Small{"small"},
    Source{"source"}, Span{"span"}, Strong{"strong"}, Style{"style"}, Sub{"sub"}, Summary{"summary"}, Sup{"sup"},
    Table{"table"}, Tbody{"tbody"}, Td{"td"}, Template{"template"}, Textarea{"textarea"}, Tfoot{"tfoot"}, Th{"th"},
    Thead{"thead"}, Time{"time"}, Tr{"tr"}, Track{"track"}, U{"u"}, Ul{"ul"}, Var{"var"}, Video{"video"},
    Wbr{"wbr"};
}  // namespace tag

// Attribute names. attr::Data("id") is data-id, attr::Aria("label") is aria-label, and
// attr::Named("x") is any other attribute.
namespace attr {
// HTML boolean attributes: `= true` writes them, `= false` leaves them out.
inline constexpr BooleanAttribute AutoFocus{"autofocus"}, AutoPlay{"autoplay"}, Checked{"checked"},
    Controls{"controls"}, Default{"default"}, Disabled{"disabled"}, Hidden{"hidden"}, Loop{"loop"},
    Multiple{"multiple"}, Muted{"muted"}, NoValidate{"novalidate"}, Open{"open"}, ReadOnly{"readonly"},
    Required{"required"}, Reversed{"reversed"}, Selected{"selected"};
// Attributes whose value is the word "true" or "false".
inline constexpr TrueFalseAttribute ContentEditable{"contenteditable"}, Draggable{"draggable"},
    SpellCheck{"spellcheck"};
// Everything else takes strings and numbers.
inline constexpr AttributeName Accept{"accept"}, AccessKey{"accesskey"}, Action{"action"}, Alt{"alt"},
    AutoComplete{"autocomplete"}, Cite{"cite"}, Class{"class"}, Cols{"cols"}, ColSpan{"colspan"}, Content{"content"},
    CrossOrigin{"crossorigin"}, DateTime{"datetime"}, Dir{"dir"}, Download{"download"}, EncType{"enctype"},
    For{"for"}, Form{"form"}, Headers{"headers"}, Height{"height"}, High{"high"}, Href{"href"},
    HrefLang{"hreflang"}, Id{"id"}, InputMode{"inputmode"}, Kind{"kind"}, Label{"label"}, Lang{"lang"},
    List{"list"}, Loading{"loading"}, Low{"low"}, Max{"max"}, MaxLength{"maxlength"}, Media{"media"},
    Method{"method"}, Min{"min"}, MinLength{"minlength"}, Name{"name"}, Optimum{"optimum"}, Pattern{"pattern"},
    Placeholder{"placeholder"}, Poster{"poster"}, Preload{"preload"}, Rel{"rel"}, Role{"role"}, Rows{"rows"},
    RowSpan{"rowspan"}, Sandbox{"sandbox"}, Scope{"scope"}, Size{"size"}, Sizes{"sizes"}, Span{"span"},
    Src{"src"}, SrcDoc{"srcdoc"}, SrcLang{"srclang"}, SrcSet{"srcset"}, Start{"start"}, Step{"step"},
    Style{"style"}, TabIndex{"tabindex"}, Target{"target"}, Title{"title"}, Translate{"translate"}, Type{"type"},
    Value{"value"}, Width{"width"}, Wrap{"wrap"};

inline BasicAttribute<std::string> Named(std::string_view name) { return BasicAttribute<std::string>(std::string(name)); }
// data-* and aria-* write `= true` and `= false` as "true" and "false".
inline BasicAttribute<std::string, Booleans::TrueFalse> Data(std::string_view key) {
    return BasicAttribute<std::string, Booleans::TrueFalse>("data-" + std::string(key));
}
inline BasicAttribute<std::string, Booleans::TrueFalse> Aria(std::string_view key) {
    return BasicAttribute<std::string, Booleans::TrueFalse>("aria-" + std::string(key));
}

// Not an HTML attribute: says which element this is among its siblings, whichever line of
// code made it, so the items of a list keep their DOM nodes when the list reorders, and a
// new key starts an element over. A string or an integer, unique among its siblings:
// attr::Key = fruit.id.
class KeyName {
public:
    detail::Prop operator=(std::string_view value) const { return {detail::Prop::Kind::Key, "key", std::string(value), {}}; }
    template <std::integral I>
        requires(!std::same_as<I, bool> && !std::same_as<I, char>)
    detail::Prop operator=(I value) const {
        return {detail::Prop::Kind::Key, "key", std::to_string(value), {}};
    }
};
inline constexpr KeyName Key{};
}  // namespace attr

// Event handlers: event::OnClick(handler). A handler takes the event (const Value&) or
// nothing; the event's currentTarget is the handler's own element. After a handler runs,
// volt2 renders the page again.
namespace event {
template <class F>
concept Handler = std::invocable<F&, const js::Value&> || std::invocable<F&>;

// Any event type: event::On("my-event", handler).
template <Handler F>
detail::Prop On(std::string type, F&& handler) {
    detail::Prop prop;
    prop.kind = detail::Prop::Kind::Event;
    prop.name = std::move(type);
    if constexpr (std::invocable<F&, const js::Value&>) {
        prop.handler = std::forward<F>(handler);
    } else {
        prop.handler = [run = std::forward<F>(handler)](const js::Value&) mutable { run(); };
    }
    return prop;
}

#define VOLT2_EVENT(Name, type)                    \
    template <Handler F>                           \
    detail::Prop Name(F&& handler) {               \
        return On(type, std::forward<F>(handler)); \
    }
VOLT2_EVENT(OnClick, "click")
VOLT2_EVENT(OnDoubleClick, "dblclick")
VOLT2_EVENT(OnContextMenu, "contextmenu")
VOLT2_EVENT(OnMouseDown, "mousedown")
VOLT2_EVENT(OnMouseUp, "mouseup")
VOLT2_EVENT(OnMouseMove, "mousemove")
VOLT2_EVENT(OnMouseOver, "mouseover")
VOLT2_EVENT(OnMouseOut, "mouseout")
VOLT2_EVENT(OnMouseEnter, "mouseenter")
VOLT2_EVENT(OnMouseLeave, "mouseleave")
VOLT2_EVENT(OnPointerDown, "pointerdown")
VOLT2_EVENT(OnPointerUp, "pointerup")
VOLT2_EVENT(OnPointerMove, "pointermove")
VOLT2_EVENT(OnPointerEnter, "pointerenter")
VOLT2_EVENT(OnPointerLeave, "pointerleave")
VOLT2_EVENT(OnPointerCancel, "pointercancel")
VOLT2_EVENT(OnTouchStart, "touchstart")
VOLT2_EVENT(OnTouchMove, "touchmove")
VOLT2_EVENT(OnTouchEnd, "touchend")
VOLT2_EVENT(OnKeyDown, "keydown")
VOLT2_EVENT(OnKeyUp, "keyup")
VOLT2_EVENT(OnInput, "input")
VOLT2_EVENT(OnChange, "change")
VOLT2_EVENT(OnSubmit, "submit")
VOLT2_EVENT(OnReset, "reset")
VOLT2_EVENT(OnInvalid, "invalid")
VOLT2_EVENT(OnFocus, "focus")
VOLT2_EVENT(OnBlur, "blur")
VOLT2_EVENT(OnFocusIn, "focusin")
VOLT2_EVENT(OnFocusOut, "focusout")
VOLT2_EVENT(OnScroll, "scroll")
VOLT2_EVENT(OnWheel, "wheel")
VOLT2_EVENT(OnDrag, "drag")
VOLT2_EVENT(OnDragStart, "dragstart")
VOLT2_EVENT(OnDragEnd, "dragend")
VOLT2_EVENT(OnDragEnter, "dragenter")
VOLT2_EVENT(OnDragLeave, "dragleave")
VOLT2_EVENT(OnDragOver, "dragover")
VOLT2_EVENT(OnDrop, "drop")
VOLT2_EVENT(OnCopy, "copy")
VOLT2_EVENT(OnCut, "cut")
VOLT2_EVENT(OnPaste, "paste")
VOLT2_EVENT(OnLoad, "load")
VOLT2_EVENT(OnError, "error")
VOLT2_EVENT(OnPlay, "play")
VOLT2_EVENT(OnPause, "pause")
VOLT2_EVENT(OnEnded, "ended")
VOLT2_EVENT(OnTimeUpdate, "timeupdate")
VOLT2_EVENT(OnVolumeChange, "volumechange")
VOLT2_EVENT(OnToggle, "toggle")
VOLT2_EVENT(OnAnimationEnd, "animationend")
VOLT2_EVENT(OnTransitionEnd, "transitionend")
#undef VOLT2_EVENT
}  // namespace event

// ============================================================================
// Rendering and the page lifecycle (both builds)
// ============================================================================
namespace detail {

// Renders a site's tree into the page, and runs its event handlers. It keeps the tree it
// rendered last and sends the page only what changed, so elements keep their DOM nodes,
// and with them focus, typed text, scroll positions and playing media.
//
// Each new element is matched with the old children of its parent's match, by identity:
// its attr::Key, or else where it was made (its tag's call site, the component calls
// between its parent and it) and its position in a range. Same identity and same tag: the
// same element, updated in place. Same identity and another tag: a new element that takes
// over the old one's children. Text keeps its node when it is the same text child of the
// same element. An id is an ordinary attribute.
class Renderer {
public:
    explicit Renderer(std::function<VirtualNode()> render) : render_(std::move(render)) {}

    void start() {
        dispatcher_ = js::function([this](const js::Args& args) { dispatch(args); });
        renderNow();
    }

    // Session::invalidate.
    void invalidate() {
        if (rendering_) return;  // the render in progress already shows the latest state
        if (depth_ > 0) {        // handlers are running: render once, when they finish
            pending_ = true;
            return;
        }
        renderNow();
    }

    // What one render sends the page, see renderNow.
    struct Patch {
        std::string steps;   // the contents of p, without its brackets
        std::string unused;  // the contents of x
        Json warnings = Json::array();
        bool reset = false;
        bool empty() const { return !reset && steps.empty() && unused.empty() && warnings.items().empty(); }
    };

    // Calls the site and compares its tree with the one shown: what renderNow sends, and
    // what tests/bench measures. If the site throws, nothing changes.
    Patch diff() {
        const CallScope base = callScope();  // the calls around site(): none
        Nodes next;
        {
            // While the site runs, component calls find their circles (local() state) in this
            // page's; afterwards, the ones it did not call are dropped.
            struct Using {
                Circles* outer;
                ~Using() { currentCircles() = outer; }
            } restore{currentCircles()};
            ++circles_.generation;
            circles_.root.seen = circles_.generation;
            circles_.root.nextChild = circles_.root.nextLocal = 0;
            circles_.frames.clear();
            currentCircles() = &circles_;
            if (VirtualNode root = render_(); !root.isNothing()) next.push_back(std::move(root));
            circles_.sweep(circles_.root);
        }

        Patch patch;
        patch.reset = std::exchange(fresh_, true);  // true until this is done, so a failure starts over
        Nodes previous = std::move(shown_);
        shown_ = std::move(next);
        handlers_.clear();
        ids_.clear();
        out_ = &patch;
        for (std::string& warning : circles_.warnings) warn(warning);
        circles_.warnings.clear();
        diffChildren(kMount, patch.reset ? nullptr : &previous, shown_, base);
        if (!patch.reset) collectUnused(previous, patch.unused);
        out_ = nullptr;
        retired_ = std::move(previous);  // freed after the page has the patch (renderNow)
        fresh_ = false;
        return patch;
    }

private:
    using Nodes = std::vector<VirtualNode>;
    using Use = VirtualNode::Use;
    static constexpr std::uint64_t kKeyed = 0xa4093822299f31d0ull;
    static constexpr std::uint64_t kMount = 0;  // the number of #volt2, which holds the page

    // One message per render, and none when nothing changed:
    //   p: steps, run in order, each naming a DOM node by its number:
    //        ["o", n, children]     n's children become these, in this order
    //        ["a", n, name, value]  sets an attribute (value null: removes it)
    //        ["t", n, text]         sets a text node's text
    //        ["e", n, types]        the event types n has handlers for
    //      A child is an existing node's number, or a new node: {n, x: text} or
    //      {n, t: tag, a: [[name, value]...], e: [types], c: [children]}.
    //   x: the numbers of the nodes no longer used;  w: warnings for the console;
    //   r: start over (the first render);  f: the event dispatcher.
    void renderNow() {
        rendering_ = true;
        pending_ = false;
        struct Done {
            Renderer& renderer;
            ~Done() {
                renderer.rendering_ = false;
                renderer.retired_.clear();  // the previous tree, once the page shows the new one
            }
        } done{*this};
        Patch patch = diff();
        if (patch.empty()) return;
        Json message = op("patch");
        message.set("p", Json::raw("[" + patch.steps + "]"));
        message.set("x", Json::raw("[" + patch.unused + "]"));
        if (!patch.warnings.items().empty()) message.set("w", std::move(patch.warnings));
        if (patch.reset) {
            message.set("r", Json::boolean(true));
            message.set("f", toWire(dispatcher_.value()));
        }
        try {
            request(std::move(message));
        } catch (...) {
            fresh_ = true;  // the page may not have it: the next render starts over
            throw;
        }
    }

    // ---- Identity

    // Who `node` is among its siblings; `scope` is the component calls its parent was made in.
    void identify(VirtualNode& node, const CallScope& scope) {
        if (node.key_) {
            node.own_ = mixId(kKeyed, hashText(*node.key_));
        } else {
            node.calls_ = callsBetween(scope, node.site_.scope);
            std::uint64_t own = mixId(node.calls_, node.site_.at.hash);
            node.own_ = node.item_ == VirtualNode::kNoItem ? own : mixId(own, node.item_);
        }
        if (const std::string* id = attribute(node, "id"); id && !ids_.insert(*id).second) {
            warn("two elements have id=\"" + *id + "\"; HTML ids must be unique on the page");
        }
    }

    // Exactly the same identity: the number first, then what it was made from.
    static bool sameIdentity(const VirtualNode& a, const VirtualNode& b) {
        if (a.own_ != b.own_) return false;
        if (a.key_ || b.key_) return a.key_ && b.key_ && *a.key_ == *b.key_;
        return a.site_.at == b.site_.at && a.item_ == b.item_ && a.calls_ == b.calls_;
    }

    static bool available(const VirtualNode& old, const VirtualNode& node) {
        return old.kind_ == VirtualNode::Kind::Element && old.use_ == Use::Unused && sameIdentity(old, node);
    }

    // The old children of one parent by identity, sorted, made only when a child is not
    // where it was: most renders never need it.
    struct Lookup {
        bool built = false;
        std::vector<std::pair<std::uint64_t, std::uint32_t>> entries;  // identity, position

        VirtualNode* find(Nodes& old, const VirtualNode& node) {
            if (!built) {
                built = true;
                entries.reserve(old.size());
                for (std::size_t i = 0; i < old.size(); ++i) {
                    if (old[i].kind_ == VirtualNode::Kind::Element) entries.emplace_back(old[i].own_, static_cast<std::uint32_t>(i));
                }
                std::ranges::sort(entries);
            }
            auto at = std::ranges::lower_bound(entries, std::pair<std::uint64_t, std::uint32_t>(node.own_, 0));
            for (; at != entries.end() && at->first == node.own_; ++at) {
                if (available(old[at->second], node)) return &old[at->second];
            }
            return nullptr;
        }
    };

    // The old child that is `node`, if any: where it was, right after the last one found,
    // or anywhere among its old siblings. The first unused one wins, so elements with the
    // same identity (a helper function called twice that is not a volt2::Component) are
    // matched in order.
    static VirtualNode* find(Nodes& old, const VirtualNode& node, std::size_t at, std::size_t after, Lookup& lookup) {
        if (at < old.size() && available(old[at], node)) return &old[at];
        if (after < old.size() && available(old[after], node)) return &old[after];
        return lookup.find(old, node);
    }

    // ---- Diff

    // The children of node `number` become `nodes`; `old` is what it had (null: nothing).
    void diffChildren(std::uint64_t number, Nodes* old, Nodes& nodes, const CallScope& scope) {
        std::string list;
        if (children(old, nodes, scope, list, false)) return;
        std::string& steps = beginStep('o', number);
        steps += ",[";
        steps += list;
        steps += "]]";
    }

    // Matches `nodes` with `old`, updating the kept ones and making the others. Writes their
    // list into `list`, unless nothing moved (and `always` is false), and says whether so.
    bool children(Nodes* old, Nodes& nodes, const CallScope& scope, std::string& list, bool always) {
        bool same = !always && old && old->size() == nodes.size();
        std::size_t oldText = 0;  // where the next old text child is looked for
        std::size_t after = 0;    // the old child after the last one found
        Lookup lookup;
        std::vector<std::string_view> keys;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            VirtualNode& node = nodes[i];
            VirtualNode* match = nullptr;
            VirtualNode* renamed = nullptr;  // the same identity under another tag
            if (node.kind_ == VirtualNode::Kind::Text) {
                if (old) {
                    while (oldText < old->size() && (*old)[oldText].kind_ != VirtualNode::Kind::Text) ++oldText;
                    if (oldText < old->size()) match = &(*old)[oldText++];
                }
            } else {
                identify(node, scope);
                if (node.key_) keys.push_back(*node.key_);
                if (old) {
                    match = find(*old, node, i, after, lookup);
                    if (match && match->name_ != node.name_) {
                        renamed = match;
                        match = nullptr;
                    }
                }
            }
            if (match || renamed) after = static_cast<std::size_t>((match ? match : renamed) - old->data()) + 1;
            if (same && match != &(*old)[i]) {
                same = false;  // from here on the list is needed: write the ones before, all kept
                for (std::size_t k = 0; k < i; ++k) {
                    if (k) list += ',';
                    writeNumber(list, nodes[k].number_);
                }
            }
            if (!same && i > 0) list += ',';
            if (match) {
                update(*match, node, same ? nullptr : &list);
            } else {
                if (renamed) renamed->use_ = Use::Replaced;
                create(node, list, renamed ? &renamed->children_ : nullptr);
            }
        }
        if (keys.size() > 1) {
            std::ranges::sort(keys);
            for (std::size_t k = 1; k < keys.size(); ++k) {
                if (keys[k] == keys[k - 1]) {
                    warn("two elements under the same parent have attr::Key = \"" + std::string(keys[k]) +
                         "\"; a key must be unique among its siblings");
                }
            }
        }
        return same;
    }

    // `node` is `old`, shown again: it keeps its DOM node, and the page gets what changed.
    void update(VirtualNode& old, VirtualNode& node, std::string* list) {
        node.number_ = old.number_;
        old.use_ = Use::Kept;
        if (list) writeNumber(*list, node.number_);
        if (node.kind_ == VirtualNode::Kind::Text) {
            if (node.name_ != old.name_) {
                std::string& steps = beginStep('t', node.number_);
                steps += ',';
                Json::writeString(steps, node.name_);
                steps += ']';
            }
            return;
        }
        if (!sameProps(old, node)) {
            diffAttributes(old, node);
            std::vector<std::string_view> types = eventTypes(node);
            if (types != eventTypes(old)) {
                std::string& steps = beginStep('e', node.number_);
                steps += ",[";
                for (std::size_t k = 0; k < types.size(); ++k) {
                    if (k) steps += ',';
                    Json::writeString(steps, types[k]);
                }
                steps += "]]";
            }
        }
        if (hasHandlers(node)) handlers_[node.number_] = &node;
        diffChildren(node.number_, &old.children_, node.children_, node.site_.scope);
    }

    // A new node, written into `list`. A new element that replaces one of another tag
    // takes over its children: `reuse`.
    void create(VirtualNode& node, std::string& list, Nodes* reuse) {
        node.number_ = nextNumber_++;
        list += "{\"n\":";
        writeNumber(list, node.number_);
        if (node.kind_ == VirtualNode::Kind::Text) {
            list += ",\"x\":";
            Json::writeString(list, node.name_);
            list += '}';
            return;
        }
        list += ",\"t\":";
        Json::writeString(list, node.name_);
        list += ",\"a\":[";  // in order: one given twice is set twice, and the last one stays
        bool first = true;
        bool handlers = false;
        for (const Prop& prop : node.props_) {
            if (prop.kind == Prop::Kind::Event) handlers = true;
            if (prop.kind != Prop::Kind::Attribute) continue;
            list += first ? "[" : ",[";
            first = false;
            Json::writeString(list, prop.name);
            list += ',';
            Json::writeString(list, prop.value);
            list += ']';
        }
        list += ']';
        if (handlers) {
            list += ",\"e\":[";
            std::vector<std::string_view> types = eventTypes(node);
            for (std::size_t k = 0; k < types.size(); ++k) {
                if (k) list += ',';
                Json::writeString(list, types[k]);
            }
            list += ']';
            handlers_[node.number_] = &node;
        }
        list += ",\"c\":[";
        children(reuse, node.children_, node.site_.scope, list, true);
        list += "]}";
    }

    // The same attributes and event types, in the same order: nothing to send (the usual case).
    static bool sameProps(const VirtualNode& old, const VirtualNode& node) {
        if (old.props_.size() != node.props_.size()) return false;
        for (std::size_t k = 0; k < node.props_.size(); ++k) {
            const Prop& a = old.props_[k];
            const Prop& b = node.props_[k];
            if (a.kind != b.kind || a.name != b.name) return false;
            if (a.kind == Prop::Kind::Attribute && a.value != b.value) return false;
        }
        return true;
    }

    void diffAttributes(const VirtualNode& old, const VirtualNode& node) {
        auto attribute = [](const VirtualNode& n, std::string_view name) {
            return Renderer::attribute(n, name);
        };
        auto set = [&](std::string_view name, const std::string* value) {
            std::string& steps = beginStep('a', node.number_);
            steps += ',';
            Json::writeString(steps, name);
            steps += ',';
            if (value) {
                Json::writeString(steps, *value);
            } else {
                steps += "null";
            }
            steps += ']';
        };
        for (std::size_t k = 0; k < node.props_.size(); ++k) {
            const Prop& prop = node.props_[k];
            if (prop.kind != Prop::Kind::Attribute || !isLast(node, k)) continue;  // the last one given counts
            const std::string* before = attribute(old, prop.name);
            if (!before || *before != prop.value) set(prop.name, &prop.value);
        }
        for (std::size_t k = 0; k < old.props_.size(); ++k) {
            const Prop& prop = old.props_[k];
            if (prop.kind == Prop::Kind::Attribute && isLast(old, k) && !attribute(node, prop.name)) set(prop.name, nullptr);
        }
    }

    static bool isLast(const VirtualNode& node, std::size_t k) {
        for (std::size_t j = k + 1; j < node.props_.size(); ++j) {
            if (node.props_[j].kind == Prop::Kind::Attribute && node.props_[j].name == node.props_[k].name) return false;
        }
        return true;
    }

    static const std::string* attribute(const VirtualNode& node, std::string_view name) {
        const std::string* value = nullptr;
        for (const Prop& prop : node.props_) {
            if (prop.kind == Prop::Kind::Attribute && prop.name == name) value = &prop.value;
        }
        return value;
    }

    static bool hasHandlers(const VirtualNode& node) {
        for (const Prop& prop : node.props_) {
            if (prop.kind == Prop::Kind::Event) return true;
        }
        return false;
    }

    static std::vector<std::string_view> eventTypes(const VirtualNode& node) {
        std::vector<std::string_view> types;
        for (const Prop& prop : node.props_) {
            if (prop.kind == Prop::Kind::Event) types.push_back(prop.name);
        }
        std::ranges::sort(types);
        types.erase(std::unique(types.begin(), types.end()), types.end());
        return types;
    }

    // Nodes of the previous tree that this render did not keep.
    static void collectUnused(const Nodes& nodes, std::string& unused) {
        for (const VirtualNode& node : nodes) {
            if (node.use_ != Use::Kept) {
                if (!unused.empty()) unused += ',';
                writeNumber(unused, node.number_);
            }
            collectUnused(node.children_, unused);
        }
    }

    // ---- Writing the patch

    std::string& beginStep(char kind, std::uint64_t number) {
        std::string& steps = out_->steps;
        if (!steps.empty()) steps += ',';
        steps += "[\"";
        steps += kind;
        steps += "\",";
        writeNumber(steps, number);
        return steps;
    }

    static void writeNumber(std::string& out, std::uint64_t number) {
        char buffer[24];
        auto result = std::to_chars(buffer, buffer + sizeof buffer, number);
        out.append(buffer, result.ptr);
    }

    void warn(const std::string& message) {
        if (!warned_.insert(message).second) return;  // once per page
#ifndef __EMSCRIPTEN__
        logError(message);
#endif
        out_->warnings.push(Json::string(message));
    }

    // ---- Events

    // Called by volt2.js with the numbers of the elements that have handlers for this
    // event, from the target upward, and for each one a view of the event whose
    // currentTarget is that element. Handlers run first, then the page renders once. A
    // handler that makes another event fire (element.call("click")) runs that one too, on
    // the same tree, and the render still happens once, at the end.
    void dispatch(const js::Args& args) {
        Json chain = Json::parse(args.at(0).as<std::string>());
        const std::string type = args.at(1).as<std::string>();
        pending_ = true;
        ++depth_;
        std::exception_ptr failure;
        try {
            const auto& numbers = chain.items();
            for (std::size_t n = 0; n < numbers.size() && n + 2 < args.size(); ++n) {
                const js::Value& event = args[n + 2];
                auto found = handlers_.find(static_cast<std::uint64_t>(numbers[n].asNumber()));
                if (found != handlers_.end()) {
                    for (const Prop& prop : found->second->props_) {
                        if (prop.kind == Prop::Kind::Event && prop.name == type) prop.handler(event);
                    }
                }
                if (n + 1 < numbers.size() && event["cancelBubble"].as<bool>()) break;  // stopPropagation()
            }
        } catch (const PageReplaced&) {
            --depth_;
            throw;
        } catch (...) {
            failure = std::current_exception();
        }
        if (--depth_ == 0 && pending_) renderNow();  // also after a failing handler, to show what changed
        if (failure) std::rethrow_exception(failure);
    }

    std::function<VirtualNode()> render_;
    js::Function dispatcher_;
    Nodes shown_;                                                     // the tree on the page
    Nodes retired_;                                                   // the one before, until it is sent
    std::unordered_map<std::uint64_t, const VirtualNode*> handlers_;  // node number -> element with handlers
    std::unordered_set<std::string_view> ids_;                        // while rendering: the ids seen
    std::unordered_set<std::string> warned_;
    Patch* out_ = nullptr;  // while rendering: the patch being written
    Circles circles_;       // the page's component calls and their local() state
    std::uint64_t nextNumber_ = 1;
    int depth_ = 0;
    bool pending_ = false;  // a render is due when the handlers finish
    bool rendering_ = false;
    bool fresh_ = true;  // the next render starts the page over
};

struct Sessions {
    static void attach(Session& session, Renderer* renderer) { session.renderer_ = renderer; }
    static Renderer* renderer(const Session& session) { return session.renderer_; }
};

// How to start a site, built by run() from your session type and functions.
struct SiteDefinition {
    std::function<std::unique_ptr<Session>()> create;
    std::function<void(Session&)> start;          // a site built by hand
    std::function<VirtualNode(Session&)> render;  // a rendered site
};

// What one page load owns. The renderer goes first: it calls into the session.
struct Page {
    std::unique_ptr<Session> session;
    std::unique_ptr<Renderer> renderer;
};

inline std::unique_ptr<Page>& currentPage() {
    static auto* page = new std::unique_ptr<Page>();
    return *page;
}

// Destroys a page whose browser page is gone. Its session's destructor may still try
// JavaScript; such calls do nothing instead of reaching the next page.
inline void discardPage(std::unique_ptr<Page> page) {
    Runtime& r = runtime();
    r.tearingDown = true;
    page.reset();
    r.tearingDown = false;
}

inline void endPage() { discardPage(std::move(currentPage())); }

inline void startPage(const SiteDefinition& site) {
    auto page = std::make_unique<Page>();
    try {
        page->session = site.create();  // your constructor
        if (site.render) {
            page->renderer = std::make_unique<Renderer>([&session = *page->session, render = site.render] {
                return render(session);
            });
            Sessions::attach(*page->session, page->renderer.get());
            page->renderer->start();
        } else if (site.start) {
            site.start(*page->session);
        }
    } catch (...) {
        discardPage(std::move(page));
        throw;
    }
    currentPage() = std::move(page);
}

// Implemented by each backend: development serves the site, WebAssembly starts it.
inline int runSite(SiteDefinition site, Options options);

}  // namespace detail

inline void Session::invalidate() {
    detail::requireVoltThread();
    if (renderer_) renderer_->invalidate();
}

// A rendered site. volt2 creates your session (one per page load), calls render to get
// the page, and calls it again after every event handler:
//
//     struct Counter : volt2::Session { int count = 0; };
//     volt2::VirtualNode site(Counter& s) { ... }
//     int main() { return volt2::run(site); }
template <SessionType S>
int run(VirtualNode (*render)(S&), Options options = {}) {
    detail::SiteDefinition site;
    site.create = [] { return std::unique_ptr<Session>(std::make_unique<S>()); };
    site.render = [render](Session& session) { return render(static_cast<S&>(session)); };
    return detail::runSite(std::move(site), options);
}

// A site built by hand with js::Value: start runs once per page load, after your constructor.
template <SessionType S>
int run(void (*start)(S&), Options options = {}) {
    detail::SiteDefinition site;
    site.create = [] { return std::unique_ptr<Session>(std::make_unique<S>()); };
    site.start = [start](Session& session) { start(static_cast<S&>(session)); };
    return detail::runSite(std::move(site), options);
}

// A site built by hand entirely in your session's constructor: volt2::run<MyPage>().
template <SessionType S>
int run(Options options = {}) {
    detail::SiteDefinition site;
    site.create = [] { return std::unique_ptr<Session>(std::make_unique<S>()); };
    return detail::runSite(std::move(site), options);
}

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
// when it no longer needs it. An object keeps the same id for as long as C++ holds
// it, so C++ can compare objects without asking the page.
//
// volt2.hpp embeds a copy of this file. After editing it, run tools/embed_js.py.
(function () {
  'use strict';

  const handles = new Map(); // id -> value
  const idOf = new Map(); // value -> id, for values C++ currently holds
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
        let id = idOf.get(value);
        if (id === undefined) {
          id = nextHandle++; // never reused, so a released id cannot mean another object
          handles.set(id, value);
          idOf.set(value, id);
        }
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
    if (!ids) return;
    for (const id of ids) {
      if (handles.has(id)) idOf.delete(handles.get(id));
      handles.delete(id);
    }
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
      case 'patch':
        return patch(op);
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

  // Rendering. C++ keeps the tree it rendered last and sends one patch per render with
  // what changed (see Renderer::renderNow in volt2.hpp for its format). Every DOM node
  // volt2 made has a number; 0 is the mount, #volt2, created at the end of <body> if the
  // page has none. One capturing listener per event type on the mount sends C++ the
  // numbers of the elements with handlers for that event, from the target upward (only
  // the target when the event does not bubble).
  const nodes = new Map(); // number -> DOM node
  const numberOf = new WeakMap(); // DOM node -> number
  const handled = new WeakMap(); // element -> the event types it has C++ handlers for
  let listening = new Set(); // event types the mount listens to
  let mount = null;
  let dispatcher = null; // C++: runs the handlers for an event
  let patching = false; // events the patch itself causes, such as focus moving, are not sent

  function mountElement() {
    let element = document.getElementById('volt2');
    if (!element) {
      element = document.createElement('div');
      element.id = 'volt2';
      document.body.appendChild(element);
    }
    return element;
  }

  function nodeNumbered(number) {
    const node = nodes.get(number);
    if (node === undefined) throw new Error('volt2: the page has no node #' + number);
    return node;
  }

  function contentOf(element) {
    return element.tagName === 'TEMPLATE' ? element.content : element; // a template's children are its content
  }

  function patch(message) {
    if (message.r || !mount) {
      const element = mountElement();
      if (element !== mount) {
        mount = element;
        listening = new Set();
      }
      nodes.clear();
      mount.replaceChildren();
      nodes.set(0, mount);
    }
    if (message.f) dispatcher = fromWire(message.f);
    for (const warning of message.w || []) console.warn('volt2: ' + warning);
    const active = document.activeElement;
    const selection = selectionOf(active);
    const live = [];
    const selects = new Set();
    patching = true;
    try {
      for (const step of message.p) {
        const select = selectAround(run(step, live));
        if (select) selects.add(select);
      }
      // A select shows the option that matches its value, which can be another option once
      // its options change: its value is applied again.
      for (const select of selects) {
        const value = select.getAttribute('value');
        if (value !== null) live.push([select, 'value', value]);
      }
      for (const change of live) setLive(change[0], change[1], change[2]);
      for (const number of message.x) forget(number);
      // A move loses the focus; arrange avoids moving the focused element, but if it had
      // to, the focus and the selection come back.
      if (active && active !== document.activeElement && active.isConnected) {
        active.focus({ preventScroll: true });
        if (selection) {
          try {
            active.setSelectionRange(selection[0], selection[1], selection[2]);
          } catch (e) {}
        }
      }
    } finally {
      patching = false;
    }
    return undefined;
  }

  function selectionOf(element) {
    try {
      if (element && typeof element.selectionStart === 'number') {
        return [element.selectionStart, element.selectionEnd, element.selectionDirection];
      }
    } catch (e) {} // some input types have no selection
    return null;
  }

  function run(step, live) {
    const node = nodeNumbered(step[1]);
    switch (step[0]) {
      case 'o':
        arrange(contentOf(node), step[2].map(function (child) { return make(child, live); }));
        break;
      case 'a':
        setAttribute(node, step[2], step[3], live);
        break;
      case 't':
        node.nodeValue = step[2];
        break;
      case 'e':
        setHandlers(node, step[2]);
        break;
      default:
        throw new Error('volt2: unknown patch step ' + step[0]);
    }
    return node;
  }

  // The select a changed node belongs to: the select itself, or one of its options.
  function selectAround(node) {
    for (let element = node.nodeType === 1 ? node : node.parentNode; element && element.nodeType === 1; element = element.parentNode) {
      if (element.tagName === 'SELECT') return element;
      if (element.tagName !== 'OPTION' && element.tagName !== 'OPTGROUP') return null;
    }
    return null;
  }

  // An existing node by number, or a new one with its children.
  function make(child, live) {
    if (typeof child === 'number') return nodeNumbered(child);
    let node;
    if ('x' in child) {
      node = document.createTextNode(child.x);
    } else {
      node = document.createElement(child.t);
      for (const pair of child.a) setAttribute(node, pair[0], pair[1], live);
      if (child.e) setHandlers(node, child.e);
      const content = contentOf(node);
      for (const grandchild of child.c) content.appendChild(make(grandchild, live));
    }
    nodes.set(child.n, node);
    numberOf.set(node, child.n);
    return node;
  }

  // Makes `wanted` the children of `content`, in that order, moving as few nodes as
  // possible: a move loses focus, selection and scroll positions, and reloads iframes.
  // Anything else goes: volt2 owns everything inside #volt2, which is what lets C++ know
  // exactly what is on the page.
  function arrange(content, wanted) {
    const keep = new Set(wanted);
    for (let child = content.firstChild; child; ) {
      const next = child.nextSibling;
      if (!keep.has(child)) content.removeChild(child);
      child = next;
    }
    const position = new Map();
    let index = 0;
    for (let child = content.firstChild; child; child = child.nextSibling) position.set(child, index++);
    const stay = staying(wanted, position);
    let before = null;
    for (let k = wanted.length - 1; k >= 0; k--) {
      if (!stay[k]) content.insertBefore(wanted[k], before);
      before = wanted[k];
    }
  }

  // Which wanted nodes stay put: the longest run already in order, so the fewest move. The
  // one holding the focus stays whenever it is already there, even if more must move.
  function staying(wanted, position) {
    const stay = new Array(wanted.length).fill(false);
    const active = document.activeElement;
    let pinned = -1;
    if (active && active !== document.body) {
      for (let k = 0; k < wanted.length && pinned < 0; k++) {
        if (position.has(wanted[k]) && wanted[k].contains(active)) pinned = k;
      }
    }
    if (pinned < 0) {
      longestInOrder(wanted, position, 0, wanted.length, -1, Infinity, stay);
    } else {
      const at = position.get(wanted[pinned]);
      stay[pinned] = true;
      longestInOrder(wanted, position, 0, pinned, -1, at, stay);
      longestInOrder(wanted, position, pinned + 1, wanted.length, at, Infinity, stay);
    }
    return stay;
  }

  // Marks the longest increasing run of current positions among wanted[from..to) that lie
  // between low and high.
  function longestInOrder(wanted, position, from, to, low, high, stay) {
    const ks = [];
    const ps = [];
    for (let k = from; k < to; k++) {
      const p = position.get(wanted[k]);
      if (p !== undefined && p > low && p < high) {
        ks.push(k);
        ps.push(p);
      }
    }
    const tails = []; // tails[length - 1]: the run of that length ending lowest
    const previous = new Array(ks.length);
    for (let j = 0; j < ks.length; j++) {
      let lo = 0;
      let hi = tails.length;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (ps[tails[mid]] < ps[j]) lo = mid + 1;
        else hi = mid;
      }
      previous[j] = lo > 0 ? tails[lo - 1] : -1;
      tails[lo] = j;
    }
    for (let j = tails.length ? tails[tails.length - 1] : -1; j >= 0; j = previous[j]) stay[ks[j]] = true;
  }

  function setAttribute(element, name, value, live) {
    if (value === null) element.removeAttribute(name);
    else element.setAttribute(name, value);
    if (name === 'value' || name === 'checked' || name === 'selected') live.push([element, name, value]);
  }

  // What a form control shows is a property, which typing and clicking change; its
  // attribute is only the starting value. So these are set as properties too, once the
  // patch's elements are in place (a select needs its options first). An unchanged value
)VOLT2JS",
R"VOLT2JS(  // is left alone, which keeps the caret where it is.
  function setLive(element, name, value) {
    const tag = element.tagName;
    try {
      if (name === 'value' && (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT')) {
        const text = value === null ? '' : value;
        if (element.value !== text) element.value = text;
      } else if (name === 'checked' && tag === 'INPUT') {
        element.checked = value !== null;
      } else if (name === 'selected' && tag === 'OPTION') {
        element.selected = value !== null;
      }
    } catch (e) {
      console.error('volt2: cannot set ' + name + ' on <' + tag.toLowerCase() + '>: ' + e.message);
    }
  }

  function setHandlers(element, types) {
    if (types.length) handled.set(element, types);
    else handled.delete(element);
    for (const type of types) {
      if (listening.has(type)) continue;
      listening.add(type);
      mount.addEventListener(type, deliver, true);
    }
  }

  function forget(number) {
    const node = nodes.get(number);
    if (node === undefined) return;
    nodes.delete(number);
    numberOf.delete(node);
    handled.delete(node);
  }

  // The event as one handler sees it: currentTarget is that handler's element (the real
  // event's is the mount, where the listener is). Everything else, methods included,
  // goes to the real event.
  function eventFor(event, element) {
    return new Proxy(event, {
      get: function (target, key) {
        if (key === 'currentTarget') return element;
        const value = Reflect.get(target, key, target);
        return typeof value === 'function' ? value.bind(target) : value;
      },
      set: function (target, key, value) {
        return Reflect.set(target, key, value, target);
      },
    });
  }

  function deliver(event) {
    if (patching || !dispatcher) return;
    const chain = [];
    const views = [];
    for (let node = event.target; node && node !== mount; node = node.parentNode) {
      const types = handled.get(node);
      if (types && types.indexOf(event.type) >= 0) {
        chain.push(numberOf.get(node));
        views.push(eventFor(event, node));
      }
      if (!event.bubbles) break;
    }
    if (chain.length) dispatcher.apply(null, [JSON.stringify(chain), event.type].concat(views));
  }

  // Runs one operation for C++. Errors are returned, never thrown across the bridge.
  // Releases come first: C++ released those ids before sending, so if the result is one
  // of those objects it must get a fresh id. (The operation itself never uses them.)
  function exec(op) {
    release(op.rel);
    try {
      return { v: toWire(perform(op)) };
    } catch (e) {
      return { error: e instanceof Error ? e.name + ': ' + e.message : String(e) };
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
    api.mode = 'development';
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
    api.mode = 'webassembly';
    transport = function (message) {
      return JSON.parse(dispatch(JSON.stringify(message)));
    };
  }

  function execJson(text) {
    return JSON.stringify(exec(JSON.parse(text)));
  }

  // The stylesheets, CSS and scripts of volt2::run's head option, in the WebAssembly build
  // (while developing, they are written into the page itself). The page stays hidden until
  // the stylesheets and scripts have loaded, 3 seconds at most, so it is never seen unstyled.
  function head(json) {
    const waits = [];
    for (const item of JSON.parse(json)) {
      let element;
      if (item[0] === 'stylesheet') {
        element = document.createElement('link');
        element.rel = 'stylesheet';
        element.href = item[1];
      } else if (item[0] === 'script') {
        element = document.createElement('script');
        element.src = item[1];
        element.async = false; // in order
      } else {
        element = document.createElement('style');
        element.textContent = item[1];
      }
      if (item[0] !== 'css') {
        waits.push(new Promise(function (done) {
          element.onload = element.onerror = done;
        }));
      }
      document.head.appendChild(element);
    }
    if (!waits.length) return;
    const root = document.documentElement;
    root.style.visibility = 'hidden';
    const timeout = new Promise(function (done) {
      setTimeout(done, 3000);
    });
    Promise.race([Promise.all(waits), timeout]).then(function () {
      root.style.visibility = '';
    });
  }

  const api = {
    mode: 'not started', // then 'development' or 'webassembly'
    startDev: startDev,
    attachWasm: attachWasm,
    head: head,
    execJson: execJson,
    handleCount: function () {
      return handles.size;
    },
    nodeCount: function () {
      return nodes.size; // DOM nodes volt2 tracks, the mount included
    },
  };
  globalThis.volt2 = api;
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

// The <head> items of run's options, as HTML, written into the page served while developing.
inline std::string& pageHead() {
    static std::string html;
    return html;
}

inline std::string headHtml(const std::vector<head::Item>& items) {
    auto attribute = [](const std::string& text) {
        std::string out;
        for (char c : text) {
            if (c == '&') out += "&amp;";
            else if (c == '"') out += "&quot;";
            else if (c == '<') out += "&lt;";
            else out += c;
        }
        return out;
    };
    std::string html;
    for (const head::Item& item : items) {
        switch (item.kind) {
            case head::Item::Kind::Stylesheet:
                html += "<link rel=\"stylesheet\" href=\"" + attribute(item.text) + "\">\n";
                break;
            case head::Item::Kind::Script:
                html += "<script src=\"" + attribute(item.text) + "\"></script>\n";
                break;
            case head::Item::Kind::Css: {
                std::string css = item.text;  // it cannot end the <style> element early
                for (std::size_t at = css.find("</"); at != std::string::npos; at = css.find("</", at + 3)) css.insert(at + 1, "\\");
                html += "<style>\n" + css + "\n</style>\n";
                break;
            }
        }
    }
    return html;
}

inline std::string devPage() {
    return "<!doctype html>\n"
           "<html>\n"
           "<head>\n"
           "<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "<title>volt2</title>\n" +
           pageHead() +
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
[[noreturn]] inline void serveForever(Bridge& bridge, const SiteDefinition& site) {
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
                endPage();    // the previous page's session, while its Values are still valid
                beginPage();  // then forget that page's handles and functions
                Json reply = returnReply();
                try {
                    startPage(site);
                    noticeGrowth();
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

// Development: serves the site and starts it for each page load. Does not return
// unless the server cannot start (then it returns 1).
inline int detail::runSite(SiteDefinition site, Options options) {
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

    pageHead() = headHtml(options.head);  // before any connection thread reads it
    static Bridge bridge;  // outlives the detached connection threads
    runtime().transport = &bridge;
    runtime().thread = std::this_thread::get_id();
    if (std::string warn = environment("VOLT2_WARN_AT"); std::atoi(warn.c_str()) > 0) {
        runtime().warnAt = static_cast<std::size_t>(std::atoi(warn.c_str()));
    }
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

// WebAssembly: starts the site once, then returns; the page keeps calling into the module.
inline int detail::runSite(SiteDefinition site, Options options) {
    runtime().thread = std::this_thread::get_id();
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
    if (!options.head.empty()) {  // stylesheets, CSS and scripts for <head>, before the first render
        static const char* const kinds[] = {"stylesheet", "css", "script"};
        Json items = Json::array();
        for (const head::Item& item : options.head) {
            Json entry = Json::array();
            entry.push(Json::string(kinds[static_cast<int>(item.kind)]));
            entry.push(Json::string(item.text));
            items.push(std::move(entry));
        }
        client.call<void>("head", items.dump());
    }
    client.call<void>("attachWasm", emscripten::val::module_property("volt2_dispatch"));
    beginPage();
    try {
        startPage(site);
    } catch (const std::exception& e) {
        logError(std::string("your site threw an exception: ") + e.what());
        return 1;
    }
    return 0;
}
#endif

}  // namespace volt2

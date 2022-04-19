#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <typeinfo>
#include <any>

namespace args {

class Parser;

class Args
{
public:
    ~Args();

    bool has(const std::string& key) const;
    template<typename T>
    bool has(const std::string& key) const;

    template<typename T, typename std::enable_if<!std::is_trivial_v<T>>::type* = nullptr>
    T value(const std::string& key, const T& defaultValue = T()) const;

    template<typename T, typename std::enable_if<std::is_trivial_v<T> && !std::is_floating_point_v<T>>::type* = nullptr>
    T value(const std::string& key, T defaultValue = T()) const;

    template<typename T, typename std::enable_if<std::is_trivial_v<T> && std::is_floating_point_v<T>>::type* = nullptr>
    T value(const std::string& key, T defaultValue = T()) const;

    size_t freeformSize() const;
    std::string freeformValue(size_t idx) const;

private:
    Args();

    friend class Parser;

private:
    std::unordered_map<std::string, std::any> mValues;
    std::vector<std::string> mFreeform;
};

inline Args::Args()
{
}

inline Args::~Args()
{
}

inline bool Args::has(const std::string& key) const
{
    return mValues.count(key) > 0;
}

template<typename T>
inline bool Args::has(const std::string& key) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return false;
    if (v->second.type() == typeid(T))
        return true;
    if (v->second.type() == typeid(int64_t)) {
        if (typeid(T) == typeid(int32_t) ||
            typeid(T) == typeid(float) ||
            typeid(T) == typeid(double))
            return true;
        return false;
    }
    if (typeid(T) == typeid(float) && v->second.type() == typeid(double))
        return true;
    return false;
}

template<typename T, typename std::enable_if<!std::is_trivial_v<T>>::type*>
inline T Args::value(const std::string& key, const T& defaultValue) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return defaultValue;
    if (v->second.type() == typeid(T))
        return std::any_cast<T>(v->second);
    return defaultValue;
}

template<typename T, typename std::enable_if<std::is_trivial_v<T> && !std::is_floating_point_v<T>>::type*>
inline T Args::value(const std::string& key, T defaultValue) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return defaultValue;
    if (v->second.type() == typeid(T))
        return std::any_cast<T>(v->second);
    if (typeid(T) == typeid(int32_t) && v->second.type() == typeid(int64_t))
        return static_cast<int32_t>(std::any_cast<int64_t>(v->second));
    return defaultValue;
}

template<typename T, typename std::enable_if<std::is_trivial_v<T> && std::is_floating_point_v<T>>::type*>
inline T Args::value(const std::string& key, T defaultValue) const
{
    auto v = mValues.find(key);
    if (v == mValues.end())
        return defaultValue;
    if (v->second.type() == typeid(double))
        return static_cast<T>(std::any_cast<double>(v->second));
    if (v->second.type() == typeid(int64_t))
        return static_cast<T>(std::any_cast<int64_t>(v->second));
    return defaultValue;
}

inline size_t Args::freeformSize() const
{
    return mFreeform.size();
}

inline std::string Args::freeformValue(size_t idx) const
{
    if (idx >= mFreeform.size())
        return std::string();
    return mFreeform[idx];
}

class Parser
{
public:
    template<typename Error>
    static Args parse(int argc, char** argv, Error&& error);
    static std::any guessValue(const std::string& val);

private:
    Parser() = delete;
};

inline std::any Parser::guessValue(const std::string& val)
{
    if (val.empty())
        return std::any(true);
    if (val == "true")
        return std::any(true);
    if (val == "false")
        return std::any(false);
    char* endptr;
    long long l = strtoll(val.c_str(), &endptr, 0);
    if (*endptr == '\0')
        return std::any(static_cast<int64_t>(l));
    double d = strtod(val.c_str(), &endptr);
    if (*endptr == '\0')
        return std::any(d);
    return std::any(val);
}

template<typename Error>
inline Args Parser::parse(int argc, char** argv, Error&& error)
{
    Args args;

    enum State { Normal, Dash, DashDash, Value, Freeform, FreeformOnly };
    State state = Normal;

    std::string key;

    auto add = [&key, &args](State state, char* start, char* end) {
        assert(state != Normal);
        switch (state) {
        case Dash:
            while (start < end - 1) {
                args.mValues[std::string(1, *(start++))] = std::any(true);
            }
            break;
        case DashDash:
            key = std::string(start, end - 1 - start);
            break;
        case Value: {
            const auto v = guessValue(std::string(start, end - 1 - start));
            if (v.type() == typeid(bool)) {
                if (key.size() > 3 && !strncmp(key.c_str(), "no-", 3)) {
                    args.mValues[key.substr(3)] = std::any(!std::any_cast<bool>(std::move(v)));
                    break;
                }
                if (key.size() > 8 && !strncmp(key.c_str(), "disable-", 8)) {
                    args.mValues[key.substr(8)] = std::any(!std::any_cast<bool>(std::move(v)));
                    break;
                }
            }
            args.mValues[key] = std::move(v);
            break; }
        case Freeform:
            args.mFreeform.push_back(std::string(start, end - 1 - start));
            break;
        default:
            break;
        }
    };

    size_t off = 0;
    for (int i = 1; i < argc; ++i) {
        char* arg = argv[i];
        char* argStart = arg;
        char* prev = arg;
        bool done = false;
        while (!done) {
            switch (*(arg++)) {
            case '-':
                switch (state) {
                case Normal:
                    prev = arg;
                    state = Dash;
                    continue;
                case Dash:
                    if (prev == arg - 1) {
                        ++prev;
                        state = DashDash;
                    } else {
                        error("unexpected dash", off + arg - argStart, argStart);
                        return Args();
                    }
                    continue;
                case Freeform:
                case FreeformOnly:
                case DashDash:
                case Value:
                    if (arg - 1 == argStart) {
                        // add value as empty and keep going with dash
                        add(Value, arg, arg + 1);
                        prev = arg;
                        state = Dash;
                    }
                    continue;
                default:
                    error("unexpected dash", off + arg - argStart, argStart);
                    return Args();
                }
                break;
            case '\0':
                done = true;
                switch (state) {
                case Normal:
                    add(Freeform, prev, arg);
                    prev = arg;
                    continue;
                case Dash:
                    add(Dash, prev, arg);
                    prev = arg;
                    state = Normal;
                    continue;
                case DashDash:
                    if (arg - argStart == 3) { // 3 = --\0
                        prev = arg;
                        state = FreeformOnly;
                    } else {
                        add(DashDash, prev, arg);
                        if (i + 1 == argc) {
                            add(Value, arg, arg + 1);
                            prev = arg;
                            state = Normal;
                        } else {
                            prev = arg;
                            state = Value;
                        }
                    }
                    continue;
                case Freeform:
                    state = Normal;
                    // fall through
                case FreeformOnly:
                    add(Freeform, prev, arg);
                    prev = arg;
                    continue;
                case Value:
                    if (arg - 1 == prev) {
                        // missing value
                        error("missing value", off + arg - argStart, argStart);
                        return Args();
                    }
                    add(Value, prev, arg);
                    prev = arg;
                    state = Normal;
                    continue;
                default:
                    error("unexpected state", off + arg - argStart, argStart);
                    return Args();
                }
                break;
            case '=':
                switch (state) {
                case Freeform:
                case FreeformOnly:
                case Value:
                    continue;
                case DashDash:
                    add(DashDash, prev, arg);
                    prev = arg;
                    state = Value;
                    continue;
                default:
                    error("unexpected equals", off + arg - argStart, argStart);
                    return Args();
                }
                break;
            default:
                if (state == Normal && argStart == arg - 1) {
                    prev = arg - 1;
                    state = Freeform;
                }
                break;
            }
        }
        off += arg - argStart;
    }

    return args;
}

} // namespace args

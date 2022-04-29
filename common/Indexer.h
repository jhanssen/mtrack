#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

template<typename T>
class Indexer
{
public:
    Indexer() = default;

    std::pair<int32_t, bool> index(const T& str);

    const T& value(int32_t index) const;
    size_t size() const;

    const std::vector<T>& values() const;

    size_t hits() const;
    size_t misses() const;

private:
    T mEmpty;
    std::unordered_map<T, int32_t> mValueMap;
    std::vector<T> mValueList;
    size_t mHits {}, mMisses {};
};

template<typename T>
inline std::pair<int32_t, bool> Indexer<T>::index(const T& str)
{
    if (str.empty())
        return std::make_pair(-1, false);
    auto it = mValueMap.find(str);
    if (it != mValueMap.end()) {
        ++mHits;
        return std::make_pair(it->second, false);
    }
    ++mMisses;
    const int32_t id = static_cast<int32_t>(mValueList.size());
    mValueMap.insert(std::make_pair(str, id));
    mValueList.push_back(str);
    return std::make_pair(id, true);
}

template<typename T>
inline const T& Indexer<T>::value(int32_t index) const
{
    if (index >= mValueList.size())
        return mEmpty;
    return mValueList[index];
}

template<typename T>
inline size_t Indexer<T>::size() const
{
    return mValueList.size();
}

template<typename T>
inline const std::vector<T>& Indexer<T>::values() const
{
    return mValueList;
}

template<typename T>
inline size_t Indexer<T>::hits() const
{
    return mHits;
}

template<typename T>
inline size_t Indexer<T>::misses() const
{
    return mMisses;
}

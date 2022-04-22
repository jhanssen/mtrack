#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Creatable.h"

template<typename T>
class Indexer
{
public:
    Indexer() = default;

    int32_t index(const T& str);

    const T& value(int32_t index) const;
    size_t size() const;

    const std::vector<T>& values() const;

private:
    T mEmpty;
    std::unordered_map<T, int32_t> mValueMap;
    std::vector<T> mValueList;
};

template<typename T>
inline int32_t Indexer<T>::index(const T& str)
{
    if (str.empty())
        return -1;
    auto it = mValueMap.find(str);
    if (it != mValueMap.end())
        return it->second;
    const int32_t id = static_cast<int32_t>(mValueList.size());
    mValueMap.insert(std::make_pair(str, id));
    mValueList.push_back(str);
    return id;
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

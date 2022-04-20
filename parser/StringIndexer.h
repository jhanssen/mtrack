#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Creatable.h"

class StringIndexer
{
public:
    static StringIndexer* instance();

    int32_t index(const std::string& str);

    const std::string& str(int32_t index) const;
    size_t size() const;

    const std::vector<std::string>& strs() const;

protected:
    StringIndexer() = default;

private:
    static std::shared_ptr<StringIndexer> sIndexer;

    std::string mEmpty;
    std::unordered_map<std::string, int32_t> mStringMap;
    std::vector<std::string> mStringList;
};

inline StringIndexer* StringIndexer::instance()
{
    if (sIndexer)
        return sIndexer.get();
    sIndexer = Creatable<StringIndexer>::create();
    return sIndexer.get();
}

inline int32_t StringIndexer::index(const std::string& str)
{
    if (str.empty())
        return -1;
    auto it = mStringMap.find(str);
    if (it != mStringMap.end())
        return it->second;
    const int32_t id = static_cast<int32_t>(mStringList.size());
    mStringMap.insert(std::make_pair(str, id));
    mStringList.push_back(str);
    return id;
}

inline const std::string& StringIndexer::str(int32_t index) const
{
    if (index >= mStringList.size())
        return mEmpty;
    return mStringList[index];
}

inline size_t StringIndexer::size() const
{
    return mStringList.size();
}

inline const std::vector<std::string>& StringIndexer::strs() const
{
    return mStringList;
}

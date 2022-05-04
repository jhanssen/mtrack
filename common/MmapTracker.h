#pragma once

#include <cstdint>
#include <tuple>
#include <vector>
#include <sys/mman.h>

class MmapTracker
{
public:
    typedef std::vector<std::tuple<uintptr_t, uintptr_t, int32_t, int32_t, int32_t>> Mmaps;

    MmapTracker() = default;

    void mmap(void* addr, size_t size, int32_t prot, int32_t flags, int32_t stack);
    void mmap(uintptr_t addr, size_t size, int32_t prot, int32_t flags, int32_t stack);
    uint64_t munmap(void* addr, size_t size);
    uint64_t munmap(uintptr_t addr, size_t size);
    int32_t mprotect(void* addr, size_t size, int32_t prot);
    int32_t mprotect(uintptr_t addr, size_t size, int32_t prot);
    uint64_t madvise(void* addr, size_t size);
    uint64_t madvise(uintptr_t addr, size_t size);

    template<typename Func>
    void forEach(Func&& func) const;

    const Mmaps& data() const;
    size_t size() const;

private:
    bool intersects(Mmaps::const_iterator it, uintptr_t start, uintptr_t end);

private:
    std::pair<Mmaps::iterator, Mmaps::iterator> find(uintptr_t addr);
    std::pair<Mmaps::const_iterator, Mmaps::const_iterator> find(uintptr_t addr) const;

private:
    Mmaps mMmaps;
};

inline bool MmapTracker::intersects(Mmaps::const_iterator it, uintptr_t start, uintptr_t end)
{
    return it != mMmaps.end() && start < std::get<1>(*it) && std::get<0>(*it) < end;
}

inline std::pair<MmapTracker::Mmaps::iterator, MmapTracker::Mmaps::iterator> MmapTracker::find(uintptr_t addr)
{
    auto foundit = std::upper_bound(mMmaps.begin(), mMmaps.end(), addr, [](auto addr, const auto& item) {
        return addr < std::get<0>(item);
    });
    auto it = foundit;
    if (it != mMmaps.begin())
        --it;
    while (it != mMmaps.end() && std::get<1>(*it) <= addr)
        ++it;
    return std::make_pair(it, foundit);
}

inline std::pair<MmapTracker::Mmaps::const_iterator, MmapTracker::Mmaps::const_iterator> MmapTracker::find(uintptr_t addr) const
{
    auto foundit = std::upper_bound(mMmaps.begin(), mMmaps.end(), addr, [](auto addr, const auto& item) {
        return addr < std::get<0>(item);
    });
    auto it = foundit;
    if (it != mMmaps.begin())
        --it;
    while (it != mMmaps.end() && std::get<1>(*it) <= addr)
        ++it;
    return std::make_pair(it, foundit);
}

inline void MmapTracker::mmap(uintptr_t iaddr, size_t size, int32_t prot, int32_t flags, int32_t stack)
{
    const auto iaddrend = iaddr + size;
    auto [ it, insertit ] = find(iaddr);
    if (intersects(it, iaddr, iaddrend)) {
        // got a hit
        do {
            if (std::get<2>(*it) != prot || std::get<3>(*it) != flags || std::get<4>(*it) != stack) {
                const auto curstart = std::get<0>(*it);
                const auto curend = std::get<1>(*it);
                const auto curprot = std::get<2>(*it);
                const auto curflags = std::get<3>(*it);
                const auto curstack = std::get<4>(*it);

                if (curstart < iaddr) {
                    // item addr is prior to input addr, update end and add new item
                    std::get<1>(*it) = iaddr;
                    it = mMmaps.insert(it + 1, std::make_tuple(iaddr, std::min(curend, iaddrend), prot, flags, stack));

                    // if we're fully contained in the item, we need to add one more at the end
                    if (iaddrend < curend) {
                        it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags, curstack));
                    }
                    ++it;
                } else if (iaddr <= curstart && iaddrend >= curend) {
                    // item is fully contained in input addr, just update prot and flags
                    std::get<2>(*it) = prot;
                    std::get<3>(*it) = flags;
                    std::get<4>(*it) = stack;
                    ++it;
                } else if (iaddrend < curend) {
                    // item end is past input end, update and add new item
                    std::get<1>(*it) = iaddrend;
                    std::get<2>(*it) = prot;
                    std::get<3>(*it) = flags;
                    std::get<4>(*it) = stack;

                    it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags, curstack));
                    return;
                }
            } else {
                ++it;
            }
        } while (intersects(it, iaddr, iaddrend));
    } else {
        mMmaps.insert(insertit, std::make_tuple(iaddr, iaddrend, prot, flags, stack));
    }
}

inline void MmapTracker::mmap(void* addr, size_t size, int32_t prot, int32_t flags, int32_t stack)
{
    return mmap(reinterpret_cast<uintptr_t>(addr), size, prot, flags, stack);
}

inline uint64_t MmapTracker::munmap(uintptr_t iaddr, size_t size)
{
    uint64_t num = 0;
    const auto iaddrend = iaddr + size;
    auto [ it, insertit ] = find(iaddr);
    while (intersects(it, iaddr, iaddrend)) {
        const auto curstart = std::get<0>(*it);
        const auto curend = std::get<1>(*it);
        const auto curprot = std::get<2>(*it);
        const auto curflags = std::get<3>(*it);
        const auto curstack = std::get<4>(*it);

        if (curstart < iaddr) {
            // item addr is prior to input addr, update end
            num += std::min(curend, iaddrend) - iaddr;
            std::get<1>(*it) = iaddr;

            // if we're fully contained in the item, we need to add one more at the end
            if (iaddrend < curend) {
                it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags, curstack));
            }
            ++it;
        } else if (iaddr <= curstart && iaddrend >= curend) {
            // item is fully contained in input addr, remove item
            num += curend - curstart;
            it = mMmaps.erase(it);
        } else if (iaddrend < curend) {
            // item end is past input end, update
            num += iaddrend - curstart;
            std::get<0>(*it) = iaddrend;
            return num;
        }
    }
    return num;
}

inline uint64_t MmapTracker::munmap(void* addr, size_t size)
{
    return munmap(reinterpret_cast<uintptr_t>(addr), size);
}

inline int32_t MmapTracker::mprotect(uintptr_t iaddr, size_t size, int32_t prot)
{
    int32_t flags = 0;
    const auto iaddrend = iaddr + size;
    auto [ it, insertit ] = find(iaddr);
    while (intersects(it, iaddr, iaddrend)) {
        if (flags == 0) {
            flags = std::get<3>(*it);
        }
        if (std::get<2>(*it) != prot) {
            const auto curstart = std::get<0>(*it);
            const auto curend = std::get<1>(*it);
            const auto curprot = std::get<2>(*it);
            const auto curflags = std::get<3>(*it);
            const auto curstack = std::get<4>(*it);

            if (curstart < iaddr) {
                // item addr is prior to input addr, update end and add new item
                std::get<1>(*it) = iaddr;
                it = mMmaps.insert(it + 1, std::make_tuple(iaddr, std::min(curend, iaddrend), prot, curflags, curstack));

                // if we're fully contained in the item, we need to add one more at the end
                if (iaddrend < curend) {
                    it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags, curstack));
                }
                ++it;
            } else if (iaddr <= curstart && iaddrend >= curend) {
                // item is fully contained in input addr, just update prot and flags
                std::get<2>(*it) = prot;
                ++it;
            } else if (iaddrend < curend) {
                // item end is past input end, update and add new item
                std::get<1>(*it) = iaddrend;
                std::get<2>(*it) = prot;

                it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags, curstack));
                return flags;
            }
        } else {
            ++it;
        }
    }
    return flags;
}

inline int32_t MmapTracker::mprotect(void* addr, size_t size, int32_t prot)
{
    return mprotect(reinterpret_cast<uintptr_t>(addr), size, prot);
}

inline uint64_t MmapTracker::madvise(uintptr_t iaddr, size_t size)
{
    uint64_t num = 0;
    const auto iaddrend = iaddr + size;
    auto [ it, insertit ] = find(iaddr);
    while (intersects(it, iaddr, iaddrend)) {
        const auto curstart = std::get<0>(*it);
        const auto curend = std::get<1>(*it);

        if (curstart < iaddr) {
            num += std::min(curend, iaddrend) - iaddr;
        } else if (iaddr <= curstart && iaddrend >= curend) {
            num += curend - curstart;
        } else if (iaddrend < curend) {
            num += iaddrend - curstart;
            return num;
        }
        ++it;
    }
    return num;
}

inline uint64_t MmapTracker::madvise(void* addr, size_t size)
{
    return madvise(reinterpret_cast<uintptr_t>(addr), size);
}

template<typename Func>
inline void MmapTracker::forEach(Func&& func) const
{
    for (const auto& t : mMmaps) {
        func(std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t), std::get<4>(t));
    }
}

inline const MmapTracker::Mmaps& MmapTracker::data() const
{
    return mMmaps;
}

inline size_t MmapTracker::size() const
{
    return mMmaps.size();
}

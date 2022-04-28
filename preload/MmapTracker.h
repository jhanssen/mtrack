#pragma once

#include <cstdint>
#include <tuple>
#include <vector>
#include <sys/mman.h>

class MmapTracker
{
public:
    MmapTracker() = default;

    void mmap(void* addr, size_t size, int prot, int flags);
    uint64_t munmap(void* addr, size_t size);
    int mprotect(void* addr, size_t size, int prot);
    uint64_t madvise(void* addr, size_t size);

private:
    typedef std::vector<std::tuple<uintptr_t, uintptr_t, int, int>> Mmaps;

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

inline void MmapTracker::mmap(void* addr, size_t size, int prot, int flags)
{
    const auto iaddr = reinterpret_cast<uintptr_t>(addr);
    const auto iaddrend = iaddr + size;
    auto [ it, insertit ] = find(iaddr);
    if (intersects(it, iaddr, iaddrend)) {
        // got a hit
        do {
            if (std::get<2>(*it) != prot || std::get<3>(*it) != flags) {
                const auto curstart = std::get<0>(*it);
                const auto curend = std::get<1>(*it);
                const auto curprot = std::get<2>(*it);
                const auto curflags = std::get<3>(*it);

                if (curstart < iaddr) {
                    // item addr is prior to input addr, update end and add new item
                    std::get<1>(*it) = iaddr;
                    it = mMmaps.insert(it + 1, std::make_tuple(iaddr, std::min(curend, iaddrend), prot, flags));

                    // if we're fully contained in the item, we need to add one more at the end
                    if (iaddrend < curend) {
                        it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
                    }
                    ++it;
                } else if (iaddr <= curstart && iaddrend >= curend) {
                    // item is fully contained in input addr, just update prot and flags
                    std::get<2>(*it) = prot;
                    std::get<3>(*it) = flags;
                    ++it;
                } else if (iaddrend < curend) {
                    // item end is past input end, update and add new item
                    std::get<1>(*it) = iaddrend;
                    std::get<2>(*it) = prot;
                    std::get<3>(*it) = flags;

                    it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
                    return;
                }
            } else {
                ++it;
            }
        } while (intersects(it, iaddr, iaddrend));
    } else {
        mMmaps.insert(insertit, std::make_tuple(iaddr, iaddrend, prot, flags));
    }
}

inline uint64_t MmapTracker::munmap(void* addr, size_t size)
{
    uint64_t num = 0;
    const auto iaddr = reinterpret_cast<uintptr_t>(addr);
    const auto iaddrend = iaddr + size;
    auto [ it, insertit ] = find(iaddr);
    while (intersects(it, iaddr, iaddrend)) {
        const auto curstart = std::get<0>(*it);
        const auto curend = std::get<1>(*it);
        const auto curprot = std::get<2>(*it);
        const auto curflags = std::get<3>(*it);

        if (curstart < iaddr) {
            // item addr is prior to input addr, update end
            num += std::min(curend, iaddrend) - iaddr;
            std::get<1>(*it) = iaddr;

            // if we're fully contained in the item, we need to add one more at the end
            if (iaddrend < curend) {
                it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
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

inline int MmapTracker::mprotect(void* addr, size_t size, int prot)
{
    int flags = 0;
    const auto iaddr = reinterpret_cast<uintptr_t>(addr);
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

            if (curstart < iaddr) {
                // item addr is prior to input addr, update end and add new item
                std::get<1>(*it) = iaddr;
                it = mMmaps.insert(it + 1, std::make_tuple(iaddr, std::min(curend, iaddrend), prot, curflags));

                // if we're fully contained in the item, we need to add one more at the end
                if (iaddrend < curend) {
                    it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
                }
                ++it;
            } else if (iaddr <= curstart && iaddrend >= curend) {
                // item is fully contained in input addr, just update prot and flags
                std::get<2>(*it) = prot;
                std::get<3>(*it) = curflags;
                ++it;
            } else if (iaddrend < curend) {
                // item end is past input end, update and add new item
                std::get<1>(*it) = iaddrend;
                std::get<2>(*it) = prot;
                std::get<3>(*it) = curflags;

                it = mMmaps.insert(it + 1, std::make_tuple(iaddrend, curend, curprot, curflags));
                return flags;
            }
        } else {
            ++it;
        }
    }
    return flags;
}

inline uint64_t MmapTracker::madvise(void* addr, size_t size)
{
    uint64_t num = 0;
    const auto iaddr = reinterpret_cast<uintptr_t>(addr);
    const auto iaddrend = iaddr + size;
    auto [ it, insertit ] = find(iaddr);
    while (intersects(it, iaddr, iaddrend)) {
        const auto curstart = std::get<0>(*it);
        const auto curend = std::get<1>(*it);

        if (curstart < iaddr) {
            // item addr is prior to input addr, update end
            num += std::min(curend, iaddrend) - iaddr;
        } else if (iaddr <= curstart && iaddrend >= curend) {
            // item is fully contained in input addr, remove item
            num += curend - curstart;
        } else if (iaddrend < curend) {
            // item end is past input end, update
            num += iaddrend - curstart;
            return num;
        }
        ++it;
    }
    return num;
}
